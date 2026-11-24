# Release Notes — v1.0.0 (2026-11-23)

First complete release of ESP32 Playground.

## Highlights

- **Patched Espressif QEMU** with the `espplay` virtual-hardware family:
  GPIO (+LEDC/RMT), UART, I2C virtual sensors/displays, WiFi (STA/SoftAP +
  frame pump into slirp user-net), BLE (NimBLE over virtual HCI).
- **Firmware hook + qcall machinery**: symbol-based hooks emulate firmware
  entry points and inject calls (windowed-call ABI) into the guest —
  including `esp_event_post` event delivery with guest-stack payloads.
- **Node backend**: multi-session QEMU manager, bridge protocol parser,
  virtual WiFi APs + DHCP, BLE controller + ATT "Virtual Phone", REST + WS API.
- **React frontend**: wiring view, serial console, WiFi manager, BLE phone,
  logic analyzer, firmware flasher, onboarding.
- **14 example projects** with prebuilt images (ESP-IDF + Arduino cores).
- **End-to-end test suite** (`tests/e2e.mjs`): blink timing, interactive
  button, SSD1306 framebuffer, WiFi join/DHCP, BLE advertise/connect/
  discover/notify.

## Packaging

- `dist/esp32-playground-qemu-<ver>-linux-x86_64.tar.gz` — prebuilt
  `qemu-system-xtensa` + `qemu-system-riscv32` (see SHA256SUMS)
- `dist/esp32-playground-backend-<ver>.tar.gz` — backend bundle
  (run `npm install && npm run build` for the bundled TypeScript)
- `dist/SHA256SUMS` — checksums for both archives

Release artifacts are uploaded to filebin via the GitHub Actions relay in
`.github/workflows/` (build sandboxes cannot reach filebin directly).

## Known limitations

See [docs/LIMITATIONS.md](docs/LIMITATIONS.md): most notably, a second
injected call in one context can hit a windowed-call state bug (guest TCP
fetch flakiness; hard-asserted e2e coverage is unaffected), and BLE e2e needs
a long timeout because NimBLE boot is slow under icount emulation.
