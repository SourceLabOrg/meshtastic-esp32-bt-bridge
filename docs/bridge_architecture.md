# Meshtastic ESP32 BLE-to-TCP Bridge Architecture

## Overview
This document outlines the architecture for the core BLE-to-TCP bridge functionality of our C++ ESP32-S3 firmware. It is heavily based on lessons learned from the `meshtastic-ble-bridge` Python reference implementation.

The primary goal of the bridge is to translate between the TCP stream protocol expected by tools like MeshMonitor and the Bluetooth Low Energy (BLE) GATT characteristics exposed by a Meshtastic device.

## Protocol Translation

### 1. TCP Protocol (Client side)
MeshMonitor expects a continuous TCP stream on port 4403. Messages are framed to define their boundaries:
- **Header**: 4 bytes `[0x94] [0xC3] [LENGTH_MSB] [LENGTH_LSB]`
- **Payload**: Raw Protobuf bytes (up to 512 bytes)
- **Behavior**: The ESP32 will act as a TCP Server. It must parse the stream, strip the 4-byte header, and forward the payload to the BLE node.

### 2. BLE Protocol (Meshtastic Node side)
The ESP32 will act as a BLE Client and connect to the target Meshtastic Node (BLE Peripheral).
- **Service UUID**: `6ba1b218-15a8-461f-9fa8-5dcae273eafd`
- **ToRadio (Write)**: `f75c76d2-129e-4dad-a1dd-7866124401e7`
  - Used to send data to the node. Raw protobufs only (no framing).
- **FromRadio (Read/Notify)**: `2c55e69e-4993-11ed-b878-0242ac120002`
  - Used to receive data from the node. Raw protobufs only.
- **FromNum (Notify)**: `ed9da18c-a800-4f66-a670-aa7547e34453`
  - Triggers when a new packet is available to be read from `FromRadio`.

## C++ ESP32 Implementation Strategy

### 1. Multithreading & Tasks
Because BLE operations (connecting, reading characteristics) can block, and we are using `ESPAsyncTCP` (which runs on Core 0), we must separate concerns using FreeRTOS tasks and queues:
- **Core 0 (Network)**: TCP Server (port 4403). Parses incoming TCP streams, creates frames, and pushes to a `tcp_to_ble_queue`. It also listens on a `ble_to_tcp_queue` to broadcast outgoing frames to connected clients.
- **Core 1 (BLE)**: A dedicated `bridge_task` that maintains the BLE connection. It waits on the `tcp_to_ble_queue` and writes to `ToRadio`. It registers notification callbacks on `FromNum`/`FromRadio` and pushes received packets into the `ble_to_tcp_queue`.

### 2. BLE Interaction Flow
- **Connect**: Scan and connect to the configured BLE MAC address (`cfg.ble_mac`).
- **Subscribe**: Register a notification callback on `FromNum`.
- **Receive (BLE -> TCP)**: 
  - When `FromNum` notifies, read the `FromRadio` characteristic.
  - Prepend the `0x94 0xC3` length header to the bytes.
  - Push the framed bytes to the `ble_to_tcp_queue`.
  - The Network task pops the queue and sends to all active TCP clients.
- **Transmit (TCP -> BLE)**:
  - The Network task receives data, buffers it until a full frame (`header + payload`) is parsed.
  - Pushes the payload (without header) to `tcp_to_ble_queue`.
  - The BLE task pops the queue and calls `writeValue()` on the `ToRadio` characteristic.

### 3. Reconnection & Stability
- Meshtastic nodes can take minutes to reboot. We need robust exponential backoff or periodic retry logic in the BLE task if the connection drops.
- If the BLE connection is down, the TCP server should still accept clients but perhaps discard incoming packets or queue them temporarily.
- MTU Negotiation: We must request an MTU of at least 512 bytes during the BLE connection phase, as Meshtastic packets can reach this size.

### 4. Memory Management & Caching
- The Python project implemented "Config Caching" to speed up reconnections. Given the limited RAM on the ESP32 and the complexity of parsing the protobufs to extract node DBs, we will **skip caching** for the initial C++ version. The ESP32 will act as a pure, dumb, fast bridge.
- **Idle Optimization**: If there are 0 TCP clients connected, the BLE task will skip allocating heap memory and queueing packets when notifications arrive, preventing unnecessary heap fragmentation.

### 5. Client Limits & Auto-Discovery
- **TCP Limits**: The bridge enforces a hard limit of `MAX_TCP_CLIENTS = 3`. Incoming TCP connections beyond this are instantly rejected to protect the ESP32's limited LwIP buffer memory and the small 10-packet FreeRTOS queues.
- **mDNS Auto-Discovery**: The bridge runs an mDNS responder (`meshtastic-bridge.local`). It advertises the `_meshtastic._tcp` service on port 4403, and critically, includes the required TXT records (`name`, `mac`, `id`) so that official Meshtastic apps can instantly discover and properly display the bridge on the local network.

## Quirks & Potential Pitfalls to Watch Out For
1. **TCP Stream Fragmentation**: `AsyncTCP` might deliver a single frame in multiple chunks, or multiple frames in a single chunk. We must implement a state machine to buffer incoming TCP bytes until a complete frame is assembled.
2. **NimBLE MTU Quirks**: Default BLE MTU is 23 bytes. To support large protobufs, we must explicitly request a larger MTU via `NimBLEDevice::setMTU(512)`.
3. **Thread Safety**: Network events trigger on Core 0, while BLE operations usually block heavily. They must be decoupled using FreeRTOS queues.
4. **NimBLE Deadlocks (FromNum)**: Calling a blocking `readValue()` inside a BLE notification callback (like `notifyFromNum`) will instantly deadlock the NimBLE background task. To prevent this, we intentionally do NOT subscribe to `FromNum` notifications. Instead, we use a 100ms polling loop on the `FromRadio` characteristic in our own task.
5. **Write Without Response**: The Meshtastic `ToRadio` characteristic expects *Write Without Response*. Attempting to write with response (requesting an ACK) will fail or hang the BLE communication.
