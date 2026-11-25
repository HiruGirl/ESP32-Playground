// QEMU session manager: process lifecycle + bridge socket + QMP control.
import { ChildProcess, spawn } from "node:child_process";
import { createServer, Server, Socket } from "node:net";
import { EventEmitter } from "node:events";
import { mkdtempSync, writeFileSync, rmSync, existsSync, copyFileSync, openSync, readFileSync } from "node:fs";
import { join, resolve, basename } from "node:path";
import { tmpdir } from "node:os";
import { MSG, FrameParser, frame, BridgeMessage } from "./protocol.js";

export interface SessionConfig {
  chip: "esp32" | "esp32s3" | "esp32c3";
  firmware: string; // path to merged .bin
  elf?: string; // optional .elf for symbol hooks
  icount?: number;
  touch?: { bus: number; csPin?: number; irqPin?: number };
  sdImage?: string; // optional CYD SPI SD image; copied per session
  flashSize?: string; // e.g. "2MB"
  tft?: { bus: number; dcPin?: number; w?: number; h?: number }; // virtual SPI LCD
}

export interface SessionInfo {
  id: string;
  chip: string;
  state: "starting" | "running" | "paused" | "stopped" | "crashed";
  firmware: string;
  uptimeMs: number;
  pid?: number;
}

const CAVE: Record<string, string> = {
  esp32: "0x400A0000",
  esp32s3: "0x400A0000",
  esp32c3: "0x403E7000",
};

export class QemuSession extends EventEmitter {
  readonly id: string;
  readonly cfg: SessionConfig;
  state: SessionInfo["state"] = "starting";
  proc?: ChildProcess;
  private socket?: Socket;
  private parser = new FrameParser();
  private bridge?: Server;
  private tmp: string;
  private startedAt = 0;
  private qemuPath: string;
  private port = 0;

  constructor(id: string, cfg: SessionConfig, qemuPath: string) {
    super();
    this.id = id;
    this.cfg = cfg;
    this.qemuPath = qemuPath;
    this.tmp = mkdtempSync(join(tmpdir(), `espplay-${id}-`));
  }

  get info(): SessionInfo {
    return {
      id: this.id,
      chip: this.cfg.chip,
      state: this.state,
      firmware: basename(this.cfg.firmware),
      uptimeMs: this.startedAt ? Date.now() - this.startedAt : 0,
      pid: this.proc?.pid,
    };
  }

  async start(): Promise<void> {
    // Flash image is copied per session so NVS persists across resets/restarts.
    const flash = join(this.tmp, "flash.bin");
    copyFileSync(this.cfg.firmware, flash);

    const sd = this.cfg.sdImage ? join(this.tmp, "sd.img") : undefined;
    if (sd && this.cfg.sdImage) copyFileSync(this.cfg.sdImage, sd);

    const port = await this.listenBridge();
    this.port = port;
    const machine = this.cfg.chip;
    const args = [
      "-machine", machine + (sd ? ",sd-spi=on" : ""),
      "-display", "none",
      "-serial", "none",
      "-drive", `file=${flash},if=mtd,format=raw`,
      ...(sd ? ["-drive", `file=${sd},if=sd,format=raw`] : []),
      "-icount", String(Math.max(1, Math.min(this.cfg.icount ?? 3, 10))), // QEMU esp32 icount shift max = 10
      "-chardev", `socket,id=espsock,host=127.0.0.1,port=${port},server=off`,
      "-netdev", "user,id=net0",
      "-device", `espplay.bridge,chardev=espsock,netdev=net0,cave-addr=${CAVE[machine]}` +
        (this.cfg.touch ? `,touch-bus=${this.cfg.touch.bus},touch-cs-pin=${this.cfg.touch.csPin ?? 33},touch-irq-pin=${this.cfg.touch.irqPin ?? 36}` : "") +
        (this.cfg.elf ? `,elf=${resolve(this.cfg.elf)}` : "") +
        (this.cfg.tft
          ? `,tft-bus=${this.cfg.tft.bus}` +
            `,tft-dc-pin=${this.cfg.tft.dcPin ?? 2}` +
            `,tft-w=${this.cfg.tft.w ?? 240}` +
            `,tft-h=${this.cfg.tft.h ?? 320}`
          : ""),
    ];
    this.proc = spawn(this.qemuPath, args, { cwd: this.tmp, stdio: ["ignore", "pipe", "pipe"] });
    this.startedAt = Date.now();
    this.state = "running";

    let out = "";
    this.proc.stdout?.on("data", (d) => { out += d.toString(); this.emit("console", d.toString()); });
    this.proc.stderr?.on("data", (d) => { out += d.toString(); this.emit("console", d.toString()); });
    this.proc.on("exit", (code, signal) => {
      this.state = code === 0 || signal === "SIGTERM" ? "stopped" : "crashed";
      this.emit("exit", { code, signal, output: out.slice(-8192) });
      this.cleanup();
    });
    this.proc.on("error", (err) => {
      this.state = "crashed";
      this.emit("exit", { code: -1, signal: null, output: String(err) });
    });
  }

  private listenBridge(): Promise<number> {
    return new Promise((res, rej) => {
      this.bridge = createServer((sock) => {
        this.socket = sock;
        for (const f of this.pending) sock.write(f);
        this.pending = [];
        sock.on("data", (chunk) => {
          for (const m of this.parser.push(chunk)) this.dispatch(m);
        });
        sock.on("close", () => this.emit("bridge-close"));
        sock.on("error", () => undefined);
      });
      this.bridge.on("error", rej);
      this.bridge.listen(0, "127.0.0.1", () => {
        res((this.bridge!.address() as { port: number }).port);
      });
    });
  }

  private dispatch(m: BridgeMessage) {
    this.emit("bridge", m);
  }

  /** Send a backend→QEMU command frame (queued until the bridge connects). */
  private pending: Buffer[] = [];

  send(type: number, payload: Buffer = Buffer.alloc(0)) {
    const f = frame(type, payload);
    if (this.socket && !this.socket.destroyed) {
      this.socket.write(f);
    } else {
      this.pending.push(f);
      if (this.pending.length > 256) this.pending.shift();
    }
  }

  // --- controls -----------------------------------------------------------
  private qmp(cmd: object): Promise<object | undefined> {
    // Lightweight QMP over a one-shot unix socket connection per command.
    return new Promise((res) => {
      const qmpPath = join(this.tmp, "qmp.sock");
      if (!existsSync(qmpPath)) return res(undefined);
      const net = require("node:net") as typeof import("node:net");
      const s = net.createConnection(qmpPath);
      let buf = "";
      const done = (v?: object) => { s.destroy(); res(v); };
      const timer = setTimeout(() => done(undefined), 1500);
      s.on("data", (d) => {
        buf += d.toString();
        const lines = buf.split("\r\n").filter(Boolean);
        for (const line of lines) {
          let msg: { return?: object; error?: object };
          try { msg = JSON.parse(line); } catch { continue; }
          if (msg.return !== undefined || msg.error !== undefined) {
            clearTimeout(timer);
            done(msg.return ?? msg.error);
            return;
          }
        }
        if (buf.includes('"QMP"')) {
          s.write(JSON.stringify({ execute: "qmp_capabilities" }) + "\r\n");
          setTimeout(() => s.write(JSON.stringify(cmd) + "\r\n"), 50);
        }
      });
      s.on("error", () => { clearTimeout(timer); done(undefined); });
    });
  }

  async pause() {
    if (this.state !== "running") return;
    await this.qmp({ execute: "stop" });
    this.state = "paused";
    this.emit("state", this.state);
  }

  async resume() {
    if (this.state !== "paused") return;
    await this.qmp({ execute: "cont" });
    this.state = "running";
    this.emit("state", this.state);
  }

  async reset() {
    this.send(MSG.STATE_RESET);
    await this.qmp({ execute: "system_reset" });
    this.emit("state", this.state);
    this.emit("bridge", { type: MSG.STATE_RESET, payload: Buffer.alloc(0) });
  }

  /** Save/restore snapshot = flash image + restart (documented approach). */
  saveSnapshot(name: string): string {
    const dest = join(this.tmp, `snap-${name.replace(/[^a-z0-9_-]/gi, "")}.bin`);
    copyFileSync(join(this.tmp, "flash.bin"), dest);
    return dest;
  }

  restoreSnapshot(name: string): boolean {
    const src = join(this.tmp, `snap-${name.replace(/[^a-z0-9_-]/gi, "")}.bin`);
    if (!existsSync(src)) return false;
    copyFileSync(src, join(this.tmp, "flash.bin"));
    return true;
  }

  stop() {
    if (this.proc && !this.proc.killed) {
      this.proc.kill("SIGTERM");
      setTimeout(() => this.proc?.kill("SIGKILL"), 1500).unref();
    }
    this.cleanup();
    this.state = "stopped";
  }

  private cleanup() {
    this.socket?.destroy();
    this.bridge?.close();
  }

  destroy() {
    this.stop();
    try { rmSync(this.tmp, { recursive: true, force: true }); } catch { /* best effort */ }
  }
}
