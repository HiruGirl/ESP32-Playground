// Serial monitor: UART0-2 tabs, input, timestamps, log coloring, download.
import { useMemo, useRef, useState, useEffect } from "react";
import type { SessionData } from "../useSession";

const LEVEL_COLORS: [RegExp, string][] = [
  [/E \(/, "var(--danger)"], [/W \(/, "var(--warn)"], [/I \(/, "var(--accent2)"], [/D \(/, "var(--fg2)"],
];

export default function SerialMonitor({
  data, send,
}: {
  data: SessionData;
  send: (m: unknown) => void;
}) {
  const [uart, setUart] = useState(0);
  const [input, setInput] = useState("");
  const [timestamps, setTimestamps] = useState(false);
  const [filter, setFilter] = useState("");
  const endRef = useRef<HTMLDivElement>(null);
  const text = useMemo(() => data.uart.join(""), [data.uart]);

  useEffect(() => { endRef.current?.scrollIntoView({ behavior: "smooth" }); }, [text]);

  const lines = useMemo(() => {
    let out = text.split(/\r?\n/);
    if (filter) out = out.filter((l) => l.toLowerCase().includes(filter.toLowerCase()));
    return out.slice(-600);
  }, [text, filter]);

  const download = () => {
    const blob = new Blob([text], { type: "text/plain" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `uart${uart}-${Date.now()}.log`;
    a.click();
  };

  return (
    <div className="panel">
      <h2>Serial Monitor</h2>
      <div className="row">
        {[0, 1, 2].map((u) => (
          <button key={u} className={u === uart ? "primary" : ""} onClick={() => setUart(u)}>UART{u}</button>
        ))}
        <label className="row mono"><input type="checkbox" checked={timestamps} onChange={(e) => setTimestamps(e.target.checked)} /> timestamps</label>
        <input placeholder="filter…" value={filter} onChange={(e) => setFilter(e.target.value)} style={{ width: 110 }} />
        <button onClick={download}>Download</button>
      </div>
      <div className="terminal">
        {lines.map((line, i) => (
          <div key={i} style={{ color: LEVEL_COLORS.find(([re]) => re.test(line))?.[1] ?? "inherit" }}>
            {timestamps ? `[${new Date().toLocaleTimeString()}] ` : ""}{line}
          </div>
        ))}
        <div ref={endRef} />
      </div>
      <form
        className="row"
        onSubmit={(e) => {
          e.preventDefault();
          send({ kind: "uartInput", uart, data: input + "\n" });
          setInput("");
        }}
      >
        <input
          style={{ flex: 1 }} value={input} onChange={(e) => setInput(e.target.value)}
          placeholder="Send to UART… (Enter)" />
        <button className="primary" type="submit">Send</button>
      </form>
    </div>
  );
}
