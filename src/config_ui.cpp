#include "config_ui.h"
#include "ble_client.h"
#include "wifi_net.h"
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

bool g_debug_logs = false;
Preferences preferences;
AsyncWebServer server(80);

const char* PREF_NAMESPACE = "bridge_cfg";

// The HTML for the modular captive portal
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Meshtastic Bridge Setup</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; padding: 16px; max-width: 480px; margin: auto; background-color: #f0f2f5; color: #1c1e21; }
  h2 { color: #1a1a1a; text-align: center; margin-bottom: 20px; font-size: 22px; }
  .card { background: white; padding: 18px; border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); margin-bottom: 16px; }
  .card-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #eee; padding-bottom: 10px; margin-bottom: 14px; }
  .card-title { font-weight: bold; font-size: 16px; display: flex; align-items: center; gap: 8px; }
  .info-row { display: flex; justify-content: space-between; margin: 8px 0; font-size: 14px; }
  .info-label { color: #65676b; font-weight: 500; }
  .info-val { font-family: monospace; font-weight: 600; color: #050505; word-break: break-all; }
  label { font-weight: 600; margin-top: 12px; display: block; color: #444; font-size: 13px; }
  input, select { width: 100%; padding: 10px; margin: 6px 0 12px; border: 1px solid #ccd0d5; border-radius: 6px; font-size: 14px; background: #fff; }
  button { width: 100%; padding: 10px; margin: 6px 0; border: none; border-radius: 6px; cursor: pointer; font-size: 14px; font-weight: 600; transition: background-color 0.2s, opacity 0.2s; }
  button:hover { opacity: 0.92; }
  button:disabled { opacity: 0.55 !important; cursor: not-allowed; }
  .btn-primary { background-color: #0066cc; color: white; }
  .btn-success { background-color: #28a745; color: white; }
  .btn-secondary { background-color: #e4e6eb; color: #1c1e21; }
  .btn-warning { background-color: #ff9800; color: white; }
  .btn-danger { background-color: #dc3545; color: white; margin-top: 8px; }
  .btn-danger:hover { background-color: #c82333; }
  .btn-edit { width: auto; padding: 6px 16px; font-size: 13px; margin: 0; background-color: #0066cc; color: white; border-radius: 6px; font-weight: 600; }
  .btn-edit:hover { background-color: #0052a3; opacity: 1; }
  .btn-group { display: flex; gap: 8px; margin-top: 8px; }
  .btn-group button { flex: 1; }
  .password-field { position: relative; display: flex; align-items: center; margin-bottom: 12px; }
  .password-field input { padding-right: 65px; margin: 0; }
  .btn-toggle-pass { position: absolute; right: 5px; width: auto; padding: 6px 10px; font-size: 12px; margin: 0; background: #e4e6eb; color: #333; border-radius: 4px; }
  .spinner { display: none; margin: 10px 0; color: #0066cc; font-style: italic; font-size: 13px; text-align: center; }
  .alert { display: none; padding: 10px; border-radius: 6px; font-size: 13px; margin: 10px 0; }
  .alert-success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
  .alert-error { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
  .alert-warning { background-color: #fff3cd; color: #856404; border: 1px solid #ffeeba; }
  .alert-info { background-color: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
</style>
</head>
<body>
  <h2>📡 Meshtastic Bridge</h2>

  <!-- WiFi Configuration Card -->
  <div class="card" id="card-wifi">
    <div class="card-header">
      <div class="card-title">📶 WiFi Network</div>
      <button class="btn-edit" id="btn-edit-wifi" onclick="toggleEditWifi(true)">Edit</button>
    </div>
    <div id="wifi-view">
      <div class="info-row">
        <span class="info-label">SSID:</span>
        <span class="info-val" id="view-wifi-ssid">Loading...</span>
      </div>
      <div class="info-row">
        <span class="info-label">Password:</span>
        <span class="info-val" id="view-wifi-pass">••••••••</span>
      </div>
    </div>
    <div id="wifi-edit" style="display:none;">
      <label>Discovered Networks:</label>
      <div id="wifi-scan-status" class="spinner">Scanning for WiFi networks...</div>
      <select id="select-wifi-ssid" onchange="onWifiNetworkSelected()">
        <option value="">-- Select or scan below --</option>
      </select>
      <button type="button" id="btn-scan-wifi" class="btn-secondary" onclick="scanWifi()">🔍 Scan for Networks</button>

      <label>WiFi SSID:</label>
      <input type="text" id="input-wifi-ssid" placeholder="Network Name">
      <label>WiFi Password:</label>
      <div class="password-field">
        <input type="password" id="input-wifi-pass" placeholder="Leave empty to keep existing">
        <button type="button" class="btn-toggle-pass" onclick="togglePassVisibility('input-wifi-pass', this)">Show</button>
      </div>
      <div id="wifi-alert" class="alert"></div>
      <div class="btn-group">
        <button class="btn-success" id="btn-save-wifi" onclick="saveWifi()">Save WiFi</button>
        <button class="btn-secondary" onclick="toggleEditWifi(false)">Cancel</button>
      </div>
    </div>
  </div>

  <!-- Bluetooth Target Card -->
  <div class="card" id="card-ble">
    <div class="card-header">
      <div class="card-title">📻 Bluetooth Target</div>
      <button class="btn-edit" id="btn-edit-ble" onclick="toggleEditBle(true)">Edit</button>
    </div>
    <div id="ble-view">
      <div class="info-row">
        <span class="info-label">Target Device:</span>
        <span class="info-val" id="view-ble-device">Loading...</span>
      </div>
      <div class="info-row">
        <span class="info-label">BLE PIN:</span>
        <span class="info-val" id="view-ble-pin">••••••</span>
      </div>
    </div>
    <div id="ble-edit" style="display:none;">
      <label>Discovered Devices:</label>
      <div id="scan-status" class="spinner">Scanning for Bluetooth devices (4 seconds)...</div>
      <select id="select-ble-device" onchange="onBleDeviceSelected()">
        <option value="">-- Select or scan below --</option>
      </select>
      <button type="button" id="btn-scan" class="btn-secondary" onclick="scanBle()">🔍 Scan for Devices</button>
      
      <input type="hidden" id="input-ble-name" value="">
      
      <label>Target MAC Address:</label>
      <input type="text" id="input-ble-mac" placeholder="AA:BB:CC:DD:EE:FF">
      
      <label>BLE PIN (6-digit):</label>
      <input type="number" id="input-ble-pin" min="0" max="999999" placeholder="123456">
      
      <div id="ble-alert" class="alert"></div>
      
      <button type="button" id="btn-test-ble" class="btn-warning" onclick="testBleConnection()">⚡ Test Connection</button>
      <div class="btn-group">
        <button class="btn-success" id="btn-save-ble" onclick="saveBle()">Save Bluetooth</button>
        <button class="btn-secondary" onclick="toggleEditBle(false)">Cancel</button>
      </div>
    </div>
  </div>

  <!-- System Actions Card -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">🚀 System</div>
    </div>
    <div id="system-alert" class="alert"></div>
    
    <div class="input-group" style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 20px;">
      <label for="sys-debug-logs" style="margin-bottom:0;">Enable Serial Debug Logs</label>
      <input type="checkbox" id="sys-debug-logs" onchange="saveSystemSettings()" style="width:20px; height:20px;">
    </div>
    
    <button class="btn-primary" id="btn-reboot" onclick="rebootBridge()">Reboot & Start Bridge</button>
    <button class="btn-danger" id="btn-reset" onclick="resetBridge()">Reset All Settings</button>
  </div>

  <!-- Custom In-DOM Modal for Captive Portal compatibility (macOS/iOS CNA) -->
  <div id="modal-overlay" style="display:none; position:fixed; top:0; left:0; right:0; bottom:0; background:rgba(0,0,0,0.55); z-index:9999; align-items:center; justify-content:center; padding:16px;">
    <div style="background:white; padding:22px; border-radius:12px; max-width:380px; width:100%; box-shadow:0 8px 24px rgba(0,0,0,0.25); text-align:center;">
      <h3 id="modal-title" style="margin-top:0; font-size:18px; color:#1a1a1a;">Confirm Action</h3>
      <p id="modal-msg" style="font-size:14px; color:#444; margin:14px 0 20px; line-height:1.45;"></p>
      <div class="btn-group">
        <button id="modal-btn-confirm" class="btn-primary" style="margin:0;">Confirm</button>
        <button class="btn-secondary" style="margin:0;" onclick="closeModal()">Cancel</button>
      </div>
    </div>
  </div>

  <script>
    let currentConfig = { wifi_ssid: '', wifi_has_pass: false, ble_mac: '', ble_pin: '' };
    let pollInterval = null;
    let wifiPollInterval = null;

    function showAlert(id, type, msg) {
      const el = document.getElementById(id);
      el.className = 'alert alert-' + type;
      el.innerHTML = msg;
      el.style.display = 'block';
    }

    function hideAlert(id) {
      document.getElementById(id).style.display = 'none';
    }

    function togglePassVisibility(inputId, btn) {
      const input = document.getElementById(inputId);
      if (input.type === 'password') {
        input.type = 'text';
        btn.innerText = 'Hide';
      } else {
        input.type = 'password';
        btn.innerText = 'Show';
      }
    }

    function loadConfig() {
      fetch('/config')
        .then(r => r.json())
        .then(cfg => {
          currentConfig = cfg;
          document.getElementById('view-wifi-ssid').innerText = cfg.wifi_ssid || '(Not configured)';
          document.getElementById('view-wifi-pass').innerText = cfg.wifi_has_pass ? '••••••••' : '(None)';

          let deviceDisplay = '(Not configured)';
          if (cfg.ble_mac) {
            deviceDisplay = cfg.ble_name ? cfg.ble_name + ' (' + cfg.ble_mac + ')' : '(' + cfg.ble_mac + ')';
          }
          document.getElementById('view-ble-device').innerText = deviceDisplay;
          document.getElementById('view-ble-pin').innerText = cfg.ble_pin ? '••••••' : '(Not set)';

          document.getElementById('input-wifi-ssid').value = cfg.wifi_ssid || '';
          document.getElementById('input-ble-name').value = cfg.ble_name || '';
          document.getElementById('input-ble-mac').value = cfg.ble_mac || '';
          document.getElementById('input-ble-pin').value = cfg.ble_pin || '';
          document.getElementById('sys-debug-logs').checked = !!cfg.debug_logs;
        })
        .catch(() => {
          document.getElementById('view-wifi-ssid').innerText = 'Error loading';
          document.getElementById('view-ble-device').innerText = 'Error loading';
        });
    }

    function toggleEditWifi(edit) {
      document.getElementById('wifi-view').style.display = edit ? 'none' : 'block';
      document.getElementById('wifi-edit').style.display = edit ? 'block' : 'none';
      document.getElementById('btn-edit-wifi').style.display = edit ? 'none' : 'block';
      hideAlert('wifi-alert');
      if (edit) {
        scanWifi();
      } else if (wifiPollInterval) {
        clearInterval(wifiPollInterval);
        wifiPollInterval = null;
      }
    }

    function onWifiNetworkSelected() {
      const select = document.getElementById('select-wifi-ssid');
      const opt = select.options[select.selectedIndex];
      if (opt && opt.value) {
        document.getElementById('input-wifi-ssid').value = opt.value;
      }
    }

    let wifiScanInFlight = false;

    function scanWifi() {
      const select = document.getElementById('select-wifi-ssid');
      const status = document.getElementById('wifi-scan-status');
      const btnScan = document.getElementById('btn-scan-wifi');
      const btnSave = document.getElementById('btn-save-wifi');

      if (wifiPollInterval) {
        clearInterval(wifiPollInterval);
        wifiPollInterval = null;
      }
      wifiScanInFlight = false;

      // Disable scan and save while scanning is active
      btnScan.disabled = true;
      btnSave.disabled = true;

      select.innerHTML = '<option value="">-- Scan in progress... --</option>';
      status.innerText = 'Scanning for WiFi networks...';
      status.style.display = 'block';

      fetch('/start_scan_wifi')
        .then(() => {
          wifiPollInterval = setInterval(() => {
            if (wifiScanInFlight) return;
            wifiScanInFlight = true;

            fetch('/scan_wifi_results')
              .then(r => r.json())
              .then(data => {
                wifiScanInFlight = false;
                if (data.status === 'done') {
                  clearInterval(wifiPollInterval);
                  wifiPollInterval = null;

                  btnScan.disabled = false;
                  btnSave.disabled = false;
                  status.style.display = 'none';

                  select.innerHTML = '<option value="">-- Select Discovered Network --</option>';
                  if (!data.networks || data.networks.length === 0) {
                    status.innerText = 'No WiFi networks found.';
                    status.style.display = 'block';
                  } else {
                    data.networks.forEach(net => {
                      const opt = document.createElement('option');
                      opt.value = net.ssid;
                      const lockStr = net.is_open ? ' 🔓' : ' 🔒';
                      const rssiStr = (net.rssi !== undefined) ? ' [' + net.rssi + ' dBm]' : '';
                      opt.textContent = net.ssid + rssiStr + lockStr;
                      select.appendChild(opt);
                    });
                  }
                }
              })
              .catch(() => {
                wifiScanInFlight = false;
                clearInterval(wifiPollInterval);
                wifiPollInterval = null;
                btnScan.disabled = false;
                btnSave.disabled = false;
                status.innerText = 'Error checking WiFi scan status.';
                status.style.display = 'block';
              });
          }, 800);
        })
        .catch(() => {
          btnScan.disabled = false;
          btnSave.disabled = false;
          status.innerText = 'Error initiating WiFi scan.';
          status.style.display = 'block';
        });
    }

    function toggleEditBle(edit) {
      document.getElementById('ble-view').style.display = edit ? 'none' : 'block';
      document.getElementById('ble-edit').style.display = edit ? 'block' : 'none';
      document.getElementById('btn-edit-ble').style.display = edit ? 'none' : 'block';
      hideAlert('ble-alert');
      if (edit) {
        scanBle();
      }
    }

    function onBleDeviceSelected() {
      const select = document.getElementById('select-ble-device');
      const opt = select.options[select.selectedIndex];
      if (opt && opt.value) {
        document.getElementById('input-ble-mac').value = opt.value;
        document.getElementById('input-ble-name').value = opt.getAttribute('data-name') || '';
      }
    }

    function scanBle() {
      const select = document.getElementById('select-ble-device');
      const status = document.getElementById('scan-status');
      const btnScan = document.getElementById('btn-scan');
      const btnTest = document.getElementById('btn-test-ble');
      const btnSave = document.getElementById('btn-save-ble');

      if (pollInterval) {
        clearInterval(pollInterval);
        pollInterval = null;
      }

      // Disable scan, test, and save while scanning is active
      btnScan.disabled = true;
      btnTest.disabled = true;
      btnSave.disabled = true;

      select.innerHTML = '<option value="">-- Scan in progress... --</option>';
      status.innerText = 'Scanning for Bluetooth devices (4 seconds)...';
      status.style.display = 'block';

      fetch('/start_scan')
        .then(() => {
          pollInterval = setInterval(() => {
            fetch('/scan_results')
              .then(r => r.json())
              .then(data => {
                if (data.status === 'done') {
                  clearInterval(pollInterval);
                  pollInterval = null;

                  btnScan.disabled = false;
                  btnTest.disabled = false;
                  btnSave.disabled = false;
                  status.style.display = 'none';

                  select.innerHTML = '<option value="">-- Select Discovered Device --</option>';
                  if (!data.devices || data.devices.length === 0) {
                    status.innerText = 'No devices found.';
                    status.style.display = 'block';
                  } else {
                    data.devices.forEach(device => {
                      const opt = document.createElement('option');
                      opt.value = device.mac;
                      opt.setAttribute('data-name', device.name || '');
                      const nameStr = device.name ? device.name : 'Unknown';
                      const rssiStr = (device.rssi !== undefined) ? ' [' + device.rssi + ' dBm]' : '';
                      opt.textContent = nameStr + ' (' + device.mac + ')' + rssiStr;
                      select.appendChild(opt);
                    });
                  }
                }
              })
              .catch(() => {
                clearInterval(pollInterval);
                pollInterval = null;
                btnScan.disabled = false;
                btnTest.disabled = false;
                btnSave.disabled = false;
                status.innerText = 'Error checking scan status.';
                status.style.display = 'block';
              });
          }, 800);
        })
        .catch(() => {
          btnScan.disabled = false;
          btnTest.disabled = false;
          btnSave.disabled = false;
          status.innerText = 'Error initiating BLE scan.';
          status.style.display = 'block';
        });
    }

    function saveWifi() {
      const ssid = document.getElementById('input-wifi-ssid').value.trim();
      const pass = document.getElementById('input-wifi-pass').value;

      if (!ssid) {
        showAlert('wifi-alert', 'error', 'WiFi SSID cannot be empty.');
        return;
      }

      const body = 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass);
      fetch('/save_wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      })
      .then(r => r.json())
      .then(res => {
        if (res.success) {
          showAlert('wifi-alert', 'success', 'WiFi credentials saved!');
          setTimeout(() => {
            toggleEditWifi(false);
            loadConfig();
          }, 800);
        } else {
          showAlert('wifi-alert', 'error', res.error || 'Failed to save WiFi.');
        }
      })
      .catch(() => showAlert('wifi-alert', 'error', 'Network error saving WiFi.'));
    }

    function saveBle() {
      const name = document.getElementById('input-ble-name').value.trim();
      const mac = document.getElementById('input-ble-mac').value.trim();
      const pin = document.getElementById('input-ble-pin').value.trim();

      if (!mac) {
        showAlert('ble-alert', 'error', 'Target BLE MAC address cannot be empty.');
        return;
      }
      if (!pin) {
        showAlert('ble-alert', 'error', 'BLE 6-digit PIN cannot be empty.');
        return;
      }

      const body = 'ble_name=' + encodeURIComponent(name) + '&ble_mac=' + encodeURIComponent(mac) + '&ble_pin=' + encodeURIComponent(pin);
      fetch('/save_ble', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      })
      .then(r => r.json())
      .then(res => {
        if (res.success) {
          showAlert('ble-alert', 'success', 'Bluetooth settings saved!');
          setTimeout(() => {
            toggleEditBle(false);
            loadConfig();
          }, 800);
        } else {
          showAlert('ble-alert', 'error', res.error || 'Failed to save Bluetooth.');
        }
      })
      .catch(() => showAlert('ble-alert', 'error', 'Network error saving Bluetooth.'));
    }

    let testInterval = null;

    function testBleConnection() {
      const mac = document.getElementById('input-ble-mac').value.trim();
      const pin = document.getElementById('input-ble-pin').value.trim();
      const btnTest = document.getElementById('btn-test-ble');
      const btnScan = document.getElementById('btn-scan');
      const btnSave = document.getElementById('btn-save-ble');

      if (!mac) {
        showAlert('ble-alert', 'error', 'Please enter or select a BLE MAC address first.');
        return;
      }
      if (!pin) {
        showAlert('ble-alert', 'error', 'Please enter the 6-digit pairing PIN.');
        return;
      }

      if (testInterval) {
        clearInterval(testInterval);
        testInterval = null;
      }

      // Disable test, scan, and save while testing is active
      btnTest.disabled = true;
      btnScan.disabled = true;
      btnSave.disabled = true;
      btnTest.innerText = 'Connecting & Testing...';
      showAlert('ble-alert', 'info', 'Connecting to ' + mac + ' and verifying Meshtastic radio service...');

      const body = 'ble_mac=' + encodeURIComponent(mac) + '&ble_pin=' + encodeURIComponent(pin);
      let elapsedTicks = 0;

      fetch('/start_test_ble', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      })
      .then(() => {
        testInterval = setInterval(() => {
          elapsedTicks++;
          if (elapsedTicks > 15) { // 12 seconds max
            clearInterval(testInterval);
            testInterval = null;
            btnTest.disabled = false;
            btnScan.disabled = false;
            btnSave.disabled = false;
            btnTest.innerText = '⚡ Test Connection';
            showAlert('ble-alert', 'error', 'Test timed out after 12 seconds.');
            return;
          }

          fetch('/test_ble_status')
            .then(r => r.json())
            .then(res => {
              if (res.status === 'done') {
                clearInterval(testInterval);
                testInterval = null;
                btnTest.disabled = false;
                btnScan.disabled = false;
                btnSave.disabled = false;
                btnTest.innerText = '⚡ Test Connection';

                if (res.success) {
                  showAlert('ble-alert', 'success', '✓ ' + (res.message || 'Connected and verified successfully!'));
                } else {
                  showAlert('ble-alert', 'error', '✗ ' + (res.error || res.message || 'Connection test failed.'));
                }
              }
            })
            .catch(() => {
              // Retry on transient poll error
            });
        }, 800);
      })
      .catch(() => {
        btnTest.disabled = false;
        btnScan.disabled = false;
        btnSave.disabled = false;
        btnTest.innerText = '⚡ Test Connection';
        showAlert('ble-alert', 'error', 'Could not initiate connection test request.');
      });
    }

    let modalConfirmCallback = null;

    function showModal(title, msg, btnText, btnClass, onConfirm) {
      document.getElementById('modal-title').innerText = title;
      document.getElementById('modal-msg').innerText = msg;
      const btn = document.getElementById('modal-btn-confirm');
      btn.innerText = btnText;
      btn.className = btnClass;
      modalConfirmCallback = onConfirm;
      document.getElementById('modal-overlay').style.display = 'flex';
    }

    function closeModal() {
      document.getElementById('modal-overlay').style.display = 'none';
      modalConfirmCallback = null;
    }

    function confirmModalAction() {
      if (modalConfirmCallback) {
        modalConfirmCallback();
      }
      closeModal();
    }

    function saveSystemSettings() {
      const debugLogs = document.getElementById('sys-debug-logs').checked;
      fetch('/save_system', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'debug_logs=' + (debugLogs ? 'true' : 'false')
      }).then(r => r.json()).then(res => {
        if (res.success) {
          showAlert('system-alert', 'success', 'System settings saved!');
          setTimeout(() => { document.getElementById('system-alert').style.display = 'none'; }, 2000);
        } else {
          showAlert('system-alert', 'error', res.error || 'Failed to save');
        }
      }).catch(err => {
        showAlert('system-alert', 'error', 'Error saving system settings');
      });
    }

    function rebootBridge() {
      showModal(
        'Reboot Bridge',
        'Are you sure you want to reboot the bridge and start in normal mode?',
        'Reboot Now',
        'btn-primary',
        function() {
          const btnReboot = document.getElementById('btn-reboot');
          const btnReset = document.getElementById('btn-reset');
          btnReboot.disabled = true;
          btnReset.disabled = true;
          showAlert('system-alert', 'info', 'Bridge is rebooting... Connect to your local WiFi to use.');
          fetch('/reboot', { method: 'POST' });
        }
      );
    }

    function resetBridge() {
      showModal(
        'Reset All Settings',
        'Are you sure you want to erase all saved settings? This will restore factory defaults and restart in setup mode.',
        'Reset & Erase',
        'btn-danger',
        function() {
          const btnReset = document.getElementById('btn-reset');
          const btnReboot = document.getElementById('btn-reboot');
          btnReset.disabled = true;
          btnReboot.disabled = true;
          showAlert('system-alert', 'warning', 'All settings erased. Rebooting into Setup Mode...');
          fetch('/reset', { method: 'POST' });
        }
      );
    }

    window.onload = function() {
      document.getElementById('modal-btn-confirm').onclick = confirmModalAction;
      loadConfig();
    };
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
    cfg.ble_name = preferences.getString("ble_name", "");
    cfg.ble_mac = preferences.getString("ble_mac", "");
    cfg.ble_pin = preferences.getString("ble_pin", "");
    cfg.debug_logs = preferences.getBool("debug_logs", false);
    g_debug_logs = cfg.debug_logs;
    return cfg;
}

void config_ui_start_server() {
    // Serve the main HTML page with explicit UTF-8 charset
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html; charset=utf-8", index_html);
    });

    // Fetch current configuration JSON
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
        BridgeConfig cfg = config_ui_load();
        String json = "{";
        json += "\"wifi_ssid\":\"" + cfg.wifi_ssid + "\",";
        json += "\"wifi_has_pass\":" + String(cfg.wifi_pass.length() > 0 ? "true" : "false") + ",";
        json += "\"ble_name\":\"" + cfg.ble_name + "\",";
        json += "\"ble_mac\":\"" + cfg.ble_mac + "\",";
        json += "\"ble_pin\":\"" + cfg.ble_pin + "\",";
        json += "\"debug_logs\":" + String(cfg.debug_logs ? "true" : "false");
        json += "}";
        request->send(200, "application/json", json);
    });

    // Trigger the WiFi scan asynchronously
    server.on("/start_scan_wifi", HTTP_GET, [](AsyncWebServerRequest *request){
        wifi_net_start_scan();
        request->send(200, "application/json", "{\"status\": \"started\"}");
    });
    
    // Poll for WiFi scan results
    server.on("/scan_wifi_results", HTTP_GET, [](AsyncWebServerRequest *request){
        if (wifi_net_is_scanning()) {
            request->send(200, "application/json", "{\"status\": \"scanning\"}");
        } else {
            String jsonResults = wifi_net_get_scan_results_json();
            request->send(200, "application/json", "{\"status\": \"done\", \"networks\": " + jsonResults + "}");
        }
    });
    
    // Trigger the BLE scan asynchronously
    server.on("/start_scan", HTTP_GET, [](AsyncWebServerRequest *request){
        ble_client_start_scan();
        request->send(200, "application/json", "{\"status\": \"started\"}");
    });
    
    // Poll for BLE scan results
    server.on("/scan_results", HTTP_GET, [](AsyncWebServerRequest *request){
        if (ble_client_is_scanning()) {
            request->send(200, "application/json", "{\"status\": \"scanning\"}");
        } else {
            String jsonResults = ble_client_get_scan_results_json();
            request->send(200, "application/json", "{\"status\": \"done\", \"devices\": " + jsonResults + "}");
        }
    });

    // Start async Bluetooth Connection Test
    server.on("/start_test_ble", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("ble_mac", true) && request->hasParam("ble_pin", true)) {
            String mac = request->getParam("ble_mac", true)->value();
            String pin = request->getParam("ble_pin", true)->value();
            
            ble_client_start_test(mac, pin);
            request->send(200, "application/json", "{\"status\":\"started\"}");
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing ble_mac or ble_pin parameter.\"}");
        }
    });

    // Poll Bluetooth Connection Test Status
    server.on("/test_ble_status", HTTP_GET, [](AsyncWebServerRequest *request){
        if (ble_client_is_testing()) {
            request->send(200, "application/json", "{\"status\":\"testing\"}");
        } else {
            BleTestResult res = ble_client_get_test_result();
            String json = "{\"status\":\"done\",";
            json += "\"success\":" + String(res.success ? "true" : "false") + ",";
            if (res.success) {
                json += "\"message\":\"" + res.message + "\"";
            } else {
                json += "\"error\":\"" + res.message + "\"";
            }
            json += "}";
            request->send(200, "application/json", json);
        }
    });

    // Save WiFi configuration
    server.on("/save_wifi", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("ssid", true)) {
            String ssid = request->getParam("ssid", true)->value();
            preferences.putString("wifi_ssid", ssid);
            if (request->hasParam("pass", true)) {
                String pass = request->getParam("pass", true)->value();
                if (pass.length() > 0) {
                    preferences.putString("wifi_pass", pass);
                }
            }
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing ssid parameter.\"}");
        }
    });

    // Save Bluetooth configuration
    server.on("/save_ble", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("ble_mac", true) && request->hasParam("ble_pin", true)) {
            String mac = request->getParam("ble_mac", true)->value();
            String pin = request->getParam("ble_pin", true)->value();
            String name = request->hasParam("ble_name", true) ? request->getParam("ble_name", true)->value() : "";
            preferences.putString("ble_name", name);
            preferences.putString("ble_mac", mac);
            preferences.putString("ble_pin", pin);
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing ble_mac or ble_pin parameter.\"}");
        }
    });

    // Save System configuration
    server.on("/save_system", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("debug_logs", true)) {
            String val = request->getParam("debug_logs", true)->value();
            g_debug_logs = (val == "true");
            preferences.putBool("debug_logs", g_debug_logs);
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing debug_logs parameter.\"}");
        }
    });

    // Reboot system
    server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting bridge...\"}");
        delay(1000);
        ESP.restart();
    });

    // Reset all settings and reboot
    server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request){
        preferences.clear();
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Settings erased. Rebooting...\"}");
        delay(1000);
        ESP.restart();
    });

    // Legacy full form POST save
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("ssid", true) && request->hasParam("ble_mac", true) && request->hasParam("ble_pin", true)) {
            String ssid = request->getParam("ssid", true)->value();
            String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
            String ble_name = request->hasParam("ble_name", true) ? request->getParam("ble_name", true)->value() : "";
            String ble_mac = request->getParam("ble_mac", true)->value();
            String ble_pin = request->getParam("ble_pin", true)->value();
            
            preferences.putString("wifi_ssid", ssid);
            if (pass.length() > 0) {
                preferences.putString("wifi_pass", pass);
            }
            preferences.putString("ble_name", ble_name);
            preferences.putString("ble_mac", ble_mac);
            preferences.putString("ble_pin", ble_pin);
            
            request->send(200, "text/html", "<html><body><h2>Settings Saved!</h2><p>The bridge is rebooting...</p></body></html>");
            delay(1500);
            ESP.restart();
        } else {
            request->send(400, "text/plain", "Missing required parameters.");
        }
    });

    // Fallback for captive portal redirection
    server.onNotFound([](AsyncWebServerRequest *request){
        request->redirect("/");
    });

    server.begin();
}

void config_ui_stop_server() {
    server.end();
}
