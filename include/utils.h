#pragma once
#include <Arduino.h>
#include <NimBLEAddress.h>

// Global debug log toggle
extern bool g_debug_logs;

// Override Arduinos hardcoded log_d to respect our runtime toggle
#undef log_d
#define log_d(format, ...) do { if(g_debug_logs) log_printf(ARDUHAL_LOG_FORMAT(D, format), ##__VA_ARGS__); } while(0)

// Dynamically enable or disable debug level logging
void utils_set_debug_logging(bool enable);

// Escape special characters for safe inclusion in JSON strings
String utils_escape_json(const String& input);
void utils_escape_json_append(String& dest, const char* input);
void utils_escape_json_append(String& dest, const String& input);

// Parses a MAC string and auto-detects if it is a Public or Random Static BLE address
NimBLEAddress utils_parse_ble_address(const String& macStr);
