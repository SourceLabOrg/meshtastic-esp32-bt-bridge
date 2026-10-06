#pragma once

// ==============================================================================
// Build Options & Hardware Configuration
//
// All constants defined here can be safely overridden at compile-time by defining
// them in your platformio.ini `build_flags` (e.g. -D BOOT_BUTTON_PIN=15)
// ==============================================================================

// -----------------------------------------
// Hardware Pins
// -----------------------------------------
// The physical button used to force Setup Mode (Captive Portal).
// On 99% of ESP32 boards, this is the physical "BOOT" button on GPIO 0.
#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

// The built-in status LED used for visual blinking.
// If your Arduino board variant automatically defines LED_BUILTIN, it will use that.
// Otherwise, it falls back to this pin (21 is standard for Seeed XIAO ESP32S3).
#ifndef LED_BUILTIN
#define LED_BUILTIN 21
#endif

// Does the LED turn ON when the pin is pulled LOW?
// Set to true for the Seeed XIAO ESP32S3, false for most standard dev boards.
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW true
#endif

// -----------------------------------------
// Network Settings
// -----------------------------------------
// The TCP port the bridge will listen on for incoming connections from Meshtastic Apps.
#ifndef TCP_PORT
#define TCP_PORT 4403
#endif

// Maximum number of simultaneous TCP clients (Apps) that can connect to the bridge simultaneously.
#ifndef MAX_TCP_CLIENTS
#define MAX_TCP_CLIENTS 3
#endif

// Bluetooth Connect Timeout in seconds
#ifndef BLUETOOTH_TIMEOUT_SECONDS
#define BLUETOOTH_TIMEOUT_SECONDS 8
#endif

// Bluetooth Scan Timeout in seconds
#ifndef BLUETOOTH_SCAN_TIME_SECONDS
#define BLUETOOTH_SCAN_TIME_SECONDS 4
#endif

// Bluetooth Max devices to return from discovery scan.
#ifndef BLUETOOTH_MAX_DEVICES_DISCOVERABLE
#define BLUETOOTH_MAX_DEVICES_DISCOVERABLE 60
#endif

// -----------------------------------------
// Firmware & Project Info
// -----------------------------------------
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.0.0-dev"
#endif

#ifndef PROJECT_GITHUB_URL
#define PROJECT_GITHUB_URL "https://www.github.com/sourcelaborg/meshtastic-esp32-bt-bridge"
#endif
