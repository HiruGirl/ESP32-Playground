# ESP32 Playground — Progress Checklist

Working order per mission: research/decisions → QEMU build and boot →
peripheral bridge → UI/components → WiFi → BLE → examples → tests →
packaging → upload → stop.

## Phase 0 — Research & decisions
- [x] Survey sandbox: no apt; network limited to github.com/codeload/api.github.com,
      registry.npmjs.org, pypi.org, files.pythonhosted.org
- [x] Espressif QEMU fork cloned (QEMU 9.2.2, commit 093b0c3), machines esp32/s3/c3(+c5/c6/h2/p4)
- [x] Native deps built from source (pkgconf, zlib, libffi, pcre2, glib 2.78, libgpg-error, libgcrypt)
- [x] WiFi seam decision (esp_wifi API hooking in QEMU + frame bridge to slirp) — see docs/DECISIONS.md
- [x] BLE seam decision (VHCI function hooking + virtual HCI controller in backend)
- [x] Toolchain acquisition decision (GitHub Actions relay caches Espressif gcc 8.4 toolchains as a git tag)

## Phase 1 — QEMU build and boot
- [ ] QEMU builds (xtensa-softmmu, riscv32-softmmu)
- [ ] hello_world (ESP-IDF) boots on esp32 in QEMU
- [ ] qemu-patches/ with reproducible build script

## Phase 2 — Peripheral bridge (QEMU patches)
- [ ] espplay chardev bridge device (GPIO/LEDC/ADC/RMT/I2C/UART events + control)
- [ ] GPIO model hook + input injection
- [ ] ADC model, LEDC observation, RMT TX decode (NeoPixel)
- [ ] I2C slave framework + BME280/MPU6050/DS3231/SSD1306/PCF8574
- [ ] DHT22 + HC-SR04 wire devices
- [ ] qcall framework (ELF-symbol function hooks, synthetic calls into firmware)

## Phase 3 — Backend + UI
- [ ] Node/TS backend: QEMU supervisor, WS API, project files (.espplay.json)
- [ ] React/Vite frontend: board visual, serial monitor, wiring editor, component palette
- [ ] GPIO logic analyzer + VCD export
- [ ] Snapshots, NVS persistence, multi-session cleanup

## Phase 4 — WiFi
- [ ] esp_wifi hook set + event delivery (STA scan/connect/DHCP/DNS/RSSI)
- [ ] Virtual Router panel (APs, password, latency/loss, disconnects)
- [ ] Inbound web server port forwarding + SoftAP virtual client
- [ ] pcap export of WiFi traffic

## Phase 5 — BLE
- [ ] VHCI hooks + esp_bt_controller stubs
- [ ] Virtual HCI controller (GAP/GATT/L2CAP/SMP Just Works) in backend
- [ ] Virtual Phone panel + virtual peripherals
- [ ] Classic BT status documented

## Phase 6 — Examples
- [ ] 12 example projects with self-built prebuilt firmware + wiring files

## Phase 7 — Tests
- [ ] Unit tests (protocol/bridge/device models)
- [ ] E2E tests running real firmware in QEMU (blink rate, button→LED, OLED pixels, WiFi IP+fetch, BLE notify)

## Phase 8 — Docs & licenses
- [ ] docs/ARCHITECTURE.md, docs/USER_GUIDE.md, docs/DECISIONS.md, docs/LIMITATIONS.md
- [ ] LICENSE, NOTICE/THIRD_PARTY_LICENSES (QEMU GPL source offer)

## Phase 9 — Packaging
- [ ] dist/ linux bundle + start.sh (verified in clean environment)
- [ ] Docker image tar + Dockerfile + docker-compose.yml
- [ ] source tarball + SHA256SUMS.txt + RELEASE_NOTES.md + README.md

## Phase 10 — Upload & stop
- [ ] Upload dist/ to filebin.net (direct, or GitHub Actions relay if blocked)
- [ ] Verify by re-download + checksum
- [ ] Final message and STOP
