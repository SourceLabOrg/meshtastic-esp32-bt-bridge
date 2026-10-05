# Project Status & Next Steps

## Completed So Far
*   **Architecture & Design:** Established the core plan (PlatformIO, Arduino Core, ESP32-S3 target).
*   **Captive Portal (Config UI):** Implemented the asynchronous web server (`config_ui.cpp`).
*   **WiFi / Boot Logic:** Implemented `wifi_net.cpp` (fallback to AP, mDNS broadcast).
*   **BLE Scanning & Connection Verification (`ble_client.cpp`):** Active scanning, MAC deduplication, background testing.
*   **Implement Normal Operation Mode BLE Connection (`ble_client.cpp`):** Handles connects, reconnects, and FromRadio/FromNum subscriptions.
*   **Implement TCP Bridge (`bridge.cpp`):** TCP server on port 4403, bidirectional routing, dynamic fast-polling, identical packet deduplication, Nagle's algorithm batched transmissions, robust queue backpressure, Mutex protection for concurrent TCP disconnects, and stack-allocated TCP frames to prevent heap fragmentation.

## Next Steps: Code Review & Refactoring
1.  **Queue Memory Optimization:** Replace `malloc()` inside `BridgePacket` with a statically sized `uint8_t data[512]` array to completely prevent heap fragmentation from rapid queue allocations, trading ~50KB of SRAM for flawless memory safety.
2.  **Variable Cleanup:** Remove manual `connectedClientsCount` and rely on `tcpClients.size()` under the new Mutex lock. Refactor global target pin/mac variables to be passed cleanly into `bridge_init`.

## Hardware Note
*   Target hardware is the **Seeed Studio XIAO ESP32S3**.

