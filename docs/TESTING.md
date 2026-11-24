# Testing

## End-to-end suite

`tests/e2e.mjs` drives the full stack: it starts sessions against the running
backend (`http://127.0.0.1:8760`), flashes prebuilt firmware, and asserts on
bridge/WS/UART behavior.

```bash
# start the backend first
cd backend && node dist/server.js &

# run everything
node tests/e2e.mjs

# run a subset by name
node tests/e2e.mjs blink button_led
node tests/e2e.mjs wifi
node tests/e2e.mjs ble
```

Exit code is 0 when every recorded check passes. Warnings (`WARN …`) do not
fail the run; they mark known limitations (see
[LIMITATIONS.md](LIMITATIONS.md)).

## What each test asserts

| Test | Checks |
| --- | --- |
| `blink` | GPIO2 toggles at the expected cadence (≥20 events, alternating levels) |
| `button_led` | Host-driven GPIO0 press makes the guest drive GPIO2 |
| `oled` | SSD1306 framebuffer arrives over the bridge with nonzero pixels |
| `wifi` | STA joins the simulated AP, DHCP state `10.0.2.15` (UART `ip=` best-effort); HTTP fetch is warn-only |
| `ble` | NimBLE advertising, Virtual Phone connect, GATT discovery, notify |

Runtime notes:

- WiFi and BLE sessions boot with `icount` clamped to 10 (the esp32 machine
  rejects larger shifts) — NimBLE boot is slow in emulation; the suite allows
  up to 10 minutes for advertising.
- The suite needs the QEMU build (`work/espressif-qemu/build/qemu-system-*`)
  and `backend/node_modules` installed.

## Bridge smoke test

`scripts/test-bridge-blink.py` is a standalone Python smoke test for the raw
bridge protocol (no backend): it launches QEMU directly and checks GPIO
frames on the chardev socket.

## Continuous integration

`.github/workflows/` contains the toolchain-cache relay used by the build
sandbox. The release flow (see [RELEASE_NOTES.md](RELEASE_NOTES.md)) packages
`dist/` tarballs and uploads them through a GitHub Actions filebin relay.
