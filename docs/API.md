# API Reference

Base URL: `http://127.0.0.1:8760` (configurable via `PORT`). All request and
response bodies are JSON except firmware upload (raw binary) and the UART
snapshot. The WebSocket endpoint is `/ws?session=<id>`.

## REST

### `GET /api/health`
Liveness probe. `→ { ok: true, qemu: "<path>", version }`

### `GET /api/examples`
List the bundled example projects and their prebuilt images.
`→ [{ id, name, description, firmware, elf, chip }]`

### `POST /api/sessions`
Start a session.

```jsonc
{
  "chip": "esp32",              // "esp32" | "esp32c3"
  "firmware": "/abs/path/firmware.bin",  // must be inside the workspace
  "elf": "/abs/path/firmware.elf",       // optional, arms hooks + symbols
  "icount": 10                  // QEMU icount shift, clamped to 1..10
}
```
`→ { id, chip, state, firmware, uptimeMs }`

### `GET /api/sessions`
`→ [sessionInfo, …]`

### `GET /api/sessions/:id`
Full snapshot: session info + `wifi`, `ble`, `gpio`, `gpioModes`, `ledc`,
`lcdText`, `displays`, `logs`.

### `POST /api/sessions/:id/control`
Body `{ "cmd": "pause" | "resume" | "reset" | "stop" | "start" }`.
Soft reset keeps the session's flash (NVS persists).

### `GET /api/sessions/:id/uart`
`→ { data }` — UART console snapshot.

### `GET /api/sessions/:id/vcd`
VCD dump of recent GPIO activity (logic analyzer).

### `POST /api/upload?name=firmware.bin`
Raw binary body; stored under the data dir. `→ { path, size }`

### `DELETE /api/sessions/:id`
Stop QEMU and remove the session. `→ { ok: true }`

## WebSocket `/ws?session=<id>`

Server→client events (JSON per message):

| `kind` | Payload highlights |
| --- | --- |
| `hello` | session info |
| `uart` | `data` — console bytes |
| `gpio` | `pin`, `level`, `mode` |
| `ledc` | channel/duty/freq |
| `rmt` | frame bytes |
| `i2c` | bus transactions |
| `display` / `lcd` | framebuffer / text |
| `wifi` | WifiSim state (scanning, aps, connected, ip, …) |
| `ble` | controller state (advertising, connected, services, …) |
| `bleNotify` | `handle`, `data` |
| `log` | bridge log lines |
| `adc` | channel/value |
| `error` / `exit` | diagnostics (QEMU exit carries its console tail) |

Client→server messages:

| `kind` | Payload | Effect |
| --- | --- | --- |
| `uartInput` | `data` | bytes into a guest UART |
| `gpioInput` | `pin`, `level` | drive a pin |
| `adcValue` | `channel`, `value` | potentiometer/ADC input |
| `sensorCfg` | `kind`, `bus`, `addr`, params | configure a virtual sensor |
| `i2cAttach` | `kind`, `bus`, `addr` | attach an I2C device (e.g. `{kind:6,bus:0,addr:0x3c}` = SSD1306) |
| `wifiScan` | — | trigger a scan |
| `wifiConnect` | `ssid`, `password` | join an AP |
| `wifiSoftap` | `ssid` | start SoftAP |
| `bleConnect` / `bleDisconnect` | — | Virtual Phone link control |
| `bleDiscover` | — | GATT discovery |
| `bleRead` / `bleWrite` | `handle`, `data?` | ATT read/write |
| `bleSubscribe` | `handle` | enable notifications |

## Firmware upload flow

1. `POST /api/upload?name=my.bin` with the raw image → `{path}`
2. `POST /api/sessions` with `firmware: path` (and `elf` when available)
3. Connect `/ws?session=<id>` and stream `uart`/`gpio`/… events
