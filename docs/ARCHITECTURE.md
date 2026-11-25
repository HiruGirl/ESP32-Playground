# Architecture

ESP32 Playground layers a virtual-hardware bridge onto Espressif's QEMU fork
and drives it from a Node backend with a React UI on top.

## 1. QEMU: patched Espressif fork

Base: `espressif/qemu` @ `093b0c3` (QEMU 9.2-based, adds `esp32`/`esp32s3`/
`esp32c3` machines). `qemu-patches/espressif-qemu-espplay.patch` plus the
`qemu-patches/overlay/` sources add the **espplay** device family:

| File | Role |
| --- | --- |
| `espplay_bridge.c` | `espplay.bridge` sysbus device: chardev socket framing, command dispatch, packet pump |
| `espplay_core.c` | shared state, ELF symbol table, virtual sensor/I2C slave models |
| `espplay_hooks.c` | firmware hook table + injected-call (qcall) machinery |
| `espplay_wifi.c` | WiFi sim: scan/connect state, event queue, TX/RX frame path |
| `espplay_ble.c` | BLE controller: VHCI hooking, HCI transport over the bridge |

Machine/device patches wire the device in (`hw/xtensa/esp32.c`), fix GPIO
register offsets, UART passthrough, LEDC, and route `cpu-exec.c` debug
exceptions into `espplay_debug_check()`.

### The bridge wire

QEMU's `espplay.bridge` connects over a TCP chardev socket to the backend.
Frames are `u32le length + u8 type + payload`. Message kinds (see
`backend/src/protocol.ts` and `include/hw/misc/espplay_bridge.h`):

- guest→host: `GPIO_LEVEL 0x02`, `GPIO_MODE 0x03`, `LEDC 0x04`, `RMT_FRAME 0x05`,
  `I2C_WRITE 0x06`, `UART_DATA 0x07`, `WIFI_TX 0x08`, `HCI_UP 0x0a`,
  `WIFI_SCAN 0x0b`, `WIFI_CONNECT_REQ 0x0c`, `ADC_READ 0x0e`, `DHT22_DONE 0x0f`,
  `LOG 0x10`, `DISPLAY_FB 0x11`, `LCD_TEXT 0x12`
- host→guest: `PING 0x80`, `GPIO_INPUT 0x81`, `ADC_VALUE 0x82`,
  `SENSOR_CFG 0x83`, `I2CDEV_ATTACH 0x84`, `WIFI_CONFIG 0x85`,
  `WIFI_RESULT 0x86`, `WIFI_SIM 0x87`, `HCI_DOWN 0x88`, `WIFI_EVENT 0x0d`,
  `UART_INPUT 0x89`, `STATE_RESET 0x8a`

### Firmware hooks and the qcall machinery

When a session starts, the guest ELF is parsed and a hook table is armed:
breakpoints are placed on key firmware symbols (`esp_wifi_*`,
`esp_vhci_host_*`, `esp_bt_controller_*`, `esp_event_post`, …). Guest code
that hits a breakpoint is handled in `espplay_debug_check()` (driven from the
TCG cpu-exec loop on `EXCP_DEBUG`); hooks emulate the call and return through
the windowed-call ABI.

**Windowed-call conventions (Xtensa):** calls are injected as `call8` frames —
RA in `a0`'s window slot `regs[8]` = `(2<<30) | (cave & 0x3FFFFFFF)`, args in
`regs[10..15]`, return values read from `regs[10..11]` after `retw` rotates
back. The injected frame sets `PS_CALLINC=2` (bits 16-17), `INTLEVEL=15`, and
a single-bit `WINDOWSTART` for the committed base. A small code "cave"
(`cave-addr`, default `0x400A0000`) marks the injected-call return point.

**qcall state machine:** `pending → active → finish`. A request schedules the
frame setup exactly once (`async_run_on_cpu`); further requests while busy are
staged (one follow-up slot); `finish` restores the exact saved lazy-window
state (pre-commit `WINDOWBASE`, `WINDOWSTART`, `PS`, registers, phys regs) and
invokes the completion callback, which may chain the next qcall.

### Event delivery into the guest

WiFi/IP lifecycle events reach the firmware through the real
`esp_event_post()` call, injected as qcalls. Payloads (e.g. the 20-byte
`ip_event_got_ip_t`) are written just below the live stack pointer and passed
by reference. Events are queued and pumped one at a time (see
`espplay_wifi.c`), so guest event handlers can safely call hooked firmware
functions (e.g. `esp_wifi_connect` from the STA_START handler).

### WiFi data path

TX: lwIP → `esp_wifi_internal_tx` hook → `0x08 WIFI_TX` → backend → QEMU
user-net (slirp). RX: slirp → `espplay_wifi_frame_rx` → `0x09`-style callback
into the guest rxcb. The backend `WifiSim` serves scan results, connection
state and DHCP lease info (`10.0.2.15/24`, gw `10.0.2.2`, dns `10.0.2.3`).

### BLE path

`espplay_ble.c` intercepts `esp_vhci_host_*` / `esp_bt_controller_*`; HCI
packets ride the bridge as `HCI_UP 0x0a` / `HCI_DOWN 0x88`. The backend
`BleController` implements a minimal LE controller + ATT server ("Virtual
Phone"): advertising, connection, GATT discovery, subscribe/notify.

## 2. Backend (Node/TypeScript)

`backend/src/`:

| Module | Role |
| --- | --- |
| `server.ts` | Express REST + WS fan-out, session lifecycle |
| `qemu.ts` | per-session QEMU process: flash copy, bridge listener, chardev |
| `protocol.ts` | wire constants + incremental frame parser |
| `wifi.ts` | virtual APs, scan/connect simulation, DHCP lease |
| `ble.ts` | HCI controller + ATT/GATT Virtual Phone |

Sessions own: a copied flash image (NVS persists across soft resets in one
session), the QEMU child process, the bridge socket, and a WS broadcast set.
The backend resolves the QEMU binary from `work/espressif-qemu/build/` (or
`QEMU_PATH`).

## 3. Frontend (React + Vite)

Board-oriented panels: wiring diagram + live pin states, serial console,
WiFi manager (scan/join), BLE phone, logic analyzer, I2C device attach,
firmware flasher, and an onboarding tour. All state arrives over the session
WebSocket; controls map to REST/WS messages listed in [API.md](API.md).

## 4. Build system

- `scripts/build-deps.sh` — host libraries from source into `work/deps`
  (pkgconf, zlib, libffi, pcre2, glib, gpg-error, gcrypt)
- `scripts/build-qemu.sh` — clone/patch/build QEMU; subproject mirrors pinned
  to GitHub (gitlab hosts are unreachable from the build sandbox)
- `scripts/build-examples.sh` — rebuilds all example images (needs the
  `xtensa-esp32-elf` toolchain and `esp-idf`/`arduino-esp32` in `work/`)
