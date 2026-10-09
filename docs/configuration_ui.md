# Configuration UI Design (Captive Portal)

## Overview
To provide a seamless setup experience without requiring hard-coded credentials, the ESP32 bridge implements a Captive Portal. This allows the user to connect to the ESP32's own WiFi network and configure both the local network (WiFi) and the target Bluetooth (BLE) settings from a mobile device or laptop browser.

The UI is built with a clean, modular card-based interface that allows inspecting current settings, updating WiFi and Bluetooth parameters independently, scanning for nearby BLE devices, and verifying live BLE connectivity and PIN pairing before rebooting into normal bridge mode.

## User Interface & Features

### 1. Modular Card Design
* **WiFi Network Card:**
  * Displays the current configured SSID and masked password status, active IP address, advertised mDNS hostname (e.g. `mesh-ae28.local`), and live WiFi RSSI signal strength with quality indicator.
  * In Edit mode, offers a live scan dropdown of nearby WiFi networks (with RSSI signal levels and lock/open security indicators), manual SSID entry for hidden networks, and Password input with a **Show/Hide** toggle.
  * Features a **🔍 Scan for Networks** button (with scan in progress indicator and scan/save buttons disabled during scans).
  * Independent **Save WiFi** button persists network credentials without affecting BLE settings.
* **Bluetooth Target Card:**
  * Displays the configured target device: `Target Device: <Name> (<MAC>)` (or `(<MAC>)` if unnamed), masked PIN, and live Bluetooth connection status pill (`Connected` / `Disconnected`).
  * In Edit mode, offers a live scan dropdown with RSSI signal strengths, manual MAC entry, and 6-digit PIN input.
  * Features a **⚡ Test Connection** button to verify BLE pairing and Meshtastic GATT services live.
  * Independent **Save Bluetooth** button persists target device name, MAC, and PIN.
* **MQTT Gateway Card:**
  * Displays feature enable/disable toggle, live radio sync status, active broker address/port/TLS mode, and published/received/dropped telemetry packet counters.
  * In Edit mode, offers:
    * **Enable MQTT Gateway** toggle.
    * **Skip Certificate Validation** toggle (for self-signed / local LAN brokers).
    * **Custom CA Root Certificate** textarea for uploading private PEM certificates.
  * Independent **Save MQTT Settings** button persists settings to NVS and applies them immediately.
* **System Actions Card:**
  * Displays formatted **System Uptime** (e.g. `1h 24m 12s`).
  * Displays active **TCP Clients** (`X / 3 active`).
  * Displays **Memory (Heap)** stats: Free Heap, Minimum Recorded Free Heap, and Maximum Allocatable Block in KB.
  * Displays **Free Task Stacks** high-water mark headroom for `bridge_ble` and `bridge_net` tasks.
  * Displays **Queue Buffers**: Visual color-coded capacity progress bars (`< 50%` Green, `50-80%` Yellow, `> 80%` Red), message counts (`waiting / capacity`), and drop counters for `TCP ➔ BLE`, `BLE ➔ TCP`, and `MQTT ➔ BLE` queues.
  * Contains a toggle for **Live Telemetry Polling (7s)** (defaults to enabled, allowing user to pause/resume live updates).
  * Contains a toggle for **Enable Serial Debug Logs**.
  * Contains a **Reboot & Start Bridge** action that restarts the ESP32 into normal runtime mode.
  * Contains a **Reset All Settings** action (with confirmation dialog) that clears all stored NVS credentials and restarts into Setup Mode.
* **Footer:**
  * Dynamically embeds the compiled Firmware Version and links to the project's GitHub repository.

### 2. Live WiFi & BLE Scanning
* **WiFi Scanning:** Runs asynchronously via `WiFi.scanNetworks(true)` in `WIFI_AP_STA` mode without dropping the captive portal AP connection. Discovered networks are deduplicated and ordered by signal strength (RSSI).
* **BLE Scanning:** Initiated via **🔍 Scan for Devices** (or automatically when expanding BLE edit mode). Runs an active 4-second BLE scan with duplicate filtering, sorted with named devices first.
* Both scanners show `-- Scan in progress... --` and disable action buttons during active scans to prevent radio contention.

### 3. Live Connection & PIN Verification
* Clicking **⚡ Test Connection** connects to the specified MAC address and authenticates with the provided 6-digit PIN.
* **Passkey Security & Bond Clearing:** Clears any existing bond (`deleteBond`) before and after the test to ensure that the PIN is genuinely challenged and validated on every test run.
* **Service & Characteristic Check:** Confirms the presence of the Meshtastic Service UUID (`6ba1b218-15a8-461f-9fa8-5dcae273eafd` or `cb0b9a0b-a8c2-49c0-bdd5-3fa12b04d84b`) and required `ToRadio` / `FromRadio` characteristics.
* **Non-Blocking Execution:** Runs in a dedicated FreeRTOS background task on Core 1 to ensure the asynchronous web server on Core 0 remains completely responsive.

## System Workflow & Endpoints

1. **AP Mode Initialization (`wifi_net.cpp`)**:
   * The ESP32 starts an open WiFi AP (`Meshtastic-Bridge-Setup` at `192.168.4.1`) in `WIFI_AP_STA` mode whenever:
     * The physical `BOOT` button is pressed during the 3-second startup window (or held for 2 seconds at runtime).
     * No WiFi credentials (`wifi_ssid`) are configured.
     * No Bluetooth target device (`ble_mac`) is configured.
     * Connecting to the configured WiFi network fails during normal boot.
   * A `DNSServer` intercepts DNS queries and redirects captive portal clients to `http://192.168.4.1/`.

2. **Web Server Endpoints (`config_ui.cpp`)**:
   * **`GET /`**: Serves the single-page HTML/CSS/JS application directly from flash (`PROGMEM`) via `AsyncProgmemResponse` with zero heap allocation.
   * **`GET /config`**: Returns current settings as JSON: `{"wifi_ssid":"...","wifi_has_pass":true,"ble_name":"...","ble_mac":"...","ble_pin":"...","debug_logs":false,"mqtt_enabled":false,"mqtt_tls_insecure":false,"mqtt_custom_ca":"..."}`.
   * **`GET /status`**: Returns unified live diagnostic and telemetry JSON polled every 7 seconds by the client: contains `uptime_seconds`, `heap` (free, min_free, max_alloc), `wifi` (connected, is_ap_mode, ip, rssi, mdns_host), `ble` (connected), `tcp` (connected_clients, max_clients), `queues` (waiting, capacity, dropped for `tcp_to_ble`, `ble_to_tcp`, `mqtt_to_ble`), `stacks` (ble_task, net_task free bytes), and `mqtt` (gateway_enabled, state, radio_proxy_enabled, broker, root, traffic counters).
   * **`GET /start_scan_wifi`**: Initiates an asynchronous WiFi network scan.
   * **`GET /scan_wifi_results`**: Polls WiFi scan progress and returns JSON array of discovered networks: `[{"ssid":"MyWiFi","rssi":-58,"is_open":false}]`.
   * **`GET /start_scan`**: Initiates an asynchronous 4-second BLE scan.
   * **`GET /scan_results`**: Polls scan progress and returns JSON array of discovered devices: `[{"name":"Meshtastic_xxxx","mac":"AA:BB:CC:DD:EE:FF","rssi":-68}]`.
   * **`POST /start_test_ble`**: Launches a background FreeRTOS task to test BLE pairing and GATT service discovery for the given MAC and PIN.
   * **`GET /test_ble_status`**: Polls the test task state and returns result JSON (`{"status":"done","success":true,"message":"..."}`).
   * **`POST /save_wifi`**: Saves `wifi_ssid` and `wifi_pass` to NVS.
   * **`POST /save_ble`**: Saves `ble_name`, `ble_mac`, and `ble_pin` to NVS.
   * **`POST /save_mqtt`**: Saves `mqtt_enabled`, `mqtt_tls_insec`, and `mqtt_custom_ca` to NVS and applies changes live if in Normal mode.
   * **`POST /save_system`**: Saves `debug_logs` boolean flag to NVS.
   * **`POST /reboot`**: Restarts the ESP32 into normal bridge mode.
   * **`POST /reset`**: Erases all stored NVS configurations and reboots the ESP32 into Setup Mode.
   * **`POST /save`**: Full-form save endpoint for backward compatibility.

## Memory & Non-Volatile Storage (NVS)
The following keys are stored in the `Preferences` namespace (`bridge_cfg`):
* `wifi_ssid` (String)
* `wifi_pass` (String)
* `ble_name` (String)
* `ble_mac` (String)
* `ble_pin` (String)
* `debug_logs` (Bool)
* `mqtt_enabled` (Bool)
* `mqtt_tls_insec` (Bool)
* `mqtt_custom_ca` (String)

## Technical Decisions & Considerations
* **Zero-Heap PROGMEM Serving:** The main web UI HTML payload is streamed directly out of flash memory via `AsyncProgmemResponse`, eliminating the ~35 KB temporary heap spike when serving web clients.
* **Client-Side Polling:** Telemetry endpoints like `/mqtt_status` are polled every 6 seconds to maintain live stats (including published, received, and dropped counts) while minimizing network traffic and CPU load.
* **Radio Concurrency:** The ESP32 shares a single 2.4GHz radio antenna for WiFi and Bluetooth. Using `ESPAsyncWebServer` combined with asynchronous background tasks for BLE operations prevents HTTP request timeouts and prevents radio collisions while switching between WiFi AP and BLE scanning/testing.
* **NimBLE Stack:** `NimBLE-Arduino` provides low memory footprint BLE client capabilities, essential for coexisting with `ESPAsyncWebServer` and `WiFi` on resource-constrained ESP32-S3 boards without heap exhaustion.
