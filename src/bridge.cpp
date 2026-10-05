#include "bridge.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <NimBLEDevice.h>
#include "config_ui.h" // for bridge config if needed

#define TCP_PORT 4403
#define MAX_TCP_CLIENTS 3
#define DBG_PRINT(...) if (g_debug_logs) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) if (g_debug_logs) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) if (g_debug_logs) Serial.printf(__VA_ARGS__)

// Queues for inter-task communication
static QueueHandle_t tcp_to_ble_queue = NULL;
static QueueHandle_t ble_to_tcp_queue = NULL;

struct BridgePacket {
    uint8_t data[512]; // Max Meshtastic protobuf size
    size_t len;
};

#include <atomic>

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
        Serial.printf("[Bridge] TCP Connection rejected from %s: Max clients (%d) reached.\n", client->remoteIP().toString().c_str(), MAX_TCP_CLIENTS);
        client->close();
        return;
    }
    
    // Crucial TCP connection parameters for stable bridging
    // We intentionally leave Nagle's algorithm enabled (false) so LwIP batches the tiny 
    // BLE packets together. This prevents WiFi congestion and TCP retransmission timeouts.
    client->setNoDelay(false); 
    client->setRxTimeout(600); // 10 minute idle timeout

    ClientContext* ctx = new ClientContext();
    ctx->client = client;
    if (xSemaphoreTake(tcpClientsMutex, portMAX_DELAY) == pdTRUE) {
        tcpClients.push_back(ctx);
        connectedClientsCount++;
        xSemaphoreGive(tcpClientsMutex);
    }

    Serial.printf("[Bridge] TCP Client connected from %s. Total clients: %zu\n", client->remoteIP().toString().c_str(), connectedClientsCount.load());

    client->onData([](void* arg, AsyncClient* c, void* data, size_t len) {
        ClientContext* ctx = (ClientContext*)arg;
        uint8_t* buf = (uint8_t*)data;
        
        DBG_PRINTF("[Bridge-TCP] RX %zu bytes from %s\n", len, c->remoteIP().toString().c_str());
        
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
                    DBG_PRINTF("[Bridge-TCP] Parsed frame: len %u\n", payload_len);
                    BridgePacket packet;
                    packet.len = payload_len;
                    if (payload_len <= sizeof(packet.data)) {
                        memcpy(packet.data, &ctx->rx_buffer[processed + 4], payload_len);
                        if (xQueueSend(tcp_to_ble_queue, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                            Serial.println("[Bridge-TCP] WARNING: tcp_to_ble_queue full, dropped packet");
                        }
                    } else {
                        Serial.printf("[Bridge-TCP] ERROR: Payload too large (%u bytes)\n", payload_len);
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
        
        if (xSemaphoreTake(tcpClientsMutex, portMAX_DELAY) == pdTRUE) {
            auto it = std::find(tcpClients.begin(), tcpClients.end(), ctx);
            if (it != tcpClients.end()) {
                tcpClients.erase(it);
            }
            connectedClientsCount--;
            xSemaphoreGive(tcpClientsMutex);
        }

        Serial.printf("[Bridge] TCP Client disconnected. Total clients remaining: %zu\n", connectedClientsCount.load());
        delete ctx;
    }, ctx);
}

static volatile bool pendingRadioRead = false;

static void notifyFromNum(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    pendingRadioRead = true;
}

static void notifyFromRadio(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (connectedClientsCount.load() == 0) return;
    DBG_PRINTF("[Bridge-BLE] notifyFromRadio triggered with %zu bytes!\n", length);

    // If the device notifies FromRadio directly
    if (length > 0) {
        BridgePacket packet;
        packet.len = length;
        if (length <= sizeof(packet.data)) {
            memcpy(packet.data, pData, length);
            if (xQueueSend(ble_to_tcp_queue, &packet, pdMS_TO_TICKS(50)) != pdTRUE) {
                Serial.printf("[Bridge-BLE] CRITICAL: ble_to_tcp_queue FULL! Dropped %zu bytes from Notify\n", length);
            }
        } else {
            Serial.printf("[Bridge-BLE] ERROR: Notify Payload too large (%zu bytes)\n", length);
        }
    }
}

// Custom BLE Client Callbacks to handle PIN authentication
class BridgeRuntimeClientCallbacks : public NimBLEClientCallbacks {
public:
    uint32_t onPassKeyRequest() override {
        Serial.printf("[Bridge-BLE] Passkey requested, providing PIN: %06u\n", targetBlePin);
        return targetBlePin;
    }
    
    bool onConfirmPIN(uint32_t pin) override {
        Serial.printf("[Bridge-BLE] Confirming PIN: %06u\n", pin);
        return (pin == targetBlePin);
    }
    
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        Serial.printf("[Bridge-BLE] Authentication complete. Encrypted: %d, Authenticated: %d\n",
                      desc->sec_state.encrypted, desc->sec_state.authenticated);
    }
};

static BridgeRuntimeClientCallbacks bridgeCallbacks;

// BLE Task (Core 1)
static void bridgeBleTask(void* parameter) {
    Serial.println("[Bridge] BLE Task started on Core 1");
    
    while (bridgeRunning) {
        if (!bleClient || !bleClient->isConnected()) {
            Serial.println("[Bridge] Attempting to connect to Meshtastic BLE device...");
            
            NimBLEAddress addr(targetBleMac.c_str());
            
            if (!bleClient) {
                NimBLEDevice::init("Meshtastic-Bridge");
                NimBLEDevice::setSecurityAuth(true, true, true);
                NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
                
                bleClient = NimBLEDevice::createClient();
                bleClient->setClientCallbacks(&bridgeCallbacks, false);
            }
            
            bleClient->setConnectTimeout(10);
            if (bleClient->connect(addr, false)) {
                Serial.println("[Bridge] Connected! Securing connection...");
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
                        DBG_PRINTF("[Bridge-BLE] Subscribed to FromNum: %d\n", sub);
                    }
                    
                    if (fromRadioChar && fromRadioChar->canNotify()) {
                        bool sub = fromRadioChar->subscribe(true, notifyFromRadio);
                        DBG_PRINTF("[Bridge-BLE] Subscribed to FromRadio: %d\n", sub);
                    }
                    Serial.println("[Bridge] BLE setup complete. Bridging active.");
                } else {
                    Serial.println("[Bridge] Meshtastic service not found!");
                    bleClient->disconnect();
                }
            } else {
                Serial.println("[Bridge] Connection failed, retrying in 5s...");
                delay(5000);
            }
        } else {
            BridgePacket packet;
            // If we have a pending read, do not sleep at all (0 ticks). 
            // Otherwise, wait up to 10ms for incoming TCP packets to keep the loop fast.
            TickType_t waitTicks = pendingRadioRead ? 0 : pdMS_TO_TICKS(10);
            
            if (xQueueReceive(tcp_to_ble_queue, &packet, waitTicks) == pdTRUE) {
                DBG_PRINTF("[Bridge-BLE] Writing %zu bytes to ToRadio...\n", packet.len);
                if (toRadioChar && toRadioChar->canWrite()) {
                    // Meshtastic ToRadio expects Write Without Response (false)
                    bool success = toRadioChar->writeValue(packet.data, packet.len, false);
                    DBG_PRINTF("[Bridge-BLE] Write success: %d\n", success);
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
                        
                        DBG_PRINTF("[Bridge-BLE] Fast-Polled %d new bytes from FromRadio!\n", currentVal.length());
                        
                        BridgePacket rx_packet;
                        rx_packet.len = currentVal.length();
                        if (rx_packet.len <= sizeof(rx_packet.data)) {
                            memcpy(rx_packet.data, currentVal.data(), rx_packet.len);
                            // Apply backpressure: Wait up to 50ms if the queue is full so we don't drop packets!
                            if (xQueueSend(ble_to_tcp_queue, &rx_packet, pdMS_TO_TICKS(50)) != pdTRUE) {
                                Serial.printf("[Bridge-BLE] CRITICAL: ble_to_tcp_queue FULL! Dropped %zu bytes\n", rx_packet.len);
                            }
                        } else {
                            Serial.printf("[Bridge-BLE] ERROR: Radio Payload too large (%zu bytes)\n", rx_packet.len);
                        }
                        
                        // We successfully pulled a NEW packet! There might be more packets 
                        // instantly waiting in the queue. Flag it to instantly read again on the next loop!
                        pendingRadioRead = true;
                    }
                }
            }
        }
    }
    
    vTaskDelete(NULL);
}

// Network broadcast task (runs on Core 0)
static void bridgeNetTask(void* parameter) {
    Serial.println("[Bridge] Network broadcasting Task started on Core 0");
    
    while (bridgeRunning) {
        BridgePacket packet;
        if (xQueueReceive(ble_to_tcp_queue, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            DBG_PRINTF("[Bridge-Net] Broadcasting %zu bytes to %zu TCP clients\n", packet.len, connectedClientsCount.load());
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
                            Serial.printf("[Bridge-Net] WARNING: Client TX buffer full! Dropped %zu bytes for %s\n", packet.len, ctx->client->remoteIP().toString().c_str());
                        }
                    }
                    xSemaphoreGive(tcpClientsMutex);
                }
            } else {
                Serial.printf("[Bridge-Net] ERROR: Packet too large for frame buffer (%zu bytes)\n", packet.len);
            }
        }
    }
    vTaskDelete(NULL);
}

void bridge_init(const String& ble_mac, uint32_t ble_pin) {
    targetBleMac = ble_mac;
    targetBlePin = ble_pin;

    // Massive queues to handle high-speed bursts of Meshtastic Node DB packets
    tcp_to_ble_queue = xQueueCreate(100, sizeof(BridgePacket));
    ble_to_tcp_queue = xQueueCreate(100, sizeof(BridgePacket));
    
    tcpClientsMutex = xSemaphoreCreateMutex();
    
    tcpServer = new AsyncServer(TCP_PORT);
    tcpServer->onClient(&onClientConnected, tcpServer);
}

void bridge_start() {
    if (bridgeRunning) return;
    bridgeRunning = true;
    
    tcpServer->begin();
    Serial.printf("[Bridge] TCP Server started on port %d\n", TCP_PORT);
    
    xTaskCreatePinnedToCore(bridgeBleTask, "bridge_ble", 8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(bridgeNetTask, "bridge_net", 4096, NULL, 1, NULL, 0);
}
