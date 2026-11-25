// Diagnostic, not a pass test: node tests/marauder-smoke.mjs <merged-4mb.bin> [sd.img] [seconds]
import { resolve } from 'node:path';
import { createRequire } from 'node:module';
const WebSocket = createRequire(resolve('backend/package.json'))('ws');
if (!process.argv[2]) throw new Error('usage: node tests/marauder-smoke.mjs <merged flash image> [SD image] [seconds]');
const base = process.env.ESPPLAY_URL ?? 'http://127.0.0.1:8760';
const payload = { chip: 'esp32', firmware: resolve(process.argv[2]), icount: Number(process.env.ICOUNT ?? 3),
  tft: { bus: 2, dcPin: 2, w: 240, h: 320 }, touch: { bus: 3, csPin: 33, irqPin: 36 } };
if (process.argv[3] && process.argv[3] !== '-') payload.sdImage = resolve(process.argv[3]);
const response = await fetch(`${base}/api/sessions`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(payload) });
const { id, error } = await response.json();
if (!id) throw new Error(`session failed: ${error}`);
const ws = new WebSocket(`${base.replace(/^http/, 'ws')}/ws?session=${id}`);
let uart = '', gpio = 0, tft = 0;
try {
  await new Promise((done, reject) => {
    const timer = setTimeout(done, (Number(process.argv[4]) || 45) * 1000);
    ws.on('error', error => { clearTimeout(timer); reject(error); });
    ws.on('message', raw => {
      const m = JSON.parse(raw.toString());
      if (m.kind === 'uart') uart += m.data;
      if (m.kind === 'gpio') gpio++;
      if (m.kind === 'tft') tft++;
    });
  });
  console.log(uart);
  console.log(`Marauder smoke: ${uart.length} UART characters, ${gpio} GPIO updates, ${tft} TFT rectangles`);
  if (!tft) console.log('NO TFT OUTPUT: this is not a successful Marauder UI simulation.');
} finally {
  ws.close();
  await fetch(`${base}/api/sessions/${id}`, { method: 'DELETE' });
}
