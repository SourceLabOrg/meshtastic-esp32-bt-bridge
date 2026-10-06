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
4. The LED will change to a **Slow Blink**, indicating it is now in Setup Mode.
5. Connect your phone or computer to the `Meshtastic-Bridge-Setup` WiFi network to access the Captive Portal.
