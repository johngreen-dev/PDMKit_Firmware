# PDMKit Firmware

[![License: PolyForm Noncommercial](https://img.shields.io/badge/License-PolyForm%20Noncommercial-blue)](LICENSE)
[![Target: ESP32-P4](https://img.shields.io/badge/Target-ESP32--P4-red)](https://www.espressif.com/en/products/socs/esp32-p4)
[![Framework: ESP-IDF v5.5](https://img.shields.io/badge/ESP--IDF-v5.5-green)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/)

Firmware for the **PDMKit** — an open, non-commercial ESP32-P4-based platform. Built on ESP-IDF with a full Windows development environment including VS Code integration and USB JTAG debugging.

---

## Hardware

| Item | Value |
|------|-------|
| SoC | ESP32-P4 |
| Debug interface | USB JTAG (built-in) |
| Default serial port | COM6 @ 115200 baud |

---

## Project Structure

```
PDMKit_Firmware/
├── main/
│   ├── hello_world_main.c   # Application entry point (app_main)
│   ├── CMakeLists.txt       # Component registration
│   └── helper/              # Reusable ESP-IDF utility modules
│       ├── storage.h/.c     # NVS key-value store wrapper
│       └── README.md        # Helper module docs
├── .vscode/                 # VS Code tasks, launch configs, settings
├── CMakeLists.txt           # Top-level ESP-IDF project file
├── sdkconfig                # Full SDK configuration (generated)
├── sdkconfig.defaults       # Baseline config (target: esp32p4)
├── CONTRIBUTING.md
└── LICENSE
```

---

## Prerequisites

- [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/) installed and on `PATH` (`idf.py` accessible in your shell)
- VS Code with the [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) (optional, for IDE features)

---

## Build & Flash

Use `idf.py` directly:

```powershell
idf.py build
idf.py -p COM6 flash monitor
```

Or use VS Code tasks (`Ctrl+Shift+P` → `Tasks: Run Task`):

| Task | Action |
|------|--------|
| `ESP-IDF: Build` | Compile the project |
| `ESP-IDF: Flash` | Flash to device (COM6) |
| `ESP-IDF: Monitor` | Open serial monitor |

---

## VS Code Integration

The [.vscode/](.vscode/) directory includes:

- **Build / Flash / Monitor** tasks
- **OpenOCD + GDB** debug launch (USB JTAG on port 3333)
- **clangd** IntelliSense using the ESP-IDF clang toolchain

---

## Helper Modules

Reusable utility modules live in [main/helper/](main/helper/). Each module is a single `.h`/`.c` pair — see [main/helper/README.md](main/helper/README.md) for full API docs.

| Module | Description |
|--------|-------------|
| `storage` | NVS key-value store for `int32`, `bool`, and `string` types |

---

## Configuration

[sdkconfig.defaults](sdkconfig.defaults) sets the target to `esp32p4`. Run `idf.py menuconfig` to adjust settings; changes are saved to `sdkconfig` (not committed).

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on reporting bugs, proposing changes, and submitting pull requests.

---

## License

PDMKit Firmware is released under the [PolyForm Noncommercial License 1.0.0](LICENSE).  
Free to use, study, and modify for **non-commercial purposes only**. Commercial use requires a separate written agreement.
