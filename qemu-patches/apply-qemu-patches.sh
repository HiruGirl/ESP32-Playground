#!/usr/bin/env bash
# apply-qemu-patches.sh
#
# Apply all QEMU patches in this directory to a clean checkout of the pinned
# upstream Espressif QEMU commit (1b75d0fd51).
#
# Usage:
#   cd work/espressif-qemu
#   git checkout 1b75d0fd51
#   ../../qemu-patches/apply-qemu-patches.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_DIR="$SCRIPT_DIR"

# Verify we're in the QEMU repo
if [[ ! -f meson.build ]] || [[ ! -d hw/xtensa ]]; then
  echo "ERROR: must be run from the espressif-qemu repository root" >&2
  exit 1
fi

# Verify we're at the pinned commit
PINNED="1b75d0fd51"
CURRENT=$(git rev-parse --short HEAD 2>/dev/null || true)
if [[ "$CURRENT" != "$PINNED"* ]]; then
  echo "WARNING: expected to be at commit $PINNED, but HEAD is $CURRENT" >&2
  echo "         patches may not apply cleanly" >&2
fi

# Apply patches in order
for patch in "$PATCH_DIR"/00*.patch; do
  [[ -f "$patch" ]] || continue
  echo "Applying $(basename "$patch")..."
  git am "$patch"
done

echo "All QEMU patches applied successfully."
