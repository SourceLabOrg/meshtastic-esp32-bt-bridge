#include <Arduino.h>
#include "config_ui.h"
#include "wifi_net.h"
#include "mqtt_net.h"
#include "status_led.h"
#include "bridge.h"
#include "build_options.h"
#include "utils.h"
#include "diag_telemetry.h"

/**
 * Main Entry point.
 */
void setup() {
    // Start serial console and enable sending log output to it.
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    // Initialize Status LED,
    status_led_init();

    // Configure "boot button" pin and setup on/off as high/low depending on board.
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    // Initial delay waiting for serial to start, USB to connect and catch up.
    delay(2000);
    log_i("\n--- Starting Meshtastic ESP32 BT-TCP Bridge ---");

    // Initialize MQTT subsystem and locks
    mqtt_net_init();

    // Load stored configuration properties from NVS
    config_ui_init();
    BridgeConfig cfg = config_ui_load();

    // Set global ESP-IDF log level based on UI setting
    if (cfg.debug_logs) {
        log_i("Debug logging enabled!");
    }
    utils_set_debug_logging(cfg.debug_logs);

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
        log_i("[BOOT] Press button in next 5 seconds to enter setup mode...");

        unsigned long startWait = millis();
        while (millis() - startWait < 5000) {
            if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                log_i("[BOOT] Entering Setup Mode (button pressed)");
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
        log_i("[BOOT] Entering Setup Mode (No saved WiFi credentials)");
        forceSetup = true;
    } else if (cfg.ble_mac.isEmpty()) {
        log_i("[BOOT] Entering Setup Mode (No Bluetooth target configured)");
        forceSetup = true;
    }

    if (forceSetup) {
        // Enter Setup mode.
        wifi_net_start_ap();
    } else {
        // Normal boot, log config
        log_i("\n==========================================");
        log_i("[BOOT] Mode: Normal Operating Mode");
        log_i("[BOOT] Target WiFi: %s", cfg.wifi_ssid.c_str());
        log_i("[BOOT] Target BLE:  %s (%s)", cfg.ble_name.c_str(), cfg.ble_mac.c_str());
        log_i("==========================================");

        // Start Medium Blink to indicate 'searching for bluetooth and wifi'
        status_led_set(LED_MED_BLINK);

        // Connect to wifi
        if (wifi_net_connect_sta(cfg.wifi_ssid, cfg.wifi_pass)) {
            // Start mDNS advertising.
            wifi_net_start_mdns(cfg);

            // Apply saved MQTT configuration
            MqttConfig mqttCfg;
            mqttCfg.enabled = cfg.mqtt_enabled;
            mqttCfg.tls_insecure = cfg.mqtt_tls_insecure;
            mqttCfg.custom_ca = cfg.mqtt_custom_ca;
            mqtt_net_apply_config(mqttCfg);

            // Start up the captive portal web server.
            config_ui_start_server();

            // Init and start bridge BLE <--> WIFI
            bridge_init(cfg.ble_mac, cfg.ble_pin.toInt());
            bridge_start();

            // Start periodic diagnostic telemetry task (Core 0, Priority 1, every 15s)
            diag_telemetry_start();
        } else {
            log_i("[BOOT] WiFi Connection Failed! Falling back to Setup Mode.");
            wifi_net_start_ap();
        }
    }
}

//  Main program loop.
void loop() {
    wifi_net_loop();
    mqtt_net_loop();
}
