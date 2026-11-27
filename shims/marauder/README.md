# Marauder-specific shims

This directory contains Marauder v1.21.0-specific binary patches. These are
**firmware-specific hacks** — they are NOT part of the core QEMU fixes and
should not be used with other firmware images.

## apply-marauder-patches.sh

Applies binary function-entry patches to a scratch copy of the Marauder
release firmware (`work/firmware/marauder-4mb.bin`). The release binary is
**never modified** — patches are written to `work/marauder-patched.bin`.

Patches applied:
1. `call8 esp_bt_controller_init` → `movi a10, 0` (return ESP_OK)
2. `call8 esp_bt_controller_enable` → `movi a10, 0` (return ESP_OK)

The ESP32 app image checksum and SHA256 are recomputed after patching so the
bootloader accepts the modified image.

**Note:** This is a temporary shim. It will be replaced by the generic radio
shim (see docs/ARCHITECTURE.md, section "Radio shim").
