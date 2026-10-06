#include "wifi_net.h"
#include "config_ui.h"
#include "bridge.h"
#include "utils.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <vector>

#define MDNS_HOSTNAME "meshtastic-bridge"
#include <algorithm>

// GPIO 0 is the physical "BOOT" button on most ESP32 boards
#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

#ifndef TCP_PORT
#define TCP_PORT 4403
#endif

DNSServer dnsServer;
bool isApMode = false;

static String cachedWifiResultsJson = "[]";

void wifi_net_start_scan() {
    int16_t status = WiFi.scanComplete();
    if (status == WIFI_SCAN_RUNNING) {
        return;
    }
    WiFi.scanDelete();
    cachedWifiResultsJson = "[]";
    WiFi.scanNetworks(true);
}

bool wifi_net_is_scanning() {
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        return true;
    }

    // If scan just completed and we haven't processed the results yet
    if (n >= 0 && cachedWifiResultsJson == "[]") {
        struct ScannedWifi {
            String ssid;
            int rssi;
            bool is_open;
        };

        std::vector<ScannedWifi> networks;
        networks.reserve(n);

        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;

            int rssi = WiFi.RSSI(i);
            bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

            bool exists = false;
            for (auto& net : networks) {
                if (net.ssid == ssid) {
                    if (rssi > net.rssi) {
                        net.rssi = rssi;
                        net.is_open = isOpen;
                    }
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                networks.push_back({ssid, rssi, isOpen});
            }
        }

        std::sort(networks.begin(), networks.end(), [](const ScannedWifi& a, const ScannedWifi& b) {
            return a.rssi > b.rssi;
        });

        // Strictly limit to the top 30 strongest networks to protect RAM
        size_t max_results = min(networks.size(), (size_t)30);

        String json;
        json.reserve(max_results * 64);
        json = "[";
        for (size_t i = 0; i < max_results; i++) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + utils_escape_json(networks[i].ssid) + "\",";
            json += "\"rssi\":" + String(networks[i].rssi) + ",";
            json += "\"is_open\":" + String(networks[i].is_open ? "true" : "false") + "}";
        }
        json += "]";

        cachedWifiResultsJson = json;
        WiFi.scanDelete();
    }

    return false;
}

String wifi_net_get_scan_results_json() {
    // Ensure any finished scan results are cached
    wifi_net_is_scanning();
    return cachedWifiResultsJson;
}

#include "status_led.h"

void wifi_net_start_ap() {
    status_led_set(LED_SLOW_BLINK);
    Serial.println("[WIFI:AP_MODE] Starting AP Mode: Meshtastic-Bridge-Setup");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Meshtastic-Bridge-Setup");

    // Setup Captive Portal DNS - Route all DNS requests to the ESP32's IP
    dnsServer.start(53, "*", WiFi.softAPIP());

    // Start the Web Server
    config_ui_start_server();
    isApMode = true;
}

// Handles connecting as a wifi client/station to configured SSID and passkey.
bool wifi_net_connect_sta(const String& ssid, const String& pass) {
    Serial.print("[WIFI] Connecting to WiFi");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Wait up to 10 seconds for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] WiFi Connected successfully!");
        Serial.print("[WIFI] IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("[WIFI] Web UI available at: http://%s/\n", WiFi.localIP().toString().c_str());
        return true;
    }
    return false;
}

/**
 * Starts the mDNS (Multicast DNS) service to broadcast the bridge on the local network.
 *
 * This allows the official Meshtastic apps (iOS/Android/Web) and other network tools
 * to automatically discover the bridge as if it were a physical Meshtastic node. It
 * intelligently parses the target's BLE name to mimic its exact identity on the network.
 *
 * Under normal/ideal circumstances (e.g., target BLE name is "DSC_af28"):
 * - mDNS Hostname: "dsc-af28.local" (Resolves to the ESP32's IP address)
 * - Instance Name: "DSC_af28 Bridge" (Pretty name shown in generic Bonjour browsers)
 * - Short Name:    "DSC" (Parsed from the prefix before the underscore)
 * - Node ID:       "!af28" (Parsed from the suffix after the underscore)
 *
 * Fallback behaviors:
 * - Hostname: If BLE name is empty or invalid, falls back to "meshtastic-bridge-<MAC>.local"
 * - Short Name: If BLE name lacks an underscore, falls back to a default (e.g., "BRDG")
 * - Node ID: If BLE name lacks an underscore, falls back to "!" + the last 4 characters of the WiFi MAC
 *
 * @param cfg The active BridgeConfig containing the target BLE name and MAC.
 */
void wifi_net_start_mdns(const BridgeConfig& cfg) {
    // Name advertised
    String mdns_name = "meshtastic_bridge";
    // Hostname advertised.
    String mdns_host = "Meshtastic_Bridge";
    // Shortname advertised
    String short_name = "Meshtastic_Bridge";
    // NodeId advertised
    String node_id = "!";

    // Grab wifi mac address as fall back.
    String wifiMac = WiFi.macAddress();
    wifiMac.replace(":", "");
    while (wifiMac.length() < 4) {
        wifiMac += "x";
    }
    wifiMac = wifiMac.substring(wifiMac.length() - 4);

    if (cfg.ble_name.length() > 0) {
        mdns_name = cfg.ble_name + " Bridge";

        // Parse the BLE name (e.g., "DSC_af28")
        int underscoreIdx = cfg.ble_name.lastIndexOf('_');
        if (underscoreIdx > 0 && underscoreIdx < cfg.ble_name.length() - 1) {
            short_name = cfg.ble_name.substring(0, underscoreIdx);
            node_id += cfg.ble_name.substring(underscoreIdx + 1);
        } else {
            // Fallback if BLE name has no '_', use last 4 of mac
            node_id += wifiMac;
        }
    } else {
        node_id += wifiMac;
    }

    // Ensure nodeId is at least 4 characters
    while (node_id.length() < 4) {
        node_id += "x";
    }

    // Sanitize host to strict RFC rules (alphanumeric and hyphens only)
    String sanitizedHost = "";
    for (int i = 0; i < cfg.ble_name.length(); i++) {
        char c = cfg.ble_name[i];
        if (isalnum(c) || c == '-') {
            sanitizedHost += c;
        } else if (c == ' ' || c == '_') {
            sanitizedHost += '-';
        }
    }
    if (sanitizedHost.length() == 0) {
        sanitizedHost = "meshtastic-bridge-" + wifiMac;
    }
    // RFC 1035 enforces a 63-character limit.
    if (sanitizedHost.length() > 63) {
        sanitizedHost = sanitizedHost.substring(0, 63);
    }
    mdns_host = sanitizedHost;
    mdns_host.toLowerCase();

    if (MDNS.begin(mdns_host.c_str())) {
        MDNS.setInstanceName(mdns_name.c_str());
        MDNS.addService("meshtastic", "tcp", TCP_PORT);
        MDNS.addServiceTxt("meshtastic", "tcp", "name", mdns_name.c_str());
        MDNS.addServiceTxt("meshtastic", "tcp", "shortname", short_name.c_str());
        MDNS.addServiceTxt("meshtastic", "tcp", "id", node_id.c_str());
        Serial.printf("[MDNS] mDNS auto-discovery started (%s.local as '%s_%s')\n", mdns_host.c_str(), short_name.c_str(), node_id.substring(node_id.length() - 4).c_str());
    } else {
        Serial.println("[MDNS] Error: Failed to start MDNS advertisement.");
    }
}

static unsigned long buttonPressStart = 0;

void wifi_net_loop() {
    if (isApMode) {
        // Must be called repeatedly to handle DNS requests for the captive portal
        dnsServer.processNextRequest();
    } else {
        // Hold BOOT button for 2 seconds at runtime to enter Setup AP mode
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            if (buttonPressStart == 0) {
                buttonPressStart = millis();
            } else if (millis() - buttonPressStart >= 2000) {
                Serial.println("\n[BUTTON] BOOT button held for 2 seconds. Switching to Setup / Configuration Mode...");
                buttonPressStart = 0;
                wifi_net_start_ap();
            }
        } else {
            buttonPressStart = 0;
        }
    }
}
