# Example Projects

Fourteen ready-to-run firmware projects. Every project ships sources plus a
merged flash image in `prebuilt/` (`firmware.bin` + `firmware.elf`), so the
playground works out of the box with no ESP-IDF toolchain.

| Example | Board focus | What it shows |
| --- | --- | --- |
| `blink` | GPIO2 LED | The classic: timed GPIO toggle, verified by the logic analyzer |
| `button_led` | GPIO0 button | Interactive input: press the on-screen button, the LED follows |
| `pot_servo` | ADC + LEDC | Potentiometer slider drives a servo's PWM duty |
| `oled_hello` | I2C SSD1306 | Text + pixel drawing on a virtual 128×64 OLED |
| `bme280_reader` | I2C BME280 | Periodic temperature/humidity/pressure from a virtual sensor |
| `neopixel_rainbow` | RMT / WS2812 | Addressable LED ring rendered from RMT frames |
| `wifi_scan` | WiFi STA | Scans the simulated APs, prints results |
| `wifi_http_client` | WiFi STA + TCP | Joins an AP, gets a DHCP lease, fetches a page over user-net |
| `wifi_webserver_led` | WiFi SoftAP | Serves a tiny web page that toggles the LED |
| `mqtt_publisher` | WiFi + MQTT | Publishes sensor readings to an MQTT broker |
| `ble_notify` | BLE (NimBLE) | Advertises, Virtual Phone connects, notifications stream |
| `ble_uart_echo` | BLE NUS | Nordic-UART-style echo service over GATT |
| `arduino_blink` | Arduino core | Same blink on the arduino-esp32 framework |
| `arduino_wifi_led` | Arduino WiFi | Web-controlled LED with the Arduino core |

## Running an example

1. Open the UI (or use the REST API):
   `POST /api/sessions` with `firmware: examples/<name>/prebuilt/firmware.bin`
   and `elf: examples/<name>/prebuilt/firmware.elf`
2. Watch the `uart` stream for the banner; interact via the board panels.

Quick API example:

```bash
curl -s -X POST localhost:8760/api/sessions -H 'content-type: application/json' \
  -d "{\"firmware\":\"$PWD/examples/blink/prebuilt/firmware.bin\",\"elf\":\"$PWD/examples/blink/prebuilt/firmware.elf\",\"icount\":10}"
```

## Rebuilding from source

`scripts/build-examples.sh` rebuilds every project. Requirements (placed in
`work/` by the toolchain scripts):

- `work/toolchains/xtensa-esp32-elf/` — GCC 8.4 xtensa toolchain
- `work/esp-idf/` — esp-idf v4.4.7 (ESP-IDF projects)
- `work/arduino-esp32/` — arduino-esp32 2.0.17 (Arduino projects)

Notes:

- All projects build with `sdkconfig.defaults` targeting 2MB flash; the
  merged image is written to `prebuilt/firmware.bin` alongside the ELF.
- `examples/` shared helpers (e.g. the SSD1306 font) come from
  `scripts/genfont.py`.
- The BLE examples use NimBLE (host) over the emulated VHCI controller.
