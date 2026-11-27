# QEMU Patches for ESP32 Playground

These patches are applied on top of the pinned upstream Espressif QEMU commit.

## Pinned upstream commit

`1b75d0fd51` — `QEMU: save espplay patches + CPU1 boot_addr experiment (to be reverted)`

## Patches

| Patch | Description |
|-------|-------------|
| `0001-Fix-CPU1-per-CPU-reset-to-start-from-ROM-reset-vector.patch` | CPU1 per-CPU reset: initialize PC to `XCHAL_RESET_VECTOR_PADDR` (0x40000400), handle APPCPU_RESET deassert (1→0) to release CPU1 from stall. Without this, CPU1 never starts and the guest deadlocks after "Using external SPI configuration...". |
| `0002-Add-CPU0-per-CPU-reset-to-match-CPU1-handling.patch` | CPU0 per-CPU reset: same treatment as CPU1. Previously used `qemu_system_reset_request` which reset the entire system. Now both CPUs are handled per-CPU via the GPIO path. Full-system resets (watchdog, esp_restart) still reset both CPUs. |

## Apply script

Use `apply-qemu-patches.sh` to apply all patches to a clean checkout of the pinned upstream commit:

```bash
cd work/espressif-qemu
git checkout 1b75d0fd51
../../qemu-patches/apply-qemu-patches.sh
```

## Build

```bash
cd work/espressif-qemu
meson setup build
ninja -C build
```

## RSA / libgcrypt workaround

The QEMU build uses `object_class_by_name(TYPE_ESP32_RSA)` to conditionally
include the RSA device, because the system's libgcrypt may not provide the
symbols QEMU expects at link time. This avoids undefined-reference errors
when building against newer libgcrypt versions that dropped the legacy ESP32
RSA API. See `work/espressif-qemu/hw/xtensa/esp32.c` for the conditional
guard around `device_cold_reset(DEVICE(&s->rsa))`.

## Pre-existing esplay patch

`espressif-qemu-espplay.patch` and the `overlay/` directory contain the
pre-existing esplay QEMU patches that were already in the repo before this
work began. These are part of the pinned base commit `1b75d0fd51`.
