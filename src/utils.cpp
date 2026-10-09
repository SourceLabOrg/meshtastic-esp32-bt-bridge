#include "utils.h"
#include "esp_log.h"

bool g_debug_logs = false;

/**
 * Enable/Disable debug logging.
 * @param enableDebugLogs true to enable debug logging, false to disable
 */
void utils_set_debug_logging(bool enableDebugLogs) {
    g_debug_logs = enableDebugLogs;
    
    if (enableDebugLogs) {
        // Set default log level to INFO.
        esp_log_level_set("*", ESP_LOG_INFO);
    } else {
        // Set default log level to warning.
        esp_log_level_set("*", ESP_LOG_WARN);
    }
}

/**
 * Append an escaped JSON string directly to an existing String buffer,
 * avoiding intermediate heap allocations.
 * @param dest Destination String buffer (should be pre-reserved)
 * @param input Raw null-terminated C string to escape
 */
void utils_escape_json_append(String& dest, const char* input) {
    if (!input) return;
    while (*input) {
        char c = *input++;
        if (c == '"') dest += "\\\"";
        else if (c == '\\') dest += "\\\\";
        else if (c == '\b') dest += "\\b";
        else if (c == '\f') dest += "\\f";
        else if (c == '\n') dest += "\\n";
        else if (c == '\r') dest += "\\r";
        else if (c == '\t') dest += "\\t";
        else if (c >= 32 && c <= 126) dest += c;
    }
}

/**
 * Overload to append an escaped Arduino String directly to a destination String buffer.
 */
void utils_escape_json_append(String& dest, const String& input) {
    utils_escape_json_append(dest, input.c_str());
}

/**
 * Given a string json value, escape it.
 * @param input Value to be escaped.
 * @return Escaped input.
 */
String utils_escape_json(const String& input) {
    String output;
    output.reserve(input.length() + 16);
    utils_escape_json_append(output, input.c_str());
    return output;
}

/**
 * @param macStr MAC Address to parse
 * @return NimBLEAddress configured with the appropriate type and MAC address
 */
NimBLEAddress utils_parse_ble_address(const String& macStr) {
    uint8_t addrType = BLE_ADDR_PUBLIC; // Default to public

    if (macStr.length() > 0) {
        char firstChar = macStr.charAt(0);
        // Bluetooth Core Specification: Random Static addresses have the two most
        // significant bits set to 1. In hex, this means they start with C, D, E, or F.
        if (firstChar == 'c' || firstChar == 'C' ||
            firstChar == 'd' || firstChar == 'D' ||
            firstChar == 'e' || firstChar == 'E' ||
            firstChar == 'f' || firstChar == 'F') {
            addrType = BLE_ADDR_RANDOM;
        }
    }

    return NimBLEAddress(macStr.c_str(), addrType);
}
