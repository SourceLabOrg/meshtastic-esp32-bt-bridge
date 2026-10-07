# Optional MQTT Broker Gateway / Proxy Feature

## 1. Overview & Objectives

This document details the architectural plan, feasibility analysis, protocol details, and implementation tasks for adding an **optional MQTT Gateway / Proxy** feature to the `meshtastic-esp32-bt-bridge`.

### Goals
* **Completely Optional:** The feature is disabled by default. If disabled, existing TCP bridging operation is 100% unaffected.
* **Dual Configuration Modes (Auto-Sync vs. Manual):**
  * **Auto-Sync from Radio (Zero-Config):** Automatically inherits broker address, credentials, encryption (TLS), and root topic directly from the connected Meshtastic radio's `ModuleConfig.mqtt` payload.
  * **Manual / Custom Override:** Allows entering custom broker settings via the WebUI (ideal for routing through a local Home Assistant / Mosquitto broker on the LAN without reconfiguring the radio).
* **Autonomous 24/7 Gateway:** When enabled, the ESP32 acts as an autonomous MQTT client proxy for the connected Meshtastic radio (which has `module_config.mqtt.proxy_to_client_enabled = true`), eliminating the need to keep a mobile phone or desktop computer running continuously.
* **Safe Coexistence with TCP Clients:** Simultaneous TCP clients (such as the Meshtastic Web UI, desktop apps, or mobile apps connecting via WiFi) can coexist with the bridge without corrupting the BLE link or causing duplicate MQTT publishes.

---

## 2. Technical Feasibility & Impact Analysis

### 2.1 Feasibility on ESP32 / ESP32-S3
* **Verdict:** 100% Feasible.
* **CPU:** The ESP32 / ESP32-S3 dual-core Xtensa CPU (240 MHz) provides ample compute:
  * Core 0 handles WiFi networking (AsyncTCP, AsyncWebServer, and the MQTT client).
  * Core 1 handles the NimBLE stack and BLE polling.
* **Flash Space:** Our `huge_app.csv` partition table allocates ~3.1 MB for firmware. The current binary is ~1.2 MB, providing ~1.9 MB of headroom for MQTT and Protobuf libraries.

### 2.2 Performance & CPU Impact
* **Negligible CPU Overhead (< 1-2%):** Meshtastic is designed for low-bandwidth LoRa links (typically 0.1 to 5 packets/second). An MQTT client processing a handful of small JSON/Protobuf packets per minute places virtually zero strain on the CPU.
* **Low Latency:** BLE packet reception to MQTT publish is sub-10ms over local WiFi. MQTT downlink to BLE `ToRadio` transmission is equally fast.

### 2.3 Memory Footprint & Safety
* **Plain MQTT (Port 1883):** Requires ~4–6 KB of RAM for socket buffers, state machines, and queues.
* **Secure MQTT / MQTTS (Port 8883 / TLS):** Requires mbedTLS (`WiFiClientSecure`). The TLS 1.2/1.3 handshake and cipher buffers require ~30–40 KB of heap during active connections.
* **Heap Margin:** The ESP32 currently has >150 KB free internal SRAM (and S3 models often have 2MB–8MB PSRAM). Even without PSRAM, a 35 KB TLS allocation leaves >110 KB of safety margin.
* **Heap Fragmentation Immunity:** We maintain the project's zero-fragmentation rule by keeping MQTT packet buffers statically or stack-allocated and reusing queues.

---

## 3. Configuration Sources: Auto-Sync vs. Manual Override

### 3.1 How the Radio Stores and Provides MQTT Settings
When a user configures MQTT via the official Meshtastic mobile or desktop apps, the settings are stored on the radio inside `ModuleConfig.mqtt`:
* **`address`**: Hostname or IP of the broker (e.g. `mqtt.meshtastic.org`).
* **`username`** & **`password`**: Broker credentials.
* **`encryption_enabled`** (`bool`): Whether TLS/SSL (port 8883) is required.
* **`root`**: Root topic prefix (e.g. `msh` or `msh/US`).
* **`proxy_to_client_enabled`** (`bool`): Tells the radio to offload MQTT to the connected client.

During the initial BLE connection sync, the radio transmits its configuration to the bridge.

### 3.2 Operating Modes

```text
                             ┌──────────────────────────────┐
                             │     MQTT Gateway Feature     │
                             │          (Enabled)           │
                             └──────────────┬───────────────┘
                                            │
                    ┌───────────────────────┴───────────────────────┐
                    ▼                                               ▼
     ┌─────────────────────────────┐                 ┌─────────────────────────────┐
     │      Auto-Sync Mode         │                 │    Manual Override Mode     │
     │      (Zero-Config)          │                 │       (Custom Broker)       │
     ├─────────────────────────────┤                 ├─────────────────────────────┤
     │ • Reads ModuleConfig.mqtt   │                 │ • Uses settings saved in    │
     │   directly from the radio   │                 │   Bridge WebUI Preferences  │
     │ • Inherits Host, User, Pass,│                 │ • Independent of radio's    │
     │   TLS, and Root Topic       │                 │   internal broker config    │
     │ • Auto-updates if radio     │                 │ • Great for private local   │
     │   config changes            │                 │   Mosquitto / Home Assist.  │
     └─────────────────────────────┘                 └─────────────────────────────┘
```

---

## 4. How Meshtastic Mobile Apps & Clients Relate to MQTT Proxy

### 4.1 Mobile App Behavior (Android & iOS)
* Both the **official Android app** and **iOS app** support MQTT Proxy using the exact same `ModuleConfig.mqtt` mechanism:
  * When a mobile device connects to a radio that has `proxy_to_client_enabled = true`, the mobile app reads the radio's `ModuleConfig.mqtt`, establishes an internet connection on the phone to the broker, and relays `MqttClientProxyMessage` packets over cellular or WiFi.

### 4.2 Why Bridge MQTT Proxy is a Major Upgrade
* **24/7 Availability:** Mobile apps are subject to operating system background execution limits, battery saver kills, network switching (WiFi $\leftrightarrow$ LTE), and leaving the radio's physical range.
* **Dedicated Appliance:** An ESP32 plugged into power on the home/office network maintains a permanent, uninterrupted uplink/downlink to the MQTT broker for the mesh.

### 4.3 Concurrency & Conflict Analysis

```text
                                 ┌─────────────────────────────────┐
                                 │   Meshtastic BLE Radio Node     │
                                 │ (proxy_to_client_enabled = true)│
                                 └───────────────┬─────────────────┘
                                                 │ BLE (ToRadio / FromRadio)
                                                 ▼
               ┌─────────────────────────────────────────────────────────────────┐
               │                     ESP32-S3 Bridge Device                      │
               │                                                                 │
               │   ┌─────────────────────┐             ┌─────────────────────┐   │
               │   │    BLE Task         │             │   MQTT Client Task  │   │
               │   │  (Core 1 - NimBLE)  │             │   (Core 0 - WiFi)   │   │
               │   └──────────┬──────────┘             └──────────┬──────────┘   │
               │              │                                   │              │
               │              │ FreeRTOS Queue                    │              │
               │              ▼                                   │              │
               │   ┌─────────────────────┐                        │              │
               │   │ TCP Server (4403)   │                        │              │
               │   │  (Core 0 - Async)   │                        │              │
               │   └──────────┬──────────┘                        │              │
               └──────────────┼───────────────────────────────────┼──────────────┘
                              │ TCP Port 4403                     │ MQTT (1883/8883)
                              ▼                                   ▼
                   ┌─────────────────────┐             ┌─────────────────────┐
                   │ Meshtastic App / UI │             │     MQTT Broker     │
                   │ (Monitor / Config)  │             │ (e.g. meshtastic.org│
                   │                     │             │  or private broker) │
                   └─────────────────────┘             └─────────────────────┘
```

#### Potential Conflicts and Resolutions:
1. **Normal TCP Client (Web UI / Phone App / Meshtastic CLI) + Bridge MQTT:**
   * **Behavior:** The TCP client sends commands and receives standard mesh packets (text messages, node DB, telemetry).
   * **BLE Arbitration:** Both TCP and MQTT transmit to the radio via a shared FreeRTOS queue (`to_ble_queue`), ensuring thread-safe, sequential writes to the `ToRadio` BLE characteristic.
   * **Result:** Seamless coexistence.

2. **External TCP Client ALSO running an MQTT Proxy (e.g., MeshMonitor with proxy enabled):**
   * **Risk:** If both the ESP32 Bridge and MeshMonitor connect to the broker and publish the same `FromRadio.mqttClientProxyMessage`, duplicate messages will hit the MQTT server.
   * **Resolution:**
     * **Packet Filtering:** When the ESP32 Bridge's MQTT client is enabled, the bridge intercepts and consumes `FromRadio.mqttClientProxyMessage` packets so they are published to MQTT directly and *not* duplicated onto the TCP client stream.
     * **UI / User Guidance:** The WebUI and documentation will instruct users to disable the proxy toggle in upstream TCP clients (like MeshMonitor) when the bridge's native MQTT feature is enabled.

---

## 5. Configuration & WebUI Specification

### 5.1 Settings to Store in NVS (`bridge_cfg` namespace)

| Key | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `mqtt_enabled` | bool | `false` | Master toggle to enable/disable MQTT gateway |
| `mqtt_mode` | string | `"auto"` | Mode: `"auto"` (sync from radio) or `"manual"` (custom settings) |
| `mqtt_server` | string | `""` | Broker hostname or IP (manual mode) |
| `mqtt_port` | uint16 | `1883` | Broker port (`1883` for plaintext, `8883` for TLS) (manual mode) |
| `mqtt_user` | string | `""` | Broker username (manual mode) |
| `mqtt_pass` | string | `""` | Broker password (manual mode) |
| `mqtt_root` | string | `"msh"` | Meshtastic root topic prefix (manual mode) |
| `mqtt_sub` | string | `""` | Custom subscribe topic filter (optional; defaults to `<root>/#`) |
| `mqtt_tls` | bool | `false` | Enable TLS/SSL encrypted connection (manual mode) |
| `mqtt_cid` | string | `""` | Custom MQTT Client ID (optional; defaults to auto-generated ID) |

### 5.2 WebUI Design & Components
A dedicated **MQTT Gateway (Optional)** card added to the captive portal:
* **Toggle Switch:** "Enable MQTT Gateway" (can be toggled on/off at any time without losing credentials).
* **Mode Selector:** Radio buttons or Segmented Switch:
  * 🔘 **Auto (Sync from Radio):** Displays live status of settings extracted from the connected radio:
    * *Detected Broker:* `mqtt.meshtastic.org:8883 (TLS)`
    * *Username:* `meshdev`
    * *Root Topic:* `msh/US`
  * 🔘 **Manual Override:** Reveals editable form inputs:
    * Broker Hostname / IP
    * Broker Port (with helper 1883/8883)
    * TLS / Encryption toggle
    * Username & Password (with show/hide toggle)
    * Root Topic & Custom Subscribe Topic
    * Client ID (optional)
* **Connection Status Pill:** Live indicator showing `Disabled`, `Waiting for Radio Config...`, `Connecting...`, `Connected (Broker: ...)` or `Connection Failed`.
* **Actions:**
  * "Save MQTT Configuration"
  * "Test MQTT Connection"

---

## 6. Protobuf Protocol Details

### 6.1 Protobuf Envelope Structures
Meshtastic defines proxy and configuration structures in `mesh.proto` and `module_config.proto`:

```protobuf
// Outbound from Radio to Client / Bridge
message FromRadio {
  // ...
  MqttClientProxyMessage mqttClientProxyMessage = 14;
}

// Inbound from Client / Bridge to Radio
message ToRadio {
  // ...
  MqttClientProxyMessage mqtt_client_proxy_message = 6;
}

// MQTT Proxy Packet
message MqttClientProxyMessage {
  string topic = 1;
  oneof payload_variant {
    bytes data = 2;   // Serialized ServiceEnvelope (binary)
    string text = 3;  // Plaintext / JSON string
  }
  bool retained = 4;
}

// MQTT Module Configuration on Radio
message MQTTConfig {
  bool enabled = 1;
  string address = 2;
  string username = 3;
  string password = 4;
  bool encryption_enabled = 5;
  bool json_enabled = 6;
  bool tls_enabled = 7;
  string root = 8;
  bool proxy_to_client_enabled = 9;
  // ...
}
```

### 6.2 Routing & Auto-Sync Logic
1. **Config Discovery (Auto Mode):**
   * Listen for incoming configuration packets (`FromRadio` containing `MQTTConfig` or `AdminMessage`).
   * Extract `address`, `username`, `password`, `encryption_enabled`/`tls_enabled`, and `root`.
   * Initialize or update the MQTT client connection parameters dynamically.
2. **Uplink (Radio $\to$ MQTT):**
   * Read packet from `FromRadio`.
   * Decode protobuf. If field 14 is present:
     * Extract `topic`, `payload`, and `retained`.
     * Publish to MQTT broker: `mqttClient.publish(topic, payload, len, retained)`.
     * If Bridge MQTT is enabled, suppress forwarding this specific packet to TCP clients to avoid duplicate external proxying.
3. **Downlink (MQTT $\to$ Radio):**
   * MQTT client receives message on subscribed topic.
   * Encode `ToRadio` message setting field 6 (`mqtt_client_proxy_message`).
   * Push encoded bytes into `to_ble_queue`.
   * BLE task writes to `ToRadio` GATT characteristic.

---

## 7. Implementation Roadmap & Task Tracker

### Phase 1: Planning & Research ✅
- [x] Analyze ESP32-S3 feasibility, CPU, memory, and flash constraints.
- [x] Investigate Meshtastic `MqttClientProxyMessage` and `MQTTConfig` protobuf specifications.
- [x] Evaluate Auto-Sync from Radio vs. Manual WebUI configuration modes.
- [x] Evaluate concurrency and collision prevention between TCP clients and MQTT proxy.
- [x] Document architecture and design in `docs/feature-mqtt-proxy.md`.

### Phase 2: Protobuf & Dependency Integration 🔲
- [ ] Add lightweight protobuf runtime (`Nanopb-Arduino` or pre-compiled Meshtastic C headers).
- [ ] Add `AsyncMqttClient` / `PubSubClient` / `esp-mqtt` dependency to `platformio.ini`.
- [ ] Verify clean build on `seeed_xiao_esp32s3`, `esp32dev`, and `esp32-s3-devkitc-1`.

### Phase 3: MQTT Client Subsystem (`mqtt_net`) 🔲
- [ ] Create `include/mqtt_net.h` and `src/mqtt_net.cpp`.
- [ ] Implement broker connection, reconnection backoff, keep-alive loop, and TLS support.
- [ ] Implement topic subscription matching (`<root>/#` or custom channels).
- [ ] Implement message publishing from `MqttClientProxyMessage` payloads.
- [ ] Implement radio `MQTTConfig` parser for Auto-Sync mode.

### Phase 4: WebUI & Storage Integration 🔲
- [ ] Add MQTT preferences keys to `include/config_ui.h` and `src/config_ui.cpp`.
- [ ] Add the "MQTT Gateway" UI card with mode toggle (Auto-Sync vs. Manual), inputs, and live radio status.
- [ ] Implement REST endpoints `/save_mqtt` and `/mqtt_status`.
- [ ] Add client-side validation and live status polling in WebUI.

### Phase 5: Bridge Pipeline Multiplexing 🔲
- [ ] Update `bridge.cpp` to inspect incoming `FromRadio` packets for field 14 (`MqttClientProxyMessage`) and config packets.
- [ ] Route `MqttClientProxyMessage` packets to `mqtt_net` for publishing.
- [ ] Route downlink MQTT packets from `mqtt_net` into `to_ble_queue`.
- [ ] Add packet filtering to prevent echoing proxy packets to TCP clients.

### Phase 6: Verification & Testing 🔲
- [ ] Test Auto-Sync mode with a radio configured for `mqtt.meshtastic.org`.
- [ ] Test Manual Override mode with local Mosquitto broker (port 1883) and TLS broker (port 8883).
- [ ] Test simultaneous TCP client connection (Meshtastic Web UI) during active MQTT proxying.
- [ ] Test edge cases: broker outage recovery, BLE disconnect/reconnect, WiFi drop/reconnect.
