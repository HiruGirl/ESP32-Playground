// GPIO logic analyzer: live timeline canvas + VCD export.
import { useEffect, useRef } from "react";
import type { SessionData } from "../useSession";

export default function LogicAnalyzer({
  data, sessionId,
}: {
  data: SessionData;
  sessionId: string | null;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const history = useRef<{ t: number; pin: number; level: number }[]>([]);

  useEffect(() => {
    const keys = Object.keys(data.gpio);
    if (!keys.length) return;
    const last = history.current[history.current.length - 1];
    for (const k of keys) {
      const pin = Number(k), level = data.gpio[Number(k)];
      if (!last || last.pin !== pin || last.level !== level) {
        history.current.push({ t: Date.now(), pin, level });
      }
    }
    if (history.current.length > 3000) history.current.splice(0, 500);
  }, [data.gpio]);

  useEffect(() => {
    const cv = canvasRef.current;
    if (!cv) return;
    const ctx = cv.getContext("2d")!;
    const draw = () => {
      ctx.clearRect(0, 0, cv.width, cv.height);
      const pins = [...new Set(history.current.map((h) => h.pin))].sort((a, b) => a - b).slice(0, 8);
      const rowH = cv.height / Math.max(1, pins.length);
      const now = Date.now();
      const windowMs = 10_000;
      pins.forEach((pin, i) => {
        const y = i * rowH + rowH * 0.25;
        const h = rowH * 0.5;
        ctx.fillStyle = "#9aa7bd";
        ctx.font = "10px monospace";
        ctx.fillText(`GPIO${pin}`, 2, y - 2);
        const events = history.current.filter((e) => e.pin === pin && now - e.t < windowMs);
        ctx.strokeStyle = "#22c55e";
        ctx.beginPath();
        let level = 0;
        for (const e of events) {
          const x = ((e.t - (now - windowMs)) / windowMs) * cv.width;
          const yy = level ? y : y + h;
          ctx.lineTo(x, yy);
          level = e.level;
          ctx.lineTo(x, level ? y : y + h);
        }
        ctx.stroke();
      });
      requestAnimationFrame(draw);
    };
    const raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, []);

  return (
    <div className="panel">
      <h2>GPIO Logic Analyzer</h2>
      <div className="timeline"><canvas ref={canvasRef} width={640} height={120} /></div>
      <div className="row">
        <button
          disabled={!sessionId}
          onClick={() => sessionId && window.open(`/api/sessions/${sessionId}/vcd`, "_blank")}
        >
          Export VCD
        </button>
        <span className="mono">{history.current.length} edges captured (10 s window)</span>
      </div>
    </div>
  );
}
