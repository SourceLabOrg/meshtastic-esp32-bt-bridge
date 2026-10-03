#include "ble_client.h"

static bool scanning = false;
static unsigned long scanStartTime = 0;

void ble_client_start_scan() {
    scanning = true;
    scanStartTime = millis();
    Serial.println("BLE Scan started (Simulated)...");
}

bool ble_client_is_scanning() {
    // Simulate a 3-second scan
    if (scanning && (millis() - scanStartTime > 3000)) {
        scanning = false;
        Serial.println("BLE Scan finished (Simulated).");
    }
    return scanning;
}

String ble_client_get_scan_results_json() {
    // Return dummy data so the UI dropdown populates
    return "[{\"name\": \"Meshtastic_b13f\", \"mac\": \"44:17:93:B1:3F:AA\"}, {\"name\": \"Meshtastic_Test\", \"mac\": \"11:22:33:44:55:66\"}]";
}
