#pragma once
#include <Arduino.h>

constexpr uint32_t BLE_SCAN_DURATION_SECONDS = 4;

void ble_client_init();
void ble_client_start_scan();
void ble_client_stop_scan();
bool ble_client_is_scanning();
String ble_client_get_scan_results_json();
