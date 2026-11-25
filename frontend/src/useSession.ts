// Session state + WebSocket bridge hook.
import { useCallback, useEffect, useRef, useState } from "react";
import { paintTftFrame } from "./tft";

export interface WifiState {
  scanning: boolean;
  aps: { ssid: string; bssid: string; rssi: number; channel: number; auth: string }[];
  connected: { ssid: string; ip: string; rssi: number } | null;
  softap: { ssid: string; ip: string; clients: number } | null;
  txFrames: number;
  rxFrames: number;
  ip: string | null;
  dns: string;
}

export interface BleService {
  uuid: string;
  characteristics: { handle: number; valueHandle: number; uuid: string; props: number }[];
}

export interface BleState {
  controllerUp: boolean;
  advertising: boolean;
  connected: boolean;
  services: BleService[];
  logs: string[];
}

export interface SessionData {
  id: string | null;
  state: string;
  uart: string[];
  gpio: Record<number, number>;
  gpioModes: Record<number, number>;
  ledc: { freq: number; duty: number } | null;
  lcdText: string;
  displays: Record<string, string>;
  rmt: string[];
  logs: string[];
  wifi: WifiState | null;
  ble: BleState | null;
  bleNotifications: { handle: number; data: string; t: number }[];
  connected: boolean;
}

const initial: SessionData = {
  id: null, state: "idle", uart: [], gpio: {}, gpioModes: {}, ledc: null,
  lcdText: "", displays: {}, rmt: [], logs: [], wifi: null, ble: null,
  bleNotifications: [], connected: false,
};

export function useSession() {
  const [data, setData] = useState<SessionData>(initial);
  const ws = useRef<WebSocket | null>(null);
  const sid = useRef<string | null>(null);
  const tftCanvas = useRef<HTMLCanvasElement | null>(null);
  const tftPixels = useRef<Uint8ClampedArray>(new Uint8ClampedArray(240 * 320 * 4));
  const tftScheduled = useRef(false);

  const send = useCallback((msg: unknown) => {
    if (ws.current?.readyState === WebSocket.OPEN) ws.current.send(JSON.stringify(msg));
  }, []);

  const connect = useCallback((id: string) => {
    sid.current = id;
    tftPixels.current.fill(0);
    tftCanvas.current?.getContext("2d")?.clearRect(0, 0, 240, 320);
    const proto = location.protocol === "https:" ? "wss" : "ws";
    const sock = new WebSocket(`${proto}://${location.host}/ws?session=${id}`);
    ws.current = sock;
    sock.onopen = () => setData((d) => ({ ...d, id, connected: true, state: "running" }));
    sock.onclose = () => setData((d) => ({ ...d, connected: false }));
    sock.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.kind === "tft") {
        paintTftFrame(tftPixels.current, m);
        if (!tftScheduled.current) {
          tftScheduled.current = true;
          requestAnimationFrame(() => {
            tftScheduled.current = false;
            const ctx = tftCanvas.current?.getContext("2d");
            if (ctx) ctx.putImageData(new ImageData(new Uint8ClampedArray(tftPixels.current), 240, 320), 0, 0);
          });
        }
        return;
      }
      setData((d) => {
        switch (m.kind) {
          case "uart":
            return { ...d, uart: [...d.uart.slice(-1999), m.data] };
          case "gpio":
            return { ...d, gpio: { ...d.gpio, [m.pin]: m.level } };
          case "gpioMode":
            return { ...d, gpioModes: { ...d.gpioModes, [m.pin]: m.mode } };
          case "ledc":
            return { ...d, ledc: { freq: m.freq, duty: m.duty } };
          case "rmt":
            return { ...d, rmt: [...d.rmt.slice(-199), m.hex] };
          case "lcd":
            return { ...d, lcdText: m.text };
          case "display":
            return { ...d, displays: { ...d.displays, [`${m.bus}-${m.addr}`]: m.fb } };
          case "log":
            return { ...d, logs: [...d.logs.slice(-299), m.text] };
          case "wifi":
            return { ...d, wifi: m.state };
          case "ble":
            return { ...d, ble: m.state };
          case "bleNotify":
            return { ...d, bleNotifications: [...d.bleNotifications.slice(-99), { handle: m.handle, data: m.data, t: Date.now() }] };
          case "sessionState":
            return { ...d, state: m.state };
          case "exit":
            return { ...d, state: "crashed", logs: [...d.logs, `session exited (code ${m.code})`] };
          default:
            return d;
        }
      });
    };
  }, []);

  useEffect(() => () => { ws.current?.close(); }, []);
  return { data, send, connect, tftCanvas };
}

export async function api(path: string, body?: unknown, method = "POST") {
  const res = await fetch(path, {
    method: body === undefined && method === "POST" ? "GET" : method,
    headers: body !== undefined ? { "Content-Type": "application/json" } : undefined,
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  return res.json();
}
