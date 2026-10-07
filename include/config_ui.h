#pragma once

#include <Arduino.h>

struct BridgeConfig {
    String wifi_ssid;
    String wifi_pass;
    String ble_name;
    String ble_mac;
    String ble_pin;
    bool debug_logs;
};

// Initialize the Preferences (NVS)
void config_ui_init();

// Load the current configuration from NVS
BridgeConfig config_ui_load();

// Start the Async Web Server for the captive portal
void config_ui_start_server();

// Stop the Async Web Server
void config_ui_stop_server();
