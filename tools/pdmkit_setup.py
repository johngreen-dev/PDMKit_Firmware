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

# Allow importing parse_nvs from the sibling scripts/ directory
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
import parse_nvs

PDMKIT_VID    = 0x303A
PDMKIT_PID    = 0x1002
BAUD_RATE     = 115200
DEFAULT_NVS   = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "storage_dump", "nvs.bin"))
NVS_ADDR      = "0x9000"
NVS_SIZE      = "0x6000"
_MSG_NO_PORT  = "No port"

# Candidates for a Python interpreter that has esptool installed.
# The IDF-managed venv is checked first; the script's own interpreter is the fallback.
_IDF_PYTHON_CANDIDATES = [
    os.path.expandvars(r"%USERPROFILE%\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"),
    os.path.expandvars(r"%USERPROFILE%\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"),
]


def _find_esptool_python() -> list[str]:
    """Return a command prefix that can run `esptool`.

    Checks for the esptool executable on PATH first, then looks for it inside
    known IDF Python virtual-environments, finally falls back to the current
    interpreter with -m esptool.
    """
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


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("PDMKit Setup")
        self.resizable(False, False)
        self._cmd_serial: serial.Serial | None = None
        self._mon_serial: serial.Serial | None = None
        self._port_map: dict[str, str] = {}
        self._storage_pending: list[tuple[str, str, str, str]] = []
        self._parsing_storage = False
        self._build_ui()
        self._refresh_ports()

    # -------------------------------------------------------------------------
    # UI
    # -------------------------------------------------------------------------

    def _build_ui(self):
        pad = {"padx": 10, "pady": 4}

        # --- Command port row ------------------------------------------------
        cmd_bar = tk.LabelFrame(self, text="Command port  (RS_ protocol)")
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

        # --- Monitor port row ------------------------------------------------
        mon_bar = tk.LabelFrame(self, text="Monitor port  (UART log, optional)")
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

        # --- Command buttons -------------------------------------------------
        cmd_frame = tk.LabelFrame(self, text="Commands")
        cmd_frame.pack(fill=tk.X, **pad)

        btn_font = font.Font(weight="bold")
        commands = [
            ("RS_StartSetup",  "#2e7d32", "white"),
            ("RS_SaveSetup",   "#1565c0", "white"),
            ("RS_CancelSetup", "#b71c1c", "white"),
        ]
        self._cmd_buttons = []
        for label, bg, fg in commands:
            b = tk.Button(cmd_frame, text=label, bg=bg, fg=fg, font=btn_font,
                          width=16, state=tk.DISABLED,
                          command=lambda c=label: self._send(c))
            b.pack(side=tk.LEFT, padx=8, pady=8)
            self._cmd_buttons.append(b)

        # --- Storage table ---------------------------------------------------
        store_frame = tk.LabelFrame(self, text="NVS Storage")
        store_frame.pack(fill=tk.X, **pad)

        cols = ("namespace", "key", "type", "value")
        self._table = ttk.Treeview(store_frame, columns=cols, show="headings", height=5)
        self._table.heading("namespace", text="Namespace")
        self._table.heading("key",       text="Key")
        self._table.heading("type",      text="Type")
        self._table.heading("value",     text="Value")
        self._table.column("namespace", width=110)
        self._table.column("key",       width=140)
        self._table.column("type",      width=70, anchor=tk.CENTER)
        self._table.column("value",     width=160)
        self._table.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0), pady=6)

        vsb = ttk.Scrollbar(store_frame, orient=tk.VERTICAL, command=self._table.yview)
        vsb.pack(side=tk.LEFT, fill=tk.Y, pady=6, padx=(0, 4))
        self._table.configure(yscrollcommand=vsb.set)

        btn_col = tk.Frame(store_frame)
        btn_col.pack(side=tk.LEFT, padx=6, anchor=tk.N, pady=6)

        self._get_api_btn = tk.Button(btn_col, text="Get via API", width=12,
                                      state=tk.DISABLED,
                                      command=lambda: self._send("RS_GetStorage"))
        self._get_api_btn.pack(pady=(0, 4))

        self._dump_btn = tk.Button(btn_col, text="Dump NVS", width=12,
                                   command=self._dump_nvs)
        self._dump_btn.pack(pady=(0, 4))

        tk.Button(btn_col, text="Load NVS File", width=12,
                  command=self._load_nvs_file).pack()

        # --- Log -------------------------------------------------------------
        log_frame = tk.LabelFrame(self, text="Log")
        log_frame.pack(fill=tk.BOTH, expand=True, **pad)

        self._log = scrolledtext.ScrolledText(log_frame, height=10, state=tk.DISABLED,
                                              bg="#1e1e1e", fg="#d4d4d4",
                                              font=("Courier", 10))
        self._log.tag_config("cmd", foreground="#9cdcfe")
        self._log.tag_config("mon", foreground="#aaaaaa")
        self._log.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        tk.Button(self, text="Clear log", command=self._clear_log).pack(anchor=tk.E, padx=10, pady=(0, 8))

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
        for b in self._cmd_buttons:
            b.config(state=tk.NORMAL)
        self._get_api_btn.config(state=tk.NORMAL)

        self._log_line(f"[cmd connected] {port}\n", "cmd")
        threading.Thread(target=self._read_loop,
                         args=(lambda: self._cmd_serial, "cmd"),
                         daemon=True).start()

    def _disconnect_cmd(self):
        if self._cmd_serial:
            self._cmd_serial.close()
            self._cmd_serial = None
        self._cmd_dot.config(fg="red")
        self._connect_btn.config(state=tk.NORMAL)
        self._cmd_combo.config(state="readonly")
        self._disconnect_btn.config(state=tk.DISABLED)
        self._get_api_btn.config(state=tk.DISABLED)
        for b in self._cmd_buttons:
            b.config(state=tk.DISABLED)
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
        self._log_line(f"← {line}\n", "cmd")

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

        # Disconnect monitor if active — esptool needs exclusive port access
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
                for line in proc.stdout:
                    self.after(0, self._log_line, f"[esptool] {line.rstrip()}\n", "mon")
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

        # parse_nvs returns (ns, key, type, value)
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
