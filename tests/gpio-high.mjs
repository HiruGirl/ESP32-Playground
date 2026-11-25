// Integration test against a running backend and the prebuilt ESP-IDF guest.
// node tests/gpio-high.mjs [backend URL]
import { resolve } from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(resolve('backend/package.json'));
const WebSocket = require('ws');
const base = process.argv[2] ?? 'http://127.0.0.1:8760';
const response = await fetch(`${base}/api/sessions`, {
  method: 'POST', headers: { 'content-type': 'application/json' },
  body: JSON.stringify({ chip: 'esp32', firmware: resolve('examples/gpio_high_test/prebuilt/firmware.bin') }),
});
const { id, error } = await response.json();
if (!id) throw new Error(`session creation failed: ${error}`);
const ws = new WebSocket(`${base.replace(/^http/, 'ws')}/ws?session=${id}`);
let text = '', seenHigh = false, seenLow = false, inputHigh = false, inputLow = false;
try {
  await new Promise((resolvePromise, reject) => {
    const timeout = setTimeout(() => reject(new Error(`timed out: gpio33=${seenHigh}/${seenLow}, gpio36=${inputHigh}/${inputLow}, uart=${text.slice(-500)}`)), 45000);
    ws.on('error', reject);
    ws.on('open', () => ws.send(JSON.stringify({ kind: 'gpioInput', pin: 36, level: 1 })));
    ws.on('message', raw => {
      const m = JSON.parse(raw.toString());
      if (m.kind === 'gpio' && m.pin === 33) {
        if (m.level) seenHigh = true; else seenLow = true;
      }
      if (m.kind === 'uart') {
        text += m.data;
        if (text.includes('GPIO36=1') && !inputHigh) {
          inputHigh = true;
          ws.send(JSON.stringify({ kind: 'gpioInput', pin: 36, level: 0 }));
        }
        if (inputHigh && text.includes('GPIO36=0')) inputLow = true;
      }
      if (seenHigh && seenLow && inputHigh && inputLow) {
        clearTimeout(timeout); resolvePromise();
      }
    });
  });
  console.log('GPIO33 output and GPIO36 high/low input passed');
} finally {
  ws.close();
  await fetch(`${base}/api/sessions/${id}`, { method: 'DELETE' });
}
