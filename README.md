# PDM — ESP32-P4 Starter Project

A minimal ESP-IDF project for the **ESP32-P4** microcontroller, with a full Windows development environment including PowerShell automation scripts, VS Code integration, and USB JTAG debugging.

## Hardware

| Item | Value |
|------|-------|
| SoC | ESP32-P4 |
| Debug interface | USB JTAG (built-in) |
| Default serial port | COM6 @ 115200 baud |

## What It Does

`main/hello_world_main.c` runs a simple FreeRTOS application that:

1. Prints `Hello World from ESP32-P4!` to the serial console
2. Counts from 0 to 9, printing each value with a 1-second delay
3. Prints `Finished counting to 10.` and exits

## Prerequisites

- [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/) installed at `C:\Users\johng\esp\v5.5\esp-idf`
- ESP-IDF environment activated in your shell (`idf.py` on PATH)
- VS Code with the [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) (optional, for IDE features)

## Build & Flash

PowerShell scripts in [scripts/](scripts/) wrap the common `idf.py` commands:

```powershell
# Build
.\scripts\build.ps1

# Flash (defaults to COM6, pass -Port COMx to override)
.\scripts\flash.ps1
.\scripts\flash.ps1 -Port COM3

# Open serial monitor
.\scripts\monitor.ps1

# Flash then open monitor in one step
.\scripts\flash_monitor.ps1

# Erase entire flash
.\scripts\erase_flash.ps1
```

Or use `idf.py` directly:

```powershell
idf.py build
idf.py -p COM6 flash monitor
```

## Project Structure

```
PDM/
├── main/
│   ├── hello_world_main.c   # Application entry point (app_main)
│   └── CMakeLists.txt       # Component registration
├── scripts/
│   ├── build.ps1
│   ├── flash.ps1
│   ├── monitor.ps1
│   ├── flash_monitor.ps1
│   └── erase_flash.ps1
├── .vscode/                 # VS Code tasks, launch configs, settings
├── CMakeLists.txt           # Top-level ESP-IDF project file
├── sdkconfig                # Full SDK configuration (generated)
└── sdkconfig.defaults       # Baseline config (target: esp32p4)
```

## VS Code Integration

The [.vscode/](.vscode/) directory includes pre-configured:

- **Build / Flash / Monitor** tasks
- **OpenOCD + GDB** debug launch (USB JTAG on port 3333)
- **clangd** IntelliSense using the ESP-IDF clang toolchain

Open the project folder in VS Code and use the ESP-IDF extension sidebar or `Ctrl+Shift+P` → `ESP-IDF: Build` to get started.

## Configuration

[sdkconfig.defaults](sdkconfig.defaults) sets the target to `esp32p4`. Run `idf.py menuconfig` to adjust any settings; changes are saved to `sdkconfig`.
