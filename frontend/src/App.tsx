// ESP32 Playground — main app shell.
import { useCallback, useEffect, useState, type PointerEvent } from "react";
import { api, useSession } from "./useSession";
import Components from "./components/Components";
import SerialMonitor from "./components/SerialMonitor";
import WifiPanel from "./components/WifiPanel";
import BlePanel from "./components/BlePanel";
import Wiring, { Wire } from "./components/Wiring";
import LogicAnalyzer from "./components/LogicAnalyzer";

interface Example {
  name: string;
  firmware: string;
  elf: string | null;
  description: string;
  variants: string[];
}

export default function App() {
  const { data, send, connect, tftCanvas } = useSession();
  const [examples, setExamples] = useState<Example[]>([]);
  const [cyd, setCyd] = useState(true);
  const [chip, setChip] = useState("esp32");
  const [selected, setSelected] = useState<string | null>(null);
  const [theme, setTheme] = useState<"dark" | "light">("dark");
  const [showOnboard, setShowOnboard] = useState(() => !localStorage.getItem("espplay-onboarded"));
  const [wires, setWires] = useState<Wire[]>([
    { component: "LED", signal: "anode", pin: 2, color: "#f59e0b" },
    { component: "Button", signal: "pin1", pin: 0, color: "#4f8cff" },
    { component: "Potentiometer", signal: "wiper", pin: 34, color: "#22c55e" },
  ]);
  const [board, setBoard] = useState("DevKitC (ESP32)");
  const [status, setStatus] = useState("");

  useEffect(() => {
    api("/api/examples").then(setExamples).catch(() => setExamples([]));
  }, []);
  useEffect(() => {
    document.documentElement.className = theme === "light" ? "light" : "";
  }, [theme]);

  const run = useCallback(async (ex?: Example) => {
    setStatus("starting session…");
    const fw = ex?.firmware;
    const elf = ex?.elf ?? undefined;
    const res = await api("/api/sessions", { chip, firmware: fw, elf, ...(chip === "esp32" && cyd ? { tft: { bus: 2, dcPin: 2, w: 240, h: 320 }, touch: { bus: 3, csPin: 33, irqPin: 36 } } : {}) });
    if (res.error) { setStatus(`error: ${res.error}`); return; }
    setSelected(res.id);
    connect(res.id);
    setStatus(`session ${res.id} running (${chip})`);
  }, [chip, connect, cyd]);

  const tftTouch = useCallback((event: PointerEvent<HTMLCanvasElement>, down: boolean) => {
    if (!cyd || chip !== "esp32" || !data.connected) return;
    const r = event.currentTarget.getBoundingClientRect();
    const x = Math.max(0, Math.min(1, (event.clientX - r.left) / r.width));
    const y = Math.max(0, Math.min(1, (event.clientY - r.top) / r.height));
    // CYD portrait calibration and XPT2046_Touchscreen rotation 0.
    send({ kind: "touch", down, x: Math.round(240 + y * 3560), y: Math.round(4095 - (200 + x * 3500)) });
  }, [cyd, chip, data.connected, send]);

  const control = useCallback(async (cmd: string) => {
    if (!selected) return;
    await api(`/api/sessions/${selected}/control`, { cmd });
    setStatus(`${cmd} ok`);
  }, [selected]);

  // Keyboard shortcuts: R run·reset? S stop, P pause/resume, R reset.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return;
      if (e.key === "F5" || (e.ctrlKey && e.key === "Enter")) { e.preventDefault(); void run(examples.find((x) => x.name === selected) ); }
      else if (e.key === "p" && e.ctrlKey) { e.preventDefault(); void control(data.state === "paused" ? "resume" : "pause"); }
      else if (e.key === "r" && e.ctrlKey) { e.preventDefault(); void control("reset"); }
      else if (e.key === "s" && e.ctrlKey) { e.preventDefault(); void control("stop"); }
      else if (e.key === "d" && e.ctrlKey) { e.preventDefault(); setTheme((t) => (t === "dark" ? "light" : "dark")); }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [run, control, data.state, examples, selected]);

  return (
    <div className="app">
      <header className="topbar">
        <h1>⚡ ESP32 Playground</h1>
        <span className={`badge ${data.state}`}>{data.state}</span>
        <span className="mono">{status}</span>
        <div className="spacer" />
        <select value={chip} onChange={(e) => setChip(e.target.value)}>
          <option value="esp32">ESP32</option>
          <option value="esp32s3">ESP32-S3</option>
          <option value="esp32c3">ESP32-C3</option>
        </select>
        <button onClick={() => void run(examples.find((x) => x.name === selected))} className="primary">▶ Run <span className="kbd">F5</span></button>
        <button onClick={() => void control("pause")} disabled={data.state !== "running"}>⏸ <span className="kbd">Ctrl+P</span></button>
        <button onClick={() => void control("resume")} disabled={data.state !== "paused"}>⏵</button>
        <button onClick={() => void control("reset")}>⟲ <span className="kbd">Ctrl+R</span></button>
        <button onClick={() => void control("stop")} className="danger">■ <span className="kbd">Ctrl+S</span></button>
        <button onClick={() => setTheme((t) => (t === "dark" ? "light" : "dark"))}>◐</button>
      </header>
      <div className="main">
        <div className="col">
          <div className="panel">
            <label><input type="checkbox" checked={cyd} onChange={e => setCyd(e.target.checked)} /> CYD TFT and touch (ESP32)</label>
          </div>
          <div className="panel">
            <h2>Examples</h2>
            <div style={{ display: "flex", flexDirection: "column", gap: 6, maxHeight: 320, overflowY: "auto" }}>
              {examples.map((ex) => (
                <button
                  key={ex.name}
                  className={selected === ex.name ? "primary" : ""}
                  style={{ textAlign: "left" }}
                  onClick={() => { setSelected(ex.name); void run(ex); }}
                  title={ex.description}
                >
                  {ex.name} <span style={{ color: "var(--fg2)", fontSize: 11 }}>{ex.description}</span>
                </button>
              ))}
            </div>
          </div>
          <div className="panel">
            <h2>Flash firmware</h2>
            <input
              type="file" accept=".bin"
              onChange={async (e) => {
                const file = e.target.files?.[0];
                if (!file) return;
                setStatus("uploading…");
                const res = await fetch(`/api/upload?name=${encodeURIComponent(file.name)}`, {
                  method: "POST", body: file,
                }).then((r) => r.json());
                if (res.path) {
                  const s = await api("/api/sessions", { chip, firmware: res.path, ...(chip === "esp32" && cyd ? { tft: { bus: 2, dcPin: 2, w: 240, h: 320 }, touch: { bus: 3, csPin: 33, irqPin: 36 } } : {}) });
                  if (!s.error) { setSelected(s.id); connect(s.id); setStatus("custom firmware running"); }
                  else setStatus(`error: ${s.error}`);
                }
              }}
            />
            <div className="mono">Merged .bin (2/4/8/16MB) or bootloader+partitions+app flashed at standard offsets.</div>
          </div>
          <Wiring wires={wires} setWires={setWires} board={board} setBoard={setBoard} />
          <LogicAnalyzer data={data} sessionId={data.id} />
        </div>
        <div className="col">
          <div className="panel">
            <h2>CYD TFT</h2>
            <canvas ref={tftCanvas} width={240} height={320} style={{ width: "min(100%, 240px)", height: "auto", background: "#000", imageRendering: "pixelated", touchAction: "none" }}
              onPointerDown={e => { e.currentTarget.setPointerCapture(e.pointerId); tftTouch(e, true); }}
              onPointerMove={e => { if (e.buttons) tftTouch(e, true); }}
              onPointerUp={e => tftTouch(e, false)} onPointerCancel={e => tftTouch(e, false)} />
            <p className="mono">ILI9341 output · touch uses approximate CYD portrait calibration.</p>
          </div>
          <Components data={data} send={send} />
          <WifiPanel data={data} send={send} />
        </div>
        <div className="col">
          <SerialMonitor data={data} send={send} />
          <BlePanel data={data} send={send} />
          <div className="panel">
            <h2>Bridge log</h2>
            <div className="terminal" style={{ height: 140 }}>{data.logs.slice(-40).join("\n")}</div>
          </div>
        </div>
      </div>
      {showOnboard && (
        <div className="onboard">
          <div className="card">
            <h2>Welcome to ESP32 Playground</h2>
            <p>Run real ESP firmware in QEMU with virtual components, WiFi, and BLE.</p>
            <ol>
              <li>Pick an <b>example</b> on the left and press <span className="kbd">F5</span> to run.</li>
              <li>Watch the <b>serial monitor</b> and the <b>virtual components</b> react live.</li>
              <li>Use the <b>WiFi</b> and <b>Virtual Phone</b> panels for network features.</li>
              <li>Wire up parts in the <b>wiring editor</b> — conflicts are flagged.</li>
            </ol>
            <p className="mono">Shortcuts: F5 run · Ctrl+P pause · Ctrl+R reset · Ctrl+S stop · Ctrl+D theme</p>
            <button className="primary" onClick={() => {
              localStorage.setItem("espplay-onboarded", "1");
              setShowOnboard(false);
            }}>Get started</button>
          </div>
        </div>
      )}
    </div>
  );
}
