#include "wifi_net.h"
#include "config_ui.h"
#include "utils.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <vector>
#include <algorithm>

// GPIO 0 is the physical "BOOT" button on most ESP32 boards
#define BOOT_BUTTON_PIN 0

DNSServer dnsServer;
bool isApMode = false;

static String cachedWifiResultsJson = "[]";

void wifi_net_start_scan() {
    int16_t status = WiFi.scanComplete();
    if (status == WIFI_SCAN_RUNNING) {
        return;
    }
    WiFi.scanDelete();
    cachedWifiResultsJson = "[]";
    WiFi.scanNetworks(true);
}

bool wifi_net_is_scanning() {
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        return true;
    }
    
    // If scan just completed and we haven't processed the results yet
    if (n >= 0 && cachedWifiResultsJson == "[]") {
        struct ScannedWifi {
            String ssid;
            int rssi;
            bool is_open;
        };
        
        std::vector<ScannedWifi> networks;
        networks.reserve(n);
        
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            
            int rssi = WiFi.RSSI(i);
            bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            
            bool exists = false;
            for (auto& net : networks) {
                if (net.ssid == ssid) {
                    if (rssi > net.rssi) {
                        net.rssi = rssi;
                        net.is_open = isOpen;
                    }
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                networks.push_back({ssid, rssi, isOpen});
            }
        }
        
        std::sort(networks.begin(), networks.end(), [](const ScannedWifi& a, const ScannedWifi& b) {
            return a.rssi > b.rssi;
        });
        
        String json = "[";
        for (size_t i = 0; i < networks.size(); i++) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + utils_escape_json(networks[i].ssid) + "\",";
            json += "\"rssi\":" + String(networks[i].rssi) + ",";
            json += "\"is_open\":" + String(networks[i].is_open ? "true" : "false") + "}";
        }
        json += "]";
        
        cachedWifiResultsJson = json;
        WiFi.scanDelete();
    }
    
    return false;
}

String wifi_net_get_scan_results_json() {
    // Ensure any finished scan results are cached
    wifi_net_is_scanning();
    return cachedWifiResultsJson;
}

void wifi_net_start_ap() {
    Serial.println("Starting AP Mode: Meshtastic-Bridge-Setup");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Meshtastic-Bridge-Setup");
    
    // Setup Captive Portal DNS - Route all DNS requests to the ESP32's IP
    dnsServer.start(53, "*", WiFi.softAPIP());
    
    // Start the Web Server
    config_ui_start_server();
    isApMode = true;
}

void wifi_net_init() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    
    BridgeConfig cfg = config_ui_load();
    
    // Check if the physical BOOT button is held down (LOW means pressed)
    bool buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    
    if (buttonPressed || cfg.wifi_ssid.isEmpty() || cfg.ble_mac.isEmpty()) {
        if (buttonPressed) {
            Serial.println("\n==========================================");
            Serial.println("[BOOT] Mode: Setup / Configuration Mode (BOOT button held)");
            Serial.println("==========================================");
        } else if (cfg.wifi_ssid.isEmpty()) {
            Serial.println("\n==========================================");
            Serial.println("[BOOT] Mode: Setup / Configuration Mode (No saved WiFi credentials)");
            Serial.println("==========================================");
        } else {
            Serial.println("\n==========================================");
            Serial.println("[BOOT] Mode: Setup / Configuration Mode (No Bluetooth target configured)");
            Serial.println("==========================================");
        }
        
        wifi_net_start_ap();
        return;
    }
    
    Serial.println("\n==========================================");
    Serial.println("[BOOT] Mode: Normal Operating Mode");
    Serial.printf("[BOOT] Target WiFi: %s\n", cfg.wifi_ssid.c_str());
    if (cfg.ble_mac.length() > 0) {
        Serial.printf("[BOOT] Target BLE:  %s (%s)\n", cfg.ble_name.c_str(), cfg.ble_mac.c_str());
    }
    Serial.println("==========================================");
    
    // Provide a 3-second window to press BOOT button to force setup mode
    Serial.println("[BOOT] Press BOOT button now (or hold anytime for 2s) to enter Setup Mode...");
    unsigned long startWait = millis();
    while (millis() - startWait < 3000) {
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            Serial.println("\n[BOOT] BOOT button pressed! Switching to Setup / Configuration Mode.");
            wifi_net_start_ap();
            return;
        }
        delay(50);
    }
    
    Serial.print("[BOOT] Connecting to WiFi");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
    
    // Wait up to 10 seconds for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[BOOT] WiFi Connected successfully!");
        Serial.print("[BOOT] IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("[BOOT] Web UI available at: http://%s/\n", WiFi.localIP().toString().c_str());
        
        // Start web server so settings can also be accessed on local network
        config_ui_start_server();
    } else {
        Serial.println("\n[BOOT] WiFi Connection Failed! Falling back to Setup / Configuration Mode.");
        wifi_net_start_ap();
    }
}

static unsigned long buttonPressStart = 0;

void wifi_net_loop() {
    if (isApMode) {
        // Must be called repeatedly to handle DNS requests for the captive portal
        dnsServer.processNextRequest();
    } else {
        // Hold BOOT button for 2 seconds at runtime to enter Setup AP mode
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            if (buttonPressStart == 0) {
                buttonPressStart = millis();
            } else if (millis() - buttonPressStart >= 2000) {
                Serial.println("\n[BUTTON] BOOT button held for 2 seconds. Switching to Setup / Configuration Mode...");
                buttonPressStart = 0;
                wifi_net_start_ap();
            }
        } else {
            buttonPressStart = 0;
        }
    }
}
