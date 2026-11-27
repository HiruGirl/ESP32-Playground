# ESP32 Playground

A complete, self-contained ESP32 development playground: run real compiled
ESP32 firmware inside a patched Espressif QEMU, wired to virtual GPIO, UART,
I2C sensors/displays, WiFi and BLE through a live browser UI.

```
┌────────────┐   WebSocket/REST   ┌──────────────┐   espplay bridge   ┌───────────────────┐
│  React UI  │ ◄────────────────► │ Node backend │ ◄────────────────► │ QEMU (patched)    │
│  (Vite)    │                    │ session mgr  │                    │ esp32 + espplay   │
└────────────┘                    └──────────────┘                    │ GPIO/UART/I2C/    │
                                      │  virtual WiFi APs            │ sensor/WiFi/BLE   │
                                      │  BLE virtual phone           └───────────────────┘
                                      ▼
                                  user-mode TCP/IP (slirp)
```

## Features

- **GPIO** — pin levels, modes, LEDC PWM, RMT frames; interactive buttons,
  potentiometer, WS2812/NeoPixel view
- **UART** — full console passthrough plus per-UART bridging
- **I2C + sensors** — attach virtual devices: SSD1306 OLED, BME280, AHT10,
  SHT31, BH1750, MPU6050, DHT22, ADS1115, PCF8574 (9 device kinds)
- **WiFi** — STA scan/join against simulated APs, DHCP lease (user-net
  10.0.2.15/24), TX/RX frame pump into QEMU's slirp user-net stack
- **BLE** — NimBLE over a virtual HCI (VHCI): advertising, connection,
  GATT discovery and notify against the built-in Virtual Phone
- **Firmware** — flash `.bin`/`.elf` images per session; symbols from the ELF
  arm the hook table automatically
- **Sessions** — multiple isolated VMs with their own flash image, WS event
  streams (UART, GPIO, WiFi, BLE, logs) and a REST control API
- **14 prebuilt examples** — ready to run with one click

## Quick start

```bash
# 1. Native deps + patched QEMU (once; needs meson, ninja, pip)
python3 -m pip install --user meson ninja
scripts/build-deps.sh          # glib, zlib, libffi, pcre2, gcrypt → work/deps
scripts/build-qemu.sh          # clone espressif/qemu, patch, build

# 2. Backend
cd backend && npm install && npm run build && node dist/server.js
# → http://127.0.0.1:8760

# 3. Frontend (dev)
cd frontend && npm install && npm run dev
```

Prebuilt firmware for every example lives in `examples/*/prebuilt/`, so no
ESP-IDF toolchain is needed to try the playground. To rebuild examples, see
[docs/EXAMPLES.md](docs/EXAMPLES.md).

## Repository layout

| Path | Contents |
| --- | --- |
| `qemu-patches/` | Espressif QEMU patch + `espplay` device overlay sources |
| `scripts/` | Reproducible build scripts (deps, QEMU, examples) + font tool |
| `backend/` | Node/TypeScript session manager, bridge protocol, WiFi/BLE sims |
| `frontend/` | React + Vite UI (wiring view, serial, WiFi, BLE phone, analyzer) |
| `examples/` | 14 example projects with sources and prebuilt images |
| `tests/` | End-to-end test suite (`node tests/e2e.mjs`) |
| `docs/` | Architecture, API, examples, testing, limitations |
| `work/` | Build scratch: QEMU checkout, toolchains, deps (gitignored) |

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the pieces fit together
- [docs/API.md](docs/API.md) — REST + WebSocket API reference
- [docs/EXAMPLES.md](docs/EXAMPLES.md) — the example projects
- [docs/TESTING.md](docs/TESTING.md) — running the e2e suite
- [docs/LIMITATIONS.md](docs/LIMITATIONS.md) — known limitations and issues

## Chip Coverage Support Matrix

| Chip | Architecture | QEMU Machine | Per-CPU Reset | eFuse Drive | SD Card | TFT | Touch | Radio Shim | Examples |
|------|-------------|--------------|---------------|-------------|---------|-----|-------|------------|----------|
| ESP32 | Xtensa | `esp32` | ✅ CPU0+CPU1 | ✅ | ✅ (spi bus 2/3) | ✅ (spi bus 2) | ✅ (spi bus 3) | ✅ tiered | blink, ble_notify, ble_uart_echo, arduino_wifi_led, ... |
| ESP32-S3 | Xtensa | `esp32s3` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |
| ESP32-C3 | RISC-V | `esp32c3` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |
| ESP32-C5 | RISC-V | `esp32c5` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |
| ESP32-C6 | RISC-V | `esp32c6` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |
| ESP32-C61 | RISC-V | `esp32c61` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |
| ESP32-H2 | RISC-V | `esp32h2` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |
| ESP32-H4 | RISC-V | `esp32h4` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |
| ESP32-P4 | RISC-V | `esp32p4` | ⚠️ partial | ❌ | ❌ | ❌ | ❌ | ❌ | — |

Legend: ✅ fully supported, ⚠️ QEMU machine exists but no per-CPU reset fix or board-level peripherals, ❌ not supported.

Per-CPU reset fix (CPU0+CPU1 starting from ROM reset vector 0x40000400, stall gated on DPORT bits) is implemented for ESP32 and exported as patches in `qemu-patches/` pinned to upstream commit `1b75d0fd51`.

eFuse drive support is implemented for ESP32, required by ROM to get past "Using external SPI configuration..." during boot.

The tiered radio shim (`none`\|`ble`\|`wifi`\|`both`) is opt-in and driven by the signature database at `work/radio-sig-db.json` (19 examples, IDF v4.4.7). It NOPs radio controller init call sites via `scripts/radio-shim.py`.

## License

MIT — see [LICENSE](LICENSE). Third-party components are listed in
[THIRD_PARTY.md](THIRD_PARTY.md); QEMU itself remains GPLv2.
