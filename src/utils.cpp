#include "utils.h"

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
