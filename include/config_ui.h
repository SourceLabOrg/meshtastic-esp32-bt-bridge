#pragma once

#include <Arduino.h>

struct BridgeConfig {
    // WiFi settings
    String wifi_ssid;
    String wifi_pass;

    // Bluetooth settings
    String ble_name;
    String ble_mac;
    String ble_pin;

    // System settings
    bool debug_logs;

    // MQTT Gateway settings
    bool mqtt_enabled;          // Global enable/disable feature flag.
    bool mqtt_tls_insecure;     // True and ssl certificate errors are ignored.
    String mqtt_custom_ca;      // Optionally provide a custom CA public key.
};

// Initialize the Preferences (NVS)
void config_ui_init();

// Load the current configuration from NVS
BridgeConfig config_ui_load();

// Start the Async Web Server for the captive portal
void config_ui_start_server();

// Stop the Async Web Server
void config_ui_stop_server();
