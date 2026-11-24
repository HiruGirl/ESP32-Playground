#!/usr/bin/env bash
# Reproducible build of the patched Espressif QEMU fork for ESP32 Playground.
#
# Prerequisites (built by scripts/build-deps.sh):
#   work/deps/           native libraries (glib, zlib, libffi, pcre2, gcrypt)
#   meson + ninja on PATH (pip install meson ninja)
# Clone + patch + build is fully scripted; subproject mirrors come from
# GitHub (gitlab.freedesktop.org is not reachable from this environment).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_DIR="$ROOT/work/espressif-qemu"
PREFIX="$ROOT/work/deps"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSTEM_INCLUDE_PATH=""
export PKG_CONFIG_SYSTEM_LIBRARY_PATH=""
export PATH="$PREFIX/bin:$HOME/.local/bin:$PATH"

if [ ! -d "$QEMU_DIR/.git" ]; then
  echo ">>> cloning espressif/qemu @ 093b0c3"
  git clone https://github.com/espressif/qemu.git "$QEMU_DIR"
  (cd "$QEMU_DIR" && git checkout 093b0c3)
fi

if [ ! -e "$QEMU_DIR/hw/misc/espplay_bridge.c" ]; then
  echo ">>> applying ESP32 Playground patches"
  cp -r "$ROOT/qemu-patches/overlay/." "$QEMU_DIR/"
  (cd "$QEMU_DIR" && git apply "$ROOT/qemu-patches/espressif-qemu-espplay.patch")
fi

# Subprojects whose upstream wraps point at unreachable gitlab hosts:
# pre-clone GitHub mirrors at the exact wrap revisions.
SUB="$QEMU_DIR/subprojects"
clone_at() { # dir url rev
  if [ ! -e "$1/.git" ]; then
    echo ">>> subproject $1"
    rm -rf "$1"
    git clone "$2" "$1"
    (cd "$1" && git fetch --depth 1 origin "$3" && git checkout -q FETCH_HEAD)
  fi
}
clone_at "$SUB/dtc" https://github.com/dgibson/dtc.git b6910bec11614980a21e46fbccc35934b671bd81 || true
clone_at "$SUB/slirp" https://github.com/deepin-community/libslirp.git dd76415fce457e319d665eb8210d05c5731360ba || true
clone_at "$SUB/keycodemapdb" https://github.com/qemu/keycodemapdb.git f5772a62ec52591ff6870b7e8ef32482371f22c6
clone_at "$SUB/berkeley-softfloat-3" https://github.com/ucb-bar/berkeley-softfloat-3.git b64af41c3276f97f0e181920400ee056b9c88037
clone_at "$SUB/berkeley-testfloat-3" https://github.com/ucb-bar/berkeley-testfloat-3.git e7af9751d9f9fd3b47911f51a5cfd08af256a9ab
cp -r "$SUB/packagefiles/berkeley-softfloat-3/." "$SUB/berkeley-softfloat-3/"
cp -r "$SUB/packagefiles/berkeley-testfloat-3/." "$SUB/berkeley-testfloat-3/"

mkdir -p "$QEMU_DIR/build"
cd "$QEMU_DIR/build"
if [ ! -f build.ninja ]; then
  ../configure --target-list=xtensa-softmmu,riscv32-softmmu \
    --disable-pixman --disable-gtk --disable-sdl --disable-vnc \
    --disable-spice --disable-docs --disable-tools --disable-usb-redir \
    --disable-libusb --disable-werror
fi
ninja qemu-system-xtensa qemu-system-riscv32
echo ">>> QEMU built:"
ls -la qemu-system-xtensa qemu-system-riscv32
