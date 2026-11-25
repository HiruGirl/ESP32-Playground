/** Paint a TFT_FRAME RGB565 rectangle into a native 240x320 RGBA buffer. */
export interface TftFrame { x: number; y: number; w: number; h: number; madctl: number; colmod: number; data: string }
export function paintTftFrame(pixels: Uint8ClampedArray, frame: TftFrame, width = 240, height = 320): void {
  if (frame.colmod !== 0x55 || frame.w <= 0 || frame.h <= 0 || frame.w * frame.h > 320 * 320) return;
  let bytes: string;
  try { bytes = atob(frame.data); } catch { return; }
  if (bytes.length !== frame.w * frame.h * 2) return;
  const mv = (frame.madctl & 0x20) !== 0;
  const mx = (frame.madctl & 0x40) !== 0;
  const my = (frame.madctl & 0x80) !== 0;
  const bgr = (frame.madctl & 0x08) !== 0;
  for (let row = 0; row < frame.h; row++) for (let col = 0; col < frame.w; col++) {
    let x = frame.x + col, y = frame.y + row;
    if (mv) [x, y] = [y, x];
    if (mx) x = width - 1 - x;
    if (my) y = height - 1 - y;
    if (x < 0 || x >= width || y < 0 || y >= height) continue;
    const i = (row * frame.w + col) * 2;
    const color = (bytes.charCodeAt(i) << 8) | bytes.charCodeAt(i + 1);
    const red = Math.round(((color >> 11) & 31) * 255 / 31);
    const blue = Math.round((color & 31) * 255 / 31);
    const p = (y * width + x) * 4;
    pixels[p] = bgr ? blue : red;
    pixels[p + 1] = Math.round(((color >> 5) & 63) * 255 / 63);
    pixels[p + 2] = bgr ? red : blue;
    pixels[p + 3] = 255;
  }
}
