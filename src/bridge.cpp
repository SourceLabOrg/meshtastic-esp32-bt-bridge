// Copyright (c) 2026 SourceLab.org (https://github.com/SourceLabOrg/meshtastic-esp32-bt-bridge)
// SPDX-License-Identifier: MIT

#include "bridge.h"
#include "ble_client.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <NimBLEDevice.h>
#include "status_led.h"
#include "build_options.h"
#include "utils.h"
#include "mqtt_net.h"
#include <atomic>
#include <cstring>

struct BridgePacket {
    uint8_t data[MESHTASTIC_MAX_PACKET_SIZE]; // Max Meshtastic ToRadio envelope / packet size
    size_t len;
};

/**
 * Queue for sending packets from TCP -> BLE.
 */
static StaticQueue_t tcp_to_ble_queue_struct;
static uint8_t tcp_to_ble_queue_storage[BRIDGE_QUEUE_SIZE * sizeof(BridgePacket)];
static QueueHandle_t tcp_to_ble_queue = NULL;

/**
 * Queue for sending packets from BLE -> TCP.
 */
static StaticQueue_t ble_to_tcp_queue_struct;
static uint8_t ble_to_tcp_queue_storage[BRIDGE_QUEUE_SIZE * sizeof(BridgePacket)];
static QueueHandle_t ble_to_tcp_queue = NULL;

/**
 * Queue for sending MQTT packets from MQTT broker -> BLE.
 */
static StaticQueue_t mqtt_to_ble_queue_struct;
static uint8_t mqtt_to_ble_queue_storage[MQTT_QUEUE_SIZE * sizeof(BridgePacket)];
static QueueHandle_t mqtt_to_ble_queue = NULL;

/**
 * Tracks if we drop packets from our queues (capacity full).
 */
static std::atomic<uint32_t> s_tcp_to_ble_dropped{0};
static std::atomic<uint32_t> s_ble_to_tcp_dropped{0};
static std::atomic<uint32_t> s_mqtt_to_ble_dropped{0};
static std::atomic<bool> s_ble_connected{false};

/**
 * Atomic traffic & packet integrity counters (wait-free, zero lock contention).
 */
static std::atomic<uint32_t> s_ble_rx_count{0};
static std::atomic<uint32_t> s_ble_tx_count{0};
static std::atomic<uint32_t> s_ble_invalid_count{0};

static std::atomic<uint32_t> s_tcp_rx_count{0};
static std::atomic<uint32_t> s_tcp_tx_count{0};
static std::atomic<uint32_t> s_tcp_invalid_count{0};

struct ClientContext {
    AsyncClient* client;
    std::vector<uint8_t> rx_buffer;
};

static AsyncServer* tcpServer = NULL;
static std::vector<ClientContext*> tcpClients;
static SemaphoreHandle_t tcpClientsMutex = NULL; // Mutex to protect tcpClients vector from concurrent modification
static std::atomic<size_t> connectedClientsCount{0};
static bool bridgeRunning = false;

/**
 * Lock-free BLE target configuration and state (zero heap allocation).
 * s_target_ble_mac is written on Core 0 before releasing s_ble_config_dirty / s_ble_deactivated,
 * and copied to a local stack buffer on Core 1 after acquiring those atomics.
 */
static char s_target_ble_mac[24] = {0};
static std::atomic<uint32_t> s_target_ble_pin{123456};
static std::atomic<BleState> s_ble_state{BLE_STATE_NOT_CONFIGURED};
static std::atomic<bool> s_ble_deactivated{false};
static std::atomic<bool> s_ble_config_dirty{false};

static NimBLEClient* bleClient = NULL;
static NimBLERemoteCharacteristic* toRadioChar = NULL;
static NimBLERemoteCharacteristic* fromRadioChar = NULL;
static NimBLERemoteCharacteristic* fromNumChar = NULL;

// TCP callbacks
static void onClientConnected(void* arg, AsyncClient* client) {
    if (connectedClientsCount.load() >= MAX_TCP_CLIENTS) {
        log_w("[Bridge] TCP Connection rejected from %s: Max clients (%d) reached.", client->remoteIP().toString().c_str(), MAX_TCP_CLIENTS);
        client->close();
        return;
    }

    // Crucial TCP connection parameters for stable bridging
    // We intentionally leave Nagle's algorithm enabled (false) so LwIP batches the tiny
    // BLE packets together. This prevents WiFi congestion and TCP retransmission timeouts.
    client->setNoDelay(false);
    client->setRxTimeout(TCP_IDLE_TIMEOUT_SECONDS); // 10 minute idle timeout

    // Create new client context to track this connection.
    ClientContext* ctx = new ClientContext();
    ctx->client = client;

    // Only update the tcpClients list after grabbing the mutex
    if (xSemaphoreTake(tcpClientsMutex, portMAX_DELAY) == pdTRUE) {
        tcpClients.push_back(ctx);
        connectedClientsCount.fetch_add(1, std::memory_order_relaxed);
        xSemaphoreGive(tcpClientsMutex);
    }

    log_i("[Bridge] TCP Client connected from %s. Total clients: %zu", client->remoteIP().toString().c_str(), connectedClientsCount.load());

    client->onData([](void* arg, AsyncClient* c, void* data, size_t len) {
        ClientContext* ctx = (ClientContext*)arg;
        uint8_t* buf = (uint8_t*)data;

        log_d("[Bridge-TCP] RX %zu bytes from %s", len, c->remoteIP().toString().c_str());

        // Protect against OOM (e.g., malicious stream or massive desync)
        if (ctx->rx_buffer.size() + len > 2048) {
            log_e("[Bridge-TCP] ERROR: RX Buffer overflow! Disconnecting client to prevent OOM.");
            s_tcp_invalid_count.fetch_add(1, std::memory_order_relaxed);
            c->close();
            return;
        }

        // Append new data to client's RX buffer
        ctx->rx_buffer.insert(ctx->rx_buffer.end(), buf, buf + len);

        // Process as many full frames as possible
        size_t processed = 0;
        while (ctx->rx_buffer.size() - processed >= 4) {
            if (ctx->rx_buffer[processed] == 0x94 && ctx->rx_buffer[processed+1] == 0xC3) {
                uint16_t payload_len = (ctx->rx_buffer[processed+2] << 8) | ctx->rx_buffer[processed+3];

                // Protect against unreasonably large lengths (e.g. invalid header masquerading as 0x94 0xC3)
                if (payload_len > 1024) {
                    log_e("[Bridge-TCP] ERROR: Invalid frame header payload length (%u bytes > 1024)", payload_len);
                    s_tcp_invalid_count.fetch_add(1, std::memory_order_relaxed);
                    processed++; // Invalid length, skip one byte and search again
                    continue;
                }

                if (ctx->rx_buffer.size() - processed >= 4 + payload_len) {
                    // Valid full frame received from client
                    log_d("[Bridge-TCP] Parsed frame: len %u", payload_len);
                    s_tcp_rx_count.fetch_add(1, std::memory_order_relaxed);
                    BridgePacket packet;
                    packet.len = payload_len;
                    if (payload_len <= sizeof(packet.data)) {
                        memcpy(packet.data, &ctx->rx_buffer[processed + 4], payload_len);
                        if (xQueueSend(tcp_to_ble_queue, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                            log_w("[Bridge-TCP] WARNING: tcp_to_ble_queue full, dropped packet");
                            s_tcp_to_ble_dropped.fetch_add(1, std::memory_order_relaxed);
                        }
                    } else {
                        log_e("[Bridge-TCP] ERROR: Payload too large (%u bytes)", payload_len);
                        s_tcp_invalid_count.fetch_add(1, std::memory_order_relaxed);
                    }
                    processed += 4 + payload_len;
                } else {
                    // Incomplete frame, break and wait for next onData
                    break;
                }
            } else {
                // Invalid header byte, skip it
                processed++;
            }
        }

        // Remove processed bytes from buffer
        if (processed > 0) {
            ctx->rx_buffer.erase(ctx->rx_buffer.begin(), ctx->rx_buffer.begin() + processed);
        }
    }, ctx);

    client->onDisconnect([](void* arg, AsyncClient* c) {
        ClientContext* ctx = (ClientContext*)arg;

        // Only update the tcpClients list after grabbing the mutex
        if (xSemaphoreTake(tcpClientsMutex, portMAX_DELAY) == pdTRUE) {
            auto it = std::find(tcpClients.begin(), tcpClients.end(), ctx);
            if (it != tcpClients.end()) {
                tcpClients.erase(it);
                connectedClientsCount.fetch_sub(1, std::memory_order_relaxed);
            }
            xSemaphoreGive(tcpClientsMutex);
        }

        log_i("[Bridge] TCP Client disconnected. Total clients remaining: %zu", connectedClientsCount.load());
        delete ctx;
    }, ctx);
}

/**
 * Async flag when the Bluetooth device notifies us there is data to be read.
 */
static volatile bool pendingRadioRead = false;
static void notifyFromNum(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    pendingRadioRead = true;
}

// Custom BLE Client Callbacks to handle PIN authentication and disconnects
class BridgeRuntimeClientCallbacks : public NimBLEClientCallbacks {
public:
    void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
        uint32_t pin = s_target_ble_pin.load(std::memory_order_relaxed);
        log_i("[Bridge-BLE] Passkey requested, injecting PIN: %06u", pin);
        NimBLEDevice::injectPassKey(connInfo, pin);
    }

    void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pin) override {
        uint32_t expectedPin = s_target_ble_pin.load(std::memory_order_relaxed);
        log_i("[Bridge-BLE] Confirming PIN: %06u", pin);
        NimBLEDevice::injectConfirmPasskey(connInfo, pin == expectedPin);
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        log_i(
            "[Bridge-BLE] Authentication complete. Encrypted: %d, Authenticated: %d",
            connInfo.isEncrypted(),
            connInfo.isAuthenticated()
        );
    }

    void onDisconnect(NimBLEClient* pClient, int reason) override {
        log_w("[Bridge-BLE] BLE client disconnected (reason=%d).", reason);
        s_ble_connected.store(false, std::memory_order_relaxed);
        if (!s_ble_deactivated.load(std::memory_order_relaxed)) {
            s_ble_state.store(BLE_STATE_DISCONNECTED, std::memory_order_relaxed);
        }
        mqtt_net_on_ble_disconnected();
    }
};

static BridgeRuntimeClientCallbacks bridgeCallbacks;

/**
 * Polls the Meshtastic FromRadio characteristic when notified by FromNum or when the
 * 250ms failsafe interval elapses.
 *
 * Uses NimBLE 2.x's NimBLEAttValue and a static byte buffer to avoid any heap allocations
 * on Core 1.
 *
 * @return true if a new packet was read and processed (indicating more packets may be
 *         queued in the radio), false otherwise.
 */
static bool pollFromRadio() {
    static unsigned long lastFailsafe = 0;
    static uint8_t lastPacket[MESHTASTIC_MAX_PACKET_SIZE];
    static size_t lastPacketLen = 0;
    static unsigned long lastPacketTime = 0;

    // If we have no connected TCP clients and aren't running the MQTT broker proxy,
    // skip polling FromRadio so packets remain queued on the radio.
    if (!fromRadioChar || (connectedClientsCount.load() == 0 && !mqtt_net_is_enabled())) {
        return false;
    }

    // Only read if notified by FromNum, or if 250ms have elapsed since the last check (failsafe)
    if (!pendingRadioRead && (millis() - lastFailsafe <= 250)) {
        return false;
    }

    bool wasNotified = pendingRadioRead; // Capture before clearing!
    pendingRadioRead = false;
    lastFailsafe = millis();

    NimBLEAttValue currentVal = fromRadioChar->readValue();
    size_t currentLen = currentVal.size();
    const uint8_t* currentData = currentVal.data();

    // In Meshtastic's BLE state machine, reading FromRadio pops one packet off the radio's
    // internal FIFO queue. When the queue is empty (or during an idle 250ms failsafe poll),
    // readValue() returns 0 bytes; returning false leaves pendingRadioRead == false and
    // terminates the fast-poll drain burst.
    if (currentLen == 0) {
        return false;
    }

    if (currentLen > MESHTASTIC_MAX_PACKET_SIZE) {
        log_e("[Bridge-BLE] ERROR: Radio Payload too large (%zu bytes > %d)", currentLen, MESHTASTIC_MAX_PACKET_SIZE);
        s_ble_invalid_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool isDifferent = (currentLen != lastPacketLen) ||
                       (memcmp(currentData, lastPacket, currentLen) != 0);

    // Accept the packet if it's different, OR if the radio explicitly notified us it was new,
    // OR if 2.5 seconds have passed (failsafe for dropped identical heartbeat notifications)
    if (!isDifferent && !wasNotified && (millis() - lastPacketTime <= 2500)) {
        return false;
    }

    memcpy(lastPacket, currentData, currentLen);
    lastPacketLen = currentLen;
    lastPacketTime = millis();

    log_d("[Bridge-BLE] Fast-Polled %zu new bytes from FromRadio!", currentLen);
    s_ble_rx_count.fetch_add(1, std::memory_order_relaxed);

    /*
     * Pass the raw packet to the MQTT subsystem to determine if it's relevant:
     *   - Discovers and auto-syncs ModuleConfig.mqtt settings from the radio.
     *   - Publishes MqttClientProxyMessage uplink packets to the active MQTT broker.
     *
     * Returns true if consumed exclusively by MQTT, false if it should be forwarded to TCP clients.
     */
    bool consumedByMqtt = mqtt_net_handle_from_radio(currentData, currentLen);

    /*
     * Only forward the packet to the TCP queue if:
     *  - It was NOT an internal MQTT proxy message (prevents echoing raw MQTT packets to apps over the TCP socket).
     *  - There is at least one active TCP client connected (prevents filling the queue).
     */
    if (!consumedByMqtt && connectedClientsCount.load() > 0) {
        BridgePacket rx_packet;
        rx_packet.len = currentLen;
        memcpy(rx_packet.data, currentData, rx_packet.len);
        // Apply backpressure: Wait up to 50ms if the queue is full so we don't drop packets!
        if (xQueueSend(ble_to_tcp_queue, &rx_packet, pdMS_TO_TICKS(50)) != pdTRUE) {
            log_e("[Bridge-BLE] CRITICAL: ble_to_tcp_queue FULL! Dropped %zu bytes", rx_packet.len);
            s_ble_to_tcp_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return true;
}

/**
 * Sleeps for up to delayMs in 250ms increments, waking early if the BLE state is
 * deactivated/activated or if the target BLE configuration is updated.
 */
static void waitReconnectDelayOrWake(uint32_t delayMs) {
    uint32_t startMs = millis();
    while ((millis() - startMs) < delayMs) {
        if (s_ble_deactivated.load(std::memory_order_acquire) ||
            s_ble_config_dirty.load(std::memory_order_acquire) ||
            !bridgeRunning) {
            return;
        }
        s_ble_state.store(BLE_STATE_DISCONNECTED, std::memory_order_relaxed);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/**
 * BLE Task (running on Core 1)
 * This is the task the manages:
 *  - reading data off of the BLE radio and pushing onto the TCP queue.
 *  - reading data off of the BLE queue and writing to the BLE radio.
 */
static void bridgeBleTask(void* parameter) {
    log_i("[Bridge-BLE] BLE Task started on Core 1");

    while (bridgeRunning) {
        // If explicitly deactivated via WebUI, disconnect on Core 1 and idle until re-activated.
        if (s_ble_deactivated.load(std::memory_order_acquire)) {
            if (bleClient && bleClient->isConnected()) {
                log_i("[Bridge-BLE] Disconnecting active BLE session (deactivated via WebUI)...");
                bleClient->disconnect();
            }
            s_ble_connected.store(false, std::memory_order_relaxed);
            s_ble_state.store(BLE_STATE_DEACTIVATED, std::memory_order_relaxed);
            status_led_set(LED_MED_BLINK);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        // If target MAC/PIN was updated via WebUI while connected, disconnect to switch targets.
        if (s_ble_config_dirty.exchange(false, std::memory_order_acq_rel)) {
            if (bleClient && bleClient->isConnected()) {
                log_i("[Bridge-BLE] BLE target config updated, disconnecting current session...");
                bleClient->disconnect();
                s_ble_connected.store(false, std::memory_order_relaxed);
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }
        }

        /**
         * If not connected to the BLE device yet, attempt to connect.
         * Blink the status LED appropriately to indicate no BLE connection is active.
         */
        if (!bleClient || !bleClient->isConnected()) {
            char localMac[sizeof(s_target_ble_mac)];
            std::atomic_thread_fence(std::memory_order_acquire);
            strncpy(localMac, s_target_ble_mac, sizeof(localMac) - 1);
            localMac[sizeof(localMac) - 1] = '\0';

            if (localMac[0] == '\0') {
                s_ble_connected.store(false, std::memory_order_relaxed);
                s_ble_state.store(BLE_STATE_NOT_CONFIGURED, std::memory_order_relaxed);
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }

            s_ble_connected.store(false, std::memory_order_relaxed);
            s_ble_state.store(BLE_STATE_CONNECTING, std::memory_order_relaxed);
            status_led_set(LED_MED_BLINK);
            log_i("[Bridge-BLE] Attempting to connect to Meshtastic BLE device (%s)...", localMac);

            NimBLEAddress addr = utils_parse_ble_address(localMac);

            if (!bleClient) {
                // Ensure unified NimBLE stack + scan callback initialization and 512-byte MTU
                // BEFORE connect() so the initial MTU exchange negotiates 512 bytes on first connect.
                ble_client_init();

                bleClient = NimBLEDevice::createClient();
            }

            // Re-assert runtime security settings and callbacks before every connection attempt
            // in case a WebUI "Test Connection" task ran while BLE was deactivated.
            NimBLEDevice::setSecurityAuth(true, true, true);
            NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
            bleClient->setClientCallbacks(&bridgeCallbacks, false);

            // NimBLE 2.x setConnectTimeout takes milliseconds
            bleClient->setConnectTimeout(BLUETOOTH_TIMEOUT_SECONDS * 1000);
            if (bleClient->connect(addr, false)) {
                log_i("[Bridge-BLE] Connected! Securing connection...");
                // Note: In NimBLE 2.x, secureConnection() (async=false) blocks synchronously
                // until the SMP encryption handshake either succeeds (returns true) or errors/disconnects (returns false).
                if (!bleClient->secureConnection()) {
                    log_e("[Bridge-BLE] BLE authentication/encryption failed! Disconnecting and retrying in %d ms...", BLUETOOTH_RECONNECT_DELAY_MS);
                    s_ble_connected.store(false, std::memory_order_relaxed);
                    bleClient->disconnect();
                    waitReconnectDelayOrWake(BLUETOOTH_RECONNECT_DELAY_MS);
                    continue;
                }

                NimBLERemoteService* pSvc = bleClient->getService("6ba1b218-15a8-461f-9fa8-5dcae273eafd");
                if (pSvc) {
                    toRadioChar = pSvc->getCharacteristic("f75c76d2-129e-4dad-a1dd-7866124401e7");
                    fromRadioChar = pSvc->getCharacteristic("2c55e69e-4993-11ed-b878-0242ac120002");
                    fromNumChar = pSvc->getCharacteristic("ed9da18c-a800-4f66-a670-aa7547e34453");

                    if (fromNumChar && fromNumChar->canNotify()) {
                        bool sub = fromNumChar->subscribe(true, notifyFromNum);
                        log_d("[Bridge-BLE] Subscribed to FromNum: %d", sub);
                    }
                    s_ble_connected.store(true, std::memory_order_relaxed);
                    s_ble_state.store(BLE_STATE_CONNECTED, std::memory_order_relaxed);
                    log_i("[Bridge-BLE] BLE setup complete. Bridging active.");
                    status_led_set(LED_SOLID_ON);
                    mqtt_net_on_ble_connected();
                } else {
                    log_w("[Bridge-BLE] Meshtastic service not found!");
                    s_ble_connected.store(false, std::memory_order_relaxed);
                    bleClient->disconnect();
                    waitReconnectDelayOrWake(BLUETOOTH_RECONNECT_DELAY_MS);
                }
            } else {
                s_ble_connected.store(false, std::memory_order_relaxed);
                log_w("[Bridge-BLE] Connection failed, retrying in %d ms...", BLUETOOTH_RECONNECT_DELAY_MS);
                waitReconnectDelayOrWake(BLUETOOTH_RECONNECT_DELAY_MS);
            }
        } else {
            BridgePacket packet;
            static unsigned long lastBleWriteMs = 0;

            // Enforce a minimum inter-write pacing interval (BLE_WRITE_PACING_MS = 12ms, ~83 pkt/s)
            // independent of whether a FromRadio read is currently pending.
            //
            // IMPORTANT BLE PACING & MSYS_1 BUFFER NOTE:
            // Because Meshtastic's ToRadio characteristic uses Write Without Response (writeValue(..., false)),
            // NimBLE's ble_gattc_write_no_rsp_flat() is non-blocking and buffers outbound writes inside
            // NimBLE's internal msys_1 mbuf pool (CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT = 24 blocks of 255B).
            // With an negotiated MTU of 512 bytes, a single >255B packet chains 2-3 msys_1 blocks.
            // During simultaneous bidirectional traffic (when pendingRadioRead == true) or multi-packet TCP bursts,
            // waitTicks is 0 so FromRadio reads are not delayed. Gating outbound queue drains on canWriteNow
            // prevents back-to-back writes at 1ms intervals from exhausting msys_1 (BLE_HS_ENOMEM) or overrunning
            // the radio's ToRadio buffer, while still allowing FromRadio reads to execute immediately at 0ms delay.
            bool canWriteNow = (millis() - lastBleWriteMs) >= BLE_WRITE_PACING_MS;
            bool hasPacket = false;

            if (canWriteNow) {
                // If we have a pending read from the radio, do not block waiting on the TCP queue (0 ticks).
                // Otherwise, wait up to 10ms for incoming high-priority TCP packets before checking MQTT.
                TickType_t waitTicks = pendingRadioRead ? 0 : pdMS_TO_TICKS(10);

                // High Priority: App / TCP commands and direct chat messages are checked first.
                if (xQueueReceive(tcp_to_ble_queue, &packet, waitTicks) == pdTRUE) {
                    hasPacket = true;
                }
                // Normal Priority: Background MQTT downlink packets are checked after the TCP window.
                else if (xQueueReceive(mqtt_to_ble_queue, &packet, 0) == pdTRUE) {
                    hasPacket = true;
                }
            }

            if (hasPacket) {
                lastBleWriteMs = millis();
                log_d("[Bridge-BLE] Writing %zu bytes to ToRadio...", packet.len);
                if (toRadioChar && (toRadioChar->canWriteNoResponse() || toRadioChar->canWrite())) {
                    // Meshtastic ToRadio expects Write Without Response (false)
                    bool success = toRadioChar->writeValue(packet.data, packet.len, false);
                    if (success) {
                        log_d("[Bridge-BLE] Write success");
                        s_ble_tx_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        log_e("[Bridge-BLE] ERROR: Failed to write packet to ToRadio characteristic");
                        s_ble_invalid_count.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    log_e("[Bridge-BLE] ERROR: ToRadio characteristic unavailable or unwritable");
                    s_ble_invalid_count.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (pollFromRadio()) {
                // We successfully pulled a NEW packet! There might be more packets
                // waiting in the radio's queue. Flag it to instantly read again on the next loop!
                pendingRadioRead = true;
            }

            // Always yield at least 1ms to allow equal-priority tasks on Core 1 (like Arduino loopTask) to run
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    vTaskDelete(NULL);
}

/**
 * Network broadcast task (runs on Core 0)
 * Handles:
 *  - reading data from TCP Queue and writing to connection TCP client(s).
 *  - reading data from connected TCP clients and writing to BLE queue.
 */
static void bridgeNetTask(void* parameter) {
    log_i("[Bridge-Net] Network broadcasting Task started on Core 0");

    while (bridgeRunning) {
        BridgePacket packet;
        if (xQueueReceive(ble_to_tcp_queue, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            log_d("[Bridge-Net] Broadcasting %zu bytes to %zu TCP client", packet.len, connectedClientsCount.load());
            // Build contiguous TCP frame to prevent fragmentation desyncs
            size_t frame_len = packet.len + 4;
            uint8_t frame[MESHTASTIC_MAX_PACKET_SIZE + 4]; // Max packet size + 4-byte header

            if (frame_len <= sizeof(frame)) {
                frame[0] = 0x94;
                frame[1] = 0xC3;
                frame[2] = (uint8_t)(packet.len >> 8);
                frame[3] = (uint8_t)(packet.len & 0xFF);
                memcpy(&frame[4], packet.data, packet.len);

                // Send to all connected clients under mutex protection!
                if (xSemaphoreTake(tcpClientsMutex, portMAX_DELAY) == pdTRUE) {
                    for (ClientContext* ctx : tcpClients) {
                        if (ctx->client->space() >= frame_len) {
                            ctx->client->write((const char*)frame, frame_len);
                            ctx->client->send(); // Force immediate transmission
                            s_tcp_tx_count.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            log_e("[Bridge-Net] ERROR: Client TX buffer full! Dropped %zu bytes for %s", packet.len, ctx->client->remoteIP().toString().c_str());
                            s_tcp_invalid_count.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    xSemaphoreGive(tcpClientsMutex);
                }
            } else {
                log_e("[Bridge-Net] ERROR: Packet too large for frame buffer (%zu bytes)", packet.len);
                s_tcp_invalid_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    vTaskDelete(NULL);
}

/**
 * Enqueue an MQTT downlink packet to be transmitted to the BLE radio.
 *
 * When a MQTT message comes from the Broker, we queue it up to be sent
 * to the BLE radio.
 *
 * @return bool true on successful queueing, false if unable to queue.
 */
bool bridge_enqueue_mqtt_to_ble(const uint8_t* data, size_t len) {
    if (!mqtt_to_ble_queue || !data || len == 0) {
        return false;
    }
    if (len > sizeof(BridgePacket::data)) {
        log_e("[Bridge-MQTT] ERROR: Downlink packet too large (%zu bytes, max %zu)", len, sizeof(BridgePacket::data));
        mqtt_net_record_invalid();
        return false;
    }
    BridgePacket packet;
    packet.len = len;
    memcpy(packet.data, data, len);

    // Apply conditional backpressure:
    // - When BLE is actively connected, wait up to 50ms for a slot to open up. Since bridgeBleTask
    //   drains 1 packet every ~11ms over BLE, waiting up to 50ms allows the underlying LwIP TCP
    //   receive window / MQTT socket buffer to naturally pace high-speed WiFi bursts (>16 packets)
    //   to match BLE transmission speed without consuming extra internal SRAM.
    // - When BLE is disconnected (60s grace period), use 0 ticks (non-blocking) so once the
    //   16-packet buffer fills up, excess packets drop immediately without stalling the MQTT task.
    TickType_t waitTicks = s_ble_connected.load(std::memory_order_relaxed) ? pdMS_TO_TICKS(50) : 0;
    if (xQueueSend(mqtt_to_ble_queue, &packet, waitTicks) != pdTRUE) {
        log_e("[Bridge-MQTT] ERROR: mqtt_to_ble_queue full (%d packets), dropped downlink packet", MQTT_QUEUE_SIZE);
        s_mqtt_to_ble_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

/**
 * Clear all pending packets in the MQTT downlink queue.
 */
void bridge_clear_mqtt_to_ble_queue() {
    if (mqtt_to_ble_queue) {
        xQueueReset(mqtt_to_ble_queue);
        log_i("[Bridge-MQTT] Cleared mqtt_to_ble_queue.");
    }
}

/**
 * Initialize Bridge.
 * @param ble_mac MAC address of BLE device to connect to.
 * @param ble_pin PIN for BLE device.
 */
void bridge_init(const char* ble_mac, uint32_t ble_pin) {
    strncpy(s_target_ble_mac, ble_mac ? ble_mac : "", sizeof(s_target_ble_mac) - 1);
    s_target_ble_mac[sizeof(s_target_ble_mac) - 1] = '\0';
    s_target_ble_pin.store(ble_pin, std::memory_order_relaxed);
    s_ble_deactivated.store(false, std::memory_order_relaxed);
    s_ble_state.store(
        (s_target_ble_mac[0] != '\0') ? BLE_STATE_DISCONNECTED : BLE_STATE_NOT_CONFIGURED,
        std::memory_order_release
    );

    /**
     * Queues for passing packets between BLE <--> TCP.
     * Statically allocate FreeRTOS queues in .bss segment (zero heap fragmentation)
     * Holds up to BRIDGE_QUEUE_SIZE (defaults 32) "packets" of data
     */
    tcp_to_ble_queue = xQueueCreateStatic(
        BRIDGE_QUEUE_SIZE,
        sizeof(BridgePacket),
        tcp_to_ble_queue_storage,
        &tcp_to_ble_queue_struct
    );
    ble_to_tcp_queue = xQueueCreateStatic(
        BRIDGE_QUEUE_SIZE,
        sizeof(BridgePacket),
        ble_to_tcp_queue_storage,
        &ble_to_tcp_queue_struct
    );

    /**
     * Queue for passing packets from MQTT broker -> BLE.
     */
    mqtt_to_ble_queue = xQueueCreateStatic(
        MQTT_QUEUE_SIZE,
        sizeof(BridgePacket),
        mqtt_to_ble_queue_storage,
        &mqtt_to_ble_queue_struct
    );

    // Register MQTT Downlink Callback to enqueue ToRadio packets for BLE transmission
    mqtt_net_set_downlink_callback([](const uint8_t* to_radio_buf, size_t len) {
        bridge_enqueue_mqtt_to_ble(to_radio_buf, len);
    });

    // Mutex for thread safe modifying/reading of connected TCP clients.
    tcpClientsMutex = xSemaphoreCreateMutex();

    // Start the tcp server
    tcpServer = new AsyncServer(TCP_PORT);
    tcpServer->onClient(&onClientConnected, tcpServer);
}

static TaskHandle_t bridgeBleTaskHandle = NULL;
static TaskHandle_t bridgeNetTaskHandle = NULL;

void bridge_start() {
    // If already running, refuse to start (again).
    if (bridgeRunning) {
        return;
    }
    bridgeRunning = true;

    tcpServer->begin();
    log_i("[Bridge] TCP Server started on port %d", TCP_PORT);

    // Starts the BLE task and pins to CPU core 1
    xTaskCreatePinnedToCore(bridgeBleTask, "bridge_ble", 8192, NULL, 1, &bridgeBleTaskHandle, 1);

    // Starts the TCP/Net task and pins to CPU core 0
    xTaskCreatePinnedToCore(bridgeNetTask, "bridge_net", 4096, NULL, 1, &bridgeNetTaskHandle, 0);
}

/**
 * @return Human-readable static string literal for a BleState enum value (zero heap allocation).
 */
const char* bridge_ble_state_to_string(BleState state) {
    switch (state) {
        case BLE_STATE_NOT_CONFIGURED: return "Not Configured";
        case BLE_STATE_DEACTIVATING:   return "Deactivating...";
        case BLE_STATE_DEACTIVATED:    return "Deactivated";
        case BLE_STATE_DISCONNECTED:   return "Disconnected";
        case BLE_STATE_CONNECTING:     return "Connecting...";
        case BLE_STATE_CONNECTED:      return "Connected";
        default:                       return "Unknown";
    }
}

/**
 * @return True if the BLE device is connected, false if not (wait-free atomic read)
 */
bool bridge_is_ble_connected() {
    return s_ble_connected.load(std::memory_order_relaxed);
}

/**
 * @return Current BleState (wait-free atomic read)
 */
BleState bridge_get_ble_state() {
    return s_ble_state.load(std::memory_order_relaxed);
}

/**
 * @return True if the BLE bridge connection loop is active (not deactivated).
 */
bool bridge_is_ble_active() {
    return !s_ble_deactivated.load(std::memory_order_relaxed);
}

/**
 * Request activation or deactivation of the BLE bridge connection/retry loop.
 * Lock-free and non-blocking on Core 0: sets the atomic request flag and state;
 * bridgeBleTask on Core 1 picks up the flag on its next loop iteration and performs
 * the actual disconnect and teardown.
 */
void bridge_set_ble_active(bool active) {
    if (!active) {
        s_ble_deactivated.store(true, std::memory_order_release);
        if (s_ble_state.load(std::memory_order_relaxed) != BLE_STATE_DEACTIVATED) {
            s_ble_state.store(BLE_STATE_DEACTIVATING, std::memory_order_relaxed);
        }
        log_i("[Bridge-BLE] BLE deactivation requested via WebUI.");
    } else {
        std::atomic_thread_fence(std::memory_order_acquire);
        bool hasMac = (s_target_ble_mac[0] != '\0');
        s_ble_deactivated.store(false, std::memory_order_release);
        if (!s_ble_connected.load(std::memory_order_relaxed)) {
            s_ble_state.store(hasMac ? BLE_STATE_DISCONNECTED : BLE_STATE_NOT_CONFIGURED, std::memory_order_relaxed);
        }
        log_i("[Bridge-BLE] BLE connection activated via WebUI.");
    }
}

/**
 * Update the target BLE MAC and PIN at runtime (lock-free, zero heap allocation)
 * and automatically activate the BLE connection loop.
 */
void bridge_update_ble_config(const char* ble_mac, uint32_t ble_pin) {
    strncpy(s_target_ble_mac, ble_mac ? ble_mac : "", sizeof(s_target_ble_mac) - 1);
    s_target_ble_mac[sizeof(s_target_ble_mac) - 1] = '\0';
    s_target_ble_pin.store(ble_pin, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    s_ble_config_dirty.store(true, std::memory_order_release);
    bridge_set_ble_active(s_target_ble_mac[0] != '\0');
}

/**
 * @return Stats about the running tasks, queues, etc...
 */
BridgeDiagStats bridge_get_diag_stats() {
    BridgeDiagStats stats = {};
    stats.tcp_to_ble_waiting = tcp_to_ble_queue ? uxQueueMessagesWaiting(tcp_to_ble_queue) : 0;
    stats.tcp_to_ble_capacity = BRIDGE_QUEUE_SIZE;
    stats.tcp_to_ble_dropped = s_tcp_to_ble_dropped.load(std::memory_order_relaxed);
    stats.ble_to_tcp_waiting = ble_to_tcp_queue ? uxQueueMessagesWaiting(ble_to_tcp_queue) : 0;
    stats.ble_to_tcp_capacity = BRIDGE_QUEUE_SIZE;
    stats.ble_to_tcp_dropped = s_ble_to_tcp_dropped.load(std::memory_order_relaxed);
    stats.mqtt_to_ble_waiting = mqtt_to_ble_queue ? uxQueueMessagesWaiting(mqtt_to_ble_queue) : 0;
    stats.mqtt_to_ble_capacity = MQTT_QUEUE_SIZE;
    stats.mqtt_to_ble_dropped = s_mqtt_to_ble_dropped.load(std::memory_order_relaxed);
    stats.connected_tcp_clients = connectedClientsCount.load();
    stats.ble_task_stack_free_bytes = bridgeBleTaskHandle ? (uxTaskGetStackHighWaterMark(bridgeBleTaskHandle) * sizeof(StackType_t)) : 0;
    stats.net_task_stack_free_bytes = bridgeNetTaskHandle ? (uxTaskGetStackHighWaterMark(bridgeNetTaskHandle) * sizeof(StackType_t)) : 0;
    stats.ble_connected = bridge_is_ble_connected();
    stats.ble_state = bridge_get_ble_state();
    stats.ble_rx_count = s_ble_rx_count.load(std::memory_order_relaxed);
    stats.ble_tx_count = s_ble_tx_count.load(std::memory_order_relaxed);
    stats.ble_invalid_count = s_ble_invalid_count.load(std::memory_order_relaxed);
    stats.tcp_rx_count = s_tcp_rx_count.load(std::memory_order_relaxed);
    stats.tcp_tx_count = s_tcp_tx_count.load(std::memory_order_relaxed);
    stats.tcp_invalid_count = s_tcp_invalid_count.load(std::memory_order_relaxed);
    return stats;
}
