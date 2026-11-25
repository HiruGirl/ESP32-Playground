// End-to-end CYD SPI SD test. Run from repo root with backend already running.
import { createRequire } from 'node:module';
import { mkdirSync, openSync, closeSync, writeSync, ftruncateSync, readFileSync, unlinkSync } from 'node:fs';
import { resolve } from 'node:path';
const require = createRequire(resolve('backend/package.json'));
const WebSocket = require('ws');
const base = process.argv[2] ?? 'http://127.0.0.1:8760';
mkdirSync('work', { recursive: true });
const image = resolve(`work/sd-test-${process.pid}.img`);
const fd = openSync(image, 'w');
const sector = Buffer.from(Array.from({ length: 512 }, (_, i) => i & 255));
writeSync(fd, sector, 0, sector.length, 0);
ftruncateSync(fd, 64 * 1024 * 1024);
closeSync(fd);
let id;
let ws;
try {
  const response = await fetch(`${base}/api/sessions`, {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ chip: 'esp32', firmware: resolve('examples/sd_spi_test/prebuilt/firmware.bin'), sdImage: image }),
  });
  const session = await response.json();
  id = session.id;
  if (!id) throw new Error(`session creation failed: ${JSON.stringify(session)}`);
  ws = new WebSocket(`${base.replace(/^http/, 'ws')}/ws?session=${id}`);
  let text = '';
  await new Promise((done, reject) => {
    const timeout = setTimeout(() => reject(new Error(`timed out: ${text.slice(-1000)}`)), 45000);
    ws.on('error', reject);
    ws.on('message', raw => {
      const m = JSON.parse(raw.toString());
      if (m.kind === 'uart') text += m.data;
      if (text.includes('SD_SPI_WRITE_PASS')) { clearTimeout(timeout); done(); }
    });
  });
  if (!text.includes('SD_SPI_READ_PASS')) throw new Error('missing read pass');
  if (!readFileSync(image).subarray(0, 512).equals(sector)) throw new Error('session modified original image');
  console.log('SPI SD initialization, sector read/write/readback, and source-image isolation passed');
} finally {
  ws?.close();
  if (id) await fetch(`${base}/api/sessions/${id}`, { method: 'DELETE' });
  unlinkSync(image);
}
