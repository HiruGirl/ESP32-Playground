# ESP32 Marauder simulation test (2026-11-25)

Goal: run the real **ESP32 Marauder v1.21.0** firmware (CYD 2432S028 build:
ESP32-WROOM + ILI9341 240x320 SPI TFT + SD + XPT2046 touch) in the ESP32
Playground QEMU stack, with TFT video output and other components exercised.

## Firmware acquisition

Release assets are CDN-blocked from the sandbox, so `.github/workflows/
asset-relay.yml` fetches them on a GitHub runner and stages them in the
`firmware-marauder-v1.21.0` tag (run 45589796985):

- `esp32_marauder_cyd_2432S028.bin` (app, sha256 a62cf0b0…)
- `marauder-installer-assets.zip` (bootloader / partition-table / ota-data)

Merged 4MB flash image (QEMU boots raw `if=mtd`), erased-flash fill 0xFF:

| part          | offset   |
|---------------|----------|
| bootloader    | 0x01000  |
| partition-tbl | 0x08000  |
| ota-data      | 0x0e000  |
| app0 (Marauder) | 0x10000 |

## Component matrix (observed in this stack)

| component      | status | evidence |
|----------------|--------|----------|
| ROM boot + stage-2 loader | WORKS | UART: `rst:…, boot:0x12`, `load:` segments, `entry 0x400805e4` |
| app entry / early init    | partial | app reaches esp-idf startup (core-dump init log) then hangs |
| UART console  | WORKS | full boot log captured clean over the bridge |
| TFT ILI9341 (SPI2, DC=2)  | WORKS (model proven with `examples/tft_ili9341`) | pixel-perfect 240x320 render: color bars + checkerboard + 8x8 font text (`work/tft_evidence.png`) |
| GPIO / DC pin tracking    | WORKS | 13k+ GPIO level events, DC-driven command/data classification |
| WiFi / BLE / I2C / ADC / buttons / sensors | WORKS (e2e 4/4) | see docs/TESTING.md |
| SD card (SPI)             | NOT MODELED | no SD/SDMMC device in the espplay bridge |
| Touch (XPT2046)           | NOT MODELED | no input device model |
| SPI DMA (GDMA)            | NOT MODELED | DMA transactions deliver zero bytes; drivers must use PIO (`dma_chan = 0`) |
| Marauder app startup      | HANGS | see below |

## Marauder boot hang (documented limitation)

Marauder's app hangs in early startup before driving any GPIO/SPI; the task
watchdog resets the SoC every ~25 s (guest time). HMP/gdbstub-level tracing
of the ROM shows the CPU looping in the ROM **eFuse double-read verify**
routine (`ets`-family, ROM 0x4000fc90/0x4000fdd5):

- eFuse op sequence (0x5aa5 opcode -> CONF, start -> CMD, poll busy) completes
  correctly (the esp32_efuse model's op timer works).
- The verify passes (the fatal `RTC_CNTL SW_SYS_RST + ill.n` path never
  executes — confirmed with `-d int` exception tracing).
- The routine tail-jumps into the ROM CPU-start stub (0x40000450) and the
  boot chain re-runs; the exact app-side caller loop was not isolated.

Our example firmwares (blink/wifi/… and tft_ili9341) boot fine; Marauder's
startup path exercises ROM-level eFuse validation that our stack does not yet
satisfy. Next steps if pursued: synthesize realistic eFuse BLK0 defaults
(MAC, chip version, calib) in `hw/nvram/esp32_efuse.c`, and trace the ROM
call chain from the app entry to the verify loop.

## Fixes that came out of this test

1. **UART text corruption (pre-existing, now fixed):** the backend parsed
   `UART_DATA` with `subarray(1)` while the wire format is
   `u8 uart, u16 n, bytes` — the u16 length leaked `0x01 0x00` before every
   character in *all* UART output. This was the "qcall2 image has 0x01
   interleaved" mystery: an artifact of our parser, not the firmware.
2. **SPI phantom command bytes (upstream espressif-qemu bug):** the USR
   transaction path emitted the command phase when `COMMAND_BITLEN` was
   merely stale (register reset value), sending 2 zero bytes before every
   pure-data transaction. Gated on `USER.COMMAND` like real hardware.
3. **SPI bus numbering:** `object_get_canonical_path_component` returns NULL
   in `instance_init`; moved to `realize` (flash = bus 1, HSPI = bus 2).
4. **TFT model new:** ILI9341/ST7789 command parser (CASET/RASET/RAMWR/
   MADCTL/COLMOD) with DC-pin classification via the GPIO out path and
   dirty-rect `ESPPLAY_MSG_TFT_FRAME` (0x13) frames on CS deassert.
