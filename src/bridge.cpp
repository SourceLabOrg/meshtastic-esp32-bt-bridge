#include "bridge.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <NimBLEDevice.h>
#include "config_ui.h" // for bridge config if needed
#include "status_led.h"
#include "build_options.h"
#include "utils.h"
#include <atomic>

// Queues for inter-task communication
static QueueHandle_t tcp_to_ble_queue = NULL;
static QueueHandle_t ble_to_tcp_queue = NULL;

struct BridgePacket {
    uint8_t data[512]; // Max Meshtastic protobuf size
    size_t len;
};

struct ClientContext {
    AsyncClient* client;
    std::vector<uint8_t> rx_buffer;
};

static AsyncServer* tcpServer = NULL;
static std::vector<ClientContext*> tcpClients;
static SemaphoreHandle_t tcpClientsMutex = NULL; // Mutex to protect tcpClients vector from concurrent modification
static std::atomic<size_t> connectedClientsCount{0};
static bool bridgeRunning = false;
static String targetBleMac = "";
static uint32_t targetBlePin = 123456;

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
        connectedClientsCount++;
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
                    processed++; // Invalid length, skip one byte and search again
                    continue;
                }

                if (ctx->rx_buffer.size() - processed >= 4 + payload_len) {
                    // Valid full frame
                    log_d("[Bridge-TCP] Parsed frame: len %u", payload_len);
                    BridgePacket packet;
                    packet.len = payload_len;
                    if (payload_len <= sizeof(packet.data)) {
                        memcpy(packet.data, &ctx->rx_buffer[processed + 4], payload_len);
                        if (xQueueSend(tcp_to_ble_queue, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                            log_w("[Bridge-TCP] WARNING: tcp_to_ble_queue full, dropped packet");
                        }
                    } else {
                        log_e("[Bridge-TCP] ERROR: Payload too large (%u bytes)", payload_len);
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
                connectedClientsCount--;
            }
            xSemaphoreGive(tcpClientsMutex);
        }

        log_i("[Bridge] TCP Client disconnected. Total clients remaining: %zu", connectedClientsCount.load());
        delete ctx;
    }, ctx);
}

/**
 * Async flag when the Bluetooth device notifies us there is data to be read.
 * NOTE: This notification path has been found to be unreliable... as a work around we aggressively
 *       poll for data.
 */
static volatile bool pendingRadioRead = false;
static void notifyFromNum(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    pendingRadioRead = true;
}

static void notifyFromRadio(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    // If no TCP clients are connected, then just ignore the data.
    if (connectedClientsCount.load() == 0) {
        return;
    }
    log_d("[Bridge-BLE] notifyFromRadio triggered with %zu bytes!", length);

    // If the device notifies FromRadio directly
    if (length > 0) {
        BridgePacket packet;
        packet.len = length;
        if (length <= sizeof(packet.data)) {
            memcpy(packet.data, pData, length);
            if (xQueueSend(ble_to_tcp_queue, &packet, pdMS_TO_TICKS(50)) != pdTRUE) {
                log_e("[Bridge-BLE] CRITICAL: ble_to_tcp_queue FULL! Dropped %zu bytes from Notify", length);
            }
        } else {
            log_e("[Bridge-BLE] ERROR: Notify Payload too large (%zu bytes)", length);
        }
    }
}

// Custom BLE Client Callbacks to handle PIN authentication
class BridgeRuntimeClientCallbacks : public NimBLEClientCallbacks {
public:
    uint32_t onPassKeyRequest() override {
        log_i("[Bridge-BLE] Passkey requested, providing PIN: %06u", targetBlePin);
        return targetBlePin;
    }

    bool onConfirmPIN(uint32_t pin) override {
        log_i("[Bridge-BLE] Confirming PIN: %06u", pin);
        return (pin == targetBlePin);
    }

    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        log_i(
            "[Bridge-BLE] Authentication complete. Encrypted: %d, Authenticated: %d",
            desc->sec_state.encrypted,
            desc->sec_state.authenticated
        );
    }
};

static BridgeRuntimeClientCallbacks bridgeCallbacks;

/**
 * BLE Task (running on Core 1)
 * This is the task the manages:
 *  - reading data off of the BLE radio and pushing onto the TCP queue.
 *  - reading data off of the BLE queue and writing to the BLE radio.
 */
static void bridgeBleTask(void* parameter) {
    log_i("[Bridge-BLE] BLE Task started on Core 1");

    while (bridgeRunning) {
        /**
         * If not connected to the BLE device yet, attempt to connect.
         * Blink the status LED appropriately to indicate no BLE connection is active.
         */
        if (!bleClient || !bleClient->isConnected()) {
            status_led_set(LED_MED_BLINK);
            log_i("[Bridge-BLE] Attempting to connect to Meshtastic BLE device...");

            NimBLEAddress addr = utils_parse_ble_address(targetBleMac);

            if (!bleClient) {
                NimBLEDevice::init("Meshtastic-Bridge");
                NimBLEDevice::setSecurityAuth(true, true, true);
                NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);

                bleClient = NimBLEDevice::createClient();
                bleClient->setClientCallbacks(&bridgeCallbacks, false);
            }

            bleClient->setConnectTimeout(BLUETOOTH_TIMEOUT_SECONDS);
            if (bleClient->connect(addr, false)) {
                log_i("[Bridge-BLE] Connected! Securing connection...");
                bleClient->secureConnection();

                delay(2000);

                // Request larger MTU for large protobuf packets
                NimBLEDevice::setMTU(512);

                NimBLERemoteService* pSvc = bleClient->getService("6ba1b218-15a8-461f-9fa8-5dcae273eafd");
                if (pSvc) {
                    toRadioChar = pSvc->getCharacteristic("f75c76d2-129e-4dad-a1dd-7866124401e7");
                    fromRadioChar = pSvc->getCharacteristic("2c55e69e-4993-11ed-b878-0242ac120002");
                    fromNumChar = pSvc->getCharacteristic("ed9da18c-a800-4f66-a670-aa7547e34453");

                    if (fromNumChar && fromNumChar->canNotify()) {
                        bool sub = fromNumChar->subscribe(true, notifyFromNum);
                        log_d("[Bridge-BLE] Subscribed to FromNum: %d", sub);
                    }

                    if (fromRadioChar && fromRadioChar->canNotify()) {
                        bool sub = fromRadioChar->subscribe(true, notifyFromRadio);
                        log_d("[Bridge-BLE] Subscribed to FromRadio: %d", sub);
                    }
                    log_i("[Bridge-BLE] BLE setup complete. Bridging active.");
                    status_led_set(LED_SOLID_ON);
                } else {
                    log_i("[Bridge-BLE] Meshtastic service not found!");
                    bleClient->disconnect();
                }
            } else {
                log_i("[Bridge-BLE] Connection failed, retrying in 5s...");
                delay(5000);
            }
        } else {
            BridgePacket packet;
            // If we have a pending read, do not sleep at all (0 ticks).
            // Otherwise, wait up to 10ms for incoming TCP packets to keep the loop fast.
            TickType_t waitTicks = pendingRadioRead ? 0 : pdMS_TO_TICKS(10);

            if (xQueueReceive(tcp_to_ble_queue, &packet, waitTicks) == pdTRUE) {
                log_d("[Bridge-BLE] Writing %zu bytes to ToRadio...", packet.len);
                if (toRadioChar && toRadioChar->canWrite()) {
                    // Meshtastic ToRadio expects Write Without Response (false)
                    bool success = toRadioChar->writeValue(packet.data, packet.len, false);
                    log_d("[Bridge-BLE] Write success: %d", success);
                }
            }

            static unsigned long lastFailsafe = 0;
            if (fromRadioChar && connectedClientsCount.load() > 0) {
                // Read if notified, or if it's been 250ms since last check (failsafe)
                bool doRead = pendingRadioRead || (millis() - lastFailsafe > 250);

                if (doRead) {
                    bool wasNotified = pendingRadioRead; // Capture this before clearing it!
                    pendingRadioRead = false;
                    lastFailsafe = millis();

                    static std::string lastPacket = "";
                    static unsigned long lastPacketTime = 0;
                    std::string currentVal = fromRadioChar->readValue();

                    // Accept the packet if it's different, OR if the radio explicitly notified us it was new,
                    // OR if 2.5 seconds have passed (failsafe for dropped identical heartbeat notifications)
                    if (currentVal.length() > 0 && (currentVal != lastPacket || wasNotified || (millis() - lastPacketTime > 2500))) {
                        lastPacket = currentVal;
                        lastPacketTime = millis();

                        log_d("[Bridge-BLE] Fast-Polled %d new bytes from FromRadio!", currentVal.length());

                        BridgePacket rx_packet;
                        rx_packet.len = currentVal.length();
                        if (rx_packet.len <= sizeof(rx_packet.data)) {
                            memcpy(rx_packet.data, currentVal.data(), rx_packet.len);
                            // Apply backpressure: Wait up to 50ms if the queue is full so we don't drop packets!
                            if (xQueueSend(ble_to_tcp_queue, &rx_packet, pdMS_TO_TICKS(50)) != pdTRUE) {
                                log_e("[Bridge-BLE] CRITICAL: ble_to_tcp_queue FULL! Dropped %zu bytes", rx_packet.len);
                            }
                        } else {
                            log_e("[Bridge-BLE] ERROR: Radio Payload too large (%zu bytes)", rx_packet.len);
                        }

                        // We successfully pulled a NEW packet! There might be more packets
                        // waiting in the queue. Flag it to instantly read again on the next loop!
                        pendingRadioRead = true;
                    }
                }
            }
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
            uint8_t frame[516]; // Max Meshtastic packet is 512 bytes + 4 byte header

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
                        } else {
                            log_w("[Bridge-Net] WARNING: Client TX buffer full! Dropped %zu bytes for %s", packet.len, ctx->client->remoteIP().toString().c_str());
                        }
                    }
                    xSemaphoreGive(tcpClientsMutex);
                }
            } else {
                log_e("[Bridge-Net] ERROR: Packet too large for frame buffer (%zu bytes)", packet.len);
            }
        }
    }
    vTaskDelete(NULL);
}

/**
 * Initialize Bridge.
 * @param ble_mac MAC address of BLE device to connect to.
 * @param ble_pin PIN for BLE device.
 */
void bridge_init(const String& ble_mac, uint32_t ble_pin) {
    targetBleMac = ble_mac;
    targetBlePin = ble_pin;

    // Queues to handle high-speed bursts of Meshtastic Node DB packets
    // Holds up to BRIDGE_QUEUE_SIZE (defaults 100) "packets" of data
    tcp_to_ble_queue = xQueueCreate(BRIDGE_QUEUE_SIZE, sizeof(BridgePacket));
    ble_to_tcp_queue = xQueueCreate(BRIDGE_QUEUE_SIZE, sizeof(BridgePacket));

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
 * @return True if the BLE device is connected, false if not
 */
bool bridge_is_ble_connected() {
    return (bleClient != NULL && bleClient->isConnected() && toRadioChar != NULL);
}

BridgeDiagStats bridge_get_diag_stats() {
    BridgeDiagStats stats = {};
    stats.tcp_to_ble_waiting = tcp_to_ble_queue ? uxQueueMessagesWaiting(tcp_to_ble_queue) : 0;
    stats.tcp_to_ble_capacity = BRIDGE_QUEUE_SIZE;
    stats.ble_to_tcp_waiting = ble_to_tcp_queue ? uxQueueMessagesWaiting(ble_to_tcp_queue) : 0;
    stats.ble_to_tcp_capacity = BRIDGE_QUEUE_SIZE;
    stats.connected_tcp_clients = connectedClientsCount.load();
    stats.ble_task_stack_free_bytes = bridgeBleTaskHandle ? (uxTaskGetStackHighWaterMark(bridgeBleTaskHandle) * sizeof(StackType_t)) : 0;
    stats.net_task_stack_free_bytes = bridgeNetTaskHandle ? (uxTaskGetStackHighWaterMark(bridgeNetTaskHandle) * sizeof(StackType_t)) : 0;
    stats.ble_connected = bridge_is_ble_connected();
    return stats;
}
