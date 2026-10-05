#include "bridge.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <NimBLEDevice.h>
#include "config_ui.h" // for bridge config if needed

#define TCP_PORT 4403
#define MAX_TCP_CLIENTS 3
#define BRIDGE_DEBUG 1

#if BRIDGE_DEBUG
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#define DBG_PRINTLN(...)
#define DBG_PRINTF(...)
#endif

// Queues for inter-task communication
static QueueHandle_t tcp_to_ble_queue = NULL;
static QueueHandle_t ble_to_tcp_queue = NULL;

struct BridgePacket {
    uint8_t* data;
    size_t len;
};

#include <atomic>

struct ClientContext {
    AsyncClient* client;
    std::vector<uint8_t> rx_buffer;
};

static AsyncServer* tcpServer = NULL;
static std::vector<ClientContext*> tcpClients;
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

    ClientContext* ctx = new ClientContext();
    ctx->client = client;
    tcpClients.push_back(ctx);
    connectedClientsCount++;

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
                    packet.data = (uint8_t*)malloc(payload_len);
                    if (packet.data) {
                        memcpy(packet.data, &ctx->rx_buffer[processed + 4], payload_len);
                        if (xQueueSend(tcp_to_ble_queue, &packet, 0) != pdTRUE) {
                            free(packet.data);
                            Serial.println("[Bridge] tcp_to_ble_queue full, dropped packet");
                        }
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
        auto it = std::find(tcpClients.begin(), tcpClients.end(), ctx);
        if (it != tcpClients.end()) {
            tcpClients.erase(it);
        }
        connectedClientsCount--;
        Serial.printf("[Bridge] TCP Client disconnected. Total clients remaining: %zu\n", connectedClientsCount.load());
        delete ctx;
    }, ctx);
}

// BLE Notify Callbacks

static void notifyFromRadio(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (connectedClientsCount.load() == 0) return;
    DBG_PRINTF("[Bridge-BLE] notifyFromRadio triggered with %zu bytes!\n", length);

    // If the device notifies FromRadio directly
    if (length > 0) {
        BridgePacket packet;
        packet.len = length;
        packet.data = (uint8_t*)malloc(length);
        if (packet.data) {
            memcpy(packet.data, pData, length);
            if (xQueueSend(ble_to_tcp_queue, &packet, 0) != pdTRUE) {
                free(packet.data);
            }
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
                    
                    // Note: We intentionally DO NOT subscribe to fromNumChar here.
                    // Calling a blocking readValue() inside a notification callback deadlocks the NimBLE stack.
                    // Instead, we rely on the 100ms polling loop below to read fromRadioChar.
                    
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
            // Wait up to 100ms for incoming TCP packets to send to BLE
            if (xQueueReceive(tcp_to_ble_queue, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
                #if BRIDGE_DEBUG
                Serial.printf("[Bridge-BLE] Writing %zu bytes to ToRadio...\n", packet.len);
                #endif
                if (toRadioChar && toRadioChar->canWrite()) {
                    // Meshtastic ToRadio expects Write Without Response (false)
                    bool success = toRadioChar->writeValue(packet.data, packet.len, false);
                    #if BRIDGE_DEBUG
                    Serial.printf("[Bridge-BLE] Write success: %d\n", success);
                    #endif
                }
                free(packet.data);
            }
            
            // POLLING FALLBACK (Matches Python reference bridge behavior)
            // Some Meshtastic devices fail to trigger FromNum notifications. 
            // We poll FromRadio every ~100ms (dictated by the xQueueReceive timeout above)
            if (fromRadioChar && connectedClientsCount.load() > 0) {
                static std::string lastPacket = "";
                std::string currentVal = fromRadioChar->readValue();
                
                if (currentVal.length() > 0 && currentVal != lastPacket) {
                    lastPacket = currentVal;
                    
                    DBG_PRINTF("[Bridge-BLE] Polled %d new bytes from FromRadio!\n", currentVal.length());
                    
                    BridgePacket rx_packet;
                    rx_packet.len = currentVal.length();
                    rx_packet.data = (uint8_t*)malloc(rx_packet.len);
                    if (rx_packet.data) {
                        memcpy(rx_packet.data, currentVal.data(), rx_packet.len);
                        if (xQueueSend(ble_to_tcp_queue, &rx_packet, 0) != pdTRUE) {
                            free(rx_packet.data);
                        }
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
            #if BRIDGE_DEBUG
            Serial.printf("[Bridge-Net] Broadcasting %zu bytes to %zu TCP clients\n", packet.len, connectedClientsCount.load());
            #endif
            // Build TCP frame
            uint8_t header[4] = {0x94, 0xC3, (uint8_t)(packet.len >> 8), (uint8_t)(packet.len & 0xFF)};
            
            // Send to all connected clients
            for (ClientContext* ctx : tcpClients) {
                if (ctx->client->space() >= packet.len + 4) {
                    ctx->client->write((const char*)header, 4);
                    ctx->client->write((const char*)packet.data, packet.len);
                }
            }
            free(packet.data);
        }
    }
    vTaskDelete(NULL);
}

void bridge_init(const String& ble_mac) {
    targetBleMac = ble_mac;
    
    // Load configured PIN if needed
    BridgeConfig cfg = config_ui_load();
    targetBlePin = cfg.ble_pin.toInt();

    tcp_to_ble_queue = xQueueCreate(10, sizeof(BridgePacket));
    ble_to_tcp_queue = xQueueCreate(10, sizeof(BridgePacket));
    
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
