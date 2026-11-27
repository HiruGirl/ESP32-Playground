#!/usr/bin/env bash
# build-qemu.sh — Reproducible Espressif QEMU build for ESP32 Playground
#
# Clones espressif/qemu at the pinned upstream commit, applies the playground
# patches, and builds qemu-system-xtensa and qemu-system-riscv32.
#
# Usage:
#   scripts/build-qemu.sh                # full clone + patch + build
#   scripts/build-qemu.sh --no-clone     # patch + build existing checkout
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU_DIR="$ROOT/work/espressif-qemu"
PATCH_DIR="$ROOT/qemu-patches"
PINNED_COMMIT="1b75d0fd51"
QEMU_REPO="https://github.com/espressif/qemu.git"
BUILD_DIR="$QEMU_DIR/build"
NPROC="${NPROC:-$(nproc 2>/dev/null || echo 4)}"

NO_CLONE=0
if [[ "${1:-}" == "--no-clone" ]]; then
  NO_CLONE=1
fi

# --- clone ---------------------------------------------------------------
if [[ "$NO_CLONE" -eq 0 ]]; then
  if [[ -d "$QEMU_DIR/.git" ]]; then
    echo "QEMU checkout already exists at $QEMU_DIR (use --no-clone to skip)"
  else
    echo "Cloning $QEMU_REPO → $QEMU_DIR"
    git clone --filter=blobless "$QEMU_REPO" "$QEMU_DIR"
    cd "$QEMU_DIR"
    git checkout "$PINNED_COMMIT"
  fi
else
  if [[ ! -d "$QEMU_DIR/.git" ]]; then
    echo "ERROR: --no-clone but $QEMU_DIR/.git not found" >&2
    exit 1
  fi
  cd "$QEMU_DIR"
  CURRENT=$(git rev-parse --short HEAD 2>/dev/null || true)
  if [[ "$CURRENT" != "$PINNED_COMMIT"* ]]; then
    echo "Checking out pinned commit $PINNED_COMMIT (currently $CURRENT)"
    git checkout "$PINNED_COMMIT"
  fi
fi

cd "$QEMU_DIR"

# --- apply patches -------------------------------------------------------
echo "Applying QEMU patches from $PATCH_DIR"
for patch in "$PATCH_DIR"/00*.patch; do
  [[ -f "$patch" ]] || continue
  echo "  $(basename "$patch")"
  git am "$patch" 2>/dev/null || {
    echo "  patch already applied or conflict; skipping"
  }
done

# --- build dependencies --------------------------------------------------
echo "Checking build dependencies"
for dep in meson ninja pkg-config python3 gcc; do
  if ! command -v "$dep" >/dev/null 2>&1; then
    echo "ERROR: $dep not found on PATH" >&2
    exit 1
  fi
done

# --- configure -----------------------------------------------------------
echo "Configuring QEMU build (targets: xtensa-softmmu, riscv32-softmmu)"
meson setup "$BUILD_DIR" \
  --target-list=xtensa-softmmu,riscv32-softmmu \
  "$@" 2>&1

# --- build ---------------------------------------------------------------
echo "Building QEMU with $NPROC jobs"
ninja -C "$BUILD_DIR" -j"$NPROC"

echo ""
echo "=== QEMU build complete ==="
echo "  xtensa:  $BUILD_DIR/qemu-system-xtensa"
echo "  riscv32: $BUILD_DIR/qemu-system-riscv32"
echo ""
echo "Verify machine options (sd-spi / sd-spi-hspi):"
"$BUILD_DIR/qemu-system-xtensa" -machine esp32,help 2>&1 | grep -E "sd-spi|sd-spi-hspi"
