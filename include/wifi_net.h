#pragma once

#include <Arduino.h>

#include "config_ui.h" // For BridgeConfig

bool wifi_net_connect_sta(const String& ssid, const String& pass);
void wifi_net_start_mdns(const BridgeConfig& cfg);
void wifi_net_start_ap();
void wifi_net_loop();

// WiFi scanning for captive portal configuration
void wifi_net_start_scan();
bool wifi_net_is_scanning();
String wifi_net_get_scan_results_json();
