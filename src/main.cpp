#include <Arduino.h>
#include "config_ui.h"
#include "wifi_net.h"

void setup() {
    Serial.begin(115200);
    delay(1000); // Give the serial monitor time to attach
    
    Serial.println("\n--- Starting Meshtastic ESP32 BT-TCP Bridge ---");
    
    config_ui_init();
    wifi_net_init();
}

void loop() {
    wifi_net_loop();
    
    // We will add the bridging loop logic here later
}
