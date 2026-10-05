# Project Status & Next Steps

## Completed So Far
*   **Architecture & Design:** Established the core plan (PlatformIO, Arduino Core, ESP32-S3 target).
*   **Captive Portal (Config UI):** Implemented the asynchronous web server (`config_ui.cpp`) that serves a mobile-friendly setup page. It handles reading/writing WiFi and BLE credentials to the ESP32's non-volatile storage (NVS).
*   **WiFi / Boot Logic:** Implemented `wifi_net.cpp` to check the physical `BOOT` button on startup, attempt to connect to saved WiFi, and fallback to the Captive Portal Access Point (`Meshtastic-Bridge-Setup`) if it fails.
*   **Stubs:** Created dummy functions in `ble_client.cpp` so the project compiles and the Web UI can be tested immediately upon flashing.
*   **BLE Scanning (`ble_client.cpp`):** Implemented active 6-second BLE scanning with `NimBLE`, returning discovered BLE devices with name, MAC address, and RSSI to the configuration portal.

## Next Steps
1.  **Flash & Test BLE Scan in AP Mode:** Run `pio run -t upload -t monitor`, connect to the `Meshtastic-Bridge-Setup` AP, and verify real BLE devices show up in the dropdown list.
2.  **Implement BLE Connection & PIN Pairing (`ble_client.cpp`):**
    *   Connect to the configured target MAC address saved in NVS.
    *   Implement NimBLE security callbacks (numeric comparison / PIN passkey) to pair securely.
    *   Subscribe to the Meshtastic `FromRadio` characteristic and locate `ToRadio`.
3.  **Implement TCP Bridge (`bridge.cpp`):**
    *   Start the TCP Server on port `4403`.
    *   Read from TCP -> Write to Meshtastic `ToRadio` BLE Characteristic.
    *   Read from Meshtastic `FromRadio` BLE Characteristic -> Write to TCP socket.

## Hardware Note
*   Target hardware is the **Seeed Studio XIAO ESP32S3**.
