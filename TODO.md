# Project Status & Next Steps

## Completed So Far
*   **Architecture & Design:** Established the core plan (PlatformIO, Arduino Core, ESP32-S3 target).
*   **Captive Portal (Config UI):** Implemented the asynchronous web server (`config_ui.cpp`) that serves a mobile-friendly setup page with independent cards for WiFi, Bluetooth Target, and System Actions.
    *   Prominent edit buttons, password unmask toggles, and dynamic scanning/testing disabled states.
    *   Stores and displays device name alongside MAC address: `Target Device: Name (AA:BB:CC:DD:EE:FF)`.
*   **WiFi / Boot Logic:** Implemented `wifi_net.cpp` to check the physical `BOOT` button on startup, attempt to connect to saved WiFi, and fallback to the Captive Portal Access Point (`Meshtastic-Bridge-Setup`) if it fails.
*   **BLE Scanning & Connection Verification (`ble_client.cpp`):**
    *   Active BLE scanning (4s) with MAC deduplication, RSSI signal display, and device name extraction.
    *   Non-blocking background testing task on Core 1 verifying PIN pairing and Meshtastic GATT services/characteristics.
    *   Bond clearing before and after tests ensuring genuine passkey challenges.

## Next Steps
1.  **Implement Normal Operation Mode BLE Connection (`ble_client.cpp`):**
    *   Connect to configured target MAC on boot when in normal mode.
    *   Maintain active connection and handle automatic reconnects if signal is lost.
    *   Subscribe to Meshtastic `FromRadio` notifications.
2.  **Implement TCP Bridge (`bridge.cpp`):**
    *   Start the TCP Server on port `4403`.
    *   Read from TCP -> Write to Meshtastic `ToRadio` BLE Characteristic.
    *   Read from Meshtastic `FromRadio` BLE Characteristic -> Write to TCP socket.

## Hardware Note
*   Target hardware is the **Seeed Studio XIAO ESP32S3**.

