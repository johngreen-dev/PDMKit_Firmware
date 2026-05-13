"""
PDMKit Setup Tool — connects to PDMKit_Controller and sends RS_ commands.
Optionally connects a second port to stream device log output.
Requires: pip install pyserial
"""

import os
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import font, scrolledtext, messagebox, ttk, filedialog
import serial
import serial.tools.list_ports

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
import parse_nvs

PDMKIT_VID   = 0x303A
PDMKIT_PID   = 0x1002
BAUD_RATE    = 115200
DEFAULT_NVS  = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "storage_dump", "nvs.bin"))
NVS_ADDR     = "0x9000"
NVS_SIZE     = "0x6000"
_MSG_NO_PORT = "No port"

_IDF_PYTHON_CANDIDATES = [
    os.path.expandvars(r"%USERPROFILE%\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"),
    os.path.expandvars(r"%USERPROFILE%\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"),
]


def _find_esptool_python() -> list[str]:
    import shutil
    exe = shutil.which("esptool") or shutil.which("esptool.exe")
    if exe:
        return [exe]
    for candidate in _IDF_PYTHON_CANDIDATES:
        if os.path.isfile(candidate):
            return [candidate, "-m", "esptool"]
    return [sys.executable, "-m", "esptool"]


def list_ports() -> list[tuple[str, str]]:
    ports = []
    for p in serial.tools.list_ports.comports():
        label = p.device
        if p.description and p.description != p.device:
            label += f"  —  {p.description}"
        if p.vid == PDMKIT_VID and p.pid == PDMKIT_PID:
            label += "  ★"
        ports.append((p.device, label))
    return sorted(ports)


# ---------------------------------------------------------------------------
# Add Pin dialog
# ---------------------------------------------------------------------------

class AddPinDialog(tk.Toplevel):
    """Modal form to build a RS_Add* command and hand it to the caller."""

    def __init__(self, parent, on_add):
        super().__init__(parent)
        self.title("Add / Update Pin")
        self.resizable(False, False)
        self.grab_set()
        self.transient(parent)
        self._on_add    = on_add
        self._gpio_var  = tk.StringVar(value="0")
        self._type_var  = tk.StringVar(value="Output")
        self._pull_var  = tk.StringVar(value="up")
        self._freq_var  = tk.StringVar(value="5000")
        self._unit_var  = tk.StringVar(value="1")
        self._ch_var    = tk.StringVar(value="0")
        self._build()
        self.wait_window()

    def _build(self):
        f = tk.Frame(self, padx=16, pady=12)
        f.pack()

        tk.Label(f, text="Name:", anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=4)
        self._name_entry = tk.Entry(f, width=16)
        self._name_entry.grid(row=0, column=1, columnspan=2, sticky=tk.W, pady=4)
        self._name_entry.focus()

        tk.Label(f, text="Type:", anchor=tk.W).grid(row=1, column=0, sticky=tk.W, pady=4)
        type_cb = ttk.Combobox(f, textvariable=self._type_var, state="readonly",
                               values=["Output", "Input", "ADC", "PWM"], width=10)
        type_cb.grid(row=1, column=1, columnspan=2, sticky=tk.W, pady=4)
        type_cb.bind("<<ComboboxSelected>>", lambda _: self._refresh_opts())

        self._opt_frame = tk.Frame(f)
        self._opt_frame.grid(row=2, column=0, columnspan=3, sticky=tk.W)
        self._refresh_opts()

        ttk.Separator(f, orient=tk.HORIZONTAL).grid(
            row=3, column=0, columnspan=3, sticky=tk.EW, pady=10)

        btn_row = tk.Frame(f)
        btn_row.grid(row=4, column=0, columnspan=3)
        tk.Button(btn_row, text="Add", width=9, command=self._submit).pack(side=tk.LEFT, padx=6)
        tk.Button(btn_row, text="Cancel", width=9, command=self.destroy).pack(side=tk.LEFT, padx=6)

    def _refresh_opts(self):
        for w in self._opt_frame.winfo_children():
            w.destroy()
        t = self._type_var.get()
        f = self._opt_frame
        row = 0

        if t in ("Output", "Input", "PWM"):
            tk.Label(f, text="GPIO pin:", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            tk.Spinbox(f, textvariable=self._gpio_var, from_=0, to=55, width=6).grid(
                row=row, column=1, sticky=tk.W, padx=8, pady=4)
            row += 1

        if t == "Input":
            tk.Label(f, text="Pull:", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            pf = tk.Frame(f)
            pf.grid(row=row, column=1, sticky=tk.W)
            for val, lbl in [("none", "None"), ("up", "Up"), ("down", "Down")]:
                tk.Radiobutton(pf, text=lbl, variable=self._pull_var, value=val).pack(side=tk.LEFT)
            row += 1

        if t == "ADC":
            tk.Label(f, text="ADC unit:", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            tk.Spinbox(f, textvariable=self._unit_var, from_=1, to=2, width=5).grid(
                row=row, column=1, sticky=tk.W, padx=8, pady=4)
            row += 1
            tk.Label(f, text="Channel:", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            tk.Spinbox(f, textvariable=self._ch_var, from_=0, to=9, width=5).grid(
                row=row, column=1, sticky=tk.W, padx=8, pady=4)
            row += 1

        if t == "PWM":
            tk.Label(f, text="Freq (Hz):", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            tk.Entry(f, textvariable=self._freq_var, width=9).grid(
                row=row, column=1, sticky=tk.W, padx=8, pady=4)
            row += 1

    def _submit(self):
        name = self._name_entry.get().strip()
        if not name:
            messagebox.showwarning("Missing name", "Enter a pin name.", parent=self)
            return
        if " " in name:
            messagebox.showwarning("Invalid name", "Pin name cannot contain spaces.", parent=self)
            return
        t = self._type_var.get()
        if t == "Output":
            cmd = f"RS_AddOutput {name} {self._gpio_var.get()}"
        elif t == "Input":
            cmd = f"RS_AddInput {name} {self._gpio_var.get()} {self._pull_var.get()}"
        elif t == "ADC":
            cmd = f"RS_AddADC {name} {self._unit_var.get()} {self._ch_var.get()}"
        elif t == "PWM":
            cmd = f"RS_AddPWM {name} {self._gpio_var.get()} {self._freq_var.get()}"
        else:
            return
        self._on_add(cmd)
        self.destroy()


# ---------------------------------------------------------------------------
# Add Rule dialog
# ---------------------------------------------------------------------------

class AddRuleDialog(tk.Toplevel):
    """Modal form to build an RS_AddRule command.
    pin_names is the list of already-configured pin names for the dropdowns.
    """

    def __init__(self, parent, on_add, pin_names: list[str] | None = None,
                 initial: dict | None = None):
        super().__init__(parent)
        self.title("Edit Rule" if initial else "Add Rule")
        self.resizable(False, False)
        self.grab_set()
        self.transient(parent)
        self._on_add    = on_add
        self._pin_names = pin_names or []
        self._type_var  = tk.StringVar(value=initial.get("type", "Link") if initial else "Link")
        self._src_var   = tk.StringVar(value=initial.get("src",  "")     if initial else "")
        self._dst_var   = tk.StringVar(value=initial.get("dst",  "")     if initial else "")
        self._on_var    = tk.StringVar(value=initial.get("on_ms",  "500") if initial else "500")
        self._off_var   = tk.StringVar(value=initial.get("off_ms", "500") if initial else "500")
        self._build()
        self.wait_window()

    def _build(self):
        f = tk.Frame(self, padx=16, pady=12)
        f.pack()

        tk.Label(f, text="Type:", anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=4)
        type_cb = ttk.Combobox(f, textvariable=self._type_var, state="readonly", width=12,
                               values=["Link", "Toggle", "ADC→PWM", "Flash"])
        type_cb.grid(row=0, column=1, columnspan=2, sticky=tk.W, pady=4)
        type_cb.bind("<<ComboboxSelected>>", lambda _: self._refresh_opts())

        self._opt_frame = tk.Frame(f)
        self._opt_frame.grid(row=1, column=0, columnspan=3, sticky=tk.W)
        self._refresh_opts()

        ttk.Separator(f, orient=tk.HORIZONTAL).grid(
            row=2, column=0, columnspan=3, sticky=tk.EW, pady=10)

        btn_row = tk.Frame(f)
        btn_row.grid(row=3, column=0, columnspan=3)
        tk.Button(btn_row, text="Add", width=9, command=self._submit).pack(side=tk.LEFT, padx=6)
        tk.Button(btn_row, text="Cancel", width=9, command=self.destroy).pack(side=tk.LEFT, padx=6)

    def _pin_combo(self, parent, var) -> ttk.Combobox:
        cb = ttk.Combobox(parent, textvariable=var, values=self._pin_names, width=14)
        return cb

    def _refresh_opts(self):
        for w in self._opt_frame.winfo_children():
            w.destroy()
        t = self._type_var.get()
        f = self._opt_frame
        row = 0

        if t != "Flash":
            tk.Label(f, text="Source pin:", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            self._pin_combo(f, self._src_var).grid(row=row, column=1, sticky=tk.W, padx=8, pady=4)
            row += 1

        tk.Label(f, text="Dest pin:", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
        self._pin_combo(f, self._dst_var).grid(row=row, column=1, sticky=tk.W, padx=8, pady=4)
        row += 1

        if t == "Flash":
            tk.Label(f, text="On (ms):", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            tk.Entry(f, textvariable=self._on_var, width=8).grid(
                row=row, column=1, sticky=tk.W, padx=8, pady=4)
            row += 1
            tk.Label(f, text="Off (ms):", anchor=tk.W).grid(row=row, column=0, sticky=tk.W, pady=4)
            tk.Entry(f, textvariable=self._off_var, width=8).grid(
                row=row, column=1, sticky=tk.W, padx=8, pady=4)
            row += 1

    def _submit(self):
        t   = self._type_var.get()
        src = self._src_var.get().strip()
        dst = self._dst_var.get().strip()
        if not dst:
            messagebox.showwarning("Missing", "Select or enter a destination pin.", parent=self)
            return
        if t != "Flash" and not src:
            messagebox.showwarning("Missing", "Select or enter a source pin.", parent=self)
            return
        type_map = {"Link": "link", "Toggle": "toggle", "ADC→PWM": "adc_pwm", "Flash": "flash"}
        rt = type_map[t]
        if t == "Flash":
            cmd = f"RS_AddRule flash {dst} {self._on_var.get()} {self._off_var.get()}"
        else:
            cmd = f"RS_AddRule {rt} {src} {dst}"
        self._on_add(cmd)
        self.destroy()


# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("PDMKit Setup")
        self.minsize(580, 500)
        self.resizable(True, True)
        self._cmd_serial: serial.Serial | None = None
        self._mon_serial: serial.Serial | None = None
        self._port_map: dict[str, str] = {}
        self._storage_pending: list[tuple[str, str, str, str]] = []
        self._parsing_storage = False
        self._pin_pending: list[str] = []
        self._parsing_pins = False
        self._rule_pending: list[str] = []
        self._parsing_rules = False
        self._in_setup = False
        self._build_ui()
        self._refresh_ports()

    # -------------------------------------------------------------------------
    # UI construction
    # -------------------------------------------------------------------------

    def _build_ui(self):
        # Scrollable canvas wrapper ----------------------------------------
        canvas = tk.Canvas(self, highlightthickness=0)
        vsb = ttk.Scrollbar(self, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side=tk.RIGHT, fill=tk.Y)
        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        inner = tk.Frame(canvas)
        win_id = canvas.create_window((0, 0), window=inner, anchor="nw")

        def _on_frame_resize(e):
            canvas.configure(scrollregion=canvas.bbox("all"))
        inner.bind("<Configure>", _on_frame_resize)

        def _on_canvas_resize(e):
            canvas.itemconfig(win_id, width=e.width)
        canvas.bind("<Configure>", _on_canvas_resize)

        def _on_mousewheel(e):
            canvas.yview_scroll(int(-1 * (e.delta / 120)), "units")
        canvas.bind_all("<MouseWheel>", _on_mousewheel)

        # All content lives in `inner` from here on
        pad = {"padx": 10, "pady": 4}

        # --- Command port ---------------------------------------------------
        cmd_bar = tk.LabelFrame(inner, text="Command port  (RS_ protocol)")
        cmd_bar.pack(fill=tk.X, padx=10, pady=(8, 2))

        self._cmd_port_var = tk.StringVar()
        self._cmd_combo = ttk.Combobox(cmd_bar, textvariable=self._cmd_port_var,
                                       state="readonly", width=38)
        self._cmd_combo.pack(side=tk.LEFT, padx=6, pady=6)
        tk.Button(cmd_bar, text="↺", width=2, command=self._refresh_ports).pack(side=tk.LEFT)

        self._connect_btn = tk.Button(cmd_bar, text="Connect", width=9, command=self._connect_cmd)
        self._connect_btn.pack(side=tk.LEFT, padx=4, pady=6)

        self._disconnect_btn = tk.Button(cmd_bar, text="Disconnect", width=9,
                                         command=self._disconnect_cmd, state=tk.DISABLED)
        self._disconnect_btn.pack(side=tk.LEFT, padx=4, pady=6)

        self._cmd_dot = tk.Label(cmd_bar, text="●", fg="red", font=("TkDefaultFont", 14))
        self._cmd_dot.pack(side=tk.RIGHT, padx=8)

        # --- Monitor port ---------------------------------------------------
        mon_bar = tk.LabelFrame(inner, text="Monitor port  (UART log, optional)")
        mon_bar.pack(fill=tk.X, padx=10, pady=(2, 6))

        self._mon_port_var = tk.StringVar()
        self._mon_combo = ttk.Combobox(mon_bar, textvariable=self._mon_port_var,
                                       state="readonly", width=38)
        self._mon_combo.pack(side=tk.LEFT, padx=6, pady=6)

        self._mon_connect_btn = tk.Button(mon_bar, text="Connect", width=9,
                                          command=self._connect_mon)
        self._mon_connect_btn.pack(side=tk.LEFT, padx=4, pady=6)

        self._mon_disconnect_btn = tk.Button(mon_bar, text="Disconnect", width=9,
                                             command=self._disconnect_mon, state=tk.DISABLED)
        self._mon_disconnect_btn.pack(side=tk.LEFT, padx=4, pady=6)

        self._mon_dot = tk.Label(mon_bar, text="●", fg="red", font=("TkDefaultFont", 14))
        self._mon_dot.pack(side=tk.RIGHT, padx=8)

        # --- Setup session --------------------------------------------------
        session_frame = tk.LabelFrame(inner, text="Setup session")
        session_frame.pack(fill=tk.X, **pad)

        bold = font.Font(weight="bold")
        self._session_buttons = []
        for label, bg, fg in [
            ("RS_StartSetup",  "#2e7d32", "white"),
            ("RS_SaveSetup",   "#1565c0", "white"),
            ("RS_CancelSetup", "#b71c1c", "white"),
        ]:
            b = tk.Button(session_frame, text=label, bg=bg, fg=fg, font=bold,
                          width=16, state=tk.DISABLED,
                          command=lambda c=label: self._send(c))
            b.pack(side=tk.LEFT, padx=8, pady=8)
            self._session_buttons.append(b)

        self._setup_lbl = tk.Label(session_frame, text="● Not in setup", fg="gray")
        self._setup_lbl.pack(side=tk.RIGHT, padx=12)

        # --- I/O Pins -------------------------------------------------------
        pins_outer = tk.LabelFrame(inner, text="I/O Pins")
        pins_outer.pack(fill=tk.X, **pad)

        pin_cols = ("name", "type", "gpio", "options")
        self._pin_table = ttk.Treeview(pins_outer, columns=pin_cols,
                                       show="headings", height=5)
        self._pin_table.heading("name",    text="Name")
        self._pin_table.heading("type",    text="Type")
        self._pin_table.heading("gpio",    text="GPIO / Ch")
        self._pin_table.heading("options", text="Options")
        self._pin_table.column("name",    width=110)
        self._pin_table.column("type",    width=70,  anchor=tk.CENTER)
        self._pin_table.column("gpio",    width=90,  anchor=tk.CENTER)
        self._pin_table.column("options", width=130)
        self._pin_table.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0), pady=6)
        self._pin_table.bind("<<TreeviewSelect>>", self._on_pin_select)

        pin_vsb = ttk.Scrollbar(pins_outer, orient=tk.VERTICAL, command=self._pin_table.yview)
        pin_vsb.pack(side=tk.LEFT, fill=tk.Y, pady=6, padx=(0, 4))
        self._pin_table.configure(yscrollcommand=pin_vsb.set)

        pin_btns = tk.Frame(pins_outer)
        pin_btns.pack(side=tk.LEFT, padx=6, anchor=tk.N, pady=6)

        self._list_pins_btn = tk.Button(pin_btns, text="Refresh", width=12,
                                        state=tk.DISABLED,
                                        command=lambda: self._send("RS_ListPins"))
        self._list_pins_btn.pack(pady=(0, 4))

        self._add_pin_btn = tk.Button(pin_btns, text="Add Pin…", width=12,
                                      state=tk.DISABLED, command=self._open_add_pin)
        self._add_pin_btn.pack(pady=(0, 4))

        self._remove_pin_btn = tk.Button(pin_btns, text="Remove", width=12,
                                         state=tk.DISABLED, command=self._remove_selected_pin)
        self._remove_pin_btn.pack()

        # Test row
        test_row = tk.Frame(pins_outer)
        test_row.pack(side=tk.BOTTOM, fill=tk.X, padx=6, pady=(0, 6))

        tk.Label(test_row, text="Test:").pack(side=tk.LEFT)
        self._test_name = tk.Entry(test_row, width=12)
        self._test_name.pack(side=tk.LEFT, padx=4)

        self._set0_btn = tk.Button(test_row, text="Set 0", width=6,
                                   state=tk.DISABLED, command=lambda: self._test_set(0))
        self._set0_btn.pack(side=tk.LEFT, padx=2)

        self._set1_btn = tk.Button(test_row, text="Set 1", width=6,
                                   state=tk.DISABLED, command=lambda: self._test_set(1))
        self._set1_btn.pack(side=tk.LEFT, padx=2)

        self._read_btn = tk.Button(test_row, text="Read", width=6,
                                   state=tk.DISABLED, command=self._test_read)
        self._read_btn.pack(side=tk.LEFT, padx=2)

        # --- Logic Rules ----------------------------------------------------
        rules_outer = tk.LabelFrame(inner, text="Logic Rules")
        rules_outer.pack(fill=tk.X, **pad)

        rule_cols = ("idx", "type", "source", "dest", "params")
        self._rule_table = ttk.Treeview(rules_outer, columns=rule_cols,
                                        show="headings", height=4)
        self._rule_table.heading("idx",    text="#")
        self._rule_table.heading("type",   text="Type")
        self._rule_table.heading("source", text="Source")
        self._rule_table.heading("dest",   text="Destination")
        self._rule_table.heading("params", text="Params")
        self._rule_table.column("idx",    width=30,  anchor=tk.CENTER)
        self._rule_table.column("type",   width=80,  anchor=tk.CENTER)
        self._rule_table.column("source", width=100)
        self._rule_table.column("dest",   width=100)
        self._rule_table.column("params", width=110)
        self._rule_table.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0), pady=6)
        self._rule_table.bind("<<TreeviewSelect>>", self._on_rule_select)

        rule_vsb = ttk.Scrollbar(rules_outer, orient=tk.VERTICAL, command=self._rule_table.yview)
        rule_vsb.pack(side=tk.LEFT, fill=tk.Y, pady=6, padx=(0, 4))
        self._rule_table.configure(yscrollcommand=rule_vsb.set)

        rule_btns = tk.Frame(rules_outer)
        rule_btns.pack(side=tk.LEFT, padx=6, anchor=tk.N, pady=6)

        self._list_rules_btn = tk.Button(rule_btns, text="Refresh", width=12,
                                         state=tk.DISABLED,
                                         command=lambda: self._send("RS_ListRules"))
        self._list_rules_btn.pack(pady=(0, 4))

        self._add_rule_btn = tk.Button(rule_btns, text="Add Rule…", width=12,
                                       state=tk.DISABLED, command=self._open_add_rule)
        self._add_rule_btn.pack(pady=(0, 4))

        self._edit_rule_btn = tk.Button(rule_btns, text="Edit Rule…", width=12,
                                        state=tk.DISABLED, command=self._open_edit_rule)
        self._edit_rule_btn.pack(pady=(0, 4))

        self._remove_rule_btn = tk.Button(rule_btns, text="Remove", width=12,
                                          state=tk.DISABLED, command=self._remove_selected_rule)
        self._remove_rule_btn.pack()

        # --- NVS Storage table ----------------------------------------------
        store_frame = tk.LabelFrame(inner, text="NVS Storage")
        store_frame.pack(fill=tk.X, **pad)

        cols = ("namespace", "key", "type", "value")
        self._table = ttk.Treeview(store_frame, columns=cols, show="headings", height=4)
        self._table.heading("namespace", text="Namespace")
        self._table.heading("key",       text="Key")
        self._table.heading("type",      text="Type")
        self._table.heading("value",     text="Value")
        self._table.column("namespace", width=110)
        self._table.column("key",       width=140)
        self._table.column("type",      width=70, anchor=tk.CENTER)
        self._table.column("value",     width=160)
        self._table.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0), pady=6)

        vsb2 = ttk.Scrollbar(store_frame, orient=tk.VERTICAL, command=self._table.yview)
        vsb2.pack(side=tk.LEFT, fill=tk.Y, pady=6, padx=(0, 4))
        self._table.configure(yscrollcommand=vsb2.set)

        btn_col = tk.Frame(store_frame)
        btn_col.pack(side=tk.LEFT, padx=6, anchor=tk.N, pady=6)

        self._get_api_btn = tk.Button(btn_col, text="Get via API", width=12,
                                      state=tk.DISABLED,
                                      command=lambda: self._send("RS_GetStorage"))
        self._get_api_btn.pack(pady=(0, 4))

        self._dump_btn = tk.Button(btn_col, text="Dump NVS", width=12, command=self._dump_nvs)
        self._dump_btn.pack(pady=(0, 4))

        tk.Button(btn_col, text="Load NVS File", width=12,
                  command=self._load_nvs_file).pack()

        # --- Log ------------------------------------------------------------
        log_frame = tk.LabelFrame(inner, text="Log")
        log_frame.pack(fill=tk.X, padx=10, pady=(4, 2))

        self._log = scrolledtext.ScrolledText(log_frame, height=10, state=tk.DISABLED,
                                              bg="#1e1e1e", fg="#d4d4d4",
                                              font=("Courier", 10))
        self._log.tag_config("cmd", foreground="#9cdcfe")
        self._log.tag_config("mon", foreground="#aaaaaa")
        self._log.tag_config("ok",  foreground="#4ec994")
        self._log.tag_config("err", foreground="#f44747")
        self._log.pack(fill=tk.X, padx=4, pady=4)

        tk.Button(inner, text="Clear log",
                  command=self._clear_log).pack(anchor=tk.E, padx=10, pady=(0, 10))

    # -------------------------------------------------------------------------
    # Port list
    # -------------------------------------------------------------------------

    def _refresh_ports(self):
        ports = list_ports()
        labels = [label for _, label in ports]
        self._port_map = {label: device for device, label in ports}

        for combo in (self._cmd_combo, self._mon_combo):
            combo["values"] = labels

        pdmkit_label = next((lbl for _, lbl in ports if "★" in lbl), None)
        if pdmkit_label and not self._cmd_port_var.get():
            self._cmd_port_var.set(pdmkit_label)
        elif labels and not self._cmd_port_var.get():
            self._cmd_port_var.set(labels[0])
        if labels and not self._mon_port_var.get():
            self._mon_port_var.set(labels[0])

    # -------------------------------------------------------------------------
    # Command port connection
    # -------------------------------------------------------------------------

    def _connect_cmd(self):
        port = self._port_map.get(self._cmd_port_var.get())
        if not port:
            messagebox.showwarning(_MSG_NO_PORT, "Select a command port first.")
            return
        try:
            self._cmd_serial = serial.Serial(port, BAUD_RATE, timeout=1)
        except serial.SerialException as e:
            messagebox.showerror("Connection error", str(e))
            return

        self._cmd_dot.config(fg="green")
        self._connect_btn.config(state=tk.DISABLED)
        self._cmd_combo.config(state=tk.DISABLED)
        self._disconnect_btn.config(state=tk.NORMAL)
        for b in self._session_buttons:
            b.config(state=tk.NORMAL)
        self._get_api_btn.config(state=tk.NORMAL)
        self._list_pins_btn.config(state=tk.NORMAL)
        self._list_rules_btn.config(state=tk.NORMAL)
        self._set0_btn.config(state=tk.NORMAL)
        self._set1_btn.config(state=tk.NORMAL)
        self._read_btn.config(state=tk.NORMAL)

        self._log_line(f"[cmd connected] {port}\n", "cmd")
        threading.Thread(target=self._read_loop,
                         args=(lambda: self._cmd_serial, "cmd"),
                         daemon=True).start()
        self.after(300, lambda: self._send("RS_ListPins"))
        self.after(500, lambda: self._send("RS_ListRules"))

    def _disconnect_cmd(self):
        if self._cmd_serial:
            self._cmd_serial.close()
            self._cmd_serial = None
        self._cmd_dot.config(fg="red")
        self._connect_btn.config(state=tk.NORMAL)
        self._cmd_combo.config(state="readonly")
        self._disconnect_btn.config(state=tk.DISABLED)
        self._get_api_btn.config(state=tk.DISABLED)
        self._list_pins_btn.config(state=tk.DISABLED)
        self._list_rules_btn.config(state=tk.DISABLED)
        self._set0_btn.config(state=tk.DISABLED)
        self._set1_btn.config(state=tk.DISABLED)
        self._read_btn.config(state=tk.DISABLED)
        for b in self._session_buttons:
            b.config(state=tk.DISABLED)
        self._set_setup_mode(False)
        self._log_line("[cmd disconnected]\n", "cmd")

    # -------------------------------------------------------------------------
    # Monitor port connection
    # -------------------------------------------------------------------------

    def _connect_mon(self):
        port = self._port_map.get(self._mon_port_var.get())
        if not port:
            messagebox.showwarning(_MSG_NO_PORT, "Select a monitor port first.")
            return
        try:
            self._mon_serial = serial.Serial(port, BAUD_RATE, timeout=1)
        except serial.SerialException as e:
            messagebox.showerror("Connection error", str(e))
            return

        self._mon_dot.config(fg="green")
        self._mon_connect_btn.config(state=tk.DISABLED)
        self._mon_combo.config(state=tk.DISABLED)
        self._mon_disconnect_btn.config(state=tk.NORMAL)

        self._log_line(f"[mon connected] {port}\n", "mon")
        threading.Thread(target=self._read_loop,
                         args=(lambda: self._mon_serial, "mon"),
                         daemon=True).start()

    def _disconnect_mon(self):
        if self._mon_serial:
            self._mon_serial.close()
            self._mon_serial = None
        self._mon_dot.config(fg="red")
        self._mon_connect_btn.config(state=tk.NORMAL)
        self._mon_combo.config(state="readonly")
        self._mon_disconnect_btn.config(state=tk.DISABLED)
        self._log_line("[mon disconnected]\n", "mon")

    # -------------------------------------------------------------------------
    # Setup mode state
    # -------------------------------------------------------------------------

    def _set_setup_mode(self, active: bool):
        self._in_setup = active
        if active:
            self._setup_lbl.config(text="● In setup", fg="#2e7d32")
            self._add_pin_btn.config(state=tk.NORMAL)
            self._remove_pin_btn.config(state=tk.NORMAL)
            self._add_rule_btn.config(state=tk.NORMAL)
            self._remove_rule_btn.config(state=tk.NORMAL)
            has_rule = bool(self._rule_table.selection())
            self._edit_rule_btn.config(state=tk.NORMAL if has_rule else tk.DISABLED)
        else:
            self._setup_lbl.config(text="● Not in setup", fg="gray")
            self._add_pin_btn.config(state=tk.DISABLED)
            self._remove_pin_btn.config(state=tk.DISABLED)
            self._add_rule_btn.config(state=tk.DISABLED)
            self._edit_rule_btn.config(state=tk.DISABLED)
            self._remove_rule_btn.config(state=tk.DISABLED)

    # -------------------------------------------------------------------------
    # Send / receive
    # -------------------------------------------------------------------------

    def _send(self, command: str):
        if not self._cmd_serial or not self._cmd_serial.is_open:
            return
        self._log_line(f"→ {command}\n", "cmd")
        self._cmd_serial.write((command + "\n").encode())

    def _read_loop(self, get_serial, channel: str):
        while True:
            s = get_serial()
            if not s or not s.is_open:
                break
            try:
                line = s.readline().decode(errors="replace").strip()
                if not line:
                    continue
                if channel == "cmd":
                    self.after(0, self._handle_cmd_line, line)
                else:
                    self.after(0, self._log_line, f"[mon] {line}\n", "mon")
            except serial.SerialException:
                if channel == "cmd":
                    self.after(0, self._disconnect_cmd)
                else:
                    self.after(0, self._disconnect_mon)
                break

    def _handle_cmd_line(self, line: str):
        if line == "PINS_BEGIN":
            self._parsing_pins = True
            self._pin_pending = []
            return
        if line == "PINS_END":
            self._parsing_pins = False
            self._apply_pins(self._pin_pending)
            return
        if self._parsing_pins:
            self._pin_pending.append(line)
            return

        if line == "RULES_BEGIN":
            self._parsing_rules = True
            self._rule_pending = []
            return
        if line == "RULES_END":
            self._parsing_rules = False
            self._apply_rules(self._rule_pending)
            return
        if self._parsing_rules:
            self._rule_pending.append(line)
            return

        if line == "STORAGE_BEGIN":
            self._parsing_storage = True
            self._storage_pending = []
            return
        if line == "STORAGE_END":
            self._parsing_storage = False
            self._apply_storage(self._storage_pending)
            return
        if self._parsing_storage:
            parts = line.split(":", 2)
            if len(parts) == 3:
                self._storage_pending.append(("storage", parts[0], parts[1], parts[2]))
            return

        tag = "ok" if line.startswith("OK_") else ("err" if line.startswith("ERR_") else "cmd")
        self._log_line(f"← {line}\n", tag)

        if line == "OK_StartSetup":
            self._set_setup_mode(True)
        elif line in ("OK_SaveSetup", "OK_CancelSetup"):
            self._set_setup_mode(False)
            self.after(200, lambda: self._send("RS_ListPins"))
            self.after(400, lambda: self._send("RS_ListRules"))
        elif line in ("OK_AddOutput", "OK_AddInput", "OK_AddADC", "OK_AddPWM", "OK_RemovePin"):
            self.after(100, lambda: self._send("RS_ListPins"))
        elif line in ("OK_AddRule", "OK_RemoveRule"):
            self.after(100, lambda: self._send("RS_ListRules"))

    # -------------------------------------------------------------------------
    # Pin table
    # -------------------------------------------------------------------------

    def _apply_pins(self, lines: list[str]):
        for item in self._pin_table.get_children():
            self._pin_table.delete(item)
        for line in lines:
            parts = line.split(":")
            if len(parts) < 3:
                continue
            name, typ, gpio_str = parts[0], parts[1], parts[2]
            opts = ":".join(parts[3:]) if len(parts) > 3 else ""
            self._pin_table.insert("", tk.END, values=(name, typ, gpio_str, opts))
        self._log_line(f"[pins] {len(lines)} pin(s)\n", "cmd")

    def _on_pin_select(self, _event=None):
        sel = self._pin_table.selection()
        if sel:
            name = self._pin_table.item(sel[0])["values"][0]
            self._test_name.delete(0, tk.END)
            self._test_name.insert(0, name)

    def _open_add_pin(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        AddPinDialog(self, self._send)

    def _remove_selected_pin(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._pin_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a pin row to remove.")
            return
        name = self._pin_table.item(sel[0])["values"][0]
        self._send(f"RS_RemovePin {name}")

    # -------------------------------------------------------------------------
    # Rule table
    # -------------------------------------------------------------------------

    def _apply_rules(self, lines: list[str]):
        for item in self._rule_table.get_children():
            self._rule_table.delete(item)
        for line in lines:
            # "0:flash:LED1:500ms/500ms"  or  "1:link:SW1->LED1"
            parts = line.split(":", 3)
            if len(parts) < 3:
                continue
            idx, rtype, rest = parts[0], parts[1], parts[2] if len(parts) > 2 else ""
            if rtype == "flash":
                dst_params = rest.split(":", 1)
                dst    = dst_params[0]
                params = dst_params[1] if len(dst_params) > 1 else ""
                self._rule_table.insert("", tk.END, values=(idx, rtype, "", dst, params))
            else:
                arrow = rest.split("->", 1)
                src = arrow[0]
                dst = arrow[1] if len(arrow) > 1 else ""
                self._rule_table.insert("", tk.END, values=(idx, rtype, src, dst, ""))
        self._log_line(f"[rules] {len(lines)} rule(s)\n", "cmd")

    def _open_add_rule(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        pin_names = [self._pin_table.item(r)["values"][0]
                     for r in self._pin_table.get_children()]
        AddRuleDialog(self, self._send, pin_names)

    def _on_rule_select(self, _event=None):
        has = bool(self._rule_table.selection())
        self._edit_rule_btn.config(state=tk.NORMAL if (has and self._in_setup) else tk.DISABLED)

    def _rule_row_to_initial(self, row) -> dict:
        _idx, rtype, src, dst, params = row
        type_map = {"link": "Link", "toggle": "Toggle", "adc_pwm": "ADC→PWM", "flash": "Flash"}
        on_ms, off_ms = "500", "500"
        if rtype == "flash" and params:
            parts = params.replace("ms", "").split("/")
            if len(parts) == 2:
                on_ms, off_ms = parts[0].strip(), parts[1].strip()
        return {"type": type_map.get(rtype, "Link"), "src": str(src),
                "dst": str(dst), "on_ms": on_ms, "off_ms": off_ms}

    def _open_edit_rule(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._rule_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a rule row to edit.")
            return
        row   = self._rule_table.item(sel[0])["values"]
        idx   = row[0]
        initial = self._rule_row_to_initial(row)
        pin_names = [self._pin_table.item(r)["values"][0]
                     for r in self._pin_table.get_children()]

        def on_edit(add_cmd):
            self._send(f"RS_RemoveRule {idx}")
            self._send(add_cmd)

        AddRuleDialog(self, on_edit, pin_names, initial=initial)

    def _remove_selected_rule(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._rule_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a rule row to remove.")
            return
        idx = self._rule_table.item(sel[0])["values"][0]
        self._send(f"RS_RemoveRule {idx}")

    # -------------------------------------------------------------------------
    # Pin test
    # -------------------------------------------------------------------------

    def _test_set(self, val: int):
        name = self._test_name.get().strip() or self._selected_pin_name()
        if name:
            self._send(f"RS_SetOutput {name} {val}")

    def _test_read(self):
        name = self._test_name.get().strip() or self._selected_pin_name()
        if name:
            self._send(f"RS_GetInput {name}")

    def _selected_pin_name(self) -> str:
        sel = self._pin_table.selection()
        if sel:
            name = self._pin_table.item(sel[0])["values"][0]
            self._test_name.delete(0, tk.END)
            self._test_name.insert(0, name)
            return name
        return ""

    # -------------------------------------------------------------------------
    # Storage table
    # -------------------------------------------------------------------------

    def _apply_storage(self, rows: list[tuple[str, str, str, str]]):
        for item in self._table.get_children():
            self._table.delete(item)
        for ns, key, typ, value in rows:
            self._table.insert("", tk.END, values=(ns, key, typ, value))
        self._log_line(f"[storage] {len(rows)} entries loaded\n", "cmd")

    def _dump_nvs(self):
        port = self._port_map.get(self._mon_port_var.get())
        if not port:
            messagebox.showwarning(_MSG_NO_PORT, "Select a monitor port to use for the flash read.")
            return

        if self._mon_serial and self._mon_serial.is_open:
            self._disconnect_mon()

        os.makedirs(os.path.dirname(DEFAULT_NVS), exist_ok=True)
        self._dump_btn.config(state=tk.DISABLED)
        self._log_line(f"[dump] reading NVS from {port}…\n", "cmd")

        def run():
            cmd = _find_esptool_python() + [
                "--port", port, "--baud", "921600",
                "read_flash", NVS_ADDR, NVS_SIZE, DEFAULT_NVS,
            ]
            try:
                proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True)
                for ln in proc.stdout:
                    self.after(0, self._log_line, f"[esptool] {ln.rstrip()}\n", "mon")
                proc.wait()
                if proc.returncode == 0:
                    self.after(0, self._on_dump_done)
                else:
                    self.after(0, self._log_line, "[dump] esptool exited with error\n", "cmd")
            except FileNotFoundError:
                self.after(0, messagebox.showerror, "esptool not found",
                           "Install esptool:  pip install esptool")
            finally:
                self.after(0, self._dump_btn.config, {"state": tk.NORMAL})

        threading.Thread(target=run, daemon=True).start()

    def _on_dump_done(self):
        self._log_line(f"[dump] saved to {DEFAULT_NVS}\n", "cmd")
        try:
            rows = parse_nvs.parse(DEFAULT_NVS)
            self._apply_storage([(ns, key, typ, str(val)) for ns, key, typ, val in rows])
        except Exception as e:
            self._log_line(f"[dump] parse error: {e}\n", "cmd")

    def _load_nvs_file(self):
        initial = DEFAULT_NVS if os.path.exists(DEFAULT_NVS) else os.path.dirname(DEFAULT_NVS)
        path = filedialog.askopenfilename(
            title="Open NVS partition binary",
            initialfile=initial if os.path.isfile(initial) else None,
            initialdir=os.path.dirname(DEFAULT_NVS),
            filetypes=[("Binary files", "*.bin"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            rows = parse_nvs.parse(path)
        except Exception as e:
            messagebox.showerror("Parse error", str(e))
            return
        self._apply_storage([(ns, key, typ, str(val)) for ns, key, typ, val in rows])
        self._log_line(f"[storage] loaded from file: {os.path.basename(path)}\n", "cmd")

    # -------------------------------------------------------------------------
    # Log helpers
    # -------------------------------------------------------------------------

    def _log_line(self, text: str, tag: str = ""):
        self._log.config(state=tk.NORMAL)
        self._log.insert(tk.END, text, tag)
        self._log.see(tk.END)
        self._log.config(state=tk.DISABLED)

    def _clear_log(self):
        self._log.config(state=tk.NORMAL)
        self._log.delete("1.0", tk.END)
        self._log.config(state=tk.DISABLED)

    def destroy(self):
        for s in (self._cmd_serial, self._mon_serial):
            if s:
                s.close()
        super().destroy()


if __name__ == "__main__":
    App().mainloop()
