#pragma once
#include <Arduino.h>

void ble_client_start_scan();
bool ble_client_is_scanning();
String ble_client_get_scan_results_json();
