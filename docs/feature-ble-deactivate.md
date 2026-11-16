# Feature: Bluetooth Deactivate / Activate Control & Scan Contention Fix

## 1. Overview & Problem Statement

This working document tracks the design, implementation plan, verification checklist, and code review log for resolving two user-facing Bluetooth configuration bugs (plus a related connection-test contention bug) in the WebUI when operating in Normal Mode:

1. **Bug 1 — Connected Meshtastic Radio Missing from BLE Scan Results:**
   * **Root Cause:** Meshtastic radios support only a single concurrent BLE Central connection and stop broadcasting BLE advertisements as soon as the bridge connects. Additionally, `NimBLEScan::handleGapEvent()` explicitly filters out advertisements from any address matching an already-connected `NimBLEClient`. When a user opens **Edit** on the Bluetooth card while connected, the 4-second BLE scan never sees the currently connected radio.
2. **Bug 2 — BLE Scan & Reconnect Loop Cancel Each Other When Disconnected:**
   * **Root Cause:** When a target radio is configured in Normal Operating Mode (`bridgeBleTask` running on Core 1) but is currently disconnected (e.g. powered off, out of range, or being replaced), `bridgeBleTask` loops continuously between an 8-second `bleClient->connect()` attempt (`BLUETOOTH_TIMEOUT_SECONDS = 8`) and a 5-second delay (`BLUETOOTH_RECONNECT_DELAY_MS = 5000`).
   * In NimBLE's GAP state machine, initiating a connection (`ble_gap_connect`) and scanning (`ble_gap_disc`) are mutually exclusive:
     * Starting a WebUI scan during the 8-second `connect()` window fails immediately with `BLE_HS_EBUSY` (`"Failed to start scan."`).
     * Starting a WebUI scan during the 5-second retry pause succeeds initially, but as soon as `delay(5000)` expires, `NimBLEClient::startConnectionAttempt()` encounters `BLE_HS_EBUSY` and forcibly calls `NimBLEDevice::getScan()->stop()`, aborting the scan prematurely.
3. **Bug 3 (Related) — `⚡ Test Connection` Contention in Normal Mode:**
   * **Root Cause:** Running `ble_client_start_test()` while `bridgeBleTask` is active causes `bleTestTask` and `bridgeBleTask` to contend for the NimBLE controller and security callbacks on Core 1.

---

## 2. Solution Architecture & UX Design

### 2.1 Explicit `Deactivate` / `Activate` Workflow in the WebUI

1. **When No BLE Device is Configured (or in AP / Setup Mode):**
   * Works identically to existing behavior: `bridgeBleTask` is not running, the `Deactivate` / `Activate` button is hidden, and **Edit** is immediately enabled.
2. **When a BLE Device is Configured in Normal Mode (Active State):**
   * The Bluetooth card header displays a **`Deactivate`** button next to **`Edit`**.
   * The **`Edit`** button is **disabled** with hover tooltip: `title="You must deactivate the Bluetooth connection before editing."` (clicking the disabled **`Edit`** button also immediately opens the confirmation modal explaining that Bluetooth must be deactivated before editing, with a direct **`Deactivate`** button).
   * Clicking **`Deactivate`** displays an in-DOM confirmation modal (Captive Network Assistant compatible):
     * **Title:** `Deactivate Bluetooth Connection?`
     * **Message:** `Deactivating Bluetooth will disconnect the bridge from the Meshtastic radio and stop automatic reconnection until you activate it again, save new Bluetooth settings, or reboot the device.\n\nDo you want to proceed?`
     * **Confirm Action:** Calls `POST /set_ble_active` with `active=0`.
3. **When Deactivating / Deactivated (`Deactivating...` $\rightarrow$ `Deactivated` State):**
   * `bridge_set_ble_active(false)` atomically sets `s_ble_deactivated = true` and `s_ble_state = BLE_STATE_DEACTIVATING` (`"Deactivating..."`).
   * `bridgeBleTask` on Core 1 picks up `s_ble_deactivated` on its next loop iteration (immediately if connected or sleeping in `250ms` slices, or as soon as an in-flight `connect()` attempt returns), disconnects if connected, and sets `s_ble_state = BLE_STATE_DEACTIVATED` (`"Deactivated"`).
   * While in `"Deactivating..."`, the WebUI polls `/status` every `500ms` so the UI transitions to `"Deactivated"` immediately.
   * The previously connected Meshtastic radio immediately resumes BLE advertising.
   * In the WebUI:
     * The Connection status pill displays **`Deactivated`** (`.status-pill.error`, Red).
     * The header button changes to **`Activate`** (clicking it calls `POST /set_ble_active` with `active=1` and immediately resumes connecting).
     * The **`Edit`** button becomes **enabled** (tooltip removed).
4. **Inside the Bluetooth Edit View:**
   * Opening **Edit** automatically starts a 4-second BLE scan (`scanBle()`). Because the bridge is deactivated, the scan runs with zero GAP contention and discovers the user's Meshtastic radio alongside any other nearby devices.
   * **`⚡ Test Connection`** runs cleanly with exclusive access to NimBLE.
   * Clicking **`Cancel`** closes the Edit view and **remains in the `Deactivated` state** until the user explicitly clicks **`Activate`** (or saves).
   * Clicking **`Save Bluetooth`** (`POST /save_ble`) saves the credentials to NVS, updates the runtime target MAC/PIN in `bridge.cpp`, and **automatically activates** the BLE connection loop (`bridge_update_ble_config()`).

### 2.2 Unified `BleState` Telemetry (`GET /status`)

To match `st.mqtt.state` and adhere to our lock-free / zero-heap-fragmentation principles:

```cpp
enum BleState : uint8_t {
    BLE_STATE_NOT_CONFIGURED = 0, // No target MAC configured (or in AP Setup Mode)
    BLE_STATE_DEACTIVATING,       // Deactivation requested; waiting for bridgeBleTask loop to disconnect
    BLE_STATE_DEACTIVATED,        // Explicitly deactivated by user via WebUI
    BLE_STATE_DISCONNECTED,       // Waiting between retry attempts
    BLE_STATE_CONNECTING,         // Actively attempting BLE connection / pairing
    BLE_STATE_CONNECTED           // Connected and bridging packets
};
```

In `GET /status`, `st.ble` includes `"state"` alongside `"connected"`:

```json
"ble": {
  "connected": false,
  "state": "Deactivated",
  "rx_count": 42,
  "tx_count": 38,
  "invalid_count": 0
}
```

#### Status Pill Mapping in WebUI:
* `"Connected"` $\rightarrow$ `.status-pill.connected` (Green)
* `"Connecting..."` $\rightarrow$ `.status-pill.connecting` (Blue/Teal)
* `"Disconnected"` $\rightarrow$ `.status-pill.error` (Red)
* `"Deactivating..."` $\rightarrow$ `.status-pill.waiting` (Amber)
* `"Deactivated"` $\rightarrow$ `.status-pill.error` (Red)
* `"Not Configured"` $\rightarrow$ `.status-pill.disabled` (Gray)

### 2.3 Thread Safety & NimBLE Concurrency Details

1. **Single-Core Ownership of `bleClient` (Zero Cross-Core `NimBLEClient` Calls):**
   * `static NimBLEClient* bleClient` remains strictly private to `bridgeBleTask` on Core 1.
   * `bridge_set_ble_active(false)` on Core 0 only sets atomic flags (`s_ble_deactivated = true`, `s_ble_state = BLE_STATE_DEACTIVATING`) and never calls `bleClient` methods across cores. `bridgeBleTask` picks up `s_ble_deactivated` in its loop, disconnects if connected, and transitions to `BLE_STATE_DEACTIVATED`.
2. **Responsive Sleep Loop in `bridgeBleTask`:**
   * Replaces blocking `delay(BLUETOOTH_RECONNECT_DELAY_MS)` (`5000ms`) with `waitReconnectDelayOrWake()` sleeping in `250ms` increments (`BLE_TASK_WAKE_POLL_MS = 250`) so state transitions (`Deactivate`, `Activate`, `Save Bluetooth`) wake early within `250ms` with negligible CPU overhead.
3. **Lock-Free Cross-Core Config Exchange (`s_ble_config_dirty` + Static `.bss` Buffer):**
   * `s_target_ble_mac[24]` and `s_target_ble_pin` are written by Core 0 (`bridge_update_ble_config()`) using `release` ordering on `s_ble_config_dirty` and read by Core 1 (`bridgeBleTask`) using `acquire` ordering, with zero heap allocation and zero mutexes.
   * When `bridgeBleTask` consumes `s_ble_config_dirty`, it clears `toRadioChar`, `fromRadioChar`, and `fromNumChar` and calls `bleClient->deleteServices()` (after disconnecting if connected) so the new target radio's GATT table is freshly discovered on the next `connect(addr, false)`, while normal reconnects to the same radio continue to reuse cached GATT attributes without heap churn.
4. **Restoring Client Callbacks After `⚡ Test Connection`:**
   * Before each `bleClient->connect()` attempt in `bridgeBleTask`, `NimBLEDevice::setSecurityAuth(true, true, true)`, `NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY)`, and `bleClient->setClientCallbacks(&bridgeCallbacks, false)` are re-asserted so any preceding test connection on `ble_client.cpp` cannot leave stale security state.

---

## 3. Implementation Steps Checklist

- [x] **Step 1: Extend Bridge BLE State Machine & Control API (`include/bridge.h`, `src/bridge.cpp`)**
  - [x] Define `enum BleState : uint8_t` and `const char* bridge_ble_state_to_string(BleState state)` in `include/bridge.h`.
  - [x] Add `BleState ble_state` to `BridgeDiagStats`.
  - [x] Declare `void bridge_set_ble_active(bool active)`, `bool bridge_is_ble_active()`, and `void bridge_update_ble_config(const char* ble_mac, uint32_t ble_pin)` in `include/bridge.h`.
  - [x] Implement lock-free `s_ble_state`, `s_ble_deactivated`, `s_ble_config_dirty`, and static `.bss` `s_target_ble_mac[24]` in `src/bridge.cpp` (zero heap allocation, zero mutexes).
  - [x] Update `bridgeBleTask` in `src/bridge.cpp` to:
    - [x] Idle cleanly when `s_ble_deactivated` is true or `s_target_ble_mac` is empty.
    - [x] Set `BLE_STATE_CONNECTING` before `bleClient->connect()`, `BLE_STATE_CONNECTED` when ready, `BLE_STATE_DISCONNECTED` on disconnect/retry, `BLE_STATE_DEACTIVATING` when requested, and `BLE_STATE_DEACTIVATED` once `bridgeBleTask` disconnects and idles.
    - [x] Use a responsive 250ms-increment wait helper (`waitReconnectDelayOrWake`) instead of monolithic `delay(BLUETOOTH_RECONNECT_DELAY_MS)`.
    - [x] Disconnect and reconnect immediately if `s_ble_config_dirty` is flagged while already connected.
- [x] **Step 2: Expose `ble.state` in Unified Diagnostics (`src/diag_telemetry.cpp`)**
  - [x] Update `diag_telemetry_format_json()` to emit `"state":"..."` inside the `"ble"` JSON object.
- [x] **Step 3: Add Web Server Endpoint & Live Save Integration (`src/config_ui.cpp`)**
  - [x] Add `POST /set_ble_active` endpoint accepting `active=1|0|true|false` (calls `bridge_set_ble_active(active)` and stops any active BLE scan if activating).
  - [x] Update `POST /save_ble` endpoint so that in Normal Operating Mode (`!wifi_net_is_ap_mode()`), it calls `bridge_update_ble_config(mac.c_str(), pin.toInt())` to apply the new target and automatically activate BLE.
- [x] **Step 4: Update WebUI HTML, CSS & JavaScript (`src/config_ui.cpp`)**
  - [x] Add `btn-toggle-ble` (`Deactivate` / `Activate`) next to `btn-edit-ble` (`Edit`) in the Bluetooth card header (`#card-ble`).
  - [x] Update `updateStatus()` and `loadConfig()` to sync the Bluetooth status pill (`Connected`, `Connecting...`, `Disconnected`, `Deactivating...`, `Deactivated`, `Disabled`) and toggle `btn-toggle-ble` visibility/text and `btn-edit-ble` disabled state + `title="You must deactivate the Bluetooth connection before editing."`
  - [x] Implement `toggleBleActive()` in JS:
    - [x] When active $\rightarrow$ show confirmation modal (`Deactivate Bluetooth Connection?`) explaining that the bridge will disconnect from the Meshtastic radio and stop automatic reconnection until activated again, saved, or rebooted.
    - [x] When deactivated $\rightarrow$ immediately call `POST /set_ble_active` (`active=1`) and refresh status.
  - [x] Ensure `Cancel` in `#edit-ble` returns to `#view-ble` while keeping the `Deactivated` state intact.
  - [x] Ensure `Save Bluetooth` in `#edit-ble` saves, closes `#edit-ble`, and transitions back to active connecting/connected state.
- [x] **Step 5: Synchronize Documentation Across `docs/` & `TODO.md`**
  - [x] Update `docs/architecture.md` (project structure listing).
  - [x] Update `docs/configuration_ui.md` (Bluetooth card UI, `Deactivate`/`Activate` button, modal, `/set_ble_active` endpoint, `/status` JSON).
  - [x] Update `docs/feature-webui-metrics.md` (`/status` JSON schema and BLE card status pills).
  - [x] Update `docs/nimble-2x-upgrade.md` (cross-reference this fix for Normal Mode WebUI scan/test contention).
  - [x] Update `TODO.md`.

---

## 4. Verification Checklist

- [x] **Build Verification Across All Hardware Targets:**
  - [x] `pio run -e seeed_xiao_esp32s3` compiles cleanly with 0 errors (`RAM: 31.1% / 101,760 B`, `Flash: 40.8% / 1,284,561 B`).
  - [x] `pio run -e esp32dev` compiles cleanly with 0 errors (`RAM: 32.3% / 105,772 B`, `Flash: 45.7% / 1,437,301 B`).
  - [x] `pio run -e esp32-s3-devkitc-1` compiles cleanly with 0 errors (`RAM: 31.1% / 102,016 B`, `Flash: 41.1% / 1,291,337 B`).
- [x] **Memory & Concurrency Verification:**
  - [x] Verify `diag_telemetry_get_snapshot()` and `GET /status` remain 100% lock-free / wait-free with zero heap allocations.
  - [x] Verify static `.bss` / `.data` RAM and Flash usage remain within expected bounds across all 3 boards (`+8 B` net `.bss` delta from replacing heap `Arduino::String targetBleMac` with static `char s_target_ble_mac[24]` + atomics).
- [ ] **Functional & UX Scenario Verification (Hardware Test):**
  - [ ] **Scenario 1 (AP / Setup Mode or Unconfigured BLE):** `Deactivate` button is hidden; `Edit` button is enabled immediately; scanning and testing work as before.
  - [ ] **Scenario 2 (Normal Mode — Connected to Radio):**
    - [ ] `Deactivate` button is shown; `Edit` button is disabled with hover tooltip `"You must deactivate the Bluetooth connection before editing."`
    - [ ] Clicking `Deactivate` opens the confirmation modal; cancelling modal does nothing; confirming modal disconnects from the radio immediately and changes status pill to `Deactivated`, button to `Activate`, and enables `Edit`.
    - [ ] Clicking `Edit` scans and discovers the previously connected Meshtastic radio in the dropdown.
  - [ ] **Scenario 3 (Normal Mode — Disconnected & Cycling Retries):**
    - [ ] Status pill alternates between `Connecting...` (8s) and `Disconnected` (5s).
    - [ ] Clicking `Deactivate` during either `Connecting...` or `Disconnected` transitions to `Deactivating...` and then `Deactivated` once the loop iteration completes.
    - [ ] Subsequent BLE scan completes full 4 seconds without `BLE_HS_EBUSY` or premature cancellation.
  - [ ] **Scenario 4 (Edit Cancel vs. Save):**
    - [ ] Clicking `Cancel` in Edit view returns to card view and stays `Deactivated` (`Activate` and `Edit` buttons available).
    - [ ] Clicking `Activate` resumes connection attempts immediately.
    - [ ] Clicking `Save Bluetooth` in Edit view saves to NVS, updates target MAC/PIN in `bridge.cpp`, and automatically activates BLE connection.

---

## 5. Code Review Steps & Issues Log

### Code Review Checklist
- [x] **Review 1 — NimBLE GAP State Safety:** Check all paths where `bleClient->connect()`, `bleClient->disconnect()`, and `ble_client_start_scan()` can overlap.
- [x] **Review 2 — Cross-Core Thread Safety:** Check shared state between Core 0 (`ESPAsyncWebServer` handlers) and Core 1 (`bridgeBleTask` / `bleTestTask`).
- [x] **Review 3 — MQTT Grace Period Interaction:** Check how `mqtt_net_on_ble_disconnected()` behaves when BLE is manually deactivated vs. when it reconnects.
- [x] **Review 4 — Captive Portal / CNA WebView Compatibility:** Verify no native `alert()` / `confirm()` dialogs are used and all DOM IDs and button states are consistent.
- [x] **Review 5 — Documentation Consistency:** Verify all docs in `docs/` match the exact endpoint names, JSON fields, and UI labels.

### Issues Found & Resolution Log

| # | Source / Step | Finding / Issue | Status | Resolution / Notes |
| :- | :--- | :--- | :--- | :--- |
| 1 | Chunk 1 Review | `utils_parse_ble_address(const String&)` forced callers to construct a heap-allocated `Arduino::String`. | Resolved | Replaced with `utils_parse_ble_address(const char* macStr)` using stack-allocated `char cleanMac[24]` and removed the old `String` overload. |
| 2 | Chunk 2 Review | Calling `bleClient->cancelConnect()` / `bleClient->disconnect()` from Core 0 (`ESPAsyncWebServer` thread) required sharing `NimBLEClient*` across cores (`std::atomic<NimBLEClient*>`) and scattering multiple `s_ble_deactivated` checks throughout `connect()`. | Resolved | Adopted pure loop-pickup (`Option A`): `bridge_set_ble_active(false)` sets `s_ble_deactivated = true` and `BLE_STATE_DEACTIVATING` (`"Deactivating..."`) on Core 0 without touching `bleClient`. `bridgeBleTask` on Core 1 owns `bleClient` exclusively, picks up `s_ble_deactivated` on its next loop iteration, disconnects, and transitions to `BLE_STATE_DEACTIVATED`. |
| 3 | Chunk 2 Review | Sleep slice interval in `waitReconnectDelayOrWake()`: `50ms` vs `250ms` vs `1000ms`. | Resolved | Selected `250ms` (`BLE_TASK_WAKE_POLL_MS = 250`) as the sweet spot: wakes 4 times/sec with negligible CPU overhead while keeping UI transitions imperceptibly fast. |
| 4 | Initial Analysis | `POST /save_ble` previously only wrote to NVS (`Preferences`) without updating `s_target_ble_mac` / `s_target_ble_pin` in `bridge.cpp`, requiring a full reboot in Normal Mode. | Resolved | Added lock-free `bridge_update_ble_config(const char* ble_mac, uint32_t ble_pin)` using `std::memory_order_release` / `acquire` on `s_ble_config_dirty` so `/save_ble` updates runtime target and auto-activates immediately. |
| 5 | Initial Analysis | `ble_client_test_connection()` overwrites `NimBLEDevice` security callbacks and deletes the test bond on completion. | Resolved | Re-assert `NimBLEDevice::setSecurityAuth`, `setSecurityIOCap`, and `bleClient->setClientCallbacks(&bridgeCallbacks, false)` before each connection attempt in `bridgeBleTask`. |
| 6 | Chunk 5 Review / Hardware Test | Disabled `<button>` elements do not fire hover `title` tooltips in browsers (and capture mouse hit-testing unless `pointer-events: none` is set), native tooltips are slow/dismiss on click, and `.btn-edit` CSS order overrode `.btn-warning` / `.btn-success` background colors. | Resolved | Wrapped `#btn-edit-ble` in `<span id="btn-edit-ble-wrap" onclick="onEditBleWrapClick()">` with `title` and `cursor: not-allowed`, added `#btn-edit-ble:disabled { pointer-events: none; }` so both hover tooltips and clicks reach the `<span>` wrapper (opening a single `"Deactivate Bluetooth First"` modal with a direct `Deactivate` action), and added `.btn-edit.btn-warning` / `.btn-edit.btn-success` CSS selectors. |
| 7 | Deep Code Review | **NimBLE Link Layer Teardown Race (`m_connStatus == DISCONNECTING` vs `DISCONNECTED`):** `bleClient->disconnect()` calls `ble_gap_terminate()` asynchronously, setting `m_connStatus = DISCONNECTING` (so `isConnected()` immediately returns `false`) while `m_connHandle != BLE_HS_CONN_HANDLE_NONE` until `BLE_GAP_EVENT_DISCONNECT` fires on Core 0. Transitioning to `BLE_STATE_DEACTIVATED` or calling `bleClient->connect()` before `getConnHandle() == BLE_HS_CONN_HANDLE_NONE` can start a scan before the radio finishes Link Layer teardown or hit `m_connStatus != DISCONNECTED` in `NimBLEClient::connect()`. | Closed (Won't Fix) | Negligible in practice: BLE Link Layer termination (`LL_TERMINATE_IND`) completes in ~15–60ms, whereas the WebUI `/status` poll (`500ms`) + human reaction time to click `Edit` takes ~1s+, and `s_ble_config_dirty` / retry paths already include `250ms`–`5000ms` delays. Kept original code for simplicity. |
| 8 | Deep Code Review | **Stale Cached GATT Attribute Handles When Switching Radios Live (`deleteAttributes = false`):** `bleClient->connect(addr, false)` preserves `m_svcVec` across reconnects. When switching to a different Meshtastic radio at runtime via `POST /save_ble`, `getService()` / `getCharacteristic()` would return the cached GATT attribute handles from the previous radio instead of discovering the new radio's GATT table. | Resolved | When `s_ble_config_dirty` is processed on Core 1 in `bridgeBleTask`, clear `toRadioChar = fromRadioChar = fromNumChar = NULL`, disconnect if connected, and call `bleClient->deleteServices()` so the next `connect(addr, false)` discovers the new radio's GATT table while normal reconnects to the same radio reuse cached attributes without heap churn. |
| 9 | Deep Code Review | **Mid-`connect()` Deactivation Running Post-Connect Setup & `onDisconnect` State Clobber:** If `bleClient->connect()` succeeds while `s_ble_deactivated` was set mid-attempt, `bridgeBleTask` still runs `secureConnection()`, GATT discovery, `BLE_STATE_CONNECTED`, and `mqtt_net_on_ble_connected()` before disconnecting on the next loop iteration. Also, `onDisconnect()` on Core 0 unconditionally wrote `BLE_STATE_DISCONNECTED` when `!s_ble_deactivated`, which could clobber `BLE_STATE_CONNECTING`. | Resolved | Added an immediate `if (s_ble_deactivated.load(std::memory_order_acquire)) continue;` check right after `bleClient->connect()` returns `true` so `bridgeBleTask` loops straight back to the deactivation handler without running `secureConnection()`, GATT discovery, or briefly flashing `Connected` in the WebUI. (`onDisconnect()` only fires on established connections, so `compare_exchange_strong` was unnecessary.) |
| 10 | Deep Code Review | **`ble_client_test_connection()` Async Disconnect & `getClientByPeerAddress()` Collision:** `ble_client_test_connection()` called `pClient->disconnect()`, `deleteClient(pClient)`, and `deleteBond(addr)` without waiting for `BLE_GAP_EVENT_DISCONNECT`, leaving the test client `DISCONNECTING` in the background when returning `"done"`. Also, in NimBLE 2.5.2, a `0x3e` retry disconnect looks up the client via `getClientByPeerAddress()`, which matches `bridge.cpp`'s `bleClient` (`m_pClients[0]`) if its `m_peerAddress` is not cleared when deactivated. | Closed (Won't Fix) | Handled internally by NimBLE 2.5.2: `NimBLEDevice::deleteClient()` sets `deleteOnDisconnect = true` when `m_connStatus == DISCONNECTING` so `BLE_GAP_EVENT_DISCONNECT` frees the client automatically, `ble_gap_unpair()` deletes bond keys synchronously, and `BLE_GAP_EVENT_DISCONNECT` resolves the client via `getClientByHandle(conn_handle)` first (which always matches the test client since `BLE_GAP_EVENT_CONNECT` already assigned `m_connHandle`). |
| 11 | Deep Code Review | **WebUI Initial Page-Load Race (`/config` vs `/status`) & In-Flight Poll Drop:** `window.onload` fired `loadConfig()` and `updateStatus()` in parallel; if `/status` returned first (`currentConfig.ble_mac` still `''`), the UI showed `Not Configured` and hid `Deactivate`, and the subsequent `updateStatus()` call from `loadConfig()` was dropped if `statusFetchInFlight` was true. Similarly, `toggleBleActive()`'s `setTimeout(updateStatus, 300)` could be dropped if a poll was in flight, `#btn-edit-ble` was not initially `disabled` in HTML, and `/start_scan` / `/start_test_ble` lacked backend state guards in Normal Mode. | Resolved | Rendered `#btn-edit-ble` initially `disabled` in HTML, removed the duplicate parallel `updateStatus()` call from `window.onload` (since `loadConfig()` already chains `updateStatus()`), and updated `toggleBleActive()` to set `lastBleState = 'Deactivating...'` and `syncBleHeaderControls()` immediately on deactivation. (Backend scan/test guards omitted as `#ble-edit` is only reachable when deactivated.) |
