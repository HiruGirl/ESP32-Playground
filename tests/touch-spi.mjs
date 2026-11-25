// End-to-end CYD touch: active-low IRQ and raw X/Y SPI conversions.
import { resolve } from 'node:path';
import { createRequire } from 'node:module';
const WebSocket = createRequire(resolve('backend/package.json'))('ws');
const base = process.argv[2] ?? 'http://127.0.0.1:8760';
const response = await fetch(`${base}/api/sessions`, {
  method: 'POST', headers: { 'content-type': 'application/json' },
  body: JSON.stringify({ chip: 'esp32', firmware: resolve('examples/touch_spi_test/prebuilt/firmware.bin'), touch: { bus: 3, csPin: 33, irqPin: 36 } }),
});
const { id, error } = await response.json();
if (!id) throw new Error(`session creation failed: ${error}`);
const ws = new WebSocket(`${base.replace(/^http/, 'ws')}/ws?session=${id}`);
let text = '', stage = 0;
try {
  await new Promise((done, reject) => {
    const timeout = setTimeout(() => reject(new Error(`timed out: ${text.slice(-600)}`)), 45000);
    ws.on('error', reject);
    ws.on('message', raw => {
      const m = JSON.parse(raw.toString());
      if (m.kind !== 'uart') return;
      text += m.data;
      if (stage === 0 && text.includes('TOUCH irq=1')) {
        stage = 1; text = '';
        ws.send(JSON.stringify({ kind: 'touch', down: true, x: 1234, y: 3456 }));
      } else if (stage === 1 && text.includes('TOUCH irq=0 x=1234 y=3456 z1=1500 z2=2500 px=1234')) {
        stage = 2; text = '';
        ws.send(JSON.stringify({ kind: 'touch', down: false, x: 1234, y: 3456 }));
      } else if (stage === 2 && text.includes('TOUCH irq=1 x=1234 y=3456')) {
        clearTimeout(timeout); done();
      }
    });
  });
  console.log('XPT2046 raw coordinates and active-low IRQ press/release passed');
} finally {
  ws.close();
  await fetch(`${base}/api/sessions/${id}`, { method: 'DELETE' });
}
