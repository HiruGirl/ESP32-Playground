// espplay bridge wire protocol (must match qemu-patches/overlay/include/hw/misc/espplay_bridge.h)
export const MSG = {
  HELLO: 0x01,
  GPIO_LEVEL: 0x02,
  GPIO_MODE: 0x03,
  LEDC: 0x04,
  RMT_FRAME: 0x05,
  I2C_WRITE: 0x06,
  UART_DATA: 0x07,
  WIFI_TX: 0x08,
  WIFI_RX_FRAME: 0x09,
  HCI_UP: 0x0a,
  WIFI_SCAN: 0x0b,
  WIFI_CONNECT_REQ: 0x0c,
  WIFI_EVENT: 0x0d,
  ADC_READ: 0x0e,
  DHT22_DONE: 0x0f,
  LOG: 0x10,
  DISPLAY_FB: 0x11,
  LCD_TEXT: 0x12,

  PING: 0x80,
  GPIO_INPUT: 0x81,
  ADC_VALUE: 0x82,
  SENSOR_CFG: 0x83,
  I2CDEV_ATTACH: 0x84,
  WIFI_CONFIG: 0x85,
  WIFI_RESULT: 0x86,
  WIFI_SIM: 0x87,
  HCI_DOWN: 0x88,
  UART_INPUT: 0x89,
  STATE_RESET: 0x8a,
} as const;

export const WIFI_EVENTS = {
  STA_START: 2,
  STA_STOP: 3,
  STA_CONNECTED: 4,
  STA_DISCONNECTED: 5,
  AP_START: 6,
  AP_STOP: 7,
  SCAN_DONE: 8,
  GOT_IP: 9,
} as const;

export interface BridgeMessage {
  type: number;
  payload: Buffer;
}

/** Incremental parser for the u32le-length-prefixed framing. */
export class FrameParser {
  private buf: Buffer = Buffer.alloc(0);
  private static readonly MAX = 1 << 20;

  push(chunk: Buffer): BridgeMessage[] {
    this.buf = this.buf.length ? Buffer.concat([this.buf, chunk]) : chunk;
    const out: BridgeMessage[] = [];
    while (this.buf.length >= 5) {
      const len = this.buf.readUInt32LE(0);
      if (len === 0 || len > FrameParser.MAX) {
        this.buf = this.buf.subarray(1); // resync
        continue;
      }
      if (this.buf.length < 4 + len) break;
      out.push({ type: this.buf[4], payload: Buffer.from(this.buf.subarray(5, 4 + len)) });
      this.buf = this.buf.subarray(4 + len);
    }
    return out;
  }
}

/** Build a backend→QEMU frame. */
export function frame(type: number, payload: Buffer = Buffer.alloc(0)): Buffer {
  const out = Buffer.allocUnsafe(5 + payload.length);
  out.writeUInt32LE(1 + payload.length, 0);
  out[4] = type;
  payload.copy(out, 5);
  return out;
}
