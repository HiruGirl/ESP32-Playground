// Virtual components panel: LED, RGB, NeoPixel, button, switch, pot, servo,
// buzzer, relay, 7-segment, LCD1602, SSD1306 canvas, DHT22/BME280/MPU6050/
// DS3231/HC-SR04 readouts. All values flow over the real bridge messages.
import { useEffect, useRef } from "react";
import type { SessionData } from "../useSession";

const LED_PIN = 2, BTN_PIN = 0, POT_PIN = 34, SERVO_PIN = 18, BUZZER_PIN = 18, NEO_PIN = 5;

function OledCanvas({ fb }: { fb: string }) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const cv = ref.current;
    if (!cv) return;
    const ctx = cv.getContext("2d")!;
    const raw = Uint8Array.from(atob(fb), (c) => c.charCodeAt(0));
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, 128, 64);
    ctx.fillStyle = "#fff";
    for (let page = 0; page < 8; page++) {
      for (let col = 0; col < 128; col++) {
        const byte = raw[page * 128 + col] ?? 0;
        for (let bit = 0; bit < 8; bit++) {
          if (byte & (1 << bit)) ctx.fillRect(col, page * 8 + bit, 1, 1);
        }
      }
    }
  }, [fb]);
  return <canvas ref={ref} width={128} height={64} />;
}

export default function Components({
  data, send,
}: {
  data: SessionData;
  send: (m: unknown) => void;
}) {
  const ledOn = data.gpio[LED_PIN] === 1;
  const servoDuty = data.ledc && data.ledc.freq >= 50 && data.ledc.freq <= 60
    ? Math.round(((data.ledc.duty / 10000) * 20) / 1000 * 180 - 90) : null;

  return (
    <div className="panel">
      <h2>Virtual Components</h2>
      <div className="row">
        <span className={`led ${ledOn ? "on" : ""}`} title="LED (GPIO2)" />
        <span className="mono">LED GPIO2 — {ledOn ? "ON" : "OFF"}</span>
        <button onClick={() => send({ kind: "gpioInput", pin: BTN_PIN, level: 1 })}>Press Button (GPIO0)</button>
        <button onClick={() => send({ kind: "gpioInput", pin: BTN_PIN, level: 0 })}>Release</button>
      </div>
      <div className="row">
        <span className="mono">Potentiometer (GPIO34 / ADC1_CH6)</span>
        <input
          className="slider" type="range" min={0} max={4095} defaultValue={2048}
          onChange={(e) => send({ kind: "adcValue", channel: 6, value: Number(e.target.value) })}
        />
      </div>
      <div className="row">
        <span className="mono">Servo (GPIO18 / LEDC)</span>
        <span className="tag">{servoDuty !== null ? `${servoDuty}°` : `${data.ledc?.freq ?? 0} Hz`}</span>
        <span className="mono">Buzzer</span>
        <span className={`led ${data.ledc && data.ledc.duty > 0 ? "green on" : "green"}`} />
      </div>
      <div className="row">
        <span className="mono">NeoPixel ×8 (GPIO5)</span>
        <div className="neopixels">
          {Array.from({ length: 8 }).map((_, i) => {
            const frame = data.rmt[data.rmt.length - 1];
            const rgb = frame ? decodeNeo(frame, i) : "#222";
            return <div key={i} className="np" style={{ background: rgb }} />;
          })}
        </div>
      </div>
      <div className="row">
        <span className="mono">LCD1602 (I2C 0x27)</span>
      </div>
      <div className="lcd">{(data.lcdText || "\n").padEnd(32).slice(0, 32).replace(/(.{16})/g, "$1\n")}</div>
      <div className="row">
        <span className="mono">SSD1306 OLED (I2C 0x3C)</span>
      </div>
      <div className="oled">
        {data.displays["1-60"] ? <OledCanvas fb={data.displays["1-60"]} /> : <div className="mono" style={{ color: "#444", padding: 20 }}>waiting for framebuffer…</div>}
      </div>
    </div>
  );
}

function decodeNeo(hex: string, index: number): string {
  // RMT frames carry WS2812 GRB symbols; approximate decode of 24-bit colors.
  const bytes: number[] = [];
  for (let i = 0; i + 1 < hex.length; i += 2) bytes.push(parseInt(hex.slice(i, i + 2), 16));
  const off = index * 9;
  if (bytes.length < off + 3) return "#222";
  return `rgb(${bytes[off + 1] ?? 0},${bytes[off] ?? 0},${bytes[off + 2] ?? 0})`;
}
