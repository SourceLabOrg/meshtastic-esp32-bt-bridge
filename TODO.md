# Project Status & Next Steps

## Completed So Far
*   **Architecture & Design:** Established the core plan (PlatformIO, Arduino Core, ESP32-S3 target).
*   **Captive Portal (Config UI):** Implemented the asynchronous web server (`config_ui.cpp`) that serves a mobile-friendly setup page. It handles reading/writing WiFi and BLE credentials to the ESP32's non-volatile storage (NVS).
*   **WiFi / Boot Logic:** Implemented `wifi_net.cpp` to check the physical `BOOT` button on startup, attempt to connect to saved WiFi, and fallback to the Captive Portal Access Point (`Meshtastic-Bridge-Setup`) if it fails.
*   **Stubs:** Created dummy functions in `ble_client.cpp` so the project compiles and the Web UI can be tested immediately upon flashing.
*   **Development Environment:** Created IntelliJ run configurations (`.idea/runConfigurations/`) and documented the build process in `docs/development.md`.

## Next Session (When the XIAO ESP32-S3 arrives)
1.  **Update Board Config:** Change `platformio.ini` to use `board = seeed_xiao_esp32s3` instead of the generic devkitc board.
2.  **Flash & Test UI:** Flash the ESP32-S3 via IntelliJ (`PIO Upload`), connect to the `Meshtastic-Bridge-Setup` WiFi network on your phone/laptop, and verify the UI looks and behaves correctly.
3.  **Implement BLE Client (`ble_client.cpp`):** 
    *   Replace the dummy scan with a real `NimBLE` scan for Meshtastic nodes.
    *   Implement the connection logic to connect to the target MAC address saved in NVS.
    *   Implement the `NimBLE` security callbacks to pass the saved 6-digit PIN to the Meshtastic radio during pairing.
4.  **Implement the Bridge (`bridge.cpp`):**
    *   Start the TCP Server on port `4403`.
    *   Read from TCP -> Write to Meshtastic `ToRadio` BLE Characteristic.
    *   Read from Meshtastic `FromRadio` BLE Characteristic -> Write to TCP socket.

## Hardware Note
*   Target hardware is the **Seeed Studio XIAO ESP32S3**.
