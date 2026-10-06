# Development & Build Guide

This project is built using **PlatformIO**. Because you are using IntelliJ on macOS, the easiest and most reliable way to build and flash the ESP32 is to use the PlatformIO Core Command Line Interface (CLI) directly in your IntelliJ terminal. 

*(Note: While we have a Docker DevContainer for pure compiling, flashing a USB device from inside a Docker container on macOS is notoriously difficult due to USB passthrough limitations. Therefore, a local installation of the CLI is highly recommended).*

## 1. Install PlatformIO Core (macOS)
You can install the PlatformIO CLI locally on your Mac using Homebrew. 

Open your terminal and run:
```bash
brew install platformio
```
*(Alternatively, if you use Python, you can run `pip install platformio`)*

Verify the installation by running:
```bash
pio --version
```

## 2. IntelliJ Setup (Code Completion)
To get IntelliJ (or CLion) to understand the C++ Arduino framework and resolve your `#include` headers properly, you can have PlatformIO generate the necessary IDE configuration files (like `CMakeLists.txt` for CLion).

In your IntelliJ terminal at the root of the project, run:
```bash
pio init --ide clion
```
*(If you are using standard IntelliJ IDEA, you can try `--ide clion`. It will generate the necessary `.iml` and workspace files).*

## 3. Building the Project
To compile the C++ code without flashing it (useful for checking for syntax errors):
```bash
pio run
```
PlatformIO will automatically download the required toolchains for the ESP32-S3 and the libraries defined in `platformio.ini` (like `NimBLE-Arduino` and `ESPAsyncWebServer`).

## 4. Flashing the ESP32
1. Plug your ESP32-S3 into your Mac via USB.
2. In the IntelliJ terminal, run:
```bash
pio run -target upload
```
*(Shortcut: `pio run -t upload`)*
PlatformIO will automatically find the correct USB/serial port on your Mac, compile the code, and upload the firmware.

## 5. Serial Monitor
To view the `Serial.println()` output for debugging, you can use the built-in serial monitor:
```bash
pio device monitor
```
To exit the serial monitor, press `Ctrl + C`.

---
## Summary of Commands
* **Build:** `pio run`
* **Upload:** `pio run -t upload`
* **Monitor:** `pio device monitor`
* **Upload & Monitor:** `pio run -t upload -t monitor`

---
## 6. Hardware Configuration & Build Options
This project is designed to be highly portable across different ESP32 hardware variants. All magic numbers (like hardware pins, port numbers, and LED behaviors) are centralized in the `include/build_options.h` file.

### Overriding at Compile Time
You do **not** need to modify the C++ code to port this project to a new board. All constants defined in `build_options.h` are wrapped in `#ifndef` guards, meaning you can dynamically override them at compile-time directly inside the `platformio.ini` file using `build_flags`.

For example, if you are using a standard NodeMCU board where the physical BOOT button is on GPIO 15 and the LED is Active High, you would add this to your `platformio.ini`:

```ini
[env:custom_board]
platform = espressif32
board = nodemcu-32s
framework = arduino
build_flags = 
    -D BOOT_BUTTON_PIN=15
    -D LED_ACTIVE_LOW=false
```

### Available Build Flags
- `BOOT_BUTTON_PIN`: The GPIO pin for the physical button used to force Setup Mode (Default: `0`).
- `LED_BUILTIN`: The GPIO pin for the visual status LED (Default: `21`).
- `LED_ACTIVE_LOW`: Set to `true` if your board's LED turns ON when the pin is pulled LOW (e.g., XIAO ESP32S3). Set to `false` if it turns ON when pulled HIGH. (Default: `true`).
- `TCP_PORT`: The network port the bridge listens on for incoming Meshtastic App connections. (Default: `4403`).
- `MAX_TCP_CLIENTS`: The maximum number of simultaneous apps that can connect to the bridge. (Default: `3`).
