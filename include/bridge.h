#pragma once
#include <Arduino.h>

void bridge_init(const String& ble_mac, uint32_t ble_pin);
void bridge_start();
bool bridge_is_ble_connected();

struct BridgeDiagStats {
    size_t tcp_to_ble_waiting;
    size_t tcp_to_ble_capacity;
    size_t ble_to_tcp_waiting;
    size_t ble_to_tcp_capacity;
    size_t connected_tcp_clients;
    uint32_t ble_task_stack_free_bytes;
    uint32_t net_task_stack_free_bytes;
    bool ble_connected;
};

BridgeDiagStats bridge_get_diag_stats();
