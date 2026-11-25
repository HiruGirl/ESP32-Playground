# Third-Party Components

ESP32 Playground bundles, patches, or links the following third-party
software. Each component remains under its own license.

## Emulation core

| Component | Source | License |
| --- | --- | --- |
| QEMU (esp32/esp32s3/esp32c3 machines) | github.com/espressif/qemu @ 093b0c3 (QEMU 9.2-based) | GPLv2 (portions BSD-2-Clause / MIT, per QEMU LICENSE) |
| libslirp (user-net) | github.com/deepin-community/libslirp @ dd76415 | BSD-3-Clause |
| dtc (device tree compiler) | github.com/dgibson/dtc @ b6910be | BSD-2-Clause (dual GPL-2.0) |
| keycodemapdb | github.com/qemu/keycodemapdb @ f5772a6 | MIT-ish (see repo) |
| berkeley-softfloat-3 / berkeley-testfloat-3 | ucb-bar mirrors | BSD-3-Clause |

## Host build dependencies (scripts/build-deps.sh)

| Component | License |
| --- | --- |
| pkgconf | ISC |
| zlib | zlib license |
| libffi | MIT |
| PCRE2 | BSD-3-Clause |
| GLib 2.78 | LGPL-2.1-or-later |
| libgpg-error / libgcrypt | LGPL-2.1-or-later / LGPL-2.1-or-later |

## Guest SDKs (for building the examples)

| Component | License |
| --- | --- |
| esp-idf v4.4.7 | Apache-2.0 (Espressif Systems) |
| NimBLE (Apache Mynewt) | Apache-2.0 |
| arduino-esp32 2.0.17 | LGPL-2.1 (core), per-library licenses for bundled libs |

## Backend / frontend

| Component | License |
| --- | --- |
| Node.js runtime | MIT |
| express | MIT |
| ws | MIT |
| esbuild | MIT |
| TypeScript | Apache-2.0 |
| React | MIT |
| Vite | MIT |
| esptool | GPL-2.0-or-later (Espressif) |

The ESP32 Playground sources (backend, frontend, examples, tests, scripts,
`qemu-patches/overlay/` device sources) are MIT — see LICENSE.
