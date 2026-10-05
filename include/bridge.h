#pragma once
#include <Arduino.h>

void bridge_init(const String& ble_mac, uint32_t ble_pin);
void bridge_start();
