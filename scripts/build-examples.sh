#!/usr/bin/env bash
# Build all example firmware projects with ESP-IDF and produce merged flash
# images in examples/<name>/prebuilt/.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export IDF_PATH="$ROOT/work/esp-idf"
export IDF_TOOLS_PATH="$HOME/.espressif"
export ARDUINO_ESP32_DIR="${ARDUINO_ESP32_DIR:-$ROOT/work/arduino-esp32}"
export PATH="$HOME/.local/bin:$ROOT/work/toolchains/xtensa-esp32-elf/bin:$ROOT/work/toolchains/xtensa-esp32s3-elf/bin:$ROOT/work/toolchains/riscv32-esp-elf/bin:$PATH"

python3 "$ROOT/scripts/genfont.py" "$ROOT/examples/oled_hello/main/font8x8_basic.c"

BUILD="$ROOT/work/build-examples"
mkdir -p "$BUILD"

build_one() { # name target
  local name="$1" target="$2"
  echo "=== building $name ($target)"
  rm -rf "$BUILD/$name"
  mkdir -p "$BUILD/$name"
  cp -r "$ROOT/examples/$name/." "$BUILD/$name/"
  # Playground config as sdkconfig.defaults (survives set-target, used at
  # first configure). NimBLE host + 1kHz tick (required by arduino-esp32).
  cat > "$BUILD/$name/sdkconfig.defaults" <<'CFG'
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y
CONFIG_FREERTOS_HZ=1000
CFG
  if ! (cd "$BUILD/$name" \
      && python3 "$IDF_PATH/tools/idf.py" set-target "$target" >"$BUILD/$name.log" 2>&1); then
    echo "FAILED $name ($target) (set-target)"
    tail -5 "$BUILD/$name.log"
    return 1
  fi
  # Apply playground config after set-target (set-target regenerates sdkconfig).
  if ! (cd "$BUILD/$name" \
      && python3 "$IDF_PATH/tools/idf.py" build >>"$BUILD/$name.log" 2>&1); then
    echo "FAILED $name ($target)"
    grep -E "error:|fatal|undefined ref|CMake Error" "$BUILD/$name.log" | head -5
    return 1
  fi
  local pre="$ROOT/examples/$name/prebuilt"
  if [ "$target" != "esp32" ]; then pre="$pre/$target"; fi
  mkdir -p "$pre"
  python3 -m esptool --chip "$target" merge_bin -o "$pre/firmware.bin" --fill-flash-size 2MB \
    0x1000 "$BUILD/$name/build/bootloader/bootloader.bin" \
    0x8000 "$BUILD/$name/build/partition_table/partition-table.bin" \
    0x10000 "$BUILD/$name/build/$name.bin" >/dev/null 2>&1
  cp "$BUILD/$name/build/$name.elf" "$pre/firmware.elf"
  echo "  -> $pre/firmware.bin"
}

if [ "${1:-}" = "--one" ]; then
  build_one "$2" "${3:-esp32}"
  exit $?
fi

for name in blink button_led pot_servo oled_hello bme280_reader neopixel_rainbow \
            wifi_scan wifi_http_client wifi_webserver_led ble_uart_echo ble_notify \
            mqtt_publisher arduino_blink arduino_wifi_led; do
  build_one "$name" esp32 || true
done

# Extra chip coverage: blink on ESP32-S3 and ESP32-C3.
build_one blink esp32s3 || true
build_one blink esp32c3 || true

echo "ALL EXAMPLES DONE"
