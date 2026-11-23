// Virtual WiFi: AP simulation, STA connection flow, router panel state.
import { EventEmitter } from "node:events";
import { MSG, WIFI_EVENTS, frame } from "./protocol.js";
import type { QemuSession } from "./qemu.js";

export interface VirtualAP {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  auth: "open" | "wpa2";
  password?: string;
}

export interface WifiState {
  scanning: boolean;
  aps: VirtualAP[];
  connected: { ssid: string; ip: string; rssi: number } | null;
  softap: { ssid: string; ip: string; clients: number } | null;
  txFrames: number;
  rxFrames: number;
  ip: string | null;
  dns: string;
  portForwards: { hostPort: number; guestPort: number }[];
}

export class WifiSim extends EventEmitter {
  state: WifiState = {
    scanning: false,
    aps: [],
    connected: null,
    softap: null,
    txFrames: 0,
    rxFrames: 0,
    ip: null,
    dns: "10.0.2.3",
    portForwards: [],
  };

  private session: QemuSession;
  private timers: NodeJS.Timeout[] = [];
  private configured: { ssid: string; password: string } | null = null;

  constructor(session: QemuSession) {
    super();
    this.session = session;
    this.session.on("bridge", (m) => this.onBridge(m));
  }

  /** Virtual radio environment: the playground AP + neighbors. */
  private scanResults(): VirtualAP[] {
    return [
      { ssid: "ESP32Playground", bssid: "24:6f:28:00:00:01", rssi: -42, channel: 6, auth: "wpa2", password: "playground123" },
      { ssid: "Cafe-Guest", bssid: "aa:bb:cc:11:22:33", rssi: -67, channel: 1, auth: "open" },
      { ssid: "Home-Network", bssid: "3c:37:86:55:44:21", rssi: -78, channel: 11, auth: "wpa2", password: "secretpass" },
    ];
  }

  private onBridge(m: { type: number; payload: Buffer }) {
    switch (m.type) {
      case MSG.WIFI_SCAN:
        this.handleScan();
        break;
      case MSG.WIFI_CONNECT_REQ: {
        const ssidLen = m.payload[0] ?? 0;
        const ssid = m.payload.subarray(1, 1 + ssidLen).toString();
        const pwLen = m.payload[1 + ssidLen] ?? 0;
        const pw = m.payload.subarray(2 + ssidLen, 2 + ssidLen + pwLen).toString();
        this.handleConnect(ssid, pw);
        break;
      }
      case MSG.WIFI_TX:
        this.state.txFrames++;
        this.emit("update");
        break;
      case MSG.WIFI_RX_FRAME:
        this.state.rxFrames++;
        this.emit("update");
        break;
    }
  }

  private handleScan() {
    this.state.scanning = true;
    this.emit("update");
    const t = setTimeout(() => {
      this.state.aps = this.scanResults();
      this.state.scanning = false;
      // SCAN_DONE event + results are pushed as WIFI_EVENT to the hooks.
      this.session.send(MSG.WIFI_EVENT, Buffer.from([WIFI_EVENTS.SCAN_DONE, 0]));
      this.emit("update");
    }, 600);
    this.timers.push(t);
  }

  private handleConnect(ssid: string, password: string) {
    const ap = this.scanResults().find((a) => a.ssid === ssid);
    this.configured = { ssid, password };
    this.emit("config", this.configured);
    if (!ap || (ap.auth === "wpa2" && ap.password !== password)) {
      const t = setTimeout(() => {
        this.session.send(MSG.WIFI_EVENT, Buffer.from([WIFI_EVENTS.STA_DISCONNECTED, 1]));
        this.emit("update");
      }, 400);
      this.timers.push(t);
      return;
    }
    const t1 = setTimeout(() => {
      this.session.send(MSG.WIFI_EVENT, Buffer.from([WIFI_EVENTS.STA_CONNECTED, 0]));
    }, 350);
    const t2 = setTimeout(() => {
      // user-mode net DHCP: 10.0.2.15/24, gw 10.0.2.2 (host), dns 10.0.2.3
      const ip = "10.0.2.15";
      this.state.connected = { ssid, ip, rssi: ap.rssi };
      this.state.ip = ip;
      const p = Buffer.concat([Buffer.from([ip.length]), Buffer.from(ip)]);
      this.session.send(MSG.WIFI_EVENT, Buffer.from([WIFI_EVENTS.GOT_IP, 0]));
      this.session.send(MSG.WIFI_RESULT, p);
      this.emit("update");
    }, 700);
    this.timers.push(t1, t2);
  }

  /** SoftAP mode for the guest (basic implementation). */
  startSoftAp(ssid: string) {
    this.state.softap = { ssid, ip: "192.168.4.1", clients: 0 };
    this.session.send(MSG.WIFI_EVENT, Buffer.from([WIFI_EVENTS.AP_START, 0]));
    this.emit("update");
  }

  stopSoftAp() {
    this.state.softap = null;
    this.session.send(MSG.WIFI_EVENT, Buffer.from([WIFI_EVENTS.AP_STOP, 0]));
    this.emit("update");
  }

  dispose() {
    for (const t of this.timers) clearTimeout(t);
    this.timers = [];
  }
}
