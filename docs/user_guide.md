# User Guide

## Status LED Indicators

The bridge uses the ESP32's built-in LED to visually communicate its current state:

*   **Fast Blink** (100ms on / 100ms off): **Booting up.** The device is starting. This lasts for 5 seconds. Press and hold the BOOT button now to enter Setup Mode.
*   **Setup Pattern** (Short, Short, Long, Long): **Setup Mode / Captive Portal.** The bridge is broadcasting its `Meshtastic-Bridge-Setup` WiFi network. Connect to it to configure the bridge.
*   **Medium Blink** (500ms on / 500ms off): **Searching.** The bridge is in normal operating mode but is currently disconnected from the target Meshtastic Bluetooth radio. It is actively scanning to reconnect.
*   **Solid ON:** **Connected.** The bridge has successfully secured a Bluetooth connection with the Meshtastic radio and is actively routing packets.

## Entering Setup Mode

If you need to change your WiFi credentials or the target Bluetooth device:

1. Press the RST (Reset) button on the ESP32.
2. The LED will begin flashing rapidly.
3. Immediately press and hold the BOOT button.
4. The LED will change to the **Setup Pattern** (short-short-long-long), indicating it is now in Setup Mode.
5. Connect your phone or computer to the `Meshtastic-Bridge-Setup` WiFi network to access the Captive Portal.

---

## Configuring the MQTT Gateway & Proxy (Optional)

To use the bridge as an autonomous 24/7 MQTT gateway for your Bluetooth radio:

### 1. Configure the Radio (via Meshtastic Mobile App)
In the Meshtastic App on your phone:
1. Go to **Radio Configuration ➔ Module Configuration ➔ MQTT**.
2. Turn **MQTT Enabled** ON.
3. Turn **Proxy to Client Enabled** (or "Proxy") ON.
4. Set the **Server Address** (e.g. `mqtt.meshtastic.org` or your private broker IP).
5. Set **Username** and **Password** if required by your broker.
6. Turn **TLS Enabled** ON if connecting securely to port 8883.
7. Under **Channels ➔ [Your Channel] ➔ Module Settings**, ensure **Uplink Enabled** and/or **Downlink Enabled** is toggled ON.

### 2. Enable on the Bridge Web UI
1. Connect to the bridge's Web UI (in Setup Mode or by navigating to `http://<bridge-name>.local`).
2. In the **MQTT Gateway** card, toggle **Enable MQTT Gateway** ON.
3. If using a private broker with a self-signed cert, check **Skip Certificate Validation** or paste your **Custom CA Root Certificate (PEM)**.
4. Click **Save MQTT Settings**.

The bridge will automatically discover the radio's MQTT configuration over Bluetooth and maintain an active broker connection.
