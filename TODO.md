# Project Status & Next Steps

## Completed So Far
*   **Architecture & Design:** Established the core plan (PlatformIO, Arduino Core, ESP32-S3 target).
*   **Captive Portal (Config UI):** Implemented the asynchronous web server (`config_ui.cpp`).
*   **WiFi / Boot Logic:** Implemented `wifi_net.cpp` (fallback to AP, mDNS broadcast).
*   **BLE Scanning & Connection Verification (`ble_client.cpp`):** Active scanning, MAC deduplication, background testing.
*   **Implement Normal Operation Mode BLE Connection (`ble_client.cpp`):** Handles connects, reconnects, and FromRadio/FromNum subscriptions.
*   **Implement TCP Bridge (`bridge.cpp`):** TCP server on port 4403, bidirectional routing, dynamic fast-polling, identical packet deduplication, Nagle's algorithm batched transmissions, robust queue backpressure, Mutex protection for concurrent TCP disconnects, and stack-allocated TCP frames to prevent heap fragmentation.
*   **Code Review & Refactoring:** Eliminated all heap fragmentation by changing `BridgePacket` to use a statically sized `uint8_t data[512]` array (trading ~50KB SRAM for memory safety) and refactored `bridge_init` to cleanly accept target arguments from the WiFi boot task.
*   **Dynamic configuration UI:** Added runtime toggle for serial debug logs to the captive portal.
*   **Dynamic mDNS naming:** The bridge automatically sanitizes the saved Bluetooth name and broadcasts it dynamically (e.g. `DSC_AE25-bridge.local`), conforming strictly to RFC 1035 length and character limits.
*   **Visual Status LED:** Added an asynchronous FreeRTOS LED task (`status_led.cpp`) mapping system states to blink patterns (Fast Blink: Boot, Slow Blink: AP Mode, Medium Blink: Bridge Searching, Solid On: Bridge Connected). Documented in `docs/user_guide.md`.

## Hardware Note
*   Target hardware is the **Seeed Studio XIAO ESP32S3**.

