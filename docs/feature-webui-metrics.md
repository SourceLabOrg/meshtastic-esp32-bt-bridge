# Feature: Live Diagnostic Metrics in WebUI

## Overview
This feature exposes live diagnostic, telemetry, and queue metrics to the Captive Portal / WebUI. By consolidating runtime status into a unified `GET /status` endpoint and rendering the data contextually across the UI cards, users can monitor bridge health, network status, Bluetooth connectivity, and FreeRTOS queue backpressure without needing a serial console attached.

---

## Core System Performance & Concurrency Principles

During the design and implementation of this feature, every decision is evaluated against the following core tenets:

1. **Overall System Performance:**
   * Telemetry and WebUI inspection must never degrade or block the fast-path BLE ⟷ TCP bridge or MQTT proxy packet forwarding loops.
   * Single consolidated `GET /status` endpoint replaces fragmented endpoint polling (`/mqtt_status`), cutting HTTP request frequency and TCP handshake churn in half.

2. **Thread-Safe Cross-Core Variable Access:**
   * Variables accessed across multiple FreeRTOS tasks and cores (Core 0 AsyncWebServer/lwIP/MQTT vs Core 1 BLE engine/Bridge task) must be memory-safe.
   * Read-only strings/structures established at boot (e.g., mDNS hostname during initialization) are safely published before the web server begins serving incoming HTTP requests.

3. **Wait-Free / Lock-Free Constructs Over Semaphores/Mutexes:**
   * Always prefer `std::atomic` variables (`std::atomic<uint32_t>`, `std::atomic<bool>`) and native non-blocking FreeRTOS inquiry APIs (`uxQueueMessagesWaiting`, `uxTaskGetStackHighWaterMark`) over acquiring mutexes or binary semaphores.
   * Atomic scalar values use relaxed memory ordering (`std::memory_order_relaxed`) for zero lock contention and minimal instruction overhead.
   * Never acquire heavy mutex locks (such as `tcpClientsMutex`) inside WebUI endpoint handlers.

4. **Staleness Concession vs. Throughput Trade-off:**
   * We explicitly prefer slightly out-of-date or staggered sampled telemetry data over any potential impact on overall bridging throughput.
   * If there is a trade-off between 100% strict point-in-time synchronization across cores vs non-blocking performance, we always err on the side of non-blocking execution to keep the bridge running smoothly.
   * All such concessions and trade-offs are explicitly documented in code comments.

---

## Architectural Design

### 1. Unified Status Endpoint (`GET /status`)
To minimize network and CPU impact on the ESP32 (Core 0), the separate `/mqtt_status` endpoint is replaced by a single, consolidated `GET /status` endpoint.

* **Single HTTP Request Per Interval**: Eliminates multiple polling timers, reducing TCP handshakes and preserving ESPAsyncWebServer / LwIP memory.
* **Atomic Telemetry Snapshot**: Samples heap, WiFi, BLE, TCP clients, queue occupancies, and MQTT proxy state in a single tick.
* **Zero Heap Fragmentation**: Response JSON is built with zero intermediate heap allocations into a single pre-reserved buffer (`json.reserve(1024)`).
* **Polling Strategy**:
  * Default Interval: **7 seconds**.
  * **Dynamic Rate Adjustment**: Pressing the <kbd>P</kbd> or <kbd>p</kbd> key anywhere in the WebUI cycles the polling frequency dynamically between **7s ➔ 3s ➔ 1s**, updating the timer and UI label immediately without full page reload.
  * **Polling Behavior**: Enabled by default on page load (with an initial fetch upon `window.onload`) to ensure MQTT telemetry and system health remain continuously synchronized. A toggle switch in the System card allows the user to pause/resume live polling if desired.

#### JSON Response Schema:
```json
{
  "uptime_seconds": 3600,
  "heap": {
    "free_bytes": 114688,
    "min_free_bytes": 98304,
    "max_alloc_bytes": 86016
  },
  "wifi": {
    "connected": true,
    "is_ap_mode": false,
    "ip": "192.168.1.145",
    "rssi": -62,
    "mdns_host": "dsc-ae28.local"
  },
  "ble": {
    "connected": true
  },
  "tcp": {
    "connected_clients": 1,
    "max_clients": 3
  },
  "queues": {
    "tcp_to_ble": {
      "waiting": 2,
      "capacity": 32,
      "dropped": 0
    },
    "ble_to_tcp": {
      "waiting": 0,
      "capacity": 32,
      "dropped": 0
    },
    "mqtt_to_ble": {
      "waiting": 0,
      "capacity": 16,
      "dropped": 0
    }
  },
  "stacks": {
    "ble_task_free_bytes": 4120,
    "net_task_free_bytes": 2048
  },
  "mqtt": {
    "gateway_enabled": true,
    "state": "Connected",
    "radio_proxy_enabled": true,
    "active_server": "mqtt.example.com",
    "active_port": 8883,
    "active_tls": true,
    "active_root": "msh",
    "published": 14,
    "received": 22,
    "dropped": 0
  }
}
```

---

## UI Card Layout & Placement

### 1. WiFi Network Card
* **Connection Status**: Displays a live status pill (`Connected` / `AP Mode` / `Disconnected` / `Not Configured`).
* **IP Address**: Displays active local IP (e.g. `192.168.1.145`) or AP IP (`192.168.4.1`).
* **mDNS Host**: Displays active mDNS broadcast address (e.g. `dsc-ae28.local`).
* **Signal Strength (RSSI)**: Displays RSSI value (e.g. `-58 dBm`) alongside a colored qualitative pill (`Excellent` / `Good` / `Fair` / `Weak`).

### 2. Bluetooth Meshtastic Device Card
* **Connection Status**: Displays a live status pill (`Connected` / `Disconnected` / `Connecting`).

### 3. MQTT Gateway Card
* **Client Status**: Displays live connection status pill (`Connected` / `Waiting for Radio Config` / `Connecting...` / `Disabled` / `Error`), radio proxy detection, active server/port/TLS mode, and traffic counters (Sent / Received).

### 4. System Card
* **Live Polling Toggle**: Toggle switch to pause/resume auto-refreshing stats every 7 seconds (defaults to **Enabled**).
* **System Uptime**: Formatted uptime string (e.g., `1h 24m 12s`).
* **Active TCP Clients**: `X / 3`.
* **Memory (Heap)**: Free Heap, Minimum Recorded Free Heap, and Maximum Allocatable Block (in KB).
* **Task Stack Free Headroom**: Free stack space for `bridge_ble` and `bridge_net` tasks.
* **Queue Buffers (Visual Progress Bars)**:
  * **TCP ➔ BLE Queue**: Capacity bar showing percentage + `X / 32` count and dropped packet counter.
  * **BLE ➔ TCP Queue**: Capacity bar showing percentage + `X / 32` count and dropped packet counter.
  * **MQTT ➔ BLE Queue**: Capacity bar showing percentage + `X / 16` count and dropped packet counter.
  * **Bar Color Thresholds**:
    * `< 50%`: Green / Primary (`#28a745` / `#0066cc`)
    * `50% - 80%`: Orange / Yellow (`#ff9800`)
    * `> 80%`: Red (`#dc3545`)

---

## Implementation Plan & Tasks

### Checklist / TODOs
- [x] **Task 1: Expose mDNS Host in `wifi_net`**
  - Add `String wifi_net_get_mdns_host()` in `include/wifi_net.h` and `src/wifi_net.cpp`.
  - Cache the active mDNS hostname during `wifi_net_start_mdns()`.
- [x] **Task 2: Build Unified `GET /status` Endpoint in `config_ui`**
  - Remove deprecated `GET /mqtt_status` route.
  - Implement `GET /status` route aggregating uptime, heap, wifi, ble, tcp, queues, stacks, and mqtt stats in a wait-free manner.
  - Pre-allocate JSON String buffer with `.reserve(768)` to eliminate heap fragmentation.
  - Add performance and concurrency trade-off comments explaining the lock-free design.
- [x] **Task 3: Implement WebUI Frontend Changes in `config_ui.cpp`**
  - **CSS Styling**: Add CSS styles for `.progress-track`, `.progress-bar`, `.progress-green`, `.progress-yellow`, `.progress-red`.
  - **WiFi Card HTML**: Add rows for IP, mDNS Host, and RSSI.
  - **BLE Card HTML**: Add row with BLE Connection Status pill.
  - **System Card HTML**: Add Uptime, TCP Clients, Memory, Stacks, and Queue Buffers with progress bars. Add Live Polling pause/resume toggle.
  - **JavaScript**: Implement `updateStatus()`, initial fetch on load, and default 7-second polling interval.
- [x] **Task 4: Compilation & Build Verification**
  - Build across all supported hardware environments (`seeed_xiao_esp32s3`, `esp32dev`, `esp32-s3-devkitc-1`) via PlatformIO to ensure zero build errors or memory overflows.
- [x] **Task 5: Documentation Updates**
  - Update `docs/configuration_ui.md` with the new `/status` schema and UI changes.
  - Update `TODO.md` roadmap items.

---

## Technical Notes, Issues & Decisions Log
*(This section is maintained throughout the implementation process as issues or design adjustments arise.)*

* **Decision 1 (Oct 9, 2026):** Consolidated `/mqtt_status` into `GET /status` to cut HTTP polling frequency in half and save Core 0 CPU/heap.
* **Decision 2 (Oct 9, 2026):** Set live polling default to **Enabled** at a 7-second interval so MQTT telemetry and queue diagnostics stay live out of the box, with a pause toggle in the UI.
* **Decision 3 (Oct 9, 2026):** Concurrency, Memory Safety & Throughput Concession: Diagnostics endpoint `/status` reads cross-core telemetry in a 100% wait-free manner using atomic operations (`std::memory_order_relaxed`) and non-blocking FreeRTOS inspector functions (`uxQueueMessagesWaiting`, `uxTaskGetStackHighWaterMark`). We intentionally avoid acquiring heavy locks (like `tcpClientsMutex`) in the webserver thread, preferring slightly staggered/sampled telemetry snapshots over any potential bridging throughput stalls.
