#include "utils.h"
#include "esp_log.h"

/**
 * Enable/Disable debug logging.
 * @param enable true to enable debug logging, false to disable
 */
void utils_set_debug_logging(bool enable) {
    esp_log_level_set("*", enable ? ESP_LOG_DEBUG : ESP_LOG_INFO);

    // Squelch exceptionally noisy libraries
    if (enable) {
        esp_log_level_set("wifi", ESP_LOG_INFO);
        esp_log_level_set("wifi_init", ESP_LOG_INFO);
    } else {
        esp_log_level_set("wifi", ESP_LOG_WARN);
        esp_log_level_set("wifi_init", ESP_LOG_WARN);
    }
}

/**
 * Given a string json value, escape it.
 * @param input Value to be escaped.
 * @return Escaped input.
 */
String utils_escape_json(const String& input) {
    String output = "";
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\b') output += "\\b";
        else if (c == '\f') output += "\\f";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else if (c >= 32 && c <= 126) output += c;
    }
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
