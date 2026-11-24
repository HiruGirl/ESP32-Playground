// Wiring editor: place component-to-board wires with pin-conflict warnings.
import { useState } from "react";

export interface Wire {
  component: string;
  signal: string;
  pin: number; // GPIO number
  color: string;
}

const COMPONENT_SIGNALS: Record<string, string[]> = {
  LED: ["anode", "cathode"],
  Button: ["pin1", "pin2"],
  Potentiometer: ["VCC", "wiper", "GND"],
  Servo: ["signal", "VCC", "GND"],
  Buzzer: ["signal", "GND"],
  NeoPixel: ["din", "VCC", "GND"],
  SSD1306: ["SDA", "SCL", "VCC", "GND"],
  LCD1602: ["SDA", "SCL", "VCC", "GND"],
  BME280: ["SDA", "SCL", "VCC", "GND"],
  MPU6050: ["SDA", "SCL", "VCC", "GND"],
  DS3231: ["SDA", "SCL", "VCC", "GND"],
  DHT22: ["data", "VCC", "GND"],
  "HC-SR04": ["trig", "echo", "VCC", "GND"],
  Relay: ["in", "VCC", "GND"],
  "7-Segment": ["data", "clk", "VCC", "GND"],
};

export const BOARD_PINS: Record<string, number[]> = {
  "DevKitC (ESP32)": [0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39],
  "ESP32-S3 DevKitC": Array.from({ length: 22 }, (_, i) => i).concat([33, 34, 35, 36, 37, 38, 39, 40, 41, 42]),
  "ESP32-C3 SuperMini": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
};

export default function Wiring({
  wires, setWires, board, setBoard,
}: {
  wires: Wire[];
  setWires: (w: Wire[]) => void;
  board: string;
  setBoard: (b: string) => void;
}) {
  const [component, setComponent] = useState("LED");
  const [signal, setSignal] = useState("anode");
  const [pin, setPin] = useState("2");
  const [color, setColor] = useState("#f59e0b");

  const conflicts = findConflicts(wires);
  const add = () => {
    const p = Number(pin);
    if (!Number.isInteger(p) || p < 0 || p > 48) return;
    setWires([...wires, { component, signal, pin: p, color }]);
  };

  return (
    <div className="panel">
      <h2>Wiring Editor</h2>
      <div className="row">
        <select value={board} onChange={(e) => setBoard(e.target.value)}>
          {Object.keys(BOARD_PINS).map((b) => <option key={b}>{b}</option>)}
        </select>
        <span className={`badge ${conflicts.length ? "crashed" : "running"}`}>
          {conflicts.length ? `${conflicts.length} conflict(s)` : "no conflicts"}
        </span>
      </div>
      <div className="row">
        <select value={component} onChange={(e) => {
          setComponent(e.target.value);
          setSignal(COMPONENT_SIGNALS[e.target.value][0]);
        }}>
          {Object.keys(COMPONENT_SIGNALS).map((c) => <option key={c}>{c}</option>)}
        </select>
        <select value={signal} onChange={(e) => setSignal(e.target.value)}>
          {COMPONENT_SIGNALS[component].map((s) => <option key={s}>{s}</option>)}
        </select>
        <select value={pin} onChange={(e) => setPin(e.target.value)}>
          {BOARD_PINS[board].map((p) => <option key={p} value={p}>GPIO{p}</option>)}
        </select>
        <input type="color" value={color} onChange={(e) => setColor(e.target.value)} style={{ width: 42, padding: 2 }} />
        <button className="primary" onClick={add}>Add wire</button>
      </div>
      <div className="wire-list">
        {wires.map((w, i) => (
          <div key={i} className={`wire ${conflicts.includes(w.pin) ? "conflict" : ""}`}>
            <span style={{ width: 14, height: 14, borderRadius: 4, background: w.color, display: "inline-block" }} />
            <span style={{ flex: 1 }}>{w.component}.{w.signal} → GPIO{w.pin}</span>
            <span className={`tag ${conflicts.includes(w.pin) ? "bad" : "ok"}`}>
              {conflicts.includes(w.pin) ? "conflict" : "ok"}
            </span>
            <button onClick={() => setWires(wires.filter((_, j) => j !== i))}>✕</button>
          </div>
        ))}
        {!wires.length && <div className="mono">No wires yet — add one above.</div>}
      </div>
      {conflicts.length > 0 && (
        <div className="mono" style={{ color: "var(--danger)" }}>
          Pin conflict: GPIO{conflicts.join(", GPIO")} used by more than one signal
        </div>
      )}
    </div>
  );
}

function findConflicts(wires: Wire[]): number[] {
  const counts = new Map<number, number>();
  for (const w of wires) counts.set(w.pin, (counts.get(w.pin) ?? 0) + 1);
  return [...counts.entries()].filter(([, n]) => n > 1).map(([p]) => p);
}
