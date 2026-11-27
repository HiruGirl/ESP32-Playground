# Known Limitations

## Guest event delivery / TCP fetch is flaky (windowed-call bug)

**Symptom:** a firmware injected call (qcall) completes cleanly the first
time; a *second* qcall in the same context (e.g. `esp_event_post` delivering
`GOT_IP` after the connect result, or `esp_efuse_*` during bring-up) can crash
the guest with a window-exception storm ending in a reboot loop. TCP fetches
(`wifi_http_client`) are the user-visible casualty: STA join and the DHCP
lease (`ip=10.0.2.15`) work, but the subsequent HTTP GET may never complete.

**Crash signature (recorded from QEMU debug logs):**

```
SETUP pc=0x40159bae (esp_efuse_utility_get_number_of_items) WB=4 WS=bit4
EX2 excp=0x5 pc=0x40088ec8 WB=5 WS=0x2a0     <- all-odd window ladder (5,7,9)
EX2 excp=0x5 pc=0x40088ece WB=7 WS=0x280
EX2 excp=0x10004 pc=0x40088d2e WB=10
EX2 excp=0x6 pc=0x40159bae WB=7 WS=0x200 a0=garbage   -> reboot (rst:0x1)
```

A normalized frame (fixed `WINDOWBASE=0`, `WINDOWSTART=1`, cleared regs) was
also executed with the *old* task state (`WB=7 WS=0xaaaa`, PRO-task stack),
proving the setup environment and the executing environment can diverge
(CPU/env handoff between `async_run_on_cpu` setup and entry execution).

**What was ruled out:** windowstart policy (v1/v2/v3 all reproduce), the
`PS_CALLINC` field encoding (fixed; persists), qcall base choice, CPU
selection (`qemu_get_cpu(1)` also fails), and the setup window itself.

**Mitigations in place:**

- pending-flag qcall state machine: exactly one frame setup per request
- single-bit windowstart V2 + `PS_CALLINC=2` at bits 16-17
- events are queued and pumped one at a time so at most one injected call is
  in flight
- e2e treats the HTTP-fetch check as **warn-only**; STA join + DHCP state are
  still hard assertions

This is the top item for future work; the instrumentation breadcrumbs were
removed from the shipped sources but the trace above is preserved here.

## BLE e2e timing (warn-only in the suite)

BLE e2e is **warn-only** in `tests/e2e.mjs` (clean icount-10 runs failed the
10-minute advertising window, consistent with the timing limit below), so a
release run reports it as `WARN` rather than a hard failure.

NimBLE boot in icount emulation is very slow (the esp32 machine rejects
`icount` shifts above 10, and image loading + controller init take several
emulated minutes). The BLE test allows up to 10 minutes for advertising; on
slow hosts it may still time out. The bridge-side BLE stack (VHCI transport,
HCI controller, ATT Virtual Phone) is implemented and protocol-level tested.

## Smaller notes

- **`icount` clamp:** QEMU's esp32 machine only accepts `-icount 0..10`; the
  backend clamps the value, larger requests are silently reduced.
- **`WIFI_RESULT` payload:** the backend sends `[ipLen, ip-ascii]` while the
  bridge comment documents `[ok, rssi, channel, bssid(6), authmode]`. The
  fields after `ok` are interpreted loosely (the guest apps do not depend on
  them). Planned cleanup: align the formats.
- **NVS persistence** is per-session (the flash image copy); it does not
  survive backend restarts.
- **Filebin relay:** outbound network from build sandboxes is restricted to
  package registries and GitHub; release artifacts are uploaded to filebin
  via a GitHub Actions relay workflow rather than directly.

## eFuse boot requirement and reset codes

**eFuse (0x3ff5a000 / DR_REG_EFUSE_BASE):** The ESP32 ROM polls the eFuse
read register at `0x3ff5a104` during bring-up ("Using external SPI
configuration..."). Without a backing eFuse NVRAM drive, reads return 0 and
the ROM poll never exits, hanging the boot. With a valid eFuse image
(512 all-zero bytes) attached via
`-global driver=nvram.esp32.efuse,property=drive,value=efuse`, the ROM poll
exits and boot proceeds. Verified: all-zeros eFuse lets blink boot; all-0xFF
eFuse hangs at ROM (coding_scheme read = 0x3, unsupported). Backend support:
`SessionConfig.efuseImage`, auto-generated as all-zeros `efuse.bin` per
session when not provided.

**ESP32 reset reason codes (`rst:0xN`):** (from RTC_CNTL_RESET_STATE register)
- `0x01` POWERON_RESET — cold boot
- `0x03` SW_RESET — software reset (abort, panic, esp_restart via software)
- `0x07` TG0WDT_SYS_RESET — Timer Group 0 watchdog system reset
- `0x08` TG1WDT_SYS_RESET — Timer Group 1 watchdog system reset
- `0x09` RTCWDT_SYS_RESET — RTC watchdog system reset
- `0x0B` TGWDT_CPU_RESET — Timer Group watchdog CPU reset (single CPU)
- `0x0C` SW_CPU_RESET — software CPU reset (per-CPU, not system-wide)
- `0x0F` BROWNOUT_RESET — brownout detector reset

Common failure modes: `rst:0x3` when BLE controller init asserts
(`esp_bt_controller_init` clock assertion at `bt.c:1549`) or SD mount fails.
The radio shim tier `ble` (via `scripts/radio-shim.py`) NOPs
`esp_bt_controller_init` / `esp_bt_controller_enable` call sites to bypass
this path.

**`0x4000fca9`** is a ROM bootloader PC address, not a register. It appears
in stack traces and backtraces when code executes the ROM bootstrap
(`0x40000400` → `0x4000fca9` → ...). It is the entry point region of the
ESP32 ROM binary, mapped at `0x40000000-0x4000ffff`. Not
`RTC_CNTL_WDW_PROTECT_REG`. QEMU does not include ROM source; gdb backtrace
from the guest would be needed to identify its exact caller (gdb not
available in this build environment).

**CPU1 stall at `0x4000fdd0`**: When CPU1 is not released from the DPORT
controlled stall, it spins in the ROM bootstrap at `0x4000fdd0`. The RTC
watchdog then fires (`rst:0x7`/`0x08`/`0x09`) and resets both CPUs. The
per-CPU reset fix (QEMU patches `3f946e7fd1`, `5544325437`, `8f82313ba9`)
releases CPU1 from the DPORT-controlled stall so it runs the app entry
point instead of spinning in ROM.
