// Copyright (c) 2026 SourceLab.org (https://github.com/SourceLabOrg/meshtastic-esp32-bt-bridge)
// SPDX-License-Identifier: MIT

#pragma once
#include <Arduino.h>

enum BleState : uint8_t {
    BLE_STATE_NOT_CONFIGURED = 0,
    BLE_STATE_DEACTIVATING,
    BLE_STATE_DEACTIVATED,
    BLE_STATE_DISCONNECTED,
    BLE_STATE_CONNECTING,
    BLE_STATE_CONNECTED
};

const char* bridge_ble_state_to_string(BleState state);

void bridge_init(const char* ble_mac, uint32_t ble_pin);
void bridge_start();
bool bridge_is_ble_connected();
BleState bridge_get_ble_state();
void bridge_set_ble_active(bool active);
bool bridge_is_ble_active();
void bridge_update_ble_config(const char* ble_mac, uint32_t ble_pin);
bool bridge_enqueue_mqtt_to_ble(const uint8_t* data, size_t len);
void bridge_clear_mqtt_to_ble_queue();

struct BridgeDiagStats {
    size_t tcp_to_ble_waiting;
    size_t tcp_to_ble_capacity;
    uint32_t tcp_to_ble_dropped;
    size_t ble_to_tcp_waiting;
    size_t ble_to_tcp_capacity;
    uint32_t ble_to_tcp_dropped;
    size_t mqtt_to_ble_waiting;
    size_t mqtt_to_ble_capacity;
    uint32_t mqtt_to_ble_dropped;
    size_t connected_tcp_clients;
    uint32_t ble_task_stack_free_bytes;
    uint32_t net_task_stack_free_bytes;
    bool ble_connected;
    BleState ble_state;
    uint32_t ble_rx_count;
    uint32_t ble_tx_count;
    uint32_t ble_invalid_count;
    uint32_t tcp_rx_count;
    uint32_t tcp_tx_count;
    uint32_t tcp_invalid_count;
};

BridgeDiagStats bridge_get_diag_stats();
