#include "config_ui.h"
#include "ble_client.h"
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

Preferences preferences;
AsyncWebServer server(80);

const char* PREF_NAMESPACE = "bridge_cfg";

// The HTML for the captive portal
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Meshtastic Bridge Setup</title>
<style>
  body { font-family: Arial, sans-serif; padding: 20px; max-width: 400px; margin: auto; background-color: #f4f4f9; }
  h2 { color: #333; text-align: center; }
  .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
  label { font-weight: bold; margin-top: 10px; display: block; color: #555; }
  input, select, button { width: 100%; padding: 10px; margin: 8px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
  button { background-color: #4CAF50; color: white; border: none; cursor: pointer; font-size: 16px; font-weight: bold; }
  button:hover { opacity: 0.9; }
  .btn-scan { background-color: #2196F3; margin-bottom: 20px; }
  .spinner { display: none; margin: 10px 0; color: #2196F3; font-style: italic; font-weight: bold; text-align: center; }
</style>
</head>
<body>
  <h2>Bridge Setup</h2>
  <div class="card">
    <form action="/save" method="POST">
      <label>WiFi SSID:</label>
      <input type="text" name="ssid" required>
      
      <label>WiFi Password:</label>
      <input type="password" name="pass">
      
      <label>Target BLE Device:</label>
      <div id="scan-status" class="spinner">Scanning for Bluetooth devices (4 seconds)...</div>
      <select name="ble_mac" id="ble_mac" required>
        <option value="">-- Select Device --</option>
      </select>
      <button type="button" id="btn-scan" class="btn-scan" onclick="scanBle()">Scan for Devices</button>
      
      <label>BLE PIN (6-digit):</label>
      <input type="number" name="ble_pin" min="0" max="999999" required>
      
      <button type="submit">Save & Reboot</button>
    </form>
  </div>
  
  <script>
    let pollInterval = null;
    
    function scanBle() {
      const select = document.getElementById('ble_mac');
      const status = document.getElementById('scan-status');
      const btn = document.getElementById('btn-scan');
      
      if (pollInterval) {
        clearInterval(pollInterval);
        pollInterval = null;
      }
      
      btn.disabled = true;
      btn.style.opacity = '0.6';
      btn.innerText = 'Scanning...';
      
      select.innerHTML = '<option value="">-- Scanning in progress --</option>';
      status.innerText = 'Scanning for Bluetooth devices (4 seconds)...';
      status.style.display = 'block';
      
      // Start the scan
      fetch('/start_scan')
        .then(() => {
          pollInterval = setInterval(() => {
            fetch('/scan_results')
              .then(r => r.json())
              .then(data => {
                if (data.status === 'done') {
                  clearInterval(pollInterval);
                  pollInterval = null;
                  
                  btn.disabled = false;
                  btn.style.opacity = '1';
                  btn.innerText = 'Scan for Devices';
                  status.style.display = 'none';
                  
                  select.innerHTML = '<option value="">-- Select Device --</option>';
                  
                  if (!data.devices || data.devices.length === 0) {
                    status.innerText = 'No devices found.';
                    status.style.display = 'block';
                  } else {
                    data.devices.forEach(device => {
                      const opt = document.createElement('option');
                      opt.value = device.mac;
                      const nameStr = device.name ? device.name : 'Unknown';
                      const rssiStr = (device.rssi !== undefined) ? ' [' + device.rssi + ' dBm]' : '';
                      opt.textContent = nameStr + ' (' + device.mac + ')' + rssiStr;
                      select.appendChild(opt);
                    });
                  }
                }
              })
              .catch(e => {
                clearInterval(pollInterval);
                pollInterval = null;
                btn.disabled = false;
                btn.style.opacity = '1';
                btn.innerText = 'Scan for Devices';
                status.innerText = 'Error checking scan status.';
                status.style.display = 'block';
              });
          }, 800);
        })
        .catch(e => {
          btn.disabled = false;
          btn.style.opacity = '1';
          btn.innerText = 'Scan for Devices';
          status.innerText = 'Error initiating BLE scan.';
          status.style.display = 'block';
        });
    }
    
    // Auto scan on load
    window.onload = scanBle;
  </script>
</body>
</html>
)rawliteral";

void config_ui_init() {
    preferences.begin(PREF_NAMESPACE, false);
}

BridgeConfig config_ui_load() {
    BridgeConfig cfg;
    cfg.wifi_ssid = preferences.getString("wifi_ssid", "");
    cfg.wifi_pass = preferences.getString("wifi_pass", "");
    cfg.ble_mac = preferences.getString("ble_mac", "");
    cfg.ble_pin = preferences.getString("ble_pin", "");
    return cfg;
}

void config_ui_start_server() {
    // 1. Serve the main HTML page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", index_html);
    });
    
    // 2. Trigger the BLE scan asynchronously
    server.on("/start_scan", HTTP_GET, [](AsyncWebServerRequest *request){
        ble_client_start_scan();
        request->send(200, "application/json", "{\"status\": \"started\"}");
    });
    
    // 3. Poll for BLE scan results
    server.on("/scan_results", HTTP_GET, [](AsyncWebServerRequest *request){
        if (ble_client_is_scanning()) {
            request->send(200, "application/json", "{\"status\": \"scanning\"}");
        } else {
            String jsonResults = ble_client_get_scan_results_json();
            request->send(200, "application/json", "{\"status\": \"done\", \"devices\": " + jsonResults + "}");
        }
    });

    // 4. Save credentials and reboot
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("ssid", true) && request->hasParam("ble_mac", true) && request->hasParam("ble_pin", true)) {
            
            String ssid = request->getParam("ssid", true)->value();
            String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
            String ble_mac = request->getParam("ble_mac", true)->value();
            String ble_pin = request->getParam("ble_pin", true)->value();
            
            preferences.putString("wifi_ssid", ssid);
            preferences.putString("wifi_pass", pass);
            preferences.putString("ble_mac", ble_mac);
            preferences.putString("ble_pin", ble_pin);
            
            request->send(200, "text/html", "<html><body><h2>Settings Saved!</h2><p>The bridge is rebooting...</p></body></html>");
            
            // Give the response time to send, then reboot
            delay(2000);
            ESP.restart();
        } else {
            request->send(400, "text/plain", "Missing required parameters.");
        }
    });

    // Fallback for captive portal to catch standard captive portal test URLs (e.g. Android/iOS)
    server.onNotFound([](AsyncWebServerRequest *request){
        request->redirect("/");
    });

    server.begin();
}

void config_ui_stop_server() {
    server.end();
}
