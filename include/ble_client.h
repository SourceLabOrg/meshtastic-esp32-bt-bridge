#pragma once
#include <Arduino.h>

struct BleTestResult {
    bool success;
    String message;
    String deviceName;
};

void ble_client_init();
void ble_client_start_scan();
void ble_client_stop_scan();
bool ble_client_is_scanning();
String ble_client_get_scan_error();
String ble_client_get_scan_results_json();

void ble_client_start_test(const String& macStr, const String& pinStr);
bool ble_client_is_testing();
BleTestResult ble_client_get_test_result();
BleTestResult ble_client_test_connection(const String& macStr, const String& pinStr);
