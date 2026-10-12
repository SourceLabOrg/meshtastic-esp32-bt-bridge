#include "diag_telemetry.h"
#include "bridge.h"
#include "mqtt_net.h"
#include "wifi_net.h"
#include "build_options.h"
#include "utils.h"
#include <WiFi.h>
#include <Arduino.h>

SystemTelemetrySnapshot diag_telemetry_get_snapshot() {
    SystemTelemetrySnapshot s;
    s.uptime_seconds = millis() / 1000;
    s.heap_free_bytes = ESP.getFreeHeap();
    s.heap_min_free_bytes = ESP.getMinFreeHeap();
    s.heap_max_alloc_bytes = ESP.getMaxAllocHeap();

    s.wifi_connected = (WiFi.status() == WL_CONNECTED);
    s.wifi_is_ap = wifi_net_is_ap_mode();
    s.wifi_ip = s.wifi_is_ap ? WiFi.softAPIP() : (s.wifi_connected ? WiFi.localIP() : IPAddress(0, 0, 0, 0));
    s.wifi_rssi = s.wifi_connected ? WiFi.RSSI() : 0;
    s.mdns_host = wifi_net_get_mdns_host();

    s.bridge = bridge_get_diag_stats();
    s.mqtt_enabled = mqtt_net_is_enabled();
    s.mqtt = mqtt_net_get_status();

    return s;
}

void diag_telemetry_format_serial(const SystemTelemetrySnapshot& s, char* buf, size_t maxLen) {
    snprintf(buf, maxLen,
             "[Diag] Heap: %u KB (Min: %u KB, MaxBlock: %u KB) | WiFi: %d dBm | TCP: %zu | "
             "Traffic: [BLE Rx:%u/Tx:%u (inv:%u), TCP Rx:%u/Tx:%u (inv:%u), MQTT Rx:%u/Tx:%u (inv:%u)] | "
             "Queues: [T->B: %zu/%zu (d:%u), B->T: %zu/%zu (d:%u), M->B: %zu/%zu (d:%u)] | "
             "Stacks: [BLE: %u B, Net: %u B] | MQTT: %s",
             s.heap_free_bytes / 1024, s.heap_min_free_bytes / 1024, s.heap_max_alloc_bytes / 1024,
             (int)s.wifi_rssi, s.bridge.connected_tcp_clients,
             s.bridge.ble_rx_count, s.bridge.ble_tx_count, s.bridge.ble_invalid_count,
             s.bridge.tcp_rx_count, s.bridge.tcp_tx_count, s.bridge.tcp_invalid_count,
             s.mqtt.msgs_received, s.mqtt.msgs_published, s.mqtt.msgs_invalid,
             s.bridge.tcp_to_ble_waiting, s.bridge.tcp_to_ble_capacity, s.bridge.tcp_to_ble_dropped,
             s.bridge.ble_to_tcp_waiting, s.bridge.ble_to_tcp_capacity, s.bridge.ble_to_tcp_dropped,
             s.bridge.mqtt_to_ble_waiting, s.bridge.mqtt_to_ble_capacity, s.bridge.mqtt_to_ble_dropped,
             s.bridge.ble_task_stack_free_bytes, s.bridge.net_task_stack_free_bytes,
             s.mqtt.state_str.c_str());
}

void diag_telemetry_format_json(const SystemTelemetrySnapshot& s, String& json) {
    // Construct response JSON with zero intermediate heap allocations into a single pre-reserved buffer
    json.reserve(1400);

    json = "{\"uptime_seconds\":";
    json += s.uptime_seconds;
    json += ",\"heap\":{\"free_bytes\":";
    json += s.heap_free_bytes;
    json += ",\"min_free_bytes\":";
    json += s.heap_min_free_bytes;
    json += ",\"max_alloc_bytes\":";
    json += s.heap_max_alloc_bytes;
    json += "},\"wifi\":{\"connected\":";
    json += (s.wifi_connected ? "true" : "false");
    json += ",\"is_ap_mode\":";
    json += (s.wifi_is_ap ? "true" : "false");
    json += ",\"ip\":\"";
    json += s.wifi_ip[0];
    json += '.';
    json += s.wifi_ip[1];
    json += '.';
    json += s.wifi_ip[2];
    json += '.';
    json += s.wifi_ip[3];
    json += "\",\"rssi\":";
    json += (int)s.wifi_rssi;
    json += ",\"mdns_host\":\"";
    utils_escape_json_append(json, s.mdns_host);
    json += "\"},\"ble\":{\"connected\":";
    json += (s.bridge.ble_connected ? "true" : "false");
    json += ",\"rx_count\":";
    json += s.bridge.ble_rx_count;
    json += ",\"tx_count\":";
    json += s.bridge.ble_tx_count;
    json += ",\"invalid_count\":";
    json += s.bridge.ble_invalid_count;
    json += "},\"tcp\":{\"connected_clients\":";
    json += s.bridge.connected_tcp_clients;
    json += ",\"max_clients\":";
    json += MAX_TCP_CLIENTS;
    json += ",\"rx_count\":";
    json += s.bridge.tcp_rx_count;
    json += ",\"tx_count\":";
    json += s.bridge.tcp_tx_count;
    json += ",\"invalid_count\":";
    json += s.bridge.tcp_invalid_count;
    json += "},\"traffic\":{\"ble\":{\"rx\":";
    json += s.bridge.ble_rx_count;
    json += ",\"tx\":";
    json += s.bridge.ble_tx_count;
    json += ",\"invalid\":";
    json += s.bridge.ble_invalid_count;
    json += "},\"tcp\":{\"rx\":";
    json += s.bridge.tcp_rx_count;
    json += ",\"tx\":";
    json += s.bridge.tcp_tx_count;
    json += ",\"invalid\":";
    json += s.bridge.tcp_invalid_count;
    json += "},\"mqtt\":{\"rx\":";
    json += s.mqtt.msgs_received;
    json += ",\"tx\":";
    json += s.mqtt.msgs_published;
    json += ",\"invalid\":";
    json += s.mqtt.msgs_invalid;
    json += "}},\"queues\":{\"tcp_to_ble\":{\"waiting\":";
    json += s.bridge.tcp_to_ble_waiting;
    json += ",\"capacity\":";
    json += s.bridge.tcp_to_ble_capacity;
    json += ",\"dropped\":";
    json += s.bridge.tcp_to_ble_dropped;
    json += "},\"ble_to_tcp\":{\"waiting\":";
    json += s.bridge.ble_to_tcp_waiting;
    json += ",\"capacity\":";
    json += s.bridge.ble_to_tcp_capacity;
    json += ",\"dropped\":";
    json += s.bridge.ble_to_tcp_dropped;
    json += "},\"mqtt_to_ble\":{\"waiting\":";
    json += s.bridge.mqtt_to_ble_waiting;
    json += ",\"capacity\":";
    json += s.bridge.mqtt_to_ble_capacity;
    json += ",\"dropped\":";
    json += s.bridge.mqtt_to_ble_dropped;
    json += "}},\"stacks\":{\"ble_task_free_bytes\":";
    json += s.bridge.ble_task_stack_free_bytes;
    json += ",\"net_task_free_bytes\":";
    json += s.bridge.net_task_stack_free_bytes;
    json += "},\"mqtt\":{\"gateway_enabled\":";
    json += (s.mqtt_enabled ? "true" : "false");
    json += ",\"state\":\"";
    utils_escape_json_append(json, s.mqtt.state_str);
    json += "\",\"radio_proxy_enabled\":";
    json += (s.mqtt.radio_proxy_enabled ? "true" : "false");
    json += ",\"active_server\":\"";
    utils_escape_json_append(json, s.mqtt.active_server);
    json += "\",\"active_port\":";
    json += s.mqtt.active_port;
    json += ",\"active_tls\":";
    json += (s.mqtt.active_tls ? "true" : "false");
    json += ",\"active_root\":\"";
    utils_escape_json_append(json, s.mqtt.active_root);
    json += "\",\"published\":";
    json += s.mqtt.msgs_published;
    json += ",\"received\":";
    json += s.mqtt.msgs_received;
    json += ",\"dropped\":";
    json += s.mqtt.msgs_dropped;
    json += ",\"invalid\":";
    json += s.mqtt.msgs_invalid;
    json += ",\"radio_server\":\"";
    utils_escape_json_append(json, s.mqtt.radio_server);
    json += "\",\"radio_port\":";
    json += s.mqtt.radio_port;
    json += ",\"radio_tls\":";
    json += (s.mqtt.radio_tls ? "true" : "false");
    json += ",\"radio_root\":\"";
    utils_escape_json_append(json, s.mqtt.radio_root);
    json += "\"}}";
}

void diag_telemetry_format_prometheus(const SystemTelemetrySnapshot& s, Print& out) {
    // 1. Info Metric
    out.print("# HELP meshbridge_info Metadata and identity of the bridge\n"
              "# TYPE meshbridge_info gauge\n"
              "meshbridge_info{version=\"" FIRMWARE_VERSION "\",mdns=\"");
    out.print(s.mdns_host);
    out.print("\",board=\"" HARDWARE_BOARD_NAME "\"} 1\n\n");

    // 2. System Uptime & Memory
    out.print("# HELP meshbridge_uptime_seconds Number of seconds since the ESP32 booted\n"
              "# TYPE meshbridge_uptime_seconds counter\n"
              "meshbridge_uptime_seconds ");
    out.println(s.uptime_seconds);

    out.print("# HELP meshbridge_heap_free_bytes Current free internal heap memory in bytes\n"
              "# TYPE meshbridge_heap_free_bytes gauge\n"
              "meshbridge_heap_free_bytes ");
    out.println(s.heap_free_bytes);

    out.print("# HELP meshbridge_heap_min_free_bytes Lowest historical free heap memory in bytes since boot\n"
              "# TYPE meshbridge_heap_min_free_bytes gauge\n"
              "meshbridge_heap_min_free_bytes ");
    out.println(s.heap_min_free_bytes);

    out.print("# HELP meshbridge_heap_max_alloc_bytes Largest contiguous free block in heap in bytes\n"
              "# TYPE meshbridge_heap_max_alloc_bytes gauge\n"
              "meshbridge_heap_max_alloc_bytes ");
    out.println(s.heap_max_alloc_bytes);

    out.print("# HELP meshbridge_task_stack_free_bytes Stack high-water mark remaining for internal FreeRTOS tasks\n"
              "# TYPE meshbridge_task_stack_free_bytes gauge\n"
              "meshbridge_task_stack_free_bytes{task=\"ble\"} ");
    out.println(s.bridge.ble_task_stack_free_bytes);
    out.print("meshbridge_task_stack_free_bytes{task=\"net\"} ");
    out.println(s.bridge.net_task_stack_free_bytes);
    out.println();

    // 3. Wireless & Network Status
    out.print("# HELP meshbridge_wifi_connected WiFi station connection status (1=connected, 0=disconnected)\n"
              "# TYPE meshbridge_wifi_connected gauge\n"
              "meshbridge_wifi_connected ");
    out.println(s.wifi_connected ? "1" : "0");

    out.print("# HELP meshbridge_wifi_rssi_dbm WiFi signal strength indicator in dBm\n"
              "# TYPE meshbridge_wifi_rssi_dbm gauge\n"
              "meshbridge_wifi_rssi_dbm ");
    out.println((int)s.wifi_rssi);

    out.print("# HELP meshbridge_ble_connected Bluetooth connection status to Meshtastic radio (1=connected, 0=disconnected)\n"
              "# TYPE meshbridge_ble_connected gauge\n"
              "meshbridge_ble_connected ");
    out.println(s.bridge.ble_connected ? "1" : "0");

    out.print("# HELP meshbridge_tcp_clients Connected TCP app clients\n"
              "# TYPE meshbridge_tcp_clients gauge\n"
              "meshbridge_tcp_clients ");
    out.println(s.bridge.connected_tcp_clients);

    out.print("# HELP meshbridge_tcp_clients_max Maximum allowed concurrent TCP clients\n"
              "# TYPE meshbridge_tcp_clients_max gauge\n"
              "meshbridge_tcp_clients_max ");
    out.println(MAX_TCP_CLIENTS);
    out.println();

    // 4. Traffic Throughput & Error Counters
    out.print("# HELP meshbridge_messages_rx_total Valid packets received at channel boundary\n"
              "# TYPE meshbridge_messages_rx_total counter\n"
              "meshbridge_messages_rx_total{channel=\"ble\"} ");
    out.println(s.bridge.ble_rx_count);
    out.print("meshbridge_messages_rx_total{channel=\"tcp\"} ");
    out.println(s.bridge.tcp_rx_count);
    out.print("meshbridge_messages_rx_total{channel=\"mqtt\"} ");
    out.println(s.mqtt.msgs_received);

    out.print("# HELP meshbridge_messages_tx_total Packets transmitted across channel boundary\n"
              "# TYPE meshbridge_messages_tx_total counter\n"
              "meshbridge_messages_tx_total{channel=\"ble\"} ");
    out.println(s.bridge.ble_tx_count);
    out.print("meshbridge_messages_tx_total{channel=\"tcp\"} ");
    out.println(s.bridge.tcp_tx_count);
    out.print("meshbridge_messages_tx_total{channel=\"mqtt\"} ");
    out.println(s.mqtt.msgs_published);

    out.print("# HELP meshbridge_messages_invalid_total Malformed, oversized, or unparseable packets rejected\n"
              "# TYPE meshbridge_messages_invalid_total counter\n"
              "meshbridge_messages_invalid_total{channel=\"ble\"} ");
    out.println(s.bridge.ble_invalid_count);
    out.print("meshbridge_messages_invalid_total{channel=\"tcp\"} ");
    out.println(s.bridge.tcp_invalid_count);
    out.print("meshbridge_messages_invalid_total{channel=\"mqtt\"} ");
    out.println(s.mqtt.msgs_invalid);
    out.println();

    // 5. FreeRTOS Queues
    out.print("# HELP meshbridge_queue_messages Current number of items waiting in queue buffer\n"
              "# TYPE meshbridge_queue_messages gauge\n"
              "meshbridge_queue_messages{queue=\"tcp_to_ble\"} ");
    out.println(s.bridge.tcp_to_ble_waiting);
    out.print("meshbridge_queue_messages{queue=\"ble_to_tcp\"} ");
    out.println(s.bridge.ble_to_tcp_waiting);
    out.print("meshbridge_queue_messages{queue=\"mqtt_to_ble\"} ");
    out.println(s.bridge.mqtt_to_ble_waiting);

    out.print("# HELP meshbridge_queue_capacity Maximum queue buffer capacity\n"
              "# TYPE meshbridge_queue_capacity gauge\n"
              "meshbridge_queue_capacity{queue=\"tcp_to_ble\"} ");
    out.println(s.bridge.tcp_to_ble_capacity);
    out.print("meshbridge_queue_capacity{queue=\"ble_to_tcp\"} ");
    out.println(s.bridge.ble_to_tcp_capacity);
    out.print("meshbridge_queue_capacity{queue=\"mqtt_to_ble\"} ");
    out.println(s.bridge.mqtt_to_ble_capacity);

    out.print("# HELP meshbridge_queue_dropped_total Packets dropped strictly due to queue capacity exhaustion\n"
              "# TYPE meshbridge_queue_dropped_total counter\n"
              "meshbridge_queue_dropped_total{queue=\"tcp_to_ble\"} ");
    out.println(s.bridge.tcp_to_ble_dropped);
    out.print("meshbridge_queue_dropped_total{queue=\"ble_to_tcp\"} ");
    out.println(s.bridge.ble_to_tcp_dropped);
    out.print("meshbridge_queue_dropped_total{queue=\"mqtt_to_ble\"} ");
    out.println(s.bridge.mqtt_to_ble_dropped);
    out.println();

    // 6. MQTT Gateway Status
    out.print("# HELP meshbridge_mqtt_enabled Whether the MQTT Gateway feature is enabled (1=enabled, 0=disabled)\n"
              "# TYPE meshbridge_mqtt_enabled gauge\n"
              "meshbridge_mqtt_enabled ");
    out.println(s.mqtt_enabled ? "1" : "0");

    out.print("# HELP meshbridge_mqtt_connected Whether the MQTT client is connected to the broker (1=connected, 0=disconnected)\n"
              "# TYPE meshbridge_mqtt_connected gauge\n"
              "meshbridge_mqtt_connected ");
    out.println((s.mqtt.state == MQTT_STATE_CONNECTED) ? "1" : "0");
}

static void diagTelemetryTask(void* parameter) {
    // Initial delay before first report to allow boot sequence to settle
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (true) {
        SystemTelemetrySnapshot s = diag_telemetry_get_snapshot();
        char buf[384];
        diag_telemetry_format_serial(s, buf, sizeof(buf));
        Serial.printf("[ %7lu][I][diag] %s\n", millis(), buf);

        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

void diag_telemetry_start() {
    // Run at low priority (1) pinned to Core 0 with a 3KB stack
    xTaskCreatePinnedToCore(diagTelemetryTask, "diag_telemetry", 3072, NULL, 1, NULL, 0);
}
