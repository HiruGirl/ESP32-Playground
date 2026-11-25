#!/usr/bin/env bash
# Package release artifacts into dist/ (see RELEASE_NOTES.md).
#
#   dist/esp32-playground-qemu-<ver>-linux-x86_64.tar.gz
#       prebuilt qemu-system-xtensa + qemu-system-riscv32 + start.sh
#   dist/esp32-playground-backend-<ver>.tar.gz
#       backend bundle (TypeScript sources + build tooling)
#   dist/SHA256SUMS
#
# Prerequisites: scripts/build-deps.sh + scripts/build-qemu.sh done.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${1:-1.0.0}"
QEMU_BUILD="$ROOT/work/espressif-qemu/build"
DIST="$ROOT/dist"
QNAME="esp32-playground-qemu-$VER-linux-x86_64"
BNAME="esp32-playground-backend-$VER"

[ -x "$QEMU_BUILD/qemu-system-xtensa" ] || { echo "missing qemu-system-xtensa (run scripts/build-qemu.sh)"; exit 1; }
[ -x "$QEMU_BUILD/qemu-system-riscv32" ] || { echo "missing qemu-system-riscv32 (run scripts/build-qemu.sh)"; exit 1; }

rm -rf "$DIST"
mkdir -p "$DIST/$QNAME" "$DIST/$BNAME"

# ---- qemu bundle ----------------------------------------------------------
cp "$QEMU_BUILD/qemu-system-xtensa" "$QEMU_BUILD/qemu-system-riscv32" "$DIST/$QNAME/"
cat > "$DIST/$QNAME/start.sh" << 'EOF'
#!/usr/bin/env bash
# Launch ESP32 Playground QEMU standalone (without the Node backend).
#   FIRMWARE=path/to/firmware.bin [PORT=5555] [ELF=path/to/firmware.elf] ./start.sh
# Then connect a bridge client to 127.0.0.1:$PORT (chardev socket protocol).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${FIRMWARE:?set FIRMWARE=/path/to/firmware.bin}"
PORT="${PORT:-5555}"
ELF_ARG=()
[ -n "${ELF:-}" ] && ELF_ARG=(",elf=$ELF")
exec "$HERE/qemu-system-xtensa" \
  -machine esp32 -display none -serial none \
  -drive "file=$FIRMWARE,if=mtd,format=raw" \
  -icount 10 \
  -chardev "socket,id=espsock,host=127.0.0.1,port=$PORT,server=on,wait=off" \
  -netdev user,id=net0 \
  -device "espplay.bridge,chardev=espsock,netdev=net0,cave-addr=0x400A0000${ELF_ARG[*]}"
EOF
chmod +x "$DIST/$QNAME/start.sh"
cat > "$DIST/$QNAME/README.md" << 'EOF'
# ESP32 Playground — QEMU bundle (linux-x86_64)

Patched Espressif QEMU (esp32 + riscv32 targets) with the `espplay`
virtual-hardware family (GPIO/LEDC/RMT, UART, I2C, WiFi, BLE).

* `qemu-system-xtensa` — ESP32 machine
* `qemu-system-riscv32` — ESP32-C3 machine
* `start.sh` — standalone launcher (see script header for usage)

Normally these binaries are driven by the Node backend from
`esp32-playground-backend-*.tar.gz`. Build reproducibility: see
`scripts/build-qemu.sh` in the source repository (QEMU fork + patch
`qemu-patches/espressif-qemu-espplay.patch`).
EOF

# ---- backend bundle -------------------------------------------------------
cp "$ROOT/backend/package.json" "$ROOT/backend/package-lock.json" \
   "$ROOT/backend/esbuild.mjs" "$ROOT/backend/tsconfig.json" "$DIST/$BNAME/"
cp -r "$ROOT/backend/src" "$DIST/$BNAME/src"
cat > "$DIST/$BNAME/README.md" << 'EOF'
# ESP32 Playground — backend bundle

    npm install && npm run build
    node dist/server.js        # http://127.0.0.1:8760  (/api/health)

Set `ESPPLAY_QEMU` / `ESPPLAY_QEMU_RISCV` to the binaries from the qemu
bundle (default: work/espressif-qemu/build/qemu-system-xtensa in the repo).
EOF

# ---- checksums -----------------------------------------------------------
(cd "$DIST" && sha256sum "$QNAME.tar.gz.tmp" 2>/dev/null || true)
tar -C "$DIST" -czf "$DIST/$QNAME.tar.gz" "$QNAME"
tar -C "$DIST" -czf "$DIST/$BNAME.tar.gz" "$BNAME"
rm -rf "$DIST/$QNAME" "$DIST/$BNAME"
(cd "$DIST" && sha256sum *.tar.gz > SHA256SUMS && cat SHA256SUMS)
echo ">>> dist ready under $DIST"
