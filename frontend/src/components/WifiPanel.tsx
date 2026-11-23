// WiFi panel: scan list, connect form, router status, SoftAP, port forwards.
import { useState } from "react";
import type { SessionData } from "../useSession";

export default function WifiPanel({
  data, send,
}: {
  data: SessionData;
  send: (m: unknown) => void;
}) {
  const w = data.wifi;
  const [ssid, setSsid] = useState("ESP32Playground");
  const [password, setPassword] = useState("playground123");
  return (
    <div className="panel">
      <h2>Virtual WiFi</h2>
      <div className="row">
        <button className="primary" onClick={() => send({ kind: "wifiScan" })} disabled={w?.scanning}>
          {w?.scanning ? "Scanning…" : "Scan Networks"}
        </button>
        <span className={`badge ${w?.connected ? "running" : ""}`}>
          {w?.connected ? `STA ${w.connected.ip}` : "STA idle"}
        </span>
        <span className={`badge ${w?.softap ? "running" : ""}`}>
          {w?.softap ? `AP ${w.softap.ssid}` : "AP off"}
        </span>
      </div>
      <div style={{ display: "flex", flexDirection: "column", gap: 6 }}>
        {(w?.aps ?? []).map((ap) => (
          <div className="ap-row" key={ap.bssid}>
            <div>
              <b>{ap.ssid}</b> <span className="mono">{ap.auth}</span>
              <div className="mono">{ap.bssid} · ch {ap.channel} · {ap.rssi} dBm</div>
            </div>
            <button onClick={() => send({ kind: "wifiConnect", ssid: ap.ssid, password: ap.auth === "open" ? "" : password })}>
              Connect
            </button>
          </div>
        ))}
        {!w?.aps.length && !w?.scanning && <div className="mono">No scan results yet.</div>}
      </div>
      <div className="row">
        <input value={ssid} onChange={(e) => setSsid(e.target.value)} placeholder="SSID" style={{ width: 130 }} />
        <input value={password} onChange={(e) => setPassword(e.target.value)} placeholder="Password" type="password" style={{ width: 130 }} />
        <button onClick={() => send({ kind: "wifiConnect", ssid, password })}>Join</button>
      </div>
      <div className="row">
        <button onClick={() => send({ kind: "wifiSoftap", enable: true, ssid: "ESP32Playground-AP" })}>Start SoftAP</button>
        <button onClick={() => send({ kind: "wifiSoftap", enable: false })}>Stop SoftAP</button>
      </div>
      <div className="row mono">
        <span>TX {w?.txFrames ?? 0} frames</span>
        <span>RX {w?.rxFrames ?? 0} frames</span>
        <span>DNS {w?.dns ?? "—"}</span>
      </div>
      <div className="mono">
        Router: guest gets 10.0.2.15/24 via DHCP · gateway/host 10.0.2.2 ·
        inbound port-forward (hostfwd) for the web-server example: host :8077 → guest :80.
      </div>
    </div>
  );
}
