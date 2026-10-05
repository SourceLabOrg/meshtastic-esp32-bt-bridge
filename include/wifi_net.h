#pragma once

#include <Arduino.h>

void wifi_net_init();
void wifi_net_loop();

// WiFi scanning for captive portal configuration
void wifi_net_start_scan();
bool wifi_net_is_scanning();
String wifi_net_get_scan_results_json();
