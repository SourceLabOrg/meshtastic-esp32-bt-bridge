#pragma once

enum LedState {
    LED_OFF,
    LED_FAST_BLINK, // Booting / Waiting for button
    LED_MED_BLINK,  // Bridge Mode (BT Disconnected)
    LED_SLOW_BLINK, // Setup Mode / AP Mode
    LED_SOLID_ON    // Bridge Mode (BT Connected)
};

void status_led_init();
void status_led_set(LedState state);
