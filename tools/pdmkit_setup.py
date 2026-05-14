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

    def __init__(self, parent, on_add, initial: dict | None = None):
        super().__init__(parent)
        self._initial   = initial or {}
        self.title("Edit Pin" if initial else "Add / Update Pin")
        self.resizable(False, False)
        self.grab_set()
        self.transient(parent)
        self._on_add    = on_add
        _type_map = {"dout": "Output", "din": "Input", "adc": "ADC", "pwm": "PWM"}
        self._gpio_var  = tk.StringVar(value=self._initial.get("gpio", "0"))
        self._type_var  = tk.StringVar(value=_type_map.get(self._initial.get("type", ""), "Output"))
        self._pull_var  = tk.StringVar(value=self._initial.get("pull", "up"))
        self._freq_var  = tk.StringVar(value=self._initial.get("freq", "5000"))
        self._unit_var  = tk.StringVar(value=self._initial.get("unit", "1"))
        self._ch_var    = tk.StringVar(value=self._initial.get("ch", "0"))
        self._build()
        self.wait_window()

    def _build(self):
        f = tk.Frame(self, padx=16, pady=12)
        f.pack()

        tk.Label(f, text="Name:", anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=4)
        self._name_entry = tk.Entry(f, width=16)
        self._name_entry.grid(row=0, column=1, columnspan=2, sticky=tk.W, pady=4)
        self._name_entry.insert(0, self._initial.get("name", ""))
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
# Rule type definitions
# ---------------------------------------------------------------------------

# (category_name, [(display_label, wire_type), ...])
RULE_CATEGORIES = [
    ("Expression", [
        ("Boolean Expression", "expr"),
    ]),
    ("Combinational", [
        ("Direct",        "direct"),
        ("AND",           "and"),
        ("OR",            "or"),
        ("NOT",           "not"),
        ("NAND / NOR",    "nand_nor"),
        ("XOR",           "xor"),
    ]),
    ("Timing", [
        ("On Delay",      "on_delay"),
        ("Off Delay",     "off_delay"),
        ("Min On Time",   "min_on"),
        ("One Shot",      "one_shot"),
        ("Pulse Stretch", "pulse_str"),
        ("Debounce",      "debounce"),
    ]),
    ("Oscillator", [
        ("Flasher",       "flasher"),
        ("Hazard",        "hazard"),
        ("Burst Pattern", "burst"),
        ("PWM Output",    "pwm_out"),
    ]),
    ("Threshold", [
        ("Threshold",     "threshold"),
        ("Hysteresis",    "hysteresis"),
        ("Window Comp",   "window"),
        ("ADC Map",       "adc_map"),
    ]),
    ("Stateful", [
        ("SR Latch",      "sr_latch"),
        ("Toggle",        "toggle"),
        ("Interlock",     "interlock"),
        ("Priority OR",   "prio_or"),
        ("N-Press",       "n_press"),
    ]),
    ("Protective", [
        ("OC Latch-off",   "oc_latch"),
        ("Retry Backoff",  "retry"),
        ("Thermal Derate", "therm_drt"),
        ("Watchdog Out",   "watchdog"),
    ]),
    ("CAN  (placeholder)", [
        ("Signal Extract",    "can_sig"),
        ("Threshold",         "can_thr"),
        ("Mapping",           "can_map"),
        ("Timeout Fallback",  "can_timeout"),
        ("Multi-Condition",   "can_mcond"),
        ("TX: Status",        "can_tx_st"),
        ("TX: Analog",        "can_tx_an"),
        ("TX: Current",       "can_tx_cur"),
        ("TX: Fault",         "can_tx_flt"),
        ("TX: Event",         "can_tx_evt"),
        ("CMD: Output Ctrl",  "can_cmd_out"),
        ("CMD: Fault Reset",  "can_cmd_fr"),
        ("CMD: Live Config",  "can_cmd_lc"),
        ("Bus-Off Recovery",  "can_boff"),
        ("Heartbeat TX",      "can_htx"),
        ("Heartbeat RX",      "can_hrx"),
        ("Error Counter Log", "can_elog"),
    ]),
]

def _f(fid, label, wtype, default=None):
    """Return a field spec tuple."""
    if default is None:
        default = False if wtype == "check" else ""
    return (fid, label, wtype, default)

# Per-type parameter field specs.
# fid conventions match io_rules.hpp JSON keys: src, dst, src2, srcs,
# on_ms, off_ms, delay_ms, window_ms, pa, pb, tlo, thi, olo, ohi, invert.
RULE_FIELDS: dict[str, list[tuple]] = {
    # Expression
    "expr": [
        _f("dst",  "Output / group",  "target",     ""),
        _f("expr", "Expression",      "expr_entry", "SW1 AND NOT SW2"),
    ],
    # Combinational
    "direct":   [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                 _f("invert","Invert output","check",False)],
    "and":      [_f("srcs","Source pins (comma-sep)","multipin"), _f("dst","Dest pin","pin")],
    "or":       [_f("srcs","Source pins (comma-sep)","multipin"), _f("dst","Dest pin","pin")],
    "not":      [_f("src","Source pin","pin"), _f("dst","Dest pin","pin")],
    "nand_nor": [_f("srcs","Source pins (comma-sep)","multipin"), _f("dst","Dest pin","pin"),
                 _f("invert","NOR mode  (unchecked = NAND)","check",False)],
    "xor":      [_f("src","Source pin 1","pin"), _f("src2","Source pin 2","pin"),
                 _f("dst","Dest pin","pin")],
    # Timing
    "on_delay":  [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                  _f("delay_ms","Delay (ms)","entry","100")],
    "off_delay": [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                  _f("delay_ms","Delay (ms)","entry","100")],
    "min_on":    [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                  _f("on_ms","Min on-time (ms)","entry","100")],
    "one_shot":  [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                  _f("on_ms","Pulse width (ms)","entry","500")],
    "pulse_str": [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                  _f("on_ms","Stretch (ms)","entry","200")],
    "debounce":  [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                  _f("delay_ms","Stable window (ms)","entry","50")],
    # Oscillator
    "flasher": [_f("dst","Dest pin","pin"),
                _f("on_ms","On (ms)","entry","500"), _f("off_ms","Off (ms)","entry","500")],
    "hazard":  [_f("dst","Dest pin","pin"),
                _f("on_ms","On (ms)","entry","500"), _f("off_ms","Off (ms)","entry","500"),
                _f("pa","Cycles  (0 = ∞)","entry","0")],
    "burst":   [_f("dst","Dest pin","pin"),
                _f("pa","Pulses per burst","entry","3"),
                _f("on_ms","Pulse on (ms)","entry","100"), _f("off_ms","Pulse off (ms)","entry","100"),
                _f("pb","Inter-burst gap (ms)","entry","1000")],
    "pwm_out": [_f("dst","Dest pin","pin"),
                _f("pa","Frequency (Hz)","entry","1000"), _f("pb","Duty cycle (%)","entry","50")],
    # Threshold
    "threshold":  [_f("src","Source pin (ADC)","pin"), _f("dst","Dest pin","pin"),
                   _f("tlo","Threshold (mv)","entry","1650"),
                   _f("invert","Invert  (high when below threshold)","check",False)],
    "hysteresis": [_f("src","Source pin (ADC)","pin"), _f("dst","Dest pin","pin"),
                   _f("tlo","Low threshold (mv)","entry","1000"),
                   _f("thi","High threshold (mv)","entry","2000")],
    "window":     [_f("src","Source pin (ADC)","pin"), _f("dst","Dest pin","pin"),
                   _f("tlo","Low (mv)","entry","1000"), _f("thi","High (mv)","entry","2000"),
                   _f("invert","Invert  (out-of-window → high)","check",False)],
    "adc_map":    [_f("src","Source pin (ADC)","pin"), _f("dst","Dest pin (PWM)","pin"),
                   _f("tlo","Input low (mv)","entry","0"), _f("thi","Input high (mv)","entry","3300"),
                   _f("olo","Output low (%)","entry","0"), _f("ohi","Output high (%)","entry","100")],
    # Stateful
    "sr_latch":  [_f("src","Set pin","pin"), _f("src2","Reset pin","pin"),
                  _f("dst","Output pin","pin")],
    "toggle":    [_f("src","Source pin","pin"), _f("dst","Dest pin","pin")],
    "interlock": [_f("srcs","Source pins (comma-sep, priority order)","multipin"),
                  _f("dst","Dest pin","pin")],
    "prio_or":   [_f("srcs","Source pins (comma-sep, priority order)","multipin"),
                  _f("dst","Dest pin","pin")],
    "n_press":   [_f("src","Source pin","pin"), _f("dst","Dest pin","pin"),
                  _f("pa","N  (press count)","entry","3"),
                  _f("window_ms","Window (ms)","entry","2000")],
    # Protective
    "oc_latch":  [_f("src","Source pin (ADC)","pin"), _f("dst","Dest pin","pin"),
                  _f("tlo","Threshold (mv)","entry","2000"),
                  _f("delay_ms","Confirm delay (ms)","entry","100")],
    "retry":     [_f("src","Fault pin","pin"), _f("dst","Output pin","pin"),
                  _f("pa","Max retries","entry","3"), _f("pb","Backoff (ms)","entry","1000")],
    "therm_drt": [_f("src","Source pin (ADC temp)","pin"), _f("dst","Dest pin (PWM)","pin"),
                  _f("tlo","Derate start (mv)","entry","1500"),
                  _f("thi","Max temp (mv)","entry","3000")],
    "watchdog":  [_f("src","Heartbeat pin","pin"), _f("dst","Output pin","pin"),
                  _f("window_ms","Timeout (ms)","entry","1000")],
}

def _wire_to_cat_label(wire: str) -> tuple[str, str]:
    """Return (category_name, label) for a wire type string, or first type if not found."""
    for cat, types in RULE_CATEGORIES:
        for label, w in types:
            if w == wire:
                return cat, label
    return RULE_CATEGORIES[0][0], RULE_CATEGORIES[0][1][0][0]

def _cat_type_labels(cat: str) -> list[tuple[str, str]]:
    for c, types in RULE_CATEGORIES:
        if c == cat:
            return types
    return []

def _label_to_wire(label: str, cat: str) -> str:
    for lbl, wire in _cat_type_labels(cat):
        if lbl == label:
            return wire
    return "direct"


# ---------------------------------------------------------------------------
# Add Rule dialog
# ---------------------------------------------------------------------------

_OSCILLATORS = {"flasher", "hazard", "burst", "pwm_out"}
_TWO_SRC     = {"sr_latch", "xor"}   # arg1 encodes "pin1/pin2"
_MULTI_SRC   = {"and", "or", "nand_nor", "interlock", "prio_or"}

class AddRuleDialog(tk.Toplevel):
    """Modal form to build an RS_AddRule command for any rule type."""

    def __init__(self, parent, on_add, pin_names: list[str] | None = None,
                 initial: dict | None = None, group_names: list[str] | None = None):
        super().__init__(parent)
        self.title("Edit Rule" if initial else "Add Rule")
        self.resizable(False, False)
        self.grab_set()
        self.transient(parent)
        self._on_add      = on_add
        self._pin_names   = pin_names or []
        self._group_names = group_names or []
        self._initial     = initial or {}
        self._vars: dict[str, tk.Variable] = {}

        init_wire = self._initial.get("type", "direct")
        init_cat, init_label = _wire_to_cat_label(init_wire)
        self._cat_var  = tk.StringVar(value=init_cat)
        self._type_var = tk.StringVar(value=init_label)

        self._build()
        self.wait_window()

    # ---- layout -------------------------------------------------------------

    def _build(self):
        f = tk.Frame(self, padx=16, pady=12)
        f.pack()

        tk.Label(f, text="Category:", anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=3)
        cat_cb = ttk.Combobox(f, textvariable=self._cat_var, state="readonly",
                               values=[c for c, _ in RULE_CATEGORIES], width=22)
        cat_cb.grid(row=0, column=1, sticky=tk.W, pady=3)
        cat_cb.bind("<<ComboboxSelected>>", self._on_cat_change)

        tk.Label(f, text="Type:", anchor=tk.W).grid(row=1, column=0, sticky=tk.W, pady=3)
        self._type_cb = ttk.Combobox(f, textvariable=self._type_var, state="readonly", width=22)
        self._type_cb.grid(row=1, column=1, sticky=tk.W, pady=3)
        self._type_cb.bind("<<ComboboxSelected>>", lambda _: self._refresh_params())

        self._params_frame = tk.LabelFrame(f, text="Parameters", padx=8, pady=6)
        self._params_frame.grid(row=2, column=0, columnspan=2, sticky=tk.EW, pady=(8, 4))

        ttk.Separator(f, orient=tk.HORIZONTAL).grid(
            row=3, column=0, columnspan=2, sticky=tk.EW, pady=6)

        btn_row = tk.Frame(f)
        btn_row.grid(row=4, column=0, columnspan=2)
        tk.Button(btn_row, text="Add", width=9, command=self._submit).pack(side=tk.LEFT, padx=6)
        tk.Button(btn_row, text="Cancel", width=9, command=self.destroy).pack(side=tk.LEFT, padx=6)

        self._sync_type_list()
        self._refresh_params()

    # ---- helpers ------------------------------------------------------------

    def _on_cat_change(self, _=None):
        self._sync_type_list()
        self._refresh_params()

    def _sync_type_list(self):
        labels = [lbl for lbl, _ in _cat_type_labels(self._cat_var.get())]
        self._type_cb["values"] = labels
        if self._type_var.get() not in labels:
            self._type_var.set(labels[0] if labels else "")

    def _current_wire(self) -> str:
        return _label_to_wire(self._type_var.get(), self._cat_var.get())

    def _pin_combo(self, parent, var, width=18) -> ttk.Combobox:
        return ttk.Combobox(parent, textvariable=var, values=self._pin_names, width=width)

    # ---- dynamic param panel ------------------------------------------------

    def _refresh_params(self):
        for w in self._params_frame.winfo_children():
            w.destroy()
        self._vars.clear()

        cat  = self._cat_var.get()
        wire = self._current_wire()

        if "CAN" in cat:
            tk.Label(self._params_frame,
                     text="CAN rules are placeholders — not yet implemented.",
                     fg="gray", wraplength=280, justify=tk.LEFT).pack(padx=4, pady=8)
            return

        fields = RULE_FIELDS.get(wire, [])
        init   = self._initial
        f      = self._params_frame

        for row, (fid, label, wtype, default) in enumerate(fields):
            prefill = init.get(fid, default)
            if wtype == "check" and not isinstance(prefill, bool):
                prefill = default

            tk.Label(f, text=f"{label}:", anchor=tk.W, width=26).grid(
                row=row, column=0, sticky=tk.W, pady=2)

            if wtype == "pin":
                var = tk.StringVar(value=str(prefill))
                self._pin_combo(f, var).grid(row=row, column=1, sticky=tk.W, padx=6, pady=2)
            elif wtype == "target":
                var = tk.StringVar(value=str(prefill))
                ttk.Combobox(f, textvariable=var,
                             values=self._pin_names + self._group_names,
                             width=18).grid(row=row, column=1, sticky=tk.W, padx=6, pady=2)
            elif wtype == "multipin":
                var = tk.StringVar(value=str(prefill))
                tk.Entry(f, textvariable=var, width=22).grid(
                    row=row, column=1, sticky=tk.W, padx=6, pady=2)
            elif wtype == "check":
                var = tk.BooleanVar(value=bool(prefill))
                tk.Checkbutton(f, variable=var).grid(
                    row=row, column=1, sticky=tk.W, padx=6, pady=2)
            elif wtype == "expr_entry":
                var = tk.StringVar(value=str(prefill))
                tk.Entry(f, textvariable=var, width=36).grid(
                    row=row, column=1, sticky=tk.W, padx=6, pady=2)
            else:   # "entry"
                var = tk.StringVar(value=str(prefill))
                tk.Entry(f, textvariable=var, width=12).grid(
                    row=row, column=1, sticky=tk.W, padx=6, pady=2)

            self._vars[fid] = var

    # ---- command building ---------------------------------------------------

    def _get(self, fid, default="") -> str:
        v = self._vars.get(fid)
        return v.get() if v is not None else default

    def _geti(self, fid, default=0) -> int:
        try:
            return int(self._get(fid, str(default)))
        except ValueError:
            return default

    def _submit(self):
        cat  = self._cat_var.get()
        wire = self._current_wire()

        if "CAN" in cat:
            messagebox.showinfo("Not implemented",
                                "CAN rules are placeholders and not yet implemented.", parent=self)
            return

        # --- Boolean expression: RS_AddRule expr <dst> <expression> ---------
        if wire == "expr":
            dst = self._get("dst").strip()
            expr_text = self._get("expr").strip()
            if not dst:
                messagebox.showwarning("Missing", "Select an output / group.", parent=self)
                return
            if not expr_text:
                messagebox.showwarning("Missing", "Enter a boolean expression.", parent=self)
                return
            cmd = f"RS_AddRule expr {dst} {expr_text}"
            self._on_add(cmd)
            self.destroy()
            return

        dst = self._get("dst").strip()
        if not dst:
            messagebox.showwarning("Missing", "Select a destination pin.", parent=self)
            return

        # --- Oscillators: RS_AddRule <type> <dst> <n1> <n2> -----------------
        if wire in _OSCILLATORS:
            if wire in ("flasher", "hazard"):
                n1, n2 = self._geti("on_ms", 500), self._geti("off_ms", 500)
            elif wire == "burst":
                n1, n2 = self._geti("pa", 3), self._geti("pb", 1000)
            else:   # pwm_out
                n1, n2 = self._geti("pa", 1000), self._geti("pb", 50)
            cmd = f"RS_AddRule {wire} {dst} {n1} {n2}"

        # --- Two named inputs: RS_AddRule <type> <pin1>/<pin2> <dst> --------
        elif wire in _TWO_SRC:
            src  = self._get("src").strip()
            src2 = self._get("src2").strip()
            if not src:
                messagebox.showwarning("Missing", "Select source pin 1 / set pin.", parent=self)
                return
            arg1 = f"{src}/{src2}" if src2 else src
            cmd  = f"RS_AddRule {wire} {arg1} {dst}"

        # --- Multi-source: RS_AddRule <type> <first_src> <dst> --------------
        elif wire in _MULTI_SRC:
            srcs_raw = self._get("srcs").strip()
            first = srcs_raw.split(",")[0].strip() if srcs_raw else ""
            if not first:
                messagebox.showwarning("Missing", "Enter at least one source pin.", parent=self)
                return
            cmd = f"RS_AddRule {wire} {first} {dst}"

        # --- Standard single-src rules with optional numeric params ----------
        else:
            src = self._get("src").strip()
            if not src:
                messagebox.showwarning("Missing", "Select a source pin.", parent=self)
                return

            # Determine n1/n2 per type
            n1 = n2 = 0
            if wire in ("on_delay", "off_delay", "debounce"):
                n1 = self._geti("delay_ms", 100)
            elif wire in ("min_on", "one_shot", "pulse_str"):
                n1 = self._geti("on_ms", 500)
            elif wire == "threshold":
                n1 = self._geti("tlo", 1650)
                n2 = 1 if self._get("invert") == "1" or self._vars.get("invert", tk.BooleanVar()).get() else 0
            elif wire in ("hysteresis", "window", "adc_map", "therm_drt"):
                n1, n2 = self._geti("tlo", 0), self._geti("thi", 3300)
            elif wire == "n_press":
                n1, n2 = self._geti("pa", 3), self._geti("window_ms", 2000)
            elif wire == "oc_latch":
                n1, n2 = self._geti("tlo", 2000), self._geti("delay_ms", 100)
            elif wire == "retry":
                n1, n2 = self._geti("pa", 3), self._geti("pb", 1000)
            elif wire == "watchdog":
                n1 = self._geti("window_ms", 1000)
            elif wire == "direct":
                n1 = 1 if self._vars.get("invert", tk.BooleanVar()).get() else 0
            elif wire == "nand_nor":
                n1 = 1 if self._vars.get("invert", tk.BooleanVar()).get() else 0

            if n1 or n2:
                cmd = f"RS_AddRule {wire} {src} {dst} {n1} {n2}"
            else:
                cmd = f"RS_AddRule {wire} {src} {dst}"

        self._on_add(cmd)
        self.destroy()


# ---------------------------------------------------------------------------
# Add Variable dialog
# ---------------------------------------------------------------------------

class AddVarDialog(tk.Toplevel):
    """Modal form to add or replace a named boolean variable."""

    def __init__(self, parent, on_add, pin_names: list[str] | None = None,
                 initial: dict | None = None):
        super().__init__(parent)
        self._initial   = initial or {}
        self.title("Edit Variable" if initial else "Add / Update Variable")
        self.resizable(False, False)
        self.grab_set()
        self.transient(parent)
        self._on_add    = on_add
        self._pin_names = pin_names or []
        self._build()
        self.wait_window()

    def _build(self):
        f = tk.Frame(self, padx=16, pady=12)
        f.pack()

        tk.Label(f, text="Name:", anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=4)
        self._name_entry = tk.Entry(f, width=20)
        self._name_entry.grid(row=0, column=1, sticky=tk.W, pady=4)
        self._name_entry.insert(0, self._initial.get("name", ""))
        self._name_entry.focus()

        tk.Label(f, text="Expression:", anchor=tk.W).grid(row=1, column=0, sticky=tk.W, pady=4)
        self._expr_entry = tk.Entry(f, width=36)
        self._expr_entry.grid(row=1, column=1, sticky=tk.W, pady=4)
        self._expr_entry.insert(0, self._initial.get("expr", ""))

        if self._pin_names:
            hint = "Pins: " + ", ".join(self._pin_names)
            tk.Label(f, text=hint, fg="gray", wraplength=320,
                     justify=tk.LEFT).grid(row=2, column=0, columnspan=2,
                                           sticky=tk.W, pady=(0, 4))

        ttk.Separator(f, orient=tk.HORIZONTAL).grid(
            row=3, column=0, columnspan=2, sticky=tk.EW, pady=8)

        btn_row = tk.Frame(f)
        btn_row.grid(row=4, column=0, columnspan=2)
        tk.Button(btn_row, text="Add", width=9, command=self._submit).pack(side=tk.LEFT, padx=6)
        tk.Button(btn_row, text="Cancel", width=9, command=self.destroy).pack(side=tk.LEFT, padx=6)

    def _submit(self):
        name = self._name_entry.get().strip()
        expr = self._expr_entry.get().strip()
        if not name:
            messagebox.showwarning("Missing name", "Enter a variable name.", parent=self)
            return
        if " " in name:
            messagebox.showwarning("Invalid name", "Variable name cannot contain spaces.", parent=self)
            return
        if not expr:
            messagebox.showwarning("Missing expression", "Enter a boolean expression.", parent=self)
            return
        self._on_add(f"RS_AddVar {name} {expr}")
        self.destroy()


# ---------------------------------------------------------------------------
# Add Group dialog
# ---------------------------------------------------------------------------

class AddGroupDialog(tk.Toplevel):
    """Modal form to add or replace a named output group."""

    def __init__(self, parent, on_add, pin_names: list[str] | None = None,
                 initial: dict | None = None):
        super().__init__(parent)
        self._initial   = initial or {}
        self.title("Edit Group" if initial else "Add / Update Group")
        self.resizable(False, False)
        self.grab_set()
        self.transient(parent)
        self._on_add    = on_add
        self._pin_names = pin_names or []
        self._build()
        self.wait_window()

    def _build(self):
        f = tk.Frame(self, padx=16, pady=12)
        f.pack()

        tk.Label(f, text="Name:", anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=4)
        self._name_entry = tk.Entry(f, width=20)
        self._name_entry.grid(row=0, column=1, sticky=tk.W, pady=4)
        self._name_entry.insert(0, self._initial.get("name", ""))
        self._name_entry.focus()

        tk.Label(f, text="Members\n(comma-sep):", anchor=tk.W, justify=tk.LEFT).grid(
            row=1, column=0, sticky=tk.W, pady=4)
        self._mem_entry = tk.Entry(f, width=36)
        self._mem_entry.grid(row=1, column=1, sticky=tk.W, pady=4)
        self._mem_entry.insert(0, self._initial.get("members", ""))

        if self._pin_names:
            hint = "Available pins: " + ", ".join(self._pin_names)
            tk.Label(f, text=hint, fg="gray", wraplength=320,
                     justify=tk.LEFT).grid(row=2, column=0, columnspan=2,
                                           sticky=tk.W, pady=(0, 4))

        ttk.Separator(f, orient=tk.HORIZONTAL).grid(
            row=3, column=0, columnspan=2, sticky=tk.EW, pady=8)

        btn_row = tk.Frame(f)
        btn_row.grid(row=4, column=0, columnspan=2)
        tk.Button(btn_row, text="Add", width=9, command=self._submit).pack(side=tk.LEFT, padx=6)
        tk.Button(btn_row, text="Cancel", width=9, command=self.destroy).pack(side=tk.LEFT, padx=6)

    def _submit(self):
        name = self._name_entry.get().strip()
        members_raw = self._mem_entry.get().strip()
        if not name:
            messagebox.showwarning("Missing name", "Enter a group name.", parent=self)
            return
        if " " in name:
            messagebox.showwarning("Invalid name", "Group name cannot contain spaces.", parent=self)
            return
        members = [m.strip() for m in members_raw.split(",") if m.strip()]
        if not members:
            messagebox.showwarning("Missing members", "Enter at least one member pin.", parent=self)
            return
        # Command: RS_AddGroup <name> <m1> <m2> ...
        self._on_add("RS_AddGroup " + name + " " + " ".join(members))
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
        self._var_pending: list[str] = []
        self._parsing_vars = False
        self._grp_pending: list[str] = []
        self._parsing_grps = False
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

        self._edit_pin_btn = tk.Button(pin_btns, text="Edit Pin…", width=12,
                                       state=tk.DISABLED, command=self._open_edit_pin)
        self._edit_pin_btn.pack(pady=(0, 4))

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

        # --- Variables ------------------------------------------------------
        vars_outer = tk.LabelFrame(inner, text="Variables")
        vars_outer.pack(fill=tk.X, **pad)

        var_cols = ("name", "expr")
        self._var_table = ttk.Treeview(vars_outer, columns=var_cols,
                                       show="headings", height=3)
        self._var_table.heading("name", text="Name")
        self._var_table.heading("expr", text="Expression")
        self._var_table.column("name", width=120)
        self._var_table.column("expr", width=280)
        self._var_table.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0), pady=6)
        self._var_table.bind("<<TreeviewSelect>>", self._on_var_select)

        var_vsb = ttk.Scrollbar(vars_outer, orient=tk.VERTICAL, command=self._var_table.yview)
        var_vsb.pack(side=tk.LEFT, fill=tk.Y, pady=6, padx=(0, 4))
        self._var_table.configure(yscrollcommand=var_vsb.set)

        var_btns = tk.Frame(vars_outer)
        var_btns.pack(side=tk.LEFT, padx=6, anchor=tk.N, pady=6)

        self._list_vars_btn = tk.Button(var_btns, text="Refresh", width=12,
                                        state=tk.DISABLED,
                                        command=lambda: self._send("RS_ListVars"))
        self._list_vars_btn.pack(pady=(0, 4))

        self._add_var_btn = tk.Button(var_btns, text="Add Var…", width=12,
                                      state=tk.DISABLED, command=self._open_add_var)
        self._add_var_btn.pack(pady=(0, 4))

        self._edit_var_btn = tk.Button(var_btns, text="Edit Var…", width=12,
                                       state=tk.DISABLED, command=self._open_edit_var)
        self._edit_var_btn.pack(pady=(0, 4))

        self._remove_var_btn = tk.Button(var_btns, text="Remove", width=12,
                                         state=tk.DISABLED, command=self._remove_selected_var)
        self._remove_var_btn.pack()

        # --- Groups ---------------------------------------------------------
        grps_outer = tk.LabelFrame(inner, text="Groups")
        grps_outer.pack(fill=tk.X, **pad)

        grp_cols = ("name", "members")
        self._grp_table = ttk.Treeview(grps_outer, columns=grp_cols,
                                       show="headings", height=3)
        self._grp_table.heading("name",    text="Name")
        self._grp_table.heading("members", text="Members")
        self._grp_table.column("name",    width=120)
        self._grp_table.column("members", width=280)
        self._grp_table.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(6, 0), pady=6)
        self._grp_table.bind("<<TreeviewSelect>>", self._on_grp_select)

        grp_vsb = ttk.Scrollbar(grps_outer, orient=tk.VERTICAL, command=self._grp_table.yview)
        grp_vsb.pack(side=tk.LEFT, fill=tk.Y, pady=6, padx=(0, 4))
        self._grp_table.configure(yscrollcommand=grp_vsb.set)

        grp_btns = tk.Frame(grps_outer)
        grp_btns.pack(side=tk.LEFT, padx=6, anchor=tk.N, pady=6)

        self._list_grps_btn = tk.Button(grp_btns, text="Refresh", width=12,
                                        state=tk.DISABLED,
                                        command=lambda: self._send("RS_ListGroups"))
        self._list_grps_btn.pack(pady=(0, 4))

        self._add_grp_btn = tk.Button(grp_btns, text="Add Group…", width=12,
                                      state=tk.DISABLED, command=self._open_add_group)
        self._add_grp_btn.pack(pady=(0, 4))

        self._edit_grp_btn = tk.Button(grp_btns, text="Edit Group…", width=12,
                                       state=tk.DISABLED, command=self._open_edit_group)
        self._edit_grp_btn.pack(pady=(0, 4))

        self._remove_grp_btn = tk.Button(grp_btns, text="Remove", width=12,
                                         state=tk.DISABLED, command=self._remove_selected_group)
        self._remove_grp_btn.pack()

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
        self._list_vars_btn.config(state=tk.NORMAL)
        self._list_grps_btn.config(state=tk.NORMAL)
        self._set0_btn.config(state=tk.NORMAL)
        self._set1_btn.config(state=tk.NORMAL)
        self._read_btn.config(state=tk.NORMAL)

        self._log_line(f"[cmd connected] {port}\n", "cmd")
        threading.Thread(target=self._read_loop,
                         args=(lambda: self._cmd_serial, "cmd"),
                         daemon=True).start()
        self.after(300, lambda: self._send("RS_ListPins"))
        self.after(500, lambda: self._send("RS_ListRules"))
        self.after(700, lambda: self._send("RS_ListVars"))
        self.after(900, lambda: self._send("RS_ListGroups"))

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
        self._list_vars_btn.config(state=tk.DISABLED)
        self._list_grps_btn.config(state=tk.DISABLED)
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
            has_pin = bool(self._pin_table.selection())
            self._edit_pin_btn.config(state=tk.NORMAL if has_pin else tk.DISABLED)
            self._add_var_btn.config(state=tk.NORMAL)
            self._remove_var_btn.config(state=tk.NORMAL)
            has_var = bool(self._var_table.selection())
            self._edit_var_btn.config(state=tk.NORMAL if has_var else tk.DISABLED)
            self._add_grp_btn.config(state=tk.NORMAL)
            self._remove_grp_btn.config(state=tk.NORMAL)
            has_grp = bool(self._grp_table.selection())
            self._edit_grp_btn.config(state=tk.NORMAL if has_grp else tk.DISABLED)
        else:
            self._setup_lbl.config(text="● Not in setup", fg="gray")
            self._add_pin_btn.config(state=tk.DISABLED)
            self._edit_pin_btn.config(state=tk.DISABLED)
            self._remove_pin_btn.config(state=tk.DISABLED)
            self._add_rule_btn.config(state=tk.DISABLED)
            self._edit_rule_btn.config(state=tk.DISABLED)
            self._remove_rule_btn.config(state=tk.DISABLED)
            self._add_var_btn.config(state=tk.DISABLED)
            self._edit_var_btn.config(state=tk.DISABLED)
            self._remove_var_btn.config(state=tk.DISABLED)
            self._add_grp_btn.config(state=tk.DISABLED)
            self._edit_grp_btn.config(state=tk.DISABLED)
            self._remove_grp_btn.config(state=tk.DISABLED)

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

        if line == "VARS_BEGIN":
            self._parsing_vars = True
            self._var_pending = []
            return
        if line == "VARS_END":
            self._parsing_vars = False
            self._apply_vars(self._var_pending)
            return
        if self._parsing_vars:
            self._var_pending.append(line)
            return

        if line == "GROUPS_BEGIN":
            self._parsing_grps = True
            self._grp_pending = []
            return
        if line == "GROUPS_END":
            self._parsing_grps = False
            self._apply_groups(self._grp_pending)
            return
        if self._parsing_grps:
            self._grp_pending.append(line)
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
            self.after(600, lambda: self._send("RS_ListVars"))
            self.after(800, lambda: self._send("RS_ListGroups"))
        elif line in ("OK_AddOutput", "OK_AddInput", "OK_AddADC", "OK_AddPWM", "OK_RemovePin"):
            self.after(100, lambda: self._send("RS_ListPins"))
        elif line in ("OK_AddRule", "OK_RemoveRule"):
            self.after(100, lambda: self._send("RS_ListRules"))
        elif line in ("OK_AddVar", "OK_RemoveVar"):
            self.after(100, lambda: self._send("RS_ListVars"))
        elif line in ("OK_AddGroup", "OK_RemoveGroup"):
            self.after(100, lambda: self._send("RS_ListGroups"))

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
        self._edit_pin_btn.config(
            state=tk.NORMAL if (sel and self._in_setup) else tk.DISABLED)

    def _open_add_pin(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        AddPinDialog(self, self._send)

    def _open_edit_pin(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._pin_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a pin row to edit.")
            return
        initial = self._pin_row_to_initial(self._pin_table.item(sel[0])["values"])
        AddPinDialog(self, self._send, initial=initial)

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
            # Unified format: "idx:type:src->dst"  (src empty for oscillators)
            parts = line.split(":", 2)
            if len(parts) < 3:
                continue
            idx, rtype, rest = parts[0], parts[1], parts[2]
            arrow = rest.split("->", 1)
            src = arrow[0]
            dst = arrow[1] if len(arrow) > 1 else ""
            self._rule_table.insert("", tk.END, values=(idx, rtype, src, dst, ""))
        self._log_line(f"[rules] {len(lines)} rule(s)\n", "cmd")

    # -------------------------------------------------------------------------
    # Var table
    # -------------------------------------------------------------------------

    def _apply_vars(self, lines: list[str]):
        for item in self._var_table.get_children():
            self._var_table.delete(item)
        for line in lines:
            parts = line.split(":", 1)
            if len(parts) == 2:
                self._var_table.insert("", tk.END, values=(parts[0], parts[1]))
        self._log_line(f"[vars] {len(lines)} var(s)\n", "cmd")

    def _on_var_select(self, _event=None):
        has = bool(self._var_table.selection())
        self._edit_var_btn.config(
            state=tk.NORMAL if (has and self._in_setup) else tk.DISABLED)

    def _open_add_var(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        pin_names = [self._pin_table.item(r)["values"][0]
                     for r in self._pin_table.get_children()]
        AddVarDialog(self, self._send, pin_names)

    def _open_edit_var(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._var_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a variable row to edit.")
            return
        row = self._var_table.item(sel[0])["values"]
        initial = {"name": str(row[0]), "expr": str(row[1])}
        pin_names = [self._pin_table.item(r)["values"][0]
                     for r in self._pin_table.get_children()]
        AddVarDialog(self, self._send, pin_names, initial=initial)

    def _remove_selected_var(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._var_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a variable row to remove.")
            return
        name = self._var_table.item(sel[0])["values"][0]
        self._send(f"RS_RemoveVar {name}")

    # -------------------------------------------------------------------------
    # Group table
    # -------------------------------------------------------------------------

    def _apply_groups(self, lines: list[str]):
        for item in self._grp_table.get_children():
            self._grp_table.delete(item)
        for line in lines:
            parts = line.split(":", 1)
            if len(parts) == 2:
                self._grp_table.insert("", tk.END, values=(parts[0], parts[1]))
        self._log_line(f"[groups] {len(lines)} group(s)\n", "cmd")

    def _on_grp_select(self, _event=None):
        has = bool(self._grp_table.selection())
        self._edit_grp_btn.config(
            state=tk.NORMAL if (has and self._in_setup) else tk.DISABLED)

    def _open_add_group(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        pin_names = [self._pin_table.item(r)["values"][0]
                     for r in self._pin_table.get_children()]
        AddGroupDialog(self, self._send, pin_names)

    def _open_edit_group(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._grp_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a group row to edit.")
            return
        row = self._grp_table.item(sel[0])["values"]
        initial = {"name": str(row[0]), "members": str(row[1])}
        pin_names = [self._pin_table.item(r)["values"][0]
                     for r in self._pin_table.get_children()]
        AddGroupDialog(self, self._send, pin_names, initial=initial)

    def _remove_selected_group(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        sel = self._grp_table.selection()
        if not sel:
            messagebox.showinfo("Nothing selected", "Select a group row to remove.")
            return
        name = self._grp_table.item(sel[0])["values"][0]
        self._send(f"RS_RemoveGroup {name}")

    def _open_add_rule(self):
        if not self._in_setup:
            messagebox.showinfo("Not in setup", "Press RS_StartSetup first.")
            return
        pin_names = [self._pin_table.item(r)["values"][0]
                     for r in self._pin_table.get_children()]
        group_names = [self._grp_table.item(r)["values"][0]
                       for r in self._grp_table.get_children()]
        AddRuleDialog(self, self._send, pin_names, group_names=group_names)

    def _on_rule_select(self, _event=None):
        has = bool(self._rule_table.selection())
        self._edit_rule_btn.config(state=tk.NORMAL if (has and self._in_setup) else tk.DISABLED)

    def _pin_row_to_initial(self, row) -> dict:
        name, typ, gpio_str, opts = row
        initial = {"name": str(name), "type": str(typ)}
        gpio_str = str(gpio_str)
        if str(typ) == "adc":
            if gpio_str.startswith("u") and "c" in gpio_str:
                uc = gpio_str[1:].split("c", 1)
                initial["unit"] = uc[0]
                initial["ch"]   = uc[1]
        else:
            initial["gpio"] = gpio_str[4:] if gpio_str.startswith("gpio") else gpio_str
        if str(typ) == "din":
            initial["pull"] = str(opts) if opts else "none"
        elif str(typ) == "pwm":
            initial["freq"] = str(opts).rstrip("Hz") if opts else "5000"
        return initial

    def _rule_row_to_initial(self, row) -> dict:
        _idx, rtype, src, dst, _ = row
        initial = {"type": str(rtype), "src": str(src), "dst": str(dst)}
        if str(rtype) == "expr":
            initial["expr"] = str(src)
        return initial

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
        group_names = [self._grp_table.item(r)["values"][0]
                       for r in self._grp_table.get_children()]

        def on_edit(add_cmd):
            self._send(f"RS_RemoveRule {idx}")
            self._send(add_cmd)

        AddRuleDialog(self, on_edit, pin_names, initial=initial, group_names=group_names)

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
