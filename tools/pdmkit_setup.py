"""
PDMKit Setup Tool — connects to PDMKit_Controller and sends RS_ commands.
Optionally connects a second port to stream device log output.
Requires: pip install pyserial
"""

import threading
import tkinter as tk
from tkinter import font, scrolledtext, messagebox, ttk
import serial
import serial.tools.list_ports

PDMKIT_VID = 0x303A
PDMKIT_PID = 0x1002
BAUD_RATE  = 115200


def list_ports() -> list[tuple[str, str]]:
    """Return (device, label) pairs for all available COM ports."""
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

        # --- Log -------------------------------------------------------------
        log_frame = tk.LabelFrame(self, text="Log")
        log_frame.pack(fill=tk.BOTH, expand=True, **pad)

        self._log = scrolledtext.ScrolledText(log_frame, height=14, state=tk.DISABLED,
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
            messagebox.showwarning("No port", "Select a command port first.")
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

        self._log_line(f"[cmd connected] {port}\n", "cmd")
        threading.Thread(target=self._read_loop,
                         args=(lambda: self._cmd_serial, "[cmd] ", "cmd"),
                         daemon=True).start()

    def _disconnect_cmd(self):
        if self._cmd_serial:
            self._cmd_serial.close()
            self._cmd_serial = None
        self._cmd_dot.config(fg="red")
        self._connect_btn.config(state=tk.NORMAL)
        self._cmd_combo.config(state="readonly")
        self._disconnect_btn.config(state=tk.DISABLED)
        for b in self._cmd_buttons:
            b.config(state=tk.DISABLED)
        self._log_line("[cmd disconnected]\n", "cmd")

    # -------------------------------------------------------------------------
    # Monitor port connection
    # -------------------------------------------------------------------------

    def _connect_mon(self):
        port = self._port_map.get(self._mon_port_var.get())
        if not port:
            messagebox.showwarning("No port", "Select a monitor port first.")
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
                         args=(lambda: self._mon_serial, "[mon] ", "mon"),
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

    def _read_loop(self, get_serial, prefix: str, tag: str):
        while True:
            s = get_serial()
            if not s or not s.is_open:
                break
            try:
                line = s.readline().decode(errors="replace").strip()
                if line:
                    self.after(0, self._log_line, f"{prefix}{line}\n", tag)
            except serial.SerialException:
                if tag == "cmd":
                    self.after(0, self._disconnect_cmd)
                else:
                    self.after(0, self._disconnect_mon)
                break

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
