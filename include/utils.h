#pragma once
#include <Arduino.h>
#include <NimBLEAddress.h>

// Dynamically enable or disable debug level logging
void utils_set_debug_logging(bool enable);

// Escape special characters for safe inclusion in JSON strings
String utils_escape_json(const String& input);

// Parses a MAC string and auto-detects if it is a Public or Random Static BLE address
NimBLEAddress utils_parse_ble_address(const String& macStr);
