// ESP32 Playground backend: HTTP API + WebSocket bridge + static frontend.
import express from "express";
import { WebSocketServer, WebSocket } from "ws";
import { createServer } from "node:http";
import { existsSync, mkdirSync, writeFileSync, readFileSync, statSync, unlinkSync } from "node:fs";
import { join, resolve, extname, basename } from "node:path";
import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
import { QemuSession, SessionConfig } from "./qemu.js";
import { WifiSim } from "./wifi.js";
import { BleController } from "./ble.js";
import { MSG, frame } from "./protocol.js";

const __dirname = fileURLToPath(new URL(".", import.meta.url));
const ROOT = resolve(__dirname, "..", ".."); // repo root when running from backend/dist
const DATA = join(ROOT, "work", "sessions");
mkdirSync(DATA, { recursive: true });

const QEMU_BIN =
  process.env.ESPPLAY_QEMU ??
  join(ROOT, "work", "espressif-qemu", "build", "qemu-system-xtensa");
const QEMU_RISCV =
  process.env.ESPPLAY_QEMU_RISCV ??
  join(ROOT, "work", "espressif-qemu", "build", "qemu-system-riscv32");

const MAX_UPLOAD = 32 * 1024 * 1024;
const PORT = Number(process.env.PORT ?? 8760);

interface SessionBundle {
  session: QemuSession;
  wifi: WifiSim;
  ble: BleController;
  uart: { data: string[]; timestamps: boolean };
  gpioLevels: Record<number, number>;
  gpioModes: Record<number, number>;
  ledc: { freq: number; duty: number } | null;
  displays: Record<string, string>;
  lcdText: string;
  adc: Record<number, number>;
  rmt: string[];
  logs: string[];
}

const sessions = new Map<string, SessionBundle>();
const app = express();
app.use(express.json({ limit: "2mb" }));
app.use(express.raw({ type: "application/octet-stream", limit: MAX_UPLOAD }));

// ---- static frontend ----------------------------------------------------
const FRONTEND = join(ROOT, "frontend", "dist");
const MIME: Record<string, string> = {
  ".html": "text/html", ".js": "text/javascript", ".css": "text/css",
  ".svg": "image/svg+xml", ".json": "application/json", ".png": "image/png",
  ".ico": "image/x-icon", ".woff2": "font/woff2",
};
app.use((req, res, next) => {
  if (req.path.startsWith("/api") || req.path.startsWith("/ws")) return next();
  let p = join(FRONTEND, req.path === "/" ? "index.html" : req.path);
  if (!existsSync(p) || statSync(p).isDirectory()) p = join(FRONTEND, "index.html");
  if (!existsSync(p)) return res.status(404).send("frontend not built");
  res.setHeader("Content-Type", MIME[extname(p)] ?? "application/octet-stream");
  res.send(readFileSync(p));
});

// ---- helpers ------------------------------------------------------------
function bundle(session: QemuSession): SessionBundle {
  const wifi = new WifiSim(session);
  const ble = new BleController(session);
  const b: SessionBundle = {
    session, wifi, ble,
    uart: { data: [], timestamps: true },
    gpioLevels: {}, gpioModes: {}, ledc: null, displays: {}, lcdText: "",
    adc: {}, rmt: [], logs: [],
  };
  session.on("bridge", (m) => {
    switch (m.type) {
      case MSG.UART_DATA: {
        const uart = m.payload[0] ?? 0;
        const text = m.payload.subarray(1).toString("utf8");
        b.uart.data.push(text);
        if (b.uart.data.length > 4000) b.uart.data.splice(0, 1000);
        broadcast(session.id, { kind: "uart", uart, data: text });
        break;
      }
      case MSG.GPIO_LEVEL: {
        const pin = m.payload[0], level = m.payload[1];
        b.gpioLevels[pin] = level;
        broadcast(session.id, { kind: "gpio", pin, level });
        break;
      }
      case MSG.GPIO_MODE: {
        const pin = m.payload[0], mode = m.payload[1];
        b.gpioModes[pin] = mode;
        broadcast(session.id, { kind: "gpioMode", pin, mode });
        break;
      }
      case MSG.LEDC: {
        b.ledc = { freq: m.payload.readUInt32LE(0), duty: m.payload.readUInt32LE(4) };
        broadcast(session.id, { kind: "ledc", ...b.ledc });
        break;
      }
      case MSG.RMT_FRAME: {
        const hex = m.payload.toString("hex");
        b.rmt.push(hex);
        if (b.rmt.length > 200) b.rmt.shift();
        broadcast(session.id, { kind: "rmt", hex });
        break;
      }
      case MSG.I2C_WRITE: {
        broadcast(session.id, { kind: "i2c", bus: m.payload[0], addr: m.payload[1], data: m.payload.subarray(2).toString("hex") });
        break;
      }
      case MSG.DISPLAY_FB: {
        const key = `${m.payload[0]}-${m.payload[1]}`;
        b.displays[key] = m.payload.subarray(2).toString("base64");
        broadcast(session.id, { kind: "display", bus: m.payload[0], addr: m.payload[1], fb: b.displays[key] });
        break;
      }
      case MSG.LCD_TEXT: {
        b.lcdText = m.payload.toString("utf8");
        broadcast(session.id, { kind: "lcd", text: b.lcdText });
        break;
      }
      case MSG.ADC_READ: {
        const ch = m.payload.readUInt16LE(0);
        broadcast(session.id, { kind: "adcRead", channel: ch });
        break;
      }
      case MSG.LOG: {
        const s = m.payload.toString("utf8");
        b.logs.push(s);
        if (b.logs.length > 500) b.logs.shift();
        broadcast(session.id, { kind: "log", text: s });
        break;
      }
      case MSG.HELLO:
        broadcast(session.id, { kind: "hello", proto: m.payload[0], chip: m.payload[1] });
        break;
    }
  });
  wifi.on("update", () => broadcast(session.id, { kind: "wifi", state: wifi.state }));
  wifi.on("config", (c) => broadcast(session.id, { kind: "wifiConfig", ...c }));
  ble.on("update", () => broadcast(session.id, { kind: "ble", state: ble.state }));
  ble.on("notification", (n) => broadcast(session.id, { kind: "bleNotify", handle: n.handle, data: n.value.toString("hex") }));
  ble.on("att", (data) => ble.handleAttFromGuest(data));
  session.on("console", (d) => broadcast(session.id, { kind: "console", data: d }));
  session.on("exit", (e) => broadcast(session.id, { kind: "exit", ...e }));
  session.on("state", (s) => broadcast(session.id, { kind: "sessionState", state: s }));
  return b;
}

// ---- WebSocket ----------------------------------------------------------
const httpServer = createServer(app);
const wss = new WebSocketServer({ server: httpServer, path: "/ws" });
const clients = new Map<string, Set<WebSocket>>();

function broadcast(sid: string, msg: unknown) {
  const set = clients.get(sid);
  if (!set) return;
  const data = JSON.stringify(msg);
  for (const ws of set) if (ws.readyState === WebSocket.OPEN) ws.send(data);
}

wss.on("connection", (ws, req) => {
  const url = new URL(req.url ?? "/", "http://localhost");
  const sid = url.searchParams.get("session") ?? "";
  if (!clients.has(sid)) clients.set(sid, new Set());
  clients.get(sid)!.add(ws);
  ws.on("close", () => clients.get(sid)?.delete(ws));
  ws.on("message", async (raw) => {
    try {
      const msg = JSON.parse(raw.toString());
      const b = sessions.get(sid);
      if (!b) return;
      switch (msg.kind) {
        case "uartInput": {
          const data = Buffer.from(msg.data ?? "", "utf8");
          const out = Buffer.concat([Buffer.from([msg.uart ?? 0]), data]);
          b.session.send(MSG.UART_INPUT, out);
          break;
        }
        case "gpioInput":
          b.session.send(MSG.GPIO_INPUT, Buffer.from([msg.pin & 0xff, msg.level ? 1 : 0]));
          break;
        case "adcValue": {
          const p = Buffer.alloc(4);
          p.writeUInt16LE(msg.channel ?? 0, 0);
          p.writeUInt16LE(Math.max(0, Math.min(4095, msg.value | 0)), 2);
          b.session.send(MSG.ADC_VALUE, p);
          break;
        }
        case "sensorCfg": {
          const cfg = Buffer.from(JSON.stringify(msg.cfg ?? {}), "utf8");
          b.session.send(MSG.SENSOR_CFG, Buffer.concat([Buffer.from([msg.kind ?? 0]), cfg]));
          break;
        }
        case "i2cAttach": {
          const p = Buffer.alloc(3);
          p[0] = msg.kind ?? 0; p[1] = msg.bus ?? 0; p[2] = msg.addr ?? 0;
          b.session.send(MSG.I2CDEV_ATTACH, p);
          break;
        }
        case "wifiScan":
          b.wifi.state.scanning = true;
          b.session.send(MSG.WIFI_SIM, Buffer.from([0])); // trigger
          break;
        case "wifiConnect": {
          const ssid = Buffer.from(msg.ssid ?? "", "utf8");
          const pw = Buffer.from(msg.password ?? "", "utf8");
          b.session.send(MSG.WIFI_CONFIG, Buffer.concat([Buffer.from([ssid.length]), ssid, Buffer.from([pw.length]), pw]));
          break;
        }
        case "wifiSoftap":
          if (msg.enable) b.wifi.startSoftAp(msg.ssid ?? "ESP32Playground-AP");
          else b.wifi.stopSoftAp();
          break;
        case "bleConnect":
          b.ble.phoneConnect();
          break;
        case "bleDisconnect":
          b.ble.phoneDisconnect();
          break;
        case "bleDiscover":
          await b.ble.discoverServices();
          break;
        case "bleWrite":
          await b.ble.writeCharacteristic(msg.handle, Buffer.from(msg.data ?? "", "hex"), msg.withResponse !== false);
          break;
        case "bleRead": {
          const v = await b.ble.readCharacteristic(msg.handle);
          ws.send(JSON.stringify({ kind: "bleReadResult", handle: msg.handle, data: v.toString("hex") }));
          break;
        }
        case "bleSubscribe":
          await b.ble.subscribe(msg.handle);
          break;
      }
    } catch (err) {
      ws.send(JSON.stringify({ kind: "error", error: String(err) }));
    }
  });
});

// ---- REST API -----------------------------------------------------------
app.get("/api/health", (_req, res) => {
  res.json({
    ok: true,
    qemu: existsSync(QEMU_BIN),
    qemuRiscv: existsSync(QEMU_RISCV),
    sessions: sessions.size,
  });
});

app.get("/api/examples", (_req, res) => {
  const dir = join(ROOT, "examples");
  const out: unknown[] = [];
  if (existsSync(dir)) {
    for (const name of require("node:fs").readdirSync(dir)) {
      const pre = join(dir, name, "prebuilt");
      if (existsSync(join(pre, "firmware.bin"))) {
        out.push({
          name,
          firmware: join(pre, "firmware.bin"),
          elf: existsSync(join(pre, "firmware.elf")) ? join(pre, "firmware.elf") : null,
          variants: ["esp32s3", "esp32c3"].filter((c) => existsSync(join(pre, c, "firmware.bin"))),
          description: describeExample(name),
        });
      }
    }
  }
  res.json(out);
});

function describeExample(name: string): string {
  const map: Record<string, string> = {
    blink: "Blink an LED on GPIO2 with precise timing",
    button_led: "Button on GPIO0 toggles LED on GPIO2",
    pot_servo: "Potentiometer on ADC1_CH6 (GPIO34) drives a servo on GPIO18",
    oled_hello: "SSD1306 OLED on I2C shows text and graphics",
    bme280_reader: "BME280 temperature/humidity/pressure over I2C",
    neopixel_rainbow: "8-LED WS2812 rainbow on GPIO5 via RMT",
    wifi_scan: "Scan virtual WiFi networks",
    wifi_http_client: "Connect to WiFi and fetch a page from the host",
    wifi_webserver_led: "Web server controlling the LED over WiFi",
    ble_uart_echo: "BLE Nordic UART Service echo server",
    ble_notify: "BLE notify demo with Virtual Phone pairing",
    mqtt_publisher: "Publish temperature to an MQTT broker",
    arduino_blink: "Arduino-ESP32 blink sketch",
    arduino_wifi_led: "Arduino WiFi + web LED control",
  };
  return map[name] ?? name;
}

app.post("/api/sessions", (req, res) => {
  try {
    const { chip = "esp32", firmware, elf, icount } = req.body ?? {};
    if (!firmware || typeof firmware !== "string") return res.status(400).json({ error: "firmware path required" });
    const fw = resolve(firmware);
    if (!fw.startsWith(ROOT) || !existsSync(fw)) return res.status(400).json({ error: "firmware not found (must be inside workspace)" });
    if (statSync(fw).size > MAX_UPLOAD) return res.status(400).json({ error: "firmware too large" });
    const id = randomUUID().slice(0, 8);
    const cfg: SessionConfig = { chip, firmware: fw, elf: elf && existsSync(resolve(elf)) ? resolve(elf) : undefined, icount };
    const session = new QemuSession(id, cfg, chip === "esp32c3" ? QEMU_RISCV : QEMU_BIN);
    const b = bundle(session);
    sessions.set(id, b);
    session.start().catch((err) => {
      broadcast(id, { kind: "error", error: String(err) });
    });
    res.json(session.info);
  } catch (err) {
    res.status(500).json({ error: String(err) });
  }
});

app.post("/api/upload", (req, res) => {
  const name = String(req.query.name ?? "firmware.bin").replace(/[^a-z0-9._-]/gi, "");
  if (!req.body || req.body.length === 0) return res.status(400).json({ error: "empty upload" });
  if (req.body.length > MAX_UPLOAD) return res.status(400).json({ error: "file too large" });
  const dir = join(DATA, "uploads");
  mkdirSync(dir, { recursive: true });
  const p = join(dir, `${Date.now()}-${name}`);
  writeFileSync(p, req.body);
  res.json({ path: p, size: req.body.length });
});

app.get("/api/sessions", (_req, res) => {
  res.json([...sessions.values()].map((b) => b.session.info));
});

app.get("/api/sessions/:id", (req, res) => {
  const b = sessions.get(req.params.id);
  if (!b) return res.status(404).json({ error: "no such session" });
  res.json({
    ...b.session.info,
    wifi: b.wifi.state,
    ble: b.ble.state,
    gpio: b.gpioLevels,
    gpioModes: b.gpioModes,
    ledc: b.ledc,
    lcdText: b.lcdText,
    displays: Object.keys(b.displays),
    logs: b.logs.slice(-50),
  });
});

app.post("/api/sessions/:id/control", async (req, res) => {
  const b = sessions.get(req.params.id);
  if (!b) return res.status(404).json({ error: "no such session" });
  const cmd = req.body?.cmd;
  if (cmd === "pause") await b.session.pause();
  else if (cmd === "resume") await b.session.resume();
  else if (cmd === "reset") await b.session.reset();
  else if (cmd === "stop") { b.session.stop(); }
  else if (cmd === "snapshot" && typeof req.body.name === "string") {
    b.session.saveSnapshot(req.body.name);
  } else if (cmd === "restore" && typeof req.body.name === "string") {
    const ok = b.session.restoreSnapshot(req.body.name);
    if (ok) await b.session.reset();
    else return res.status(404).json({ error: "no such snapshot" });
  } else if (cmd === "replaceFlash" && typeof req.body.firmware === "string") {
    const fw = resolve(req.body.firmware);
    if (!fw.startsWith(ROOT) || !existsSync(fw)) return res.status(400).json({ error: "firmware not found" });
    b.session.stop();
    const cfg = { ...b.session.cfg, firmware: fw };
    b.session.destroy();
    const ns = new QemuSession(b.session.id, cfg, cfg.chip === "esp32c3" ? QEMU_RISCV : QEMU_BIN);
    sessions.set(req.params.id, bundle(ns));
    await ns.start();
    return res.json({ ok: true, state: "running" });
  } else return res.status(400).json({ error: "unknown command" });
  res.json({ ok: true, state: b.session.state });
});

app.delete("/api/sessions/:id", (req, res) => {
  const b = sessions.get(req.params.id);
  if (!b) return res.status(404).json({ error: "no such session" });
  b.wifi.dispose();
  b.session.destroy();
  sessions.delete(req.params.id);
  res.json({ ok: true });
});

app.get("/api/sessions/:id/uart", (req, res) => {
  const b = sessions.get(req.params.id);
  if (!b) return res.status(404).json({ error: "no such session" });
  res.json({ data: b.uart.data.join("") });
});

app.get("/api/sessions/:id/vcd", (req, res) => {
  const b = sessions.get(req.params.id);
  if (!b) return res.status(404).json({ error: "no such session" });
  // Minimal VCD export of the observed GPIO level log (from live events).
  const pins = [...new Set(Object.keys(b.gpioLevels).map(Number))].sort((x, y) => x - y);
  let vcd = "$date generated by ESP32 Playground $end\n$timescale 1us $end\n$scope module esp32 $end\n";
  pins.forEach((p, i) => { vcd += `$var wire 1 ${String.fromCharCode(33 + i)} gpio${p} $end\n`; });
  vcd += "$upscope $end $enddefinitions $end\n#0\n";
  pins.forEach((p, i) => { vcd += `${b.gpioLevels[p] ?? 0}${String.fromCharCode(33 + i)}\n`; });
  res.setHeader("Content-Type", "text/plain");
  res.send(vcd);
});

httpServer.listen(PORT, "127.0.0.1", () => {
  console.log(`ESP32 Playground backend on http://127.0.0.1:${PORT}`);
  console.log(`QEMU: ${QEMU_BIN} (${existsSync(QEMU_BIN) ? "ok" : "MISSING"})`);
});
