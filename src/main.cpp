#include <Arduino.h>
#include "config_ui.h"
#include "wifi_net.h"
#include "status_led.h"
#include "bridge.h"
#include "build_options.h"

/**
 * Main Entry point.
 */
void setup() {
    // Start serial console.
    Serial.begin(115200);

    // Initialize Status LED,
    status_led_init();

    // Configure "boot button" pin and setup on/off as high/low depending on board.
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    // Initial delay waiting for serial to start, USB to connect and catch up.
    delay(2000);
    Serial.println("\n--- Starting Meshtastic ESP32 BT-TCP Bridge ---");

    // Load stored configuration properties from NVS
    config_ui_init();
    BridgeConfig cfg = config_ui_load();

    // Determine which boot mode to enter.
    // Check if BOOT button is held during power-on.
    // If the boot button is pressed, or we are missing configuration properties, it will
    // enter "setup" mode and start up an Access Point with the configuration web UI.
    bool forceSetup = false;
    bool btnPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);

    // Start Fast Blink to indicate that we're booting up.
    status_led_set(LED_FAST_BLINK);

    // Wait up to 5 seconds for setup button to be pressed.
    if (!btnPressed && !cfg.wifi_ssid.isEmpty() && !cfg.ble_mac.isEmpty()) {
        Serial.println("[BOOT] Press button in next 5 seconds to enter setup mode...");

        unsigned long startWait = millis();
        while (millis() - startWait < 5000) {
            if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                Serial.println("[BOOT] Entering Setup Mode (button pressed)");
                btnPressed = true;
                break;
            }
            delay(50);
        }
    }

    // Log if we're going to enter setup mode, and why.
    if (btnPressed) {
        forceSetup = true;
    } else if (cfg.wifi_ssid.isEmpty()) {
        Serial.println("[BOOT] Entering Setup Mode (No saved WiFi credentials)");
        forceSetup = true;
    } else if (cfg.ble_mac.isEmpty()) {
        Serial.println("[BOOT] Entering Setup Mode (No Bluetooth target configured)");
        forceSetup = true;
    }

    if (forceSetup) {
        // Enter Setup mode.
        wifi_net_start_ap();
    } else {
        // Normal boot, log config
        Serial.println("\n==========================================");
        Serial.println("[BOOT] Mode: Normal Operating Mode");
        Serial.printf("[BOOT] Target WiFi: %s\n", cfg.wifi_ssid.c_str());
        Serial.printf("[BOOT] Target BLE:  %s (%s)\n", cfg.ble_name.c_str(), cfg.ble_mac.c_str());
        Serial.println("==========================================");

        // Start Medium Blink to indicate 'searching for bluetooth and wifi'
        status_led_set(LED_MED_BLINK);

        // Connect to wifi
        if (wifi_net_connect_sta(cfg.wifi_ssid, cfg.wifi_pass)) {
            // Start mDNS advertising.
            wifi_net_start_mdns(cfg);

            // Start up the captive portal web server.
            // TODO: Should this be password protected / Optionally enabled?
            config_ui_start_server();

            // Init and start bridge BLE <--> WIFI
            bridge_init(cfg.ble_mac, cfg.ble_pin.toInt());
            bridge_start();
        } else {
            Serial.println("[BOOT] WiFi Connection Failed! Falling back to Setup Mode.");
            wifi_net_start_ap();
        }
    }
}

//  Main program loop.
void loop() {
    wifi_net_loop();
}
