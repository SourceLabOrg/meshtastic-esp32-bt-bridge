# BLE Task Optimization Guide & Tracking

This document outlines planned performance, memory, and latency optimizations for the Bluetooth Low Energy (BLE) engine (`bridgeBleTask`) in `src/bridge.cpp`. It serves as the active roadmap and checklist for implementation.

---

## Core Engineering Principles & Requirements

When designing and implementing any change in this roadmap, adhere strictly to the following guidelines:

1. **Documentation in Lockstep**:
   * Any modification to architecture, queue behaviors, timeouts, or task responsibilities must be updated across all relevant docs (`docs/architecture.md`, `docs/bridge_architecture.md`, `docs/development.md`, etc.) in the exact same commit/PR.
2. **Zero / Minimal Heap Footprint**:
   * Maintain the project's zero-fragmentation goal in fast paths. Avoid dynamic memory allocation (`malloc`, `new`, `std::string`, `String`) in the bridging loop. Prefer fixed stack/static buffers.
3. **Non-Blocking & Task Isolation**:
   * Never block critical networking stacks (`AsyncTCP`, `LwIP`) or high-priority tasks. Keep task interactions non-blocking or bounded with strict backpressure timeouts.
4. **Lock-Free & Thread-Safe Concurrency**:
   * Favor lock-free atomic primitives (`std::atomic`, memory order relaxed/acquire/release) and FreeRTOS direct-to-task notifications over heavy mutexes wherever applicable. Ensure cross-core memory visibility between Core 0 and Core 1.

---

## Optimization Roadmap & Status

### Phase 1: Zero-Allocation Fast-Path Buffer (Memory & Fragmentation)
- [ ] **Status: Planned**
- **Target File(s)**: `src/bridge.cpp`
- **Goal**: Eliminate `std::string` heap allocations and copy operations during `FromRadio` reads.
- **Implementation Details**:
  - Replace `std::string currentVal` and `static std::string lastPacket` with direct `NimBLEAttValue` inspection and a static byte buffer (`uint8_t lastPacket[MESHTASTIC_MAX_PACKET_SIZE]`, `size_t lastPacketLen`).
  - Use `memcmp()` for payload difference checks.
  - Zero dynamic allocations on every GATT read.
- **Verification**:
  - Monitor heap high-water mark via telemetry (`diag_telemetry`) to confirm 0 heap fluctuation during continuous high-throughput packet transfers.

---

### Phase 2: Tight Burst-Draining Loop (Sync & Throughput Performance)
- [ ] **Status: Planned**
- **Target File(s)**: `src/bridge.cpp`
- **Goal**: Accelerate initial node and channel sync by draining the `FromRadio` mailbox in an inner loop.
- **Implementation Details**:
  - Instead of yielding 1ms between individual burst packets, drain `FromRadio` in a tight loop until `readValue()` returns empty or unchanged.
  - Enforce a maximum batch limit (e.g., max 16 packets per cycle) to guarantee fairness and prevent starving outgoing transmissions to `ToRadio`.
- **Verification**:
  - Measure time-to-sync when connecting Meshtastic WebUI/App from initial connection to fully populated node list.

---

### Phase 3: Event-Driven Task Notification (Latency & CPU Utilization)
- [ ] **Status: Planned**
- **Target File(s)**: `src/bridge.cpp`
- **Goal**: Replace the 10ms idle polling delay with FreeRTOS Direct-to-Task Notifications.
- **Implementation Details**:
  - When `notifyFromNum` fires, trigger `xTaskNotifyGive(bridgeBleTaskHandle)` / `vTaskNotifyGiveFromISR(...)` to wake `bridgeBleTask` instantly.
  - When packets are pushed to `tcp_to_ble_queue` or `mqtt_to_ble_queue`, notify `bridgeBleTask`.
  - Replace `static volatile bool pendingRadioRead` with an atomic flag / task notification state.
  - `bridgeBleTask` blocks efficiently on `ulTaskNotifyTake` or the queue with a long failsafe timeout instead of spinning every 10ms.
- **Verification**:
  - Validate round-trip ping latency between TCP client and BLE radio; confirm idle CPU wake frequency drops from ~90 Hz to near 0 Hz.

---

### Phase 4: Adaptive Failsafe Polling (Radio Airtime & Power Conservation)
- [ ] **Status: Planned**
- **Target File(s)**: `src/bridge.cpp`, `include/build_options.h`
- **Goal**: Reduce unnecessary over-the-air GATT reads when the BLE connection is idle.
- **Implementation Details**:
  - Increase the failsafe poll interval from 250ms to 1500ms–2500ms (configurable via `BLE_IDLE_POLL_INTERVAL_MS`).
  - Rely on `FromNum` notifications for immediate packet delivery; keep the periodic read strictly as a recovery fallback for dropped BLE packets.
- **Verification**:
  - Verify that idle BLE radio traffic is reduced by >80% without introducing packet loss on nodes that intermittently fail to notify.

---

## Progress Log

| Date | Phase / Item | Description of Change | Impact / Benchmark Results | Docs Updated |
| :--- | :--- | :--- | :--- | :---: |
| *Pending* | - | Initial plan created | - | - |
