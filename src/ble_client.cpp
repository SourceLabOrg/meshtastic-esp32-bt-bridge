#include "ble_client.h"
#include <NimBLEDevice.h>
#include <vector>
#include <algorithm>

static bool isInitialized = false;
static bool isScanningFlag = false;

void ble_client_init() {
    if (isInitialized) return;
    NimBLEDevice::init("Meshtastic-Bridge");
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);       // Active scan requests scan response packets to get device names
    pScan->setInterval(100);          // 100ms scan interval
    pScan->setWindow(99);             // 99ms scan window
    pScan->setDuplicateFilter(true);   // Filter duplicate advertisements during a single scan
    
    isInitialized = true;
    Serial.println("BLE Client initialized with NimBLE.");
}

void ble_client_start_scan() {
    if (!isInitialized) {
        ble_client_init();
    }
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (isScanningFlag || pScan->isScanning()) {
        Serial.println("BLE scan already in progress.");
        return;
    }
    
    // Clear previously cached results before starting a fresh scan
    pScan->clearResults();
    isScanningFlag = true;
    
    Serial.printf("Starting BLE scan (%d seconds)...\n", BLE_SCAN_DURATION_SECONDS);
    // Start asynchronous scan for BLE_SCAN_DURATION_SECONDS
    bool started = pScan->start(BLE_SCAN_DURATION_SECONDS, [](NimBLEScanResults results) {
        isScanningFlag = false;
        Serial.printf("BLE scan finished. Found %d device(s).\n", results.getCount());
    }, false);

    if (!started) {
        isScanningFlag = false;
        Serial.println("Failed to start BLE scan.");
    }
}

void ble_client_stop_scan() {
    if (!isInitialized) return;
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (isScanningFlag || pScan->isScanning()) {
        pScan->stop();
        isScanningFlag = false;
        Serial.println("BLE scan stopped.");
    }
}

bool ble_client_is_scanning() {
    if (!isInitialized) return false;
    return isScanningFlag || NimBLEDevice::getScan()->isScanning();
}

static String escapeJsonString(const String& input) {
    String output = "";
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\b') output += "\\b";
        else if (c == '\f') output += "\\f";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else if (c >= 32 && c <= 126) output += c;
    }
    return output;
}

struct DiscoveredDevice {
    String name;
    String mac;
    int rssi;
};

String ble_client_get_scan_results_json() {
    if (!isInitialized) return "[]";
    
    NimBLEScanResults results = NimBLEDevice::getScan()->getResults();
    int count = results.getCount();
    
    std::vector<DiscoveredDevice> devices;
    devices.reserve(count);
    
    for (int i = 0; i < count; i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        String name = String(device.getName().c_str());
        String mac = String(device.getAddress().toString().c_str());
        int rssi = device.getRSSI();
        
        if (mac.isEmpty()) continue;
        
        // Deduplicate by MAC
        bool exists = false;
        for (auto& d : devices) {
            if (d.mac.equalsIgnoreCase(mac)) {
                // If existing entry has no name but this advertisement has one, update it
                if (d.name.isEmpty() && !name.isEmpty()) {
                    d.name = name;
                }
                if (rssi > d.rssi) {
                    d.rssi = rssi;
                }
                exists = true;
                break;
            }
        }
        
        if (!exists) {
            devices.push_back({name, mac, rssi});
        }
    }
    
    // Sort: devices with names first, then by RSSI (strongest signal first)
    std::sort(devices.begin(), devices.end(), [](const DiscoveredDevice& a, const DiscoveredDevice& b) {
        bool aHasName = !a.name.isEmpty();
        bool bHasName = !b.name.isEmpty();
        if (aHasName != bHasName) {
            return aHasName > bHasName;
        }
        return a.rssi > b.rssi;
    });
    
    String json = "[";
    for (size_t i = 0; i < devices.size(); i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + escapeJsonString(devices[i].name) + "\",";
        json += "\"mac\":\"" + devices[i].mac + "\",";
        json += "\"rssi\":" + String(devices[i].rssi) + "}";
    }
    json += "]";
    
    return json;
}

static uint32_t activePasskey = 123456;
static bool isTestingFlag = false;
static BleTestResult lastTestResult = {false, "", ""};

class BridgeClientCallbacks : public NimBLEClientCallbacks {
public:
    bool passkeyPrompted = false;
    bool authCompleted = false;
    bool authFailed = false;

    void reset() {
        passkeyPrompted = false;
        authCompleted = false;
        authFailed = false;
    }

    uint32_t onPassKeyRequest() override {
        passkeyPrompted = true;
        Serial.printf("[BLE] Passkey requested by server, providing PIN: %06u\n", activePasskey);
        return activePasskey;
    }
    
    bool onConfirmPIN(uint32_t pin) override {
        passkeyPrompted = true;
        Serial.printf("[BLE] Confirming PIN: %06u\n", pin);
        return (pin == activePasskey);
    }
    
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        authCompleted = true;
        authFailed = !desc->sec_state.encrypted;
        Serial.printf("[BLE] Authentication complete. Encrypted: %d, Authenticated: %d, Bonded: %d\n",
                      desc->sec_state.encrypted, desc->sec_state.authenticated, desc->sec_state.bonded);
    }
};

static BridgeClientCallbacks bridgeCallbacks;

BleTestResult ble_client_test_connection(const String& macStr, const String& pinStr) {
    if (!isInitialized) {
        ble_client_init();
    }
    
    // Stop any active scan first
    ble_client_stop_scan();
    
    if (macStr.isEmpty()) {
        return {false, "No MAC address provided.", ""};
    }
    
    activePasskey = pinStr.toInt();
    bridgeCallbacks.reset();
    
    Serial.printf("[BLE Test] Attempting test connection to %s with PIN %06u...\n", macStr.c_str(), activePasskey);
    
    NimBLEAddress addr(macStr.c_str());
    
    // Always delete existing bonding data so the PIN is genuinely challenged every test
    NimBLEDevice::deleteBond(addr);
    
    // Set security configuration for PIN authentication
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
    
    NimBLEClient* pClient = NimBLEDevice::createClient();
    if (!pClient) {
        return {false, "Failed to create BLE Client instance (out of memory).", ""};
    }
    
    pClient->setClientCallbacks(&bridgeCallbacks, false);
    pClient->setConnectTimeout(6); // 6 seconds timeout
    
    Serial.println("[BLE Test] Connecting to peripheral...");
    bool connected = pClient->connect(addr, false);
    if (!connected) {
        Serial.println("[BLE Test] Connection failed or timed out.");
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deleteBond(addr);
        return {false, "Could not connect to BLE device. Ensure it is powered on and within range.", ""};
    }
    
    Serial.println("[BLE Test] Connected. Requesting secure pairing...");
    pClient->secureConnection();
    
    // Wait up to 6 seconds for authentication handshake to complete
    unsigned long startAuth = millis();
    while (millis() - startAuth < 6000) {
        if (!pClient->isConnected() || bridgeCallbacks.authFailed) {
            break;
        }
        if (bridgeCallbacks.authCompleted) {
            break;
        }
        delay(100);
    }
    
    bool isAuth = pClient->isConnected() && bridgeCallbacks.authCompleted && !bridgeCallbacks.authFailed;
    if (isAuth) {
        NimBLEConnInfo connInfo = pClient->getConnInfo();
        isAuth = connInfo.isEncrypted() || connInfo.isAuthenticated();
    }
    
    if (!isAuth) {
        Serial.printf("[BLE Test] Pairing/Auth rejected: connected=%d, authCompleted=%d, authFailed=%d\n",
                      pClient->isConnected(),
                      bridgeCallbacks.authCompleted,
                      bridgeCallbacks.authFailed);
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deleteBond(addr);
        return {false, "Pairing rejected by Meshtastic radio. The 6-digit PIN is incorrect.", ""};
    }
    
    Serial.printf("[BLE Test] Auth verified (Encrypted: %d, Authenticated: %d). Discovering services...\n",
                  pClient->getConnInfo().isEncrypted(), pClient->getConnInfo().isAuthenticated());
    // Check for Meshtastic Service
    NimBLERemoteService* pSvc = pClient->getService("6ba1b218-15a8-461f-9fa8-5dcae273eafd");
    if (!pSvc) {
        pSvc = pClient->getService("cb0b9a0b-a8c2-49c0-bdd5-3fa12b04d84b");
    }
    
    if (!pSvc) {
        Serial.println("[BLE Test] Meshtastic Service UUID not found.");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deleteBond(addr);
        return {false, "Connected, but device does NOT provide the Meshtastic BLE Service.", ""};
    }
    
    Serial.println("[BLE Test] Checking Meshtastic characteristics...");
    // Check for ToRadio and FromRadio characteristics
    NimBLERemoteCharacteristic* pFromRadio = pSvc->getCharacteristic("2c55e69e-4993-11ed-b878-0242ac120002");
    if (!pFromRadio) {
        pFromRadio = pSvc->getCharacteristic("e275fb98-3496-413f-9813-1b32525da4d9");
    }
    NimBLERemoteCharacteristic* pToRadio = pSvc->getCharacteristic("f75c76d2-129e-4dad-a1dd-7866124401e7");
    
    if (!pFromRadio || !pToRadio) {
        Serial.println("[BLE Test] Required ToRadio/FromRadio characteristics missing.");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deleteBond(addr);
        return {false, "Meshtastic service found, but required ToRadio / FromRadio characteristics are missing.", ""};
    }
    
    Serial.println("[BLE Test] Success! Disconnecting and clearing test bond.");
    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    NimBLEDevice::deleteBond(addr);
    
    return {true, "Connected, paired, and verified Meshtastic radio service successfully!", ""};
}

struct TestTaskParams {
    String mac;
    String pin;
};

static void bleTestTask(void* parameter) {
    TestTaskParams* params = (TestTaskParams*)parameter;
    lastTestResult = ble_client_test_connection(params->mac, params->pin);
    delete params;
    isTestingFlag = false;
    vTaskDelete(NULL);
}

void ble_client_start_test(const String& macStr, const String& pinStr) {
    if (isTestingFlag) return;
    isTestingFlag = true;
    lastTestResult = {false, "Testing...", ""};
    
    TestTaskParams* params = new TestTaskParams{macStr, pinStr};
    xTaskCreatePinnedToCore(
        bleTestTask,
        "ble_test",
        4096,
        params,
        1,
        NULL,
        1 // Core 1
    );
}

bool ble_client_is_testing() {
    return isTestingFlag;
}

BleTestResult ble_client_get_test_result() {
    return lastTestResult;
}
