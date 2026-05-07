# Contributing to PDMKit Firmware

Thanks for your interest in contributing. This project is non-commercial and community-driven — all contributions must comply with the [PolyForm Noncommercial License](LICENSE).

---

## Ways to Contribute

- **Bug reports** — open a GitHub Issue with reproduction steps and hardware details
- **Feature proposals** — open an Issue to discuss before writing code
- **Code contributions** — submit a Pull Request against `main`
- **Documentation** — improvements to READMEs, helper docs, and code comments are welcome

---

## Before You Start

- Search existing Issues and PRs to avoid duplicating work
- For anything beyond a small fix, open an Issue first to align on scope
- All contributions must be non-commercial and compatible with the project license

---

## Development Setup

1. Install [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/) and ensure `idf.py` is on your PATH
2. Clone the repo and open it in VS Code
3. Build with `idf.py build` to confirm your environment works

---

## Pull Request Process

1. Fork the repo and create a feature branch from `main`:
   ```
   git checkout -b fix/nvs-read-null-check
   ```
2. Make your changes, keeping commits focused and atomic
3. Verify the project builds cleanly:
   ```
   idf.py build
   ```
4. Open a PR against `main` with:
   - A clear title describing *what* changed
   - A short description of *why* (link the related Issue)
   - Hardware you tested on (e.g. ESP32-P4 dev board)
5. Address review feedback; a maintainer will merge once approved

---

## Reporting Bugs

Open a GitHub Issue and include:

- **Hardware:** SoC, board, any peripherals connected
- **ESP-IDF version:** output of `idf.py --version`
- **Steps to reproduce:** minimal code or config that triggers the issue
- **Expected vs actual behaviour**
- **Logs:** serial output or OpenOCD/GDB output if relevant

---

## Proposing New Helper Modules

New helpers under `main/helper/` should:

- Wrap a single ESP-IDF subsystem or concept
- Expose a minimal, orthogonal API returning `esp_err_t`
- Be documented in `main/helper/README.md` with setup, API, and example sections
- Add the `.c` file to `SRCS` in `main/CMakeLists.txt`

---

## License Agreement

By submitting a contribution you agree that your work will be licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE) and that you have the right to grant this license.
