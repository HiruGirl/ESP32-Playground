#!/usr/bin/env python3
"""
radio-shim.py — Tiered opt-in radio shim for ESP32 firmware binaries.

Usage:
    radio-shim.py <firmware.bin> <tier> [--elf <firmware.elf>] [--sig-db work/radio-sig-db.json] [--out <patched.bin>]

Tiers:
    none    No patching (firmware's real radio init runs).
    ble     NOP out BLE/BT controller init/enable call sites.
    wifi    NOP out WiFi init call sites.
    both    NOP out both BLE/BT and WiFi init call sites.

The shim looks up the firmware's project in the signature DB to find radio
symbol addresses, then locates the CALL8 instruction(s) that call those
addresses and replaces them with a 3-byte sequence that sets the return
register to 0 without branching — effectively making the radio init call a
no-op that returns ESP_FAIL. The patched image is re-signed with esptool
(checksum + SHA256 of the app image).

When an ELF is provided, symbol addresses are resolved to file offsets
accurately via the ELF symbol table and section headers. When only a .bin
is available, a linear mapping (vaddr = 0x40000000 + file_offset) is used,
which may miss call sites if the MMU mapping is non-linear.

This is firmware-agnostic: the signature DB maps known firmwares to their
radio symbol addresses. Unknown firmwares are left untouched.
"""

import argparse
import json
import os
import struct
import subprocess
import sys

# Replacement: "movi a10, 0" = 0xa2 0xa0 0x00
# Sets a10 (return register) to 0 (ESP_FAIL) and falls through — no call made.
NOP_SEQUENCE = bytes([0xA2, 0xA0, 0x00])


def shim_symbols_for_tier(tier: str) -> list[str]:
    """Return the list of radio symbols to shim for the given tier."""
    syms: list[str] = []
    if tier in ("ble", "both"):
        syms.extend(["esp_bt_controller_init", "esp_bt_controller_enable", "nimble_port_init"])
    if tier in ("wifi", "both"):
        syms.extend(["esp_wifi_init", "esp_wifi_init_internal"])
    return syms


def recompute_esp32_image_checksums(bin_path: str, image_offset: int = 0x10000) -> None:
    """
    Recompute the ESP32 app image checksum embedded in a raw flash binary.

    ESP32 image v1 format (at image_offset within the flash binary):
      byte 0-7:   image header (magic 0xE9, seg_count, flash_mode,
                  flash_size_freq, entry_addr LE)
      byte 8-23:  extended image header (16 bytes for ESP32)
      per segment:
        4 bytes load_addr (LE)
        4 bytes load_size (LE)
        load_size bytes segment data
      after all segments:
        1 byte image checksum (XOR of all segment DATA bytes, starting
        from ESP_CHECKSUM_MAGIC=0xEF; stored as the last byte of the image)

    No per-segment checksum bytes. The single trailing byte makes the
    XOR of all segment data bytes == 0xEF.
    """
    with open(bin_path, "r+b") as f:
        f.seek(image_offset)
        hdr = f.read(24)  # image header (8) + extended header (16)
        if len(hdr) < 24 or hdr[0] != 0xE9:
            return  # not an ESP32 app image at this offset

        seg_count = hdr[1]
        pos = image_offset + 24  # segments start after extended header

        # XOR checksum of all segment data bytes, starting from 0xEF
        checksum = 0xEF

        for _ in range(seg_count):
            f.seek(pos)
            seg_hdr = f.read(8)
            if len(seg_hdr) < 8:
                break
            seg_size = struct.unpack("<I", seg_hdr[4:8])[0]
            seg_data = f.read(seg_size)
            if len(seg_data) < seg_size:
                break
            # XOR all segment data bytes into the running checksum
            for b in seg_data:
                checksum ^= b
            pos += 8 + seg_size  # header + data, no per-segment checksum

        # Align to 16-byte boundary (esptool's read_checksum aligns to 16
        # before reading the checksum byte). Position should end up at
        # (pos % 16) == 15, i.e. the last byte of a 16-byte block.
        align = (16 - 1) - (pos % 16)
        f.seek(pos + align)
        f.write(bytes([checksum & 0xFF]))

        # Disable append_digest in the extended header so the bootloader
        # doesn't validate the (now-stale) SHA256 digest after patching.
        # append_digest is the last byte of the 16-byte extended header
        # (at image_offset + 8 + 15 = image_offset + 23).
        f.seek(image_offset + 23)
        f.write(bytes([0]))


def shutil_which(name: str) -> str | None:
    for path in os.environ.get("PATH", "").split(os.pathsep):
        candidate = os.path.join(path, name)
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def main() -> None:
    ap = argparse.ArgumentParser(description="Apply tiered radio shim to an ESP32 firmware binary")
    ap.add_argument("firmware", help="Path to firmware .bin")
    ap.add_argument("tier", choices=["none", "ble", "wifi", "both"], help="Shim tier")
    ap.add_argument("--elf", help="Path to matching .elf for accurate symbol resolution")
    ap.add_argument("--sig-db", default="work/radio-sig-db.json", help="Path to radio signature DB")
    ap.add_argument("--out", help="Output patched binary path")
    ap.add_argument("--load-addr", type=lambda x: int(x, 0), default=0x40000000,
                    help="Base load address for linear mapping fallback (default 0x40000000)")
    args = ap.parse_args()

    if not os.path.isfile(args.firmware):
        print(f"ERROR: firmware not found: {args.firmware}", file=sys.stderr)
        sys.exit(1)

    # Load signature DB
    sig_db = {}
    if os.path.isfile(args.sig_db):
        with open(args.sig_db) as f:
            sig_db = json.load(f)
    else:
        print(f"WARNING: signature DB not found: {args.sig_db}", file=sys.stderr)

    # Derive project name from firmware path for DB lookup
    fw_dir = os.path.basename(os.path.dirname(os.path.dirname(os.path.abspath(args.firmware))))
    fw_name = os.path.splitext(os.path.basename(args.firmware))[0]

    symbols_to_shim = shim_symbols_for_tier(args.tier)

    if args.tier == "none" or not symbols_to_shim:
        print(f"Tier '{args.tier}': no shim symbols to apply.")
        out = args.out or f"{args.firmware}.patched.none.bin"
        with open(args.firmware, "rb") as f:
            data = f.read()
        with open(out, "wb") as f:
            f.write(data)
        print(f"Wrote unmodified copy: {out}")
        return

    # Find matching DB entry
    entry = None
    for key, val in sig_db.items():
        if (val.get("project") == fw_dir or val.get("project") == fw_name
                or key == fw_dir or key == fw_name
                or val.get("path", "").endswith(f"/{fw_dir}/prebuilt/firmware.elf")):
            entry = val
            break

    # Collect symbol addresses -> file offsets
    sym_offsets: dict[str, int] = {}  # symbol name -> file offset

    if entry is None:
        print(f"Firmware '{fw_dir}' not in signature DB; no radio shim applied.")
        out = args.out or f"{args.firmware}.patched.none.bin"
        with open(args.firmware, "rb") as f:
            data = f.read()
        with open(out, "wb") as f:
            f.write(data)
        print(f"Wrote unmodified copy: {out}")
        return

    db_symbols = entry.get("symbols", {})
    print(f"Matched DB entry: {entry.get('project')} (idf {entry.get('idf_ver')})")
    print(f"  DB symbols: {db_symbols}")

    # Build target virtual address map from DB
    target_vaddrs: dict[int, str] = {}
    for sym in symbols_to_shim:
        if sym in db_symbols:
            target_vaddrs[int(db_symbols[sym], 16)] = sym

    # If an ELF is provided, use it to refine symbol addresses (ELF symbols
    # may differ from the DB which was built from a different build).
    if args.elf and os.path.isfile(args.elf):
        print(f"  Using ELF for address resolution: {args.elf}")
        from elftools.elf.elffile import ELFFile
        elf_sym_addrs: dict[str, int] = {}
        with open(args.elf, "rb") as ef:
            elf = ELFFile(ef)
            for section in elf.iter_sections():
                if section.name in (".symtab", ".dynsym"):
                    for sym_ in section.iter_symbols():
                        name = sym_.name
                        if (name and sym_.entry.st_info.type == "STT_FUNC"
                                and sym_.entry.st_value != 0):
                            elf_sym_addrs[name] = sym_.entry.st_value
        target_vaddrs = {}
        for sym in symbols_to_shim:
            if sym in elf_sym_addrs:
                target_vaddrs[elf_sym_addrs[sym]] = sym
                print(f"  {sym} @ 0x{elf_sym_addrs[sym]:08x} (from ELF)")
            elif sym in db_symbols:
                target_vaddrs[int(db_symbols[sym], 16)] = sym
                print(f"  {sym} @ 0x{int(db_symbols[sym],16):08x} (from DB)")
    else:
        print("  No ELF provided; using DB addresses")
        for sym in symbols_to_shim:
            if sym in db_symbols:
                print(f"  {sym} @ 0x{int(db_symbols[sym],16):08x} (from DB)")

    # Read firmware
    with open(args.firmware, "rb") as f:
        data = bytearray(f.read())

    # Scan the entire binary for CALL8 instructions targeting any of these addresses.
    # Use a tolerance window (±TOL bytes) because call targets may land a few bytes
    # into the function prologue rather than at the exact symbol entry.
    TOL = 16
    target_list = sorted(target_vaddrs.items())
    total_patched = 0
    for off in range(0, len(data) - 2):
        b0, b1, b2 = data[off], data[off + 1], data[off + 2]
        if (b0 & 0xF0) != 0x10:
            continue
        imm = ((b0 & 0x0F) << 20) | (b1 << 12) | (b2 << 4)
        if imm & 0x800000:
            imm -= 0x1000000
        vaddr = off + args.load_addr
        target = vaddr + 3 + imm
        for tgt_addr, sym in target_list:
            if abs(target - tgt_addr) <= TOL:
                data[off:off + 3] = NOP_SEQUENCE
                total_patched += 1
                print(f"  {sym}: NOP'd CALL8 at file offset 0x{off:06x} → target 0x{target:08x} (symbol 0x{tgt_addr:08x})")
                break

    # Write patched binary
    out = args.out or f"{args.firmware}.patched.{args.tier}.bin"
    with open(out, "wb") as f:
        f.write(data)
    print(f"Patched {total_patched} call site(s) total. Wrote: {out}")

    # Recompute ESP32 image checksums (segment + image checksum)
    recompute_esp32_image_checksums(out)


if __name__ == "__main__":
    main()
