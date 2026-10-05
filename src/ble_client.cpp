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
