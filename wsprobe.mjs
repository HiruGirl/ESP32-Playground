import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const WebSocket = require("./backend/node_modules/ws");
const BASE = "http://127.0.0.1:8760";

const res = await (await fetch(`${BASE}/api/sessions`, {
  method: "POST", headers: { "content-type": "application/json" },
  body: JSON.stringify({
    chip: "esp32",
    firmware: `/home/user/ESP32-Playground/examples/ble_notify/prebuilt/firmware.bin`,
    elf: `/home/user/ESP32-Playground/examples/ble_notify/prebuilt/firmware.elf`,
    icount: 10,
  }),
})).json();
console.log("session", JSON.stringify(res).slice(0,200));

const ws = new WebSocket(`ws://127.0.0.1:8760/ws?session=${res.id}`);
let uart = "";
const kinds = {};
const bleEvents = [];
ws.on("message", (buf) => {
  try {
    const m = JSON.parse(buf.toString());
    kinds[m.kind] = (kinds[m.kind] || 0) + 1;
    if (m.kind === "uart") uart += m.data || "";
    if (m.kind === "ble" || m.kind === "bleNotify") bleEvents.push(JSON.stringify(m).slice(0, 200));
    if (m.kind === "log") console.log("LOG:", m.data);
    if (m.kind === "exit") console.log("EXIT EVENT:", JSON.stringify(m).slice(0, 300));
  } catch {}
});

let ticks = 0;
const prog = setInterval(() => {
  ticks++;
  console.log(`t=${ticks*60}s UART_TAIL:`, uart.replace(/\x1b\[[0-9;]*m/g, "").slice(-160).replace(/[\x00-\x08\x0b-\x1f]/g, ""));
}, 60000);

setTimeout(async () => {
  clearInterval(prog);
  console.log("KINDS:", JSON.stringify(kinds));
  console.log("BLE EVENTS:", bleEvents.slice(0, 10).join("\n") || "(none)");
  console.log("UART TAIL:", uart.replace(/\x1b\[[0-9;]*m/g, "").slice(-900).replace(/[\x00-\x08\x0b-\x1f]/g, ""));
  await fetch(`${BASE}/api/sessions/${res.id}`, { method: "DELETE" }).catch(() => {});
  ws.close();
  process.exit(0);
}, 600000);
