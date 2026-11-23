// Virtual BLE: HCI controller emulation + Virtual Phone (ATT central).
//
// The firmware's NimBLE host speaks HCI through the esp_vhci_host_* seam;
// messages ride the bridge as HCI_UP (guest→us) and HCI_DOWN (us→guest).
// This module is a real HCI controller implementation (events, ACL) plus a
// real ATT client ("Virtual Phone") that talks GATT to the guest's NimBLE
// GATT server over ATT PDUs — no mocked results.
import { EventEmitter } from "node:events";
import { MSG, frame } from "./protocol.js";
import type { QemuSession } from "./qemu.js";

const HCI_CMD = 0x01, HCI_ACL = 0x02, HCI_EVT = 0x04;

// OGF/OCF
const RESET = 0x0c03, READ_BD_ADDR = 0x1009, READ_LOCAL_VERSION = 0x1001;
const READ_BUFFER_SIZE = 0x1005, LE_SET_ADV_PARAMS = 0x2006, LE_SET_ADV_DATA = 0x2008;
const LE_SET_SCAN_RSP = 0x2009, LE_SET_ADV_ENABLE = 0x200a, LE_SET_RANDOM_ADDR = 0x2005;
const LE_READ_BUFFER_SIZE = 0x2002, LE_SET_EVENT_MASK = 0x2001, SET_EVENT_MASK = 0x0c01;
const DISCONNECT = 0x0406, LE_CREATE_CONN_CANCEL = 0x200e;

export interface BleState {
  controllerUp: boolean;
  advertising: boolean;
  advData: string;
  connected: boolean;
  peerAddress: string;
  services: GattService[];
  logs: string[];
}

export interface GattCharacteristic {
  handle: number;
  valueHandle: number;
  uuid: string;
  props: number;
  value?: Buffer;
}

export interface GattService {
  startHandle: number;
  endHandle: number;
  uuid: string;
  characteristics: GattCharacteristic[];
}

const CONN_HANDLE = 0x0001;

export class BleController extends EventEmitter {
  state: BleState = {
    controllerUp: false, advertising: false, advData: "", connected: false,
    peerAddress: "", services: [], logs: [],
  };
  private session: QemuSession;
  private aclFromPhone: Buffer[] = [];

  constructor(session: QemuSession) {
    super();
    this.session = session;
    this.session.on("bridge", (m) => {
      if (m.type === MSG.HCI_UP) this.onGuestPacket(m.payload);
      if (m.type === MSG.STATE_RESET) this.reset();
    });
  }

  private log(s: string) {
    this.state.logs.push(s);
    if (this.state.logs.length > 200) this.state.logs.shift();
    this.emit("update");
  }

  private sendToGuest(hci: Buffer) {
    this.session.send(MSG.HCI_DOWN, Buffer.concat([Buffer.from([(hci.length) & 0xff, (hci.length >> 8) & 0xff]), hci]));
  }

  private event(evt: number, params: Buffer) {
    this.sendToGuest(Buffer.concat([Buffer.from([HCI_EVT, evt, params.length]), params]));
  }

  private commandComplete(opcode: number, returnParams: Buffer) {
    this.event(0x0e, Buffer.concat([Buffer.from([1, opcode & 0xff, (opcode >> 8) & 0xff, 0x00]), returnParams]));
  }

  private commandStatus(opcode: number, status = 0x00) {
    this.event(0x0f, Buffer.from([status, 1, opcode & 0xff, (opcode >> 8) & 0xff]));
  }

  // ---- HCI command handling (guest → controller) ------------------------
  private onGuestPacket(payload: Buffer) {
    if (payload.length < 2) return;
    const n = payload.readUInt16LE(0);
    const pkt = payload.subarray(2, 2 + n);
    if (pkt.length < 1) return;
    if (pkt[0] === HCI_CMD) this.onCommand(pkt.subarray(1));
    else if (pkt[0] === HCI_ACL) this.onGuestAcl(pkt.subarray(1));
  }

  private onCommand(cmd: Buffer) {
    if (cmd.length < 3) return;
    const opcode = cmd.readUInt16LE(0);
    const params = cmd.subarray(3);
    switch (opcode) {
      case RESET:
        this.commandComplete(opcode, Buffer.alloc(0));
        this.log("HCI Reset");
        break;
      case READ_BD_ADDR:
        this.commandComplete(opcode, Buffer.concat([Buffer.from([0x00]), Buffer.from("246f2800beef", "hex")]));
        break;
      case READ_LOCAL_VERSION:
        this.commandComplete(opcode, Buffer.from([0x00, 0x09, 0x00, 0x00, 0x09, 0x1d, 0x00, 0x00]));
        break;
      case READ_BUFFER_SIZE:
        this.commandComplete(opcode, Buffer.from([0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00]));
        break;
      case LE_READ_BUFFER_SIZE:
        this.commandComplete(opcode, Buffer.from([0x00, 0x1b, 0x00, 0x01]));
        break;
      case SET_EVENT_MASK:
      case LE_SET_EVENT_MASK:
        this.commandComplete(opcode, Buffer.from([0x00]));
        break;
      case LE_SET_RANDOM_ADDR:
      case LE_SET_ADV_PARAMS:
      case LE_SET_ADV_DATA:
      case LE_SET_SCAN_RSP:
        this.commandComplete(opcode, Buffer.from([0x00]));
        break;
      case LE_SET_ADV_ENABLE: {
        const on = params[0] === 1;
        this.state.advertising = on;
        this.commandComplete(opcode, Buffer.from([0x00]));
        this.log(on ? "Advertising enabled" : "Advertising disabled");
        break;
      }
      case DISCONNECT:
        this.commandStatus(opcode, 0x00);
        this.handleDisconnect();
        break;
      case LE_CREATE_CONN_CANCEL:
        this.commandComplete(opcode, Buffer.from([0x00]));
        break;
      default:
        // Accept unknown commands with success to keep the stack alive.
        this.commandStatus(opcode, 0x00);
        break;
    }
  }

  // ---- ACL (guest ↔ phone) ---------------------------------------------
  private onGuestAcl(hdr: Buffer) {
    if (hdr.length < 4) return;
    const dlen = hdr.readUInt16LE(2);
    const data = hdr.subarray(4, 4 + dlen);
    // Route to the Virtual Phone's ATT client.
    this.emit("att", Buffer.from(data));
    this.log(`ACL ← guest ${data.length}B`);
  }

  sendAclFromPhone(att: Buffer) {
    if (!this.state.connected) return;
    const hdr = Buffer.alloc(4);
    hdr.writeUInt16LE(CONN_HANDLE | 0x2000, 0); // handle + PB flag
    hdr.writeUInt16LE(att.length, 2);
    this.sendToGuest(Buffer.concat([Buffer.from([HCI_ACL]), hdr, att]));
    this.log(`ACL → guest ${att.length}B`);
  }

  // ---- Phone actions ---------------------------------------------------
  phoneConnect(): boolean {
    if (!this.state.advertising || this.state.connected) return false;
    this.state.connected = true;
    this.state.peerAddress = "24:6f:28:00:be:ef";
    this.state.services = [];
    // LE Meta: Connection Complete (role=1 central... guest is peripheral → role 0)
    const addr = Buffer.from("246f2800beef", "hex");
    this.event(0x3e, Buffer.concat([
      Buffer.from([0x0b, 0x01, 0x00, 0x00, 0x00]), addr,
      Buffer.from([0x18, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00]),
    ]));
    this.log("Phone connected");
    return true;
  }

  phoneDisconnect() {
    if (!this.state.connected) return;
    this.handleDisconnect();
  }

  private handleDisconnect() {
    if (!this.state.connected) return;
    this.state.connected = false;
    this.state.services = [];
    // Disconnection Complete event (reason 0x13 remote user terminated)
    this.event(0x05, Buffer.from([0x00, CONN_HANDLE & 0xff, 0x00, 0x13]));
    this.log("Phone disconnected");
    this.emit("update");
  }

  reset() {
    this.state.advertising = false;
    this.state.connected = false;
    this.state.services = [];
  }

  // ---- ATT client (the Virtual Phone) -----------------------------------
  private attSeq: { resolve: (b: Buffer) => void } | null = null;

  /** Exchange one ATT request/response with the guest GATT server. */
  async attRequest(pdu: Buffer, timeoutMs = 2000): Promise<Buffer> {
    if (!this.state.connected) throw new Error("not connected");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.attSeq = null;
        reject(new Error("ATT timeout"));
      }, timeoutMs);
      this.attSeq = {
        resolve: (b) => { clearTimeout(timer); this.attSeq = null; resolve(b); },
      };
      this.sendAclFromPhone(pdu);
    });
  }

  /** Called when ACL data arrives from the guest (notifications + responses). */
  handleAttFromGuest(data: Buffer) {
    if (data.length === 0) return;
    const op = data[0];
    if (op === 0x1b || op === 0x1d) {
      // Handle Value Notification / Indication → phone UI
      const handle = data.readUInt16LE(1);
      const value = data.subarray(3);
      this.emit("notification", { handle, value: Buffer.from(value) });
      this.log(`Notification handle=${handle} ${value.toString("hex").slice(0, 32)}`);
      return;
    }
    if (this.attSeq) this.attSeq.resolve(Buffer.from(data));
  }

  /** Full GATT service discovery via Read By Group Type / Read By Type. */
  async discoverServices(): Promise<GattService[]> {
    const svcs: GattService[] = [];
    let start = 1;
    for (let guard = 0; guard < 16; guard++) {
      const req = Buffer.from([0x10, start & 0xff, start >> 8, 0xff, 0xff, 0x00, 0x28]);
      const rsp = await this.attRequest(req);
      if (rsp[0] === 0x01 || rsp[0] !== 0x11) break; // error → done
      const elen = rsp[1];
      let off = 2;
      while (off + elen <= rsp.length) {
        const sh = rsp.readUInt16LE(off);
        const eh = rsp.readUInt16LE(off + 2);
        const uuid16 = elen === 6 ? rsp.readUInt16LE(off + 4).toString(16).padStart(4, "0") : rsp.subarray(off + 4).reverse().toString("hex");
        svcs.push({ startHandle: sh, endHandle: eh, uuid: `0x${uuid16}`, characteristics: [] });
        off += elen;
      }
      const last = svcs[svcs.length - 1];
      start = last.endHandle + 1;
      if (last.endHandle >= 0xffff) break;
    }
    for (const svc of svcs) svc.characteristics = await this.discoverCharacteristics(svc);
    this.state.services = svcs;
    this.emit("update");
    return svcs;
  }

  private async discoverCharacteristics(svc: GattService): Promise<GattCharacteristic[]> {
    const chars: GattCharacteristic[] = [];
    let start = svc.startHandle;
    for (let guard = 0; guard < 32; guard++) {
      const req = Buffer.from([0x08, start & 0xff, start >> 8, svc.endHandle & 0xff, svc.endHandle >> 8, 0x03, 0x28]);
      const rsp = await this.attRequest(req);
      if (rsp[0] === 0x01 || rsp[0] !== 0x09) break;
      const elen = rsp[1];
      let off = 2;
      while (off + elen <= rsp.length) {
        const decl = rsp.readUInt16LE(off);
        const props = rsp[off + 2];
        const vh = rsp.readUInt16LE(off + 3);
        const uuid16 = elen === 7 ? rsp.readUInt16LE(off + 5).toString(16).padStart(4, "0") : rsp.subarray(off + 5).reverse().toString("hex");
        chars.push({ handle: decl, valueHandle: vh, uuid: `0x${uuid16}`, props });
        off += elen;
      }
      start = chars[chars.length - 1].handle + 1;
      if (chars.length && chars[chars.length - 1].handle >= svc.endHandle) break;
    }
    return chars;
  }

  async readCharacteristic(valueHandle: number): Promise<Buffer> {
    const rsp = await this.attRequest(Buffer.from([0x0a, valueHandle & 0xff, valueHandle >> 8]));
    if (rsp[0] === 0x0b) return rsp.subarray(1);
    if (rsp[0] === 0x01) throw new Error(`ATT error ${rsp[3]}`);
    return Buffer.alloc(0);
  }

  async writeCharacteristic(valueHandle: number, value: Buffer, withResponse = true): Promise<void> {
    const op = withResponse ? 0x12 : 0x52;
    const pdu = Buffer.concat([Buffer.from([op, valueHandle & 0xff, valueHandle >> 8]), value]);
    if (withResponse) {
      const rsp = await this.attRequest(pdu);
      if (rsp[0] === 0x01) throw new Error(`ATT error ${rsp[3]}`);
    } else {
      this.sendAclFromPhone(pdu);
    }
  }

  /** Enable notifications: write CCCD 0x2902 = 0x0001 (discovered via Find Info). */
  async subscribe(valueHandle: number): Promise<void> {
    // Find descriptors in [valueHandle+1 .. next char handle]
    let cccd = valueHandle + 1;
    for (let h = valueHandle + 1; h < valueHandle + 6; h++) {
      const rsp = await this.attRequest(Buffer.from([0x05, h & 0xff, h >> 8, (h + 4) & 0xff, (h + 4) >> 8]));
      if (rsp[0] === 0x06 && rsp[1] === 0x01) {
        for (let off = 2; off + 4 <= rsp.length; off += 4) {
          const uuid = rsp.readUInt16LE(off + 2);
          if (uuid === 0x2902) { cccd = rsp.readUInt16LE(off); break; }
        }
        break;
      }
    }
    await this.writeCharacteristic(cccd, Buffer.from([0x01, 0x00]));
  }
}
