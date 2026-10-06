# Meshtastic ESP32 BLE-to-TCP Bridge

A simple, lightweight ESP32 firmware that bridges a Meshtastic radio's Bluetooth connection to your local WiFi network. 

If you have a Bluetooth-only Meshtastic device (like a T-Echo, SenseCAP, or a base station without WiFi) and want to connect to it over your home network using the Meshtastic Web UI, MeshMonitor, or the mobile apps, this bridge acts as a transparent middleman. It connects to your radio via Bluetooth Low Energy (BLE) and exposes it as a standard Meshtastic network node on TCP port 4403.

> **Note:** This project is heavily inspired by the excellent Python-based [meshtastic-ble-bridge](https://github.com/meshtastic/meshtastic-ble-bridge). If you are looking to run a bridge on a Raspberry Pi, Mac, or Windows computer rather than dedicated microcontroller hardware, we highly recommend using the Python project instead!

## Supported Hardware

This firmware is written in C++ using the Arduino framework and PlatformIO. It is designed to be as portable as possible and should work on almost any ESP32 variant that supports both WiFi and Bluetooth Low Energy:

*   **ESP32-S3** (Recommended, tested extensively on the Seeed XIAO ESP32S3)
*   **ESP32** (Classic)
*   **ESP32-S2 / C3** (Assuming BLE/WiFi combo support)

## How It Works

Once flashed to an ESP32, the bridge operates entirely standalone:

1. **Setup Mode:** On first boot, the ESP32 acts as its own WiFi Access Point. You connect to it with your phone or laptop to access a simple Web UI.
2. **Configuration:** In the Web UI, you provide your local home WiFi credentials, and scan for your Meshtastic radio's Bluetooth MAC address and PIN.
3. **Bridge Mode:** The ESP32 reboots, connects to your home WiFi, pairs securely with your Meshtastic radio over Bluetooth, and quietly runs in the background. 
4. **Auto-Discovery:** The bridge broadcasts itself on your local network using mDNS. Official Meshtastic apps will automatically discover it as if the radio itself was directly plugged into your router.

## Installation (Zero-Install Web Flasher)

The absolute easiest way to install this firmware is by using our Web Flasher. You don't need to download any tools or compilers—just plug your ESP32 into your computer via USB and click a button in your browser (Chrome, Edge, or Opera required).

**[Launch the Meshtastic Bridge Web Flasher](https://sourcelaborg.github.io/meshtastic-esp32-bt-bridge/)**

## Basic Setup

*(Assuming you have already flashed the firmware to your ESP32)*

1. Power on your ESP32. The built-in LED will blink rapidly for 5 seconds.
2. The LED will change to a repeating setup pattern (short, short, long, long). This indicates it is broadcasting its Setup WiFi network.
3. On your phone or computer, connect to the WiFi network named **`Meshtastic-Bridge-Setup`**.
4. A Captive Portal should automatically appear (if it doesn't, navigate to `http://192.168.4.1` in your browser).
5. Enter your home WiFi credentials.
6. Scan for your target Meshtastic device and enter its 6-digit Bluetooth PIN. (You can click "Test Connection" to verify it works).
7. Click **Reboot & Start Bridge**.

The ESP32 will reboot, and its LED will turn **solid** once it has successfully connected to both your WiFi network and your Meshtastic radio.

---
