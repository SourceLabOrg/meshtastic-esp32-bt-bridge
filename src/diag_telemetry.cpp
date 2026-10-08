#include "diag_telemetry.h"
#include "bridge.h"
#include <WiFi.h>
#include <Arduino.h>

static void diagTelemetryTask(void* parameter) {
    // Initial delay before first report to allow boot sequence to settle
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (true) {
        // Memory statistics
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        uint32_t maxAlloc = ESP.getMaxAllocHeap();

        // Bridge & Queue statistics
        BridgeDiagStats bStats = bridge_get_diag_stats();

        // WiFi statistics
        int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

        char buf[320];
        snprintf(buf, sizeof(buf),
                 "[Diag] Heap: %u KB (Min: %u KB, MaxBlock: %u KB) | WiFi: %d dBm | TCP: %zu | Queues: [T->B: %zu/%zu, B->T: %zu/%zu] | Stacks: [BLE: %u B, Net: %u B] | BLE: %s",
                 freeHeap / 1024, minHeap / 1024, maxAlloc / 1024,
                 (int)rssi, bStats.connected_tcp_clients,
                 bStats.tcp_to_ble_waiting, bStats.tcp_to_ble_capacity,
                 bStats.ble_to_tcp_waiting, bStats.ble_to_tcp_capacity,
                 bStats.ble_task_stack_free_bytes, bStats.net_task_stack_free_bytes,
                 bStats.ble_connected ? "Connected" : "Disconnected");

        Serial.printf("[ %7lu][I][diag] %s\n", millis(), buf);

        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

void diag_telemetry_start() {
    // Run at low priority (1) pinned to Core 0 with a 3KB stack
    xTaskCreatePinnedToCore(diagTelemetryTask, "diag_telemetry", 3072, NULL, 1, NULL, 0);
}
