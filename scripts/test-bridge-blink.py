#!/usr/bin/env python3
"""ESP32 Playground QEMU bridge smoke test: run a blink firmware image and
verify GPIO2 toggles via the bridge protocol.

usage: test-bridge-blink.py [firmware.bin] [firmware.elf] [seconds]
"""
import socket, subprocess, struct, sys, time, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEMU = f"{ROOT}/work/espressif-qemu/build/qemu-system-xtensa"
FW = sys.argv[1] if len(sys.argv) > 1 else f"{ROOT}/examples/blink/prebuilt/firmware.bin"
ELF = sys.argv[2] if len(sys.argv) > 2 else f"{ROOT}/examples/blink/prebuilt/firmware.elf"
DURATION = float(sys.argv[3]) if len(sys.argv) > 3 else 25.0

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0))
port = srv.getsockname()[1]
srv.listen(1)

cmd = [QEMU, "-machine", "esp32", "-nographic",
       "-drive", f"file={FW},if=mtd,format=raw",
       "-icount", "3",
       "-chardev", f"socket,id=espsock,host=127.0.0.1,port={port},server=off",
       "-device", f"espplay.bridge,chardev=espsock,elf={ELF},cave-addr=0x400A0000"]
print("spawning qemu:", " ".join(cmd), flush=True)
proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
srv.settimeout(10)
try:
    conn, _ = srv.accept()
except socket.timeout:
    print("FAIL: bridge never connected")
    proc.kill()
    sys.exit(1)
print("bridge connected", flush=True)

buf = b""
gpio_events = []
logs = []
t0 = time.time()
conn.settimeout(1.0)
while time.time() - t0 < DURATION:
    try:
        chunk = conn.recv(65536)
    except socket.timeout:
        continue
    if not chunk:
        break
    buf += chunk
    while len(buf) >= 5:
        (ln,) = struct.unpack("<I", buf[:4])
        if ln == 0 or ln > 70000:
            buf = buf[1:]
            continue
        if len(buf) < 4 + ln:
            break
        typ = buf[4]
        payload = buf[5:4 + ln]
        buf = buf[4 + ln:]
        if typ == 0x02 and len(payload) >= 2:
            gpio_events.append((time.time() - t0, payload[0], payload[1]))
        elif typ == 0x01:
            print("HELLO:", payload.hex(), flush=True)
        elif typ == 0x10:
            logs.append(payload.decode(errors="replace"))
            print("LOG:", logs[-1], flush=True)
        elif typ == 0x04 and len(payload) >= 10:
            freq = struct.unpack("<I", payload[1:5])[0]
            duty = struct.unpack("<I", payload[6:10])[0]
            print(f"LEDC: timer={payload[0]} freq={freq} ch={payload[5]} duty={duty/100:.1f}%",
                  flush=True)

proc.kill()
qout = proc.stdout.read().decode(errors="replace")
print(f"gpio events: {len(gpio_events)}")
for e in gpio_events[:16]:
    print(f"  t={e[0]:.3f} pin={e[1]} level={e[2]}")
lv = [e[2] for e in gpio_events if e[1] == 2]
print("pin2 transitions:", len(lv), "sequence:", lv[:12])
ok = len(lv) >= 6 and lv[:6] == [1, 0, 1, 0, 1, 0]
if not ok:
    print("=== QEMU OUTPUT (tail) ===")
    print(qout[-3000:])
print("BLINK TEST:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
