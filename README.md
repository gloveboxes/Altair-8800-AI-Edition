# ESP32-S3 Altair 8800 Emulator

> **Target SDK:** ESP-IDF v6.0 (Espressif IoT Development Framework)

An Altair 8800 emulator running on ESP32-S3 with WebSocket terminal access.

## Building

### Prerequisites

- ESP-IDF v6.0 (stable release) installed (see [ESP-IDF Getting Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/))
- [ESP-IDF Toolchain](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html#get-started-build) (CMake, Ninja, cross-compiler)
- ESP32-S3 target board

### Setup ESP-IDF Environment

#### 1. Install ESP-IDF

Follow the [ESP-IDF Getting Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) guide. On macOS, the default install location is `~/.espressif/v6.0/esp-idf/`.

#### 2. Set up the Python virtual environment

After installing ESP-IDF, you must run the install script to create the Python virtual environment:

```bash
~/.espressif/v6.0/esp-idf/install.sh esp32s3
```

If you see `ESP-IDF Python virtual environment ... not found` when sourcing `export.sh`, re-run this step.

#### 3. Source the environment

Before running any `idf.py` commands, source the ESP-IDF environment script:

```bash
source $HOME/.espressif/v6.0/esp-idf/export.sh
```

**Tip:** Add an alias to your shell profile (`~/.zshrc` or `~/.zprofile`):

```bash
alias get_idf='source $HOME/.espressif/v6.0/esp-idf/export.sh'
```

Then simply run `get_idf` before building.

### VS Code Extension Setup

The [ESP-IDF VS Code Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) (`espressif.esp-idf-extension`) provides build/flash/monitor commands and IntelliSense.

After installing the extension, configure `.vscode/settings.json` with the `idf.*` settings. Use `${env:HOME}` to keep paths portable across machines:

```json
{
  "idf.espIdfPath": "${env:HOME}/.espressif/v6.0/esp-idf",
  "idf.toolsPath": "${env:HOME}/.espressif",
  "idf.pythonInstallPath": "${env:HOME}/.espressif/python_env/idf6.0_py3.14_env/bin/python"
}
```

**Troubleshooting:**

- **`command 'espIdf.buildDevice' not found`** — The extension failed to activate. Check the Output panel (`Cmd+Shift+U` → "ESP-IDF") for errors.
- **Python virtual environment not found** — Run `~/.espressif/v6.0/esp-idf/install.sh esp32s3` to create it.
- **`idf.toolsPath`** should point to `~/.espressif` (the root), not `~/.espressif/tools`.

### Build Commands

This project is set up to be built and maintained using the [ESP-IDF VS Code Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension). The extension provides all the tools needed to clean, build, flash, and monitor the project directly from VS Code — no CLI setup required. Use the command palette (`Cmd+Shift+P`) and search for "ESP-IDF" to access the available commands.

### CLI Build Commands

For CLI usage outside of VS Code:

```bash
# Source ESP-IDF environment
source $HOME/.espressif/v6.0/esp-idf/export.sh

# Set target (first time only)
idf.py set-target esp32s3

# Build (use ninja directly for parallel control — j=6 benchmarked fastest on Apple Silicon)
idf.py reconfigure && ninja -C build -j6 all

# Flash and monitor (adjust port for your board)
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

If the build fails with a Python environment mismatch, run `idf.py fullclean` first.

## Configuration

### Updating sdkconfig from sdkconfig.defaults

The `sdkconfig.defaults` file contains the project's default configuration settings. The `sdkconfig` file is generated from these defaults and may contain machine-specific settings.

**To regenerate sdkconfig from defaults:**

```bash
# Option 1: Delete sdkconfig and rebuild (recommended)
rm sdkconfig
idf.py build

# Option 2: Use reconfigure
idf.py reconfigure

# Option 3: Full clean rebuild
idf.py fullclean
idf.py build
```

**To open the configuration menu:**

```bash
idf.py menuconfig
```

After making changes in menuconfig, settings are saved to `sdkconfig`. To preserve important settings for the project, add them to `sdkconfig.defaults`.

**Note:** `sdkconfig` is typically added to `.gitignore` since it contains machine-specific paths. Only `sdkconfig.defaults` should be committed to version control.

## Project Structure

```
├── main/               # Application entry point and networking
├── altair8800/         # Altair 8800 emulator core (Intel 8080 CPU)
├── front_panel/        # ILI9341 display driver and panel rendering
├── port_drivers/       # I/O port emulation
├── disks/              # CP/M disk images
├── terminal/           # Web terminal HTML/JS
├── captive_portal/     # WiFi configuration portal
└── drivers/            # Hardware drivers (SD card)
```

## WebSocket Terminal

Connect to the device's IP address in a web browser to access the terminal. The server supports one client at a time - new connections automatically take over from existing ones.
