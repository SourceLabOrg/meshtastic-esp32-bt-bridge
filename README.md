# Meshtastic ESP32 BLE-to-TCP Bridge

A simple, lightweight ESP32 firmware that bridges a Meshtastic radio's Bluetooth connection to your local WiFi network. 

If you have a Bluetooth-only Meshtastic device (like a T-Echo, SenseCAP, or a base station without WiFi) and want to connect to it over your home network using the Meshtastic Web UI, MeshMonitor, or the mobile apps, this bridge acts as a transparent middleman. It connects to your radio via Bluetooth Low Energy (BLE) and exposes it as a standard Meshtastic network node on TCP port 4403.

> **Note:** This project is heavily inspired by the excellent Python-based [meshtastic-ble-bridge](https://github.com/meshtastic/meshtastic-ble-bridge). If you are looking to run a bridge on a Raspberry Pi, Mac, or Windows computer rather than dedicated microcontroller hardware, we highly recommend using the Python project instead!

## How It Works

Once flashed to an ESP32, the bridge operates entirely standalone:

1. **Setup Mode:** On first boot, the ESP32 acts as its own WiFi Access Point. You connect to it with your phone or laptop to access a simple Web UI.
2. **Configuration:** In the Web UI, you provide your local home WiFi credentials, and scan for your Meshtastic radio's Bluetooth MAC address and PIN.
3. **Bridge Mode:** The ESP32 reboots, connects to your home WiFi, pairs securely with your Meshtastic radio over Bluetooth, and quietly runs in the background. 
4. **Auto-Discovery:** The bridge broadcasts itself on your local network using mDNS. Official Meshtastic apps will automatically discover it as if the radio itself was directly plugged into your router.

## What Hardware do I Need?

You will need a supported ESP32 microcontroller board. 

**Highly Recommended:**
*   **[Seeed Studio XIAO ESP32-S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html)**: This is the officially recommended board for this project. It is incredibly tiny, has native USB support, and features a powerful antenna.
    *   *Optional:* You can **[3D print this excellent case](https://www.printables.com/model/1445678-case-for-seeed-xiao-esp32s3-and-the-default-antenn/files)** designed specifically to hold the board and its default antenna!

**Other Supported Hardware:**
*   **Generic ESP32-S3 DevKit:** Any standard ESP32-S3 development board with native USB.
*   **Generic ESP32 (Classic):** Standard WROOM-32 or NodeMCU style boards.

*(Note: ESP32-S2 boards are not supported as they lack Bluetooth hardware).*

## How Do I Install It?

The absolute easiest way to install this firmware is by using our zero-install Web Flasher. You don't need to download any tools or compilers.

1. Plug your ESP32 board into your computer via USB.
2. Open a supported browser (Chrome, Edge, or Opera on a desktop OS).
3. **[Launch the Meshtastic Bridge Web Flasher](https://sourcelaborg.github.io/meshtastic-esp32-bt-bridge/)**
4. Locate your hardware in the list.
5. If this is a brand new board, click **Factory Reset (Wipes Data)**. If you are upgrading an existing bridge, click **Update Firmware**.

## How do I set it up?

Once you have successfully flashed the firmware to your ESP32, follow these steps to configure it:

1. Power on your ESP32. The built-in LED will blink rapidly for 5 seconds.
2. The LED will change to a repeating setup pattern (short, short, long, long). This indicates it is broadcasting its Setup WiFi network.
3. On your phone or computer, connect to the WiFi network named **`Meshtastic-Bridge-Setup`**.
4. A Captive Portal should automatically appear (if it doesn't, navigate to `http://192.168.4.1` in your browser).
5. Enter your home WiFi credentials.
6. Scan for your target Meshtastic device and enter its 6-digit Bluetooth PIN. (You can click "Test Connection" to verify it works).
7. Click **Reboot & Start Bridge**.

The ESP32 will reboot, and its LED will turn **solid** once it has successfully connected to both your WiFi network and your Meshtastic radio.

## How do I use it?

Once the bridge is running with a solid LED, it is entirely transparent!

1. Open your Meshtastic App (iOS, Android, or Web UI).
2. Select a **Network / TCP** connection.
3. The bridge will automatically advertise itself on your local network using Apple Bonjour / mDNS. It will show up using the exact same name as your radio's Bluetooth name (e.g., `Meshtastic_ABCD`).
4. Simply click to connect! 

> **Note:** The bridge supports multiplexing, meaning **up to 3 clients** (e.g., your phone, your tablet, and your desktop) can all connect to the radio simultaneously over WiFi!

### Standalone MQTT Gateway & Proxy (Optional)

In addition to serving local TCP app clients, the bridge can act as an **autonomous MQTT Gateway** for your Bluetooth radio:

* **Auto-Sync from Radio:** When MQTT is enabled on your radio (**MQTT Enabled** and **Proxy to Client Enabled** in the Meshtastic app), the bridge automatically discovers your broker address, port, credentials, root topic (`msh`, `ptp`, etc.), and TLS configuration over Bluetooth and establishes the connection.
* **3-Tier TLS Security:** Supports public brokers (via an embedded Mozilla Root CA bundle for Let's Encrypt / DigiCert), custom/private CA certificates (uploadable via Web UI), or self-signed insecure mode.
* **Dual-Queue Prioritization:** Prioritizes local app chats and commands while streaming background MQTT downlink packets to the radio.
* **Disconnect Grace Period:** If the Bluetooth connection drops momentarily, the bridge keeps the MQTT broker connection alive for up to 60 seconds and buffers incoming messages without losing your session.

### Re-configuring the Bridge

If you ever need to change your WiFi credentials or connect to a different Meshtastic radio, you have two options:

**Option A (Via your local network):**
If the bridge is already connected to your home WiFi, simply open a web browser and navigate to `http://<your_device_name>.local` (e.g., `http://Meshtastic_ABCD.local`). This will load the Captive Portal UI directly over your home network.

**Option B (Hardware Reset):**
If you changed your WiFi router and the bridge can no longer connect to your network, you can force it back into Setup Mode:
1. Press the Reset (RST) button on your ESP32.
2. The LED will begin flashing rapidly for 5 seconds.
3. While it is flashing, press and hold the **BOOT** button on the ESP32.
4. The LED will switch to the setup pattern, and it will begin broadcasting the `Meshtastic-Bridge-Setup` WiFi Access Point again.
