# Maintenance & Pre-Release Checklist

This document outlines the standard maintenance workflow and verification checklist to run through before cutting a new firmware release.

---

## 1. Dependency Audit & Upgrades

Review all project dependencies for newer versions.

> **Rule of Thumb:**
> 1. **Pin Exact Versions:** Pin dependencies in `platformio.ini` to exact versions (e.g., `2.5.2`, `0.4.93`) rather than floating `^` ranges. This guarantees deterministic local and CI builds and ensures changes to `platformio.ini` automatically invalidate the GitHub Actions `.pio` build cache (`hashFiles('**/platformio.ini', ...)`).
> 2. **One Upgrade Per PR:** Upgrade **one dependency at a time** in its own dedicated branch and Pull Request (`git checkout -b chore/upgrade-<dep-name>`). This isolates breaking API changes, memory footprint shifts, and runtime regressions so they can be properly migrated, tested, and reviewed before merging.

### 1.1 PlatformIO Platform & Libraries (`platformio.ini`)
Check for outdated PlatformIO packages by running:
```bash
pio pkg outdated
```
*(Note: Even when packages are pinned to an exact version in `platformio.ini`, `pio pkg outdated` compares the installed `Current`/`Wanted` version against the `Latest` version available in the PlatformIO Registry).*

Inspect each of the following in `platformio.ini`:
- [ ] **`platform = espressif32`** (Pinned to an exact version; Arduino Core / ESP-IDF toolchain)
  - Check release notes for ESP-IDF / Arduino Core changes affecting WiFi, LwIP, mbedTLS, or FreeRTOS.
- [ ] **`h2zero/NimBLE-Arduino`** (Pinned to an exact version)
  - When upgrading, audit internal buffer sizing (`CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT`), `NimBLEAttValue` allocation sizing (`CONFIG_NIMBLE_CPP_ATT_VALUE_INIT_LENGTH=512`), and FreeRTOS task notification usage against the invariants documented in `docs/nimble-2x-upgrade.md`.
- [ ] **`ESP32Async/AsyncTCP`** and **`ESP32Async/ESPAsyncWebServer`** (Pinned to exact versions)
  - Verify TCP bridge connections (port `4403`), captive portal endpoints, `AsyncResponseStream` buffer pre-sizing (`GET /metrics`, `GET /config`, `GET /status`), and WebUI asset serving.
- [ ] **`nanopb/Nanopb`** (Pinned to an exact version)
  - Verify compatibility with `generate_protos.py` and the Meshtastic `.proto` definitions (`generate_protos.py` hashes `nanopb_generator.py` so changing the Nanopb version automatically triggers regeneration of all `.pb.h`/`.pb.cpp` files).

### 1.2 Meshtastic Protobufs Submodule (`proto/meshtastic`)
The project uses the official [meshtastic/protobufs](https://github.com/meshtastic/protobufs) repository as a Git submodule to decode and encode radio packets (`ToRadio`, `FromRadio`, `ModuleConfig.mqtt`, `MqttClientProxyMessage`).

1. Fetch upstream tags and check out the latest official release tag in a dedicated branch (preferring tagged releases over untagged `master` commits):
   ```bash
   git -C proto/meshtastic fetch --tags origin
   git -C proto/meshtastic status -sb
   git -C proto/meshtastic tag --sort=-v:refname | head -n 5
   git -C proto/meshtastic checkout <latest-tag>
   ```
2. Run a build (`generate_protos.py` automatically detects modified `.proto` / `.options` files via MD5 hash and regenerates the affected Nanopb `.pb.cpp` / `.pb.h` files):
   ```bash
   pio run
   ```
3. Verify that:
   - All `.proto` and `.options` files compile cleanly under Nanopb.
   - No changes to `ToRadio`, `FromRadio`, `ModuleConfig.MQTTConfig`, or `MqttClientProxyMessage` break field tags, callbacks, or exceed `MESHTASTIC_MAX_PACKET_SIZE` (`576` bytes). You can verify the Nanopb-computed maximum wire sizes directly in the generated header:
     ```bash
     grep -E "meshtastic_(FromRadio|ToRadio|MqttClientProxyMessage)_size" .pio/build/seeed_xiao_esp32s3/nanopb/meshtastic/mesh.pb.h
     ```

### 1.3 Web Flasher, CI/CD & DevContainer Dependencies
- [ ] **ESP Web Tools (`web/index.html`):** Check if a newer major/minor version of `esp-web-tools` is available on unpkg (`https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module`).
- [ ] **GitHub Actions (`.github/workflows/*.yml`):** Check `pr_check.yml`, `release.yml`, and `pages.yml` for outdated runner OS images (`ubuntu-24.04`) or action versions (`actions/checkout`, `actions/setup-python`, `actions/cache`, `softprops/action-gh-release`, `actions/configure-pages`, `actions/upload-pages-artifact`, `actions/deploy-pages`).
- [ ] **DevContainer (`.devcontainer/Dockerfile`):** Verify the Python base image and `protobuf` / `grpcio-tools` packages still build cleanly.

### 1.4 Per-Upgrade Verification (Run for Each Dependency PR)
Before merging any dependency upgrade branch:
1. **Multi-Board Build & Footprint Check:**
   Run `pio run` and compare RAM (`.bss` + `.data`) and Flash usage across all three target environments:
   - `seeed_xiao_esp32s3`
   - `esp32dev`
   - `esp32-s3-devkitc-1`
2. **Hardware Runtime Heap & Stack Audit:**
   Flash the branch onto hardware and monitor `GET /status` (or the 15-second `[DIAG]` serial telemetry log) under active BLE + TCP + MQTT traffic:
   - **Heap Health:** Confirm `free_heap` and `min_free_heap` remain within expected bounds (~95+ KB on `esp32dev`, ~105+ KB on `esp32s3`) and `max_alloc_heap` stays at ~85%+ of `free_heap` with zero steady downward drift (confirming no heap leaks or fragmentation).
   - **Task Stack High-Water Marks:** Confirm `stack_ble_min_free`, `stack_net_min_free`, `stack_mqtt_min_free`, and `stack_diag_min_free` maintain safe headroom and do not approach `0`.

---

## 2. Refresh Embedded TLS Root CA Bundle

To ensure MQTTS connections continue to trust current public certificate authorities (Let's Encrypt, AWS IoT, HiveMQ, EMQX, etc.), check if a newer Mozilla / cURL CA store (`cacert.pem`) has been published since the release date recorded in [docs/development.md](development.md#8-tls-root-certificate-authority-ca-bundle-pipeline):

```bash
curl -s https://curl.se/ca/cacert.pem | head -n 5
```

Compare the `## Certificate data from Mozilla as of: ...` line against the **Current Embedded Mozilla CA Store Release Date** in `docs/development.md`:
- If the date matches, the embedded `data/cert/x509_crt_bundle.bin` is already up to date and you can skip the rest of this section.
- If a newer CA bundle is available, regenerate `data/cert/x509_crt_bundle.bin` on its own branch/PR (e.g., `chore/update-ca-bundle`):

```bash
# 1. Create a temporary Python virtual environment with cryptography
python3 -m venv /tmp/ca_env
/tmp/ca_env/bin/pip install cryptography --quiet

# 2. Download Espressif's standalone bundle generator and the latest Mozilla CA store
curl -s https://raw.githubusercontent.com/espressif/esp-idf/release/v5.1/components/mbedtls/esp_crt_bundle/gen_crt_bundle.py -o /tmp/gen_crt_bundle.py
curl -s https://curl.se/ca/cacert.pem -o /tmp/cacert.pem

# 3. Generate the compact binary bundle and move to data/cert/
/tmp/ca_env/bin/python /tmp/gen_crt_bundle.py -i /tmp/cacert.pem
mv x509_crt_bundle data/cert/x509_crt_bundle.bin

# 4. Clean up temporary files
rm -rf /tmp/ca_env /tmp/gen_crt_bundle.py /tmp/cacert.pem
```

- [ ] Update the **Current Embedded Mozilla CA Store Release Date** (and SHA-256) in `docs/development.md`.
- [ ] Run `pio run` to verify the binary bundle embeds cleanly into all board targets and commit `data/cert/x509_crt_bundle.bin` and `docs/development.md`.

*(See [docs/development.md](development.md#8-tls-root-certificate-authority-ca-bundle-pipeline) for architectural details on how the CA bundle is embedded and verified at runtime).*

---

## 3. Documentation & Configuration Audit

- [ ] **Build Flags & Defaults (`include/build_options.h`):**
  Ensure any newly added compile-time flags in `include/build_options.h` or `platformio.ini` are documented in `docs/development.md`.
- [ ] **Documentation & Roadmap Sync:**
  Ensure `README.md`, `TODO.md`, `docs/architecture.md`, `docs/user_guide.md`, and feature docs (`docs/feature-*.md`) accurately reflect all changes since the last release.

---

## 4. Pre-Release Hardware Smoke Test Checklist

Before tagging the release, flash the release candidate onto physical hardware (at minimum the primary `seeed_xiao_esp32s3` target) and verify:

- [ ] **Setup / Captive Portal Mode:**
  - Hold the `BOOT` button during the 5-second rapid-blink boot window to enter Setup Mode (`Meshtastic-Bridge-Setup` AP + short-short-long-long LED pattern).
  - Connect to `http://192.168.4.1`, run a WiFi scan and BLE scan, verify the target radio appears, and run **Test Connection** with the BLE PIN.
  - Save configuration and reboot into Normal Mode.
- [ ] **Normal Bridge Operation (BLE <-> TCP):**
  - Confirm the status LED transitions to **Solid On** once connected to WiFi and the Meshtastic radio.
  - Verify mDNS discovery (`<radio_name>-bridge.local`) and connect via a Meshtastic client (iOS, Android, Web, or Python CLI) on TCP port `4403`.
  - Confirm initial `want_config_id` download completes cleanly and two-way packets (`FromRadio` / `ToRadio`) flow without drops.
- [ ] **Runtime BLE Deactivate / Activate Control:**
  - Open the WebUI in Normal Mode (`http://<bridge>.local`), click **Deactivate** under Bluetooth Configuration, confirm BLE disconnects cleanly, run a BLE scan without contention, and click **Activate** to resume bridging without rebooting.
- [ ] **MQTT Gateway / Proxy & TLS Verification:**
  - Enable the MQTT Gateway with **Auto-Sync** from the radio (`ModuleConfig.mqtt`).
  - Verify TLS connection (e.g., `mqtt.meshtastic.org:8883` using the embedded CA bundle) reaches `Connected` state and proxies uplink/downlink packets.
- [ ] **Diagnostics & Prometheus Telemetry:**
  - Verify the WebUI live metrics bars (`GET /status`) update properly.
  - Verify `curl http://<bridge>.local/metrics` returns valid Prometheus / OpenMetrics output with zero invalid/dropped queue anomalies.

---

## 5. Cutting the Release

Once all maintenance PRs are merged to `main` and smoke testing passes (see [docs/development.md](development.md#9-releasing-a-new-version) for background details):

1. **Ensure `main` is clean and up to date:**
   ```bash
   git checkout main
   git pull origin main
   ```
2. **Tag and push the SemVer release:**
   ```bash
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```
3. **Verify the Draft Release Workflow (`.github/workflows/release.yml`):**
   - Wait for the **Draft Release** GitHub Action to complete.
   - Confirm it injects `FIRMWARE_VERSION` and `COPYRIGHT_YEAR` into `include/build_options.h`, compiles all board profiles, and attaches the `firmware-<board>-vX.Y.Z.zip` archives to a new Draft Release.
4. **Publish the Release:**
   - Open **Releases** on GitHub, edit the generated Draft Release, review/curate the release notes (highlighting dependency updates, CA bundle refresh, new features, and bug fixes), and click **Publish Release**.
5. **Verify the Web Flasher Deployment (`.github/workflows/pages.yml`):**
   - Publishing the release automatically triggers the **Deploy Web Flasher to GitHub Pages** workflow.
   - Once complete, open [https://sourcelaborg.github.io/meshtastic-esp32-bt-bridge/](https://sourcelaborg.github.io/meshtastic-esp32-bt-bridge/) and confirm it displays `Firmware Version: vX.Y.Z` and loads the board manifests properly.
