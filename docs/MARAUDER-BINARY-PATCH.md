# Marauder binary function-entry patching — method, findings, blocker

This document describes the **binary function-entry patching** approach applied to the
release Marauder firmware (`work/firmware/marauder-4mb.bin`, untouched) to keep the
simulated ESP32 running, and the hard blocker that prevents a full boot to the main menu
in this QEMU stack.

## Scope decision (per user direction)

Source rebuild is infeasible here (no platformio/arduino-cli/arduino-esp32 core,
`platformio.ini` only has a `native` test env, git submodules unchecked out). Per the
revised goal, the approach is **binary function-entry patching (option 2)**:

* make BLE / Wi-Fi init chains return early,
* watchdog enabled,
* `SIMULATION BUILD` marker.

The release binary is **never modified or committed**; patches are applied to scratch
copies in `work/` and recomputed checksums/hashes are documented here.

## What the release binary contains

The merged 4MB flash (`marauder-4mb.bin`) is a raw image (not ELF), so it has no usable
symbol table. Symbol addresses were recovered by matching known ESP-IDF 4.4.7 library
code against byte patterns in the flash (see `work/match_symbols.py`,
`work/match_spin.py`) and by reading live guest memory via QEMU GDB (`m` packet returns
ASCII-encoded hex bytes; Xtensa is little-endian so objdump shows instruction bytes
reversed vs. memory order).

Confirmed locations in `marauder-4mb.bin`:

| symbol | vaddr | file offset | notes |
|--------|-------|-------------|-------|
| `esp_wifi_init_internal` | `0x4012e2e8` | `0x12e2e8` | matched from `ieee80211_api.o`; internal, no string ref |
| `esp_bt_controller_init` call site | `0x4010879f` | `0x8879f` | `call8 0x4011bb38` |
| `esp_bt_controller_enable` call site | `0x401087bb` | `0x887bb` | `call8 0x400f57b8` |
| `esp_dport_access_stall_other_cpu` entry | `0x40098520` | `0x198fd8` | `subx8 a1,a13,a0` |
| spin branch inside that fn | `0x40098569` | `0x199021` | `beq a4,a8,0x4009855f` |

`esp_wifi_init` has no string reference (purely internal). `NimBLEDevice::init`
string at `0x22634` references `esp_bt_controller_init(&bt_cfg)`.

## Patch encoding

Xtensa encodings (verified with `xtensa-esp32-elf-as`, memory order == file order):

| instruction | bytes | meaning |
|-------------|-------|---------|
| `ret.n` | `0d f0` | return to `a0` (valid at function entry; `a0` holds return addr) |
| `movi a10, 0` | `a2 a0 00` | force return value `ESP_OK`, fall through |
| `j 0x400985e9` | `46 1e 00` | unconditional jump to function exit |

The `bt-skip2.bin` diagnostic patch (derived in earlier work) replaces the two BLE
init `call8`s with `movi a10, 0` so the caller thinks controller init/enable returned
`ESP_OK`, and recomputes the SHA256 image hash at file offset `0x19cecf`:

* `0x8879f`: `a53913` (`call8 esp_bt_controller_init`) → `a2a000` (`movi a10,0`)
* `0x887bb`: `a501ed` (`call8 esp_bt_controller_enable`) → `a2a000` (`movi a10,0`)
* `0x19cecf`: SHA256 recomputed (33 bytes)

QEMU does **not** enforce the ESP32 app-image checksum, so a patched image boots
without recomputing the hash (verified empirically). The hash recompute is only for
integrity tracking.

## What was attempted

1. **Patch `esp_dport_access_stall_other_cpu` entry to `ret.n`** (`0x198fd8`: `00 1d f0`
   → `0d f0`). Encoding verified correct (`0d f0` decodes as `ret.n` at `0x40098520`).
   Result: guest reboots in a loop (`rst:0x3 SW_RESET`) before even printing the
   ESP-IDF version — the stall is required for correct early dual-core clock bring-up,
   so returning immediately crashes bring-up.

2. **Patch the spin branch to an unconditional exit** (`0x199021`: `87 14 f2`
   → `46 1e 00`). Result: same immediate reboot loop. Leaving the stall flag set or
   exiting the synchronization primitive early breaks bring-up.

3. **`bt-skip2.bin` (BLE init call sites → `movi a10,0`)** alone: UART gets past the
   ROM bootloader, prints `ESP-IDF version is: v4.4.5` and `Using external SPI
   configuration...`, then stalls at `0x4009852c`. So skipping BLE init does **not**
   fix the early hang — the stall happens before BLE init.

4. **`bypass-ipc.bin` (BLE skip + IPC stall bypass)** alone: identical stall at
   `0x4009852c` in this QEMU build.

## The hard blocker: dual-core stall, CPU1 never started

Every variant stalls at `0x4009852c`, inside `esp_dport_access_stall_other_cpu` (the
cross-core stall/IPC spin). HMP sampling shows:

* CPU0 solidly at `0x4009852c` / `0x40098569` (spin waiting on the stall flag,
  interrupts masked at level 3, so the watchdog cannot fire either).
* CPU1 in ROM idle/delay loops at `0x4000854a` / `0x40008540` region, never executing
  IDF code, so it never acknowledges the stall.

Root cause (confirmed in `work/espressif-qemu/hw/xtensa/esp32.c`):

* `esp32_v3_machine_init` writes CPU0's reset vector (`0x40000400`) and sets
  `ss->cpu[0].env.pc`.
* CPU1's ROM (`esp32-v3-rom-app.bin`) is loaded into CPU1's address space, but
  `ss->cpu[1].env.pc` is **never initialized**. `esp32_cpu_reset` resets CPU1, but it
  idles in ROM forever.

`esp_dport_access_stall_other_cpu` sets a SRAM stall flag and spins waiting for CPU1
to clear it. CPU1 never runs IDF code, so it never clears the flag → permanent deadlock
right after `Using external SPI configuration...`, before any UI, radio, SD, or touch
code runs.

### Why binary patching cannot fix it

Patching `esp_dport_access_stall_other_cpu` to not stall breaks the dual-core
bring-up that genuinely requires the synchronization (early clock configuration), and
reboots before `ESP-IDF version` is printed. There is no app-only byte patch that makes
CPU1 respond to the stall.

### What is needed to unblock (outside binary-patching scope)

A QEMU-side shim: start CPU1 running IDF code so it services the stall flag /
`esp_ipc_isr_handler`. Concretely, in `hw/xtensa/esp32.c` `esp32_v3_machine_init`,
initialize CPU1 to run the app's CPU1 entry (`call_start_cpu1`) with a stack, replicating
the ROM's CPU1 bring-up. This is **not** a binary function-entry patch and is explicitly
out of scope per the user's direction; it is documented here honestly as the required
shim.

Until CPU1 is started, Marauder cannot reach `app_main`, so none of the acceptance
criteria can be demonstrated in the real release firmware:

1. 60 s run without reboot — BLOCKED (deadlocks before `app_main`).
2. Main menu framebuffer — BLOCKED (no UI reached).
3. Touch injection → UI change — BLOCKED.
4. SD mount + write/readback — BLOCKED in Marauder (the standalone SD SPI guest
   read/write/readback test in `tests/sd-spi.mjs` DOES pass; only Marauder integration
   is blocked).
5. Script + README + evidence — this document + script below.

## Reproducible script

`work/apply-marauder-patches.sh` assembles the patch copies from the untouched release
binary and documents each patch. It does not commit anything; the release binary is
never touched.

## Shim honesty

Per the user's instruction, simulation shims are documented honestly:

* **BLE / Wi-Fi init**: return early via `movi a10, 0` at the call sites
  (`esp_bt_controller_init`, `esp_bt_controller_enable`, `esp_wifi_init_internal` at
  `0x4012e2e8`). This avoids the `bt.c:1549` clock assertion once CPU1 is started.
* **Dual-core stall**: cannot be patched in-app; requires starting CPU1 in QEMU (above).
* **Watchdog**: `wdt_disable` defaults to `false` (enabled) in the rebuilt QEMU; the
  guest must feed it or the run will reset — this is part of the 60 s criterion and
  cannot be satisfied until the app boots.
