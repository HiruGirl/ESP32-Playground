// BLE panel: Virtual Phone — scan/connect, GATT discovery, read/write/notify.
import { useState } from "react";
import type { SessionData } from "../useSession";

export default function BlePanel({
  data, send,
}: {
  data: SessionData;
  send: (m: unknown) => void;
}) {
  const b = data.ble;
  const [writeHandle, setWriteHandle] = useState("");
  const [writeData, setWriteData] = useState("");
  return (
    <div className="panel">
      <h2>Virtual Phone · BLE</h2>
      <div className="row">
        <span className={`badge ${b?.advertising ? "running" : ""}`}>{b?.advertising ? "ADV on" : "ADV off"}</span>
        <span className={`badge ${b?.connected ? "running" : ""}`}>{b?.connected ? "Connected" : "Disconnected"}</span>
        {!b?.connected
          ? <button className="primary" disabled={!b?.advertising} onClick={() => send({ kind: "bleConnect" })}>Connect</button>
          : <button className="danger" onClick={() => send({ kind: "bleDisconnect" })}>Disconnect</button>}
        <button disabled={!b?.connected} onClick={() => send({ kind: "bleDiscover" })}>Discover Services</button>
      </div>
      <div>
        {(b?.services ?? []).map((svc, i) => (
          <div className="svc" key={i}>
            <div className="mono"><b>Service {svc.uuid}</b></div>
            {svc.characteristics.map((c) => (
              <div className="row mono" key={c.valueHandle} style={{ marginLeft: 8 }}>
                <span>{c.uuid} · h {c.valueHandle} · props 0x{c.props.toString(16)}</span>
                <button onClick={() => send({ kind: "bleRead", handle: c.valueHandle })}>Read</button>
                <button disabled={!(c.props & 0x10)} onClick={() => send({ kind: "bleSubscribe", handle: c.valueHandle })}>Notify</button>
              </div>
            ))}
          </div>
        ))}
        {!!b?.services.length && (
          <button onClick={() => send({ kind: "bleWrite", handle: Number(writeHandle), data: hexEncode(writeData), withResponse: true })}>
            Write
          </button>
        )}
      </div>
      <div className="row">
        <input placeholder="value handle" value={writeHandle} onChange={(e) => setWriteHandle(e.target.value)} style={{ width: 110 }} />
        <input placeholder="text or hex:…" value={writeData} onChange={(e) => setWriteData(e.target.value)} style={{ flex: 1 }} />
      </div>
      <div>
        <h2>Notifications</h2>
        <div className="terminal" style={{ height: 120 }}>
          {data.bleNotifications.map((n, i) => (
            <div key={i}>h{n.handle}: {hexToText(n.data)} <span style={{ color: "var(--fg2)" }}>{n.data}</span></div>
          ))}
        </div>
      </div>
      <div>
        <h2>Controller log</h2>
        <div className="terminal" style={{ height: 100 }}>
          {(b?.logs ?? []).slice(-30).join("\n")}
        </div>
      </div>
    </div>
  );
}

function hexEncode(s: string): string {
  if (s.startsWith("hex:")) return s.slice(4);
  return [...new TextEncoder().encode(s)].map((b) => b.toString(16).padStart(2, "0")).join("");
}
function hexToText(hex: string): string {
  const bytes = hex.match(/.{1,2}/g)?.map((x) => parseInt(x, 16)) ?? [];
  return new TextDecoder().decode(new Uint8Array(bytes)).replace(/[^\x20-\x7e]/g, ".");
}
