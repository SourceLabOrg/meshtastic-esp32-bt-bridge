#include <Arduino.h>
#include "status_led.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN 21 // Fallback just in case the board variant doesn't define it
#endif

static volatile LedState currentState = LED_OFF;

static void ledTask(void* param) {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    while(true) {
        LedState state = currentState;
        if (state == LED_OFF) {
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (state == LED_SOLID_ON) {
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            int delayMs = 500;
            if (state == LED_FAST_BLINK) delayMs = 100;
            else if (state == LED_SLOW_BLINK) delayMs = 1000;
            
            digitalWrite(LED_BUILTIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(delayMs));
            
            // Check state again so we don't force a full blink cycle if the state changed rapidly
            if (currentState != state) continue;
            
            digitalWrite(LED_BUILTIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(delayMs));
        }
    }
}

void status_led_init() {
    // Run on Core 1 at low priority (1) so it doesn't interrupt anything important
    xTaskCreatePinnedToCore(ledTask, "led_task", 2048, NULL, 1, NULL, 1);
}

void status_led_set(LedState state) {
    currentState = state;
}
