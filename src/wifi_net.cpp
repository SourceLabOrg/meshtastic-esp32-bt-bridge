#include "wifi_net.h"
#include "config_ui.h"
#include <WiFi.h>
#include <DNSServer.h>

// GPIO 0 is the physical "BOOT" button on most ESP32 boards
#define BOOT_BUTTON_PIN 0

DNSServer dnsServer;
bool isApMode = false;

void wifi_net_start_ap() {
    Serial.println("Starting AP Mode: Meshtastic-Bridge-Setup");
    WiFi.mode(WIFI_AP);
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
    
    if (buttonPressed || cfg.wifi_ssid.isEmpty()) {
        if (buttonPressed) Serial.println("Boot button held! Forcing AP mode.");
        else Serial.println("No WiFi credentials found. Starting AP mode.");
        
        wifi_net_start_ap();
        return;
    }
    
    Serial.print("Attempting to connect to WiFi: ");
    Serial.println(cfg.wifi_ssid);
    
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
        Serial.println("WiFi Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        // TODO: Start TCP Server here in a later step
    } else {
        Serial.println("WiFi Connection Failed. Falling back to AP mode.");
        wifi_net_start_ap();
    }
}

void wifi_net_loop() {
    if (isApMode) {
        // Must be called repeatedly to handle DNS requests for the captive portal
        dnsServer.processNextRequest();
    }
}
