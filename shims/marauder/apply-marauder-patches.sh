#!/usr/bin/env bash
# apply-marauder-patches.sh
#
# MARAUDER-SPECIFIC SHIM — not part of the core QEMU fixes.
#
# Reproducible script: assemble Marauder binary patch copies from the untouched
# release firmware. The release binary (work/firmware/marauder-4mb.bin) is NEVER
# modified or committed; only scratch copies under work/ are produced.
#
# Patches (binary function-entry / call-site patching):
#   1. BLE init skip:   call8 esp_bt_controller_init  -> movi a10, 0  (ESP_OK)
#   2. BLE enable skip: call8 esp_bt_controller_enable -> movi a10, 0 (ESP_OK)
#
# The app image checksum and SHA256 are recomputed with esptool after patching
# so the bootloader accepts the modified image.
#
# Usage:
#   shims/marauder/apply-marauder-patches.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT"

SRC="$ROOT/work/firmware/marauder-4mb.bin"
OUT="$ROOT/work/marauder-patched.bin"

python3 - "$SRC" "$OUT" <<'PYEOF'
import sys
sys.path.insert(0, '/usr/local/lib/python3.11/dist-packages')
from esptool.bin_image import ESP32FirmwareImage

src, out = sys.argv[1], sys.argv[2]

# Load the app image from the full flash (app starts at 0x10000)
full = open(src, 'rb').read()
assert len(full) == 4 * 1024 * 1024, f'unexpected flash size: {len(full)}'

app_data = full[0x10000:]

with open('/tmp/marauder-app-tmp.bin', 'wb') as f:
    f.write(app_data)

with open('/tmp/marauder-app-tmp.bin', 'rb') as f:
    img = ESP32FirmwareImage(load_file=f)

print(f"Loaded app image: {len(img.segments)} segments, entry={img.entrypoint:#x}")
print(f"Original checksum: {img.checksum:#04x}")

# Find Seg 2 (IROM at 0x400d0020) — contains esp_bt_controller_init/enable
seg2 = img.segments[2]
seg2.data = bytearray(seg2.data)

# Patch 1: call8 esp_bt_controller_init at vaddr 0x4010879f
patch1_seg_off = 0x4010879f - seg2.addr
assert seg2.data[patch1_seg_off:patch1_seg_off+3] == bytes.fromhex('a53913'), \
    f'Patch 1 bytes mismatch at seg offset {patch1_seg_off:#x}: {seg2.data[patch1_seg_off:patch1_seg_off+3].hex()}'
seg2.data[patch1_seg_off:patch1_seg_off+3] = bytes.fromhex('a2a000')  # movi a10, 0

# Patch 2: call8 esp_bt_controller_enable at vaddr 0x401087bb
patch2_seg_off = 0x401087bb - seg2.addr
assert seg2.data[patch2_seg_off:patch2_seg_off+3] == bytes.fromhex('a501ed'), \
    f'Patch 2 bytes mismatch at seg offset {patch2_seg_off:#x}: {seg2.data[patch2_seg_off:patch2_seg_off+3].hex()}'
seg2.data[patch2_seg_off:patch2_seg_off+3] = bytes.fromhex('a2a000')  # movi a10, 0

# Add names to segments (required by esptool save)
for i, seg in enumerate(img.segments):
    if not hasattr(seg, 'name') or seg.name is None:
        seg.name = f'seg{i}'

# Save with esptool (auto-recomputes checksum and SHA256)
img.save('/tmp/marauder-patched-app-tmp.bin')

# Verify with esptool image-info
import subprocess
result = subprocess.run(
    ['esptool.py', 'image-info', '/tmp/marauder-patched-app-tmp.bin'],
    capture_output=True, text=True
)
print(result.stdout)
if result.returncode != 0:
    print(result.stderr, file=sys.stderr)
    sys.exit(1)

# Reassemble into full 4MB flash
patched_app = open('/tmp/marauder-patched-app-tmp.bin', 'rb').read()
result_full = bytearray(full)
result_full[0x10000:0x10000+len(patched_app)] = patched_app
open(out, 'wb').write(result_full)

print(f"\nWrote {out} ({len(result_full)} bytes)")
print("Patches applied:")
print("  0x4010879f: call8 esp_bt_controller_init -> movi a10, 0 (ESP_OK)")
print("  0x401087bb: call8 esp_bt_controller_enable -> movi a10, 0 (ESP_OK)")
print("  Checksum and SHA256 recomputed by esptool")
PYEOF

echo
echo "--- Verifying patched image against source ---"
python3 - "$SRC" "$OUT" <<'PY'
import sys
from pathlib import Path
a = Path(sys.argv[1]).read_bytes()
b = Path(sys.argv[2]).read_bytes()
diffs = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
print(f'differing bytes: {len(diffs)}')
for i in diffs:
    print(f'  {i:#08x}: {a[i]:02x} -> {b[i]:02x}')
PY