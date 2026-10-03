# Configuration UI Design (Captive Portal)

## Overview
To provide a seamless setup experience without requiring hard-coded credentials, the ESP32 bridge implements a Captive Portal. This allows the user to connect to the ESP32's own WiFi network and configure both the local network (WiFi) and the target Bluetooth (BLE) settings from a mobile device or laptop.

## Design Requirements
1. **Fallback & Manual Trigger**: 
   - If the ESP32 fails to connect to the configured WiFi, it should automatically fallback to Access Point (AP) mode.
   - The user must be able to force AP mode manually by holding down a physical button (e.g., the `BOOT` button on GPIO 0) during startup.
2. **Unified Configuration Form**: A single web page must handle all settings:
   - WiFi SSID
   - WiFi Password
   - Target BLE Device (Selected from a dropdown)
   - BLE PIN code (6-digit)
3. **Active BLE Scanning**: The web interface must present a live list of discovered BLE devices.
   - We will not filter by Meshtastic UUID; all discovered BLE devices will be shown to ensure maximum flexibility and reliability.
   - The dropdown list will display the BLE Local Name (e.g., `Meshtastic_1234`) alongside the MAC Address to help the user identify their specific radio.

## System Workflow
1. **AP Mode Initialization (`wifi_net.cpp`)**:
   - The ESP32 starts its own WiFi AP (e.g., `Meshtastic-Bridge-Setup`).
   - A `DNSServer` intercepts all DNS requests and resolves them to the ESP32's IP (`192.168.4.1`), triggering the OS-level "Sign in to network" captive portal prompt on the user's device.
2. **Web Server (`config_ui.cpp`)**:
   - `ESPAsyncWebServer` listens on port 80.
   - **`GET /`**: Returns the HTML form (embedded as a PROGMEM string in the C++ code).
   - **`GET /scan_ble`**: When the HTML UI loads, JavaScript makes an asynchronous request to this endpoint. The ESP32 temporarily activates the `NimBLE` client, performs an active BLE scan for ~3-5 seconds, and returns a JSON array: `[{"name": "Meshtastic_xxxx", "mac": "AA:BB:CC..."}]`.
   - **`POST /save`**: The HTML form submits data to this endpoint. The ESP32 parses the URL-encoded parameters, saves them to NVS via the `Preferences` library, displays a success page, and initiates a system restart to apply the settings.

## Memory & Non-Volatile Storage (NVS)
The following keys are stored in the `Preferences` namespace (e.g., `bridge_cfg`):
* `wifi_ssid` (String)
* `wifi_pass` (String)
* `ble_mac` (String)
* `ble_pin` (String)

## Technical Decisions & Considerations
* **Radio Concurrency:** The ESP32 shares a single 2.4GHz radio antenna for both WiFi and Bluetooth. We use `ESPAsyncWebServer` because standard synchronous web servers can block the CPU and cause WiFi/BLE tasks to drop packets or timeout. The `/scan_ble` background fetch ensures the web UI doesn't hang while the ESP32 changes radio contexts to scan for Bluetooth.
* **NimBLE Stack:** We chose `NimBLE-Arduino` for BLE scanning and client connections because it uses significantly less RAM than the standard ESP32 Bluedroid stack. This is absolutely critical when running a Web Server, WiFi AP, and BLE stack simultaneously without exhausting the ESP32's heap memory.
