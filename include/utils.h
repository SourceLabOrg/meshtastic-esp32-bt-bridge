#pragma once

#include <Arduino.h>
#include <NimBLEAddress.h>

/**
 * Global debug log enable/disable flag.
 * If true, additional debug logs will be generated.
 */
extern bool g_debug_logs;

#define DBG_PRINT(...) if (g_debug_logs) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) if (g_debug_logs) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) if (g_debug_logs) Serial.printf(__VA_ARGS__)

// Escape special characters for safe inclusion in JSON strings
String utils_escape_json(const String& input);

// Parses a MAC string and auto-detects if it is a Public or Random Static BLE address
NimBLEAddress utils_parse_ble_address(const String& macStr);
