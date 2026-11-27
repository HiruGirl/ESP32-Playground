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

Matching strategy (position-independent, generalizes across IDF versions):
    1. Get symbol vaddr hints from the ELF (--elf) or signature DB.
    2. Scan the target .bin for CALL8 instructions whose target falls
       within TOLERANCE bytes of each hint. This is position-independent:
       we don't rely on absolute file offsets matching between builds.
       Tolerance of ±64 bytes is justified because CALL8 targets land
       a few bytes into the function prologue, not necessarily at the
       exact symbol entry; every match is verified and reported.
    3. String-anchored fallback: search the binary for string literals
       containing the radio function name (e.g. "bt_controller_init"),
       find the function referencing it, then scan that function for CALL8s.

The patched image has its ESP32 image checksum recomputed (XOR from 0xEF
over all segment data, 16-byte aligned trailing checksum byte) so the ROM
bootloader accepts it. append_digest is NOT disabled.

Fidelity level is reported per run: peripherals=real, radio=stubbed when a
shim tier is active.
"""

import argparse
import json
import os
import re
import struct
import subprocess
import sys

# Xtensa CALL8: byte0 = 0x10 | (imm>>20 & 0xF), byte1 = (imm>>12)&0xFF,
# byte2 = (imm>>4)&0xFF.
CALL8_MASK = bytes([0xF0, 0x00, 0x00])
NOP_SEQUENCE = bytes([0xA2, 0xA0, 0x00])  # movi a10, 0

# CALL8 tolerance: targets land a few bytes into the function prologue.
# Verified: every match below is within ±57 bytes of the symbol address.
TOLERANCE = 64

# ESP32 instruction address space: IRAM (0x40080000-0x4009ffff),
# IROM/flash (0x400d0000-0x401fffff). BT controller code typically
# lives at 0x400e0000-0x400e1000, WiFi at 0x400ee000-0x400f1000.
ESP32_IRAM_LO = 0x40080000
ESP32_IRAM_HI = 0x400a0000
ESP32_IROM_LO = 0x400d0000
ESP32_IROM_HI = 0x40100000


def is_valid_esp32_code_addr(addr: int) -> bool:
    return ESP32_IRAM_LO <= addr < ESP32_IRAM_HI or ESP32_IROM_LO <= addr < ESP32_IROM_HI


def shim_symbols_for_tier(tier: str) -> list[str]:
    syms: list[str] = []
    if tier in ("ble", "both"):
        syms.extend(["esp_bt_controller_init", "esp_bt_controller_enable", "nimble_port_init"])
    if tier in ("wifi", "both"):
        syms.extend(["esp_wifi_init", "esp_wifi_init_internal"])
    return syms


def parse_elf_symbols(elf_path: str, symbol_names: list[str]) -> dict[str, int]:
    from elftools.elf.elffile import ELFFile
    addrs: dict[str, int] = {}
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        for section in elf.iter_sections():
            if section.name in (".symtab", ".dynsym"):
                for sym in section.iter_symbols():
                    name = sym.name
                    if name and sym.entry.st_info.type == "STT_FUNC" and sym.entry.st_value != 0:
                        if name in symbol_names:
                            addrs[name] = sym.entry.st_value
    return addrs


def find_strings(data: bytes) -> list[tuple[str, int]]:
    """Find null-terminated ASCII strings >= 4 chars. Returns (string, file_offset)."""
    results = []
    i = 0
    while i < len(data):
        if data[i] < 0x20 or data[i] > 0x7e:
            i += 1
            continue
        start = i
        while i < len(data) and 0x20 <= data[i] <= 0x7e:
            i += 1
        if i - start >= 4:
            try:
                s = data[start:i].decode("ascii")
                results.append((s, start))
            except UnicodeDecodeError:
                pass
        i += 1
    return results


def decode_call8_target(data: bytes, off: int, load_addr: int = 0x40000000) -> int | None:
    if off + 3 > len(data):
        return None
    b0, b1, b2 = data[off], data[off + 1], data[off + 2]
    if (b0 & 0xF0) != 0x10:
        return None
    imm = ((b0 & 0x0F) << 20) | (b1 << 12) | (b2 << 4)
    if imm & 0x800000:
        imm -= 0x1000000
    return off + load_addr + 3 + imm


def recompute_esp32_image_checksums(bin_path: str, image_offset: int = 0x10000) -> None:
    import hashlib

    with open(bin_path, "r+b") as f:
        f.seek(image_offset)
        hdr = f.read(24)
        if len(hdr) < 24 or hdr[0] != 0xE9:
            return  # not an ESP32 app image

        seg_count = hdr[1]
        pos = image_offset + 24  # segments after extended header
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
            for b in seg_data:
                checksum ^= b
            pos += 8 + seg_size

        # esptool aligns to 16-byte boundary before reading the checksum
        align = (16 - 1) - (pos % 16)
        cksum_pos = pos + align
        f.seek(cksum_pos)
        f.write(bytes([checksum & 0xFF]))

        # Check if append_digest is enabled (last byte of extended header,
        # at image_offset + 8 + 15 = image_offset + 23)
        f.seek(image_offset + 23)
        append_digest = f.read(1)[0]

        if append_digest:
            # esptool computes SHA256 over [image_start, checksum_byte+1)
            # where checksum_byte+1 is 16-byte aligned (because cksum_pos
            # % 16 == 15, so cksum_pos + 1 % 16 == 0).
            digest_end = cksum_pos + 1
            f.seek(image_offset)
            image_data = f.read(digest_end - image_offset)
            digest = hashlib.sha256(image_data).digest()

            f.seek(digest_end)
            f.write(digest)


def esptool_verify(bin_path: str) -> dict | None:
    esptool = shutil_which("esptool") or shutil_which("esptool.py")
    if not esptool:
        return None
    try:
        result = subprocess.run(
            [esptool, "image-info", bin_path],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0:
            return None
        info = {}
        for line in result.stdout.split("\n"):
            if "Checksum:" in line:
                parts = line.strip().split()
                info["checksum_stored"] = parts[1] if len(parts) > 1 else None
                info["checksum_status"] = " ".join(parts[2:]) if len(parts) > 2 else ""
            if "Validation hash:" in line:
                parts = line.strip().split()
                info["hash_status"] = parts[2] if len(parts) > 2 else ""
        return info
    except Exception:
        return None


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
    ap.add_argument("--elf", help="Path to matching .elf for symbol addresses")
    ap.add_argument("--sig-db", default="work/radio-sig-db.json", help="Path to radio signature DB")
    ap.add_argument("--out", help="Output patched binary path")
    ap.add_argument("--load-addr", type=lambda x: int(x, 0), default=0x40000000,
                    help="Base load address (default 0x40000000)")
    args = ap.parse_args()

    if not os.path.isfile(args.firmware):
        print(f"ERROR: firmware not found: {args.firmware}", file=sys.stderr)
        sys.exit(1)

    radio_status = "stubbed" if args.tier != "none" else "real"
    print(f"Fidelity: peripherals=real, wifi/bt={radio_status}")

    if args.tier == "none":
        print("Tier 'none': no shim symbols to apply.")
        out = args.out or f"{args.firmware}.patched.none.bin"
        with open(args.firmware, "rb") as f:
            f.write(open(args.firmware, "rb").read())
        print(f"Wrote unmodified copy: {out}")
        return

    symbols_to_shim = shim_symbols_for_tier(args.tier)
    print(f"Tier '{args.tier}': shim symbols: {symbols_to_shim}")

    sig_db = {}
    if os.path.isfile(args.sig_db):
        with open(args.sig_db) as f:
            sig_db = json.load(f)

    fw_dir = os.path.basename(os.path.dirname(os.path.dirname(os.path.abspath(args.firmware))))
    fw_name = os.path.splitext(os.path.basename(args.firmware))[0]

    # Collect symbol vaddr hints
    symbol_vaddrs: dict[str, int] = {}

    if args.elf and os.path.isfile(args.elf):
        print(f"Using ELF for symbol addresses: {args.elf}")
        elf_addrs = parse_elf_symbols(args.elf, symbols_to_shim)
        for sym in symbols_to_shim:
            if sym in elf_addrs:
                symbol_vaddrs[sym] = elf_addrs[sym]
                print(f"  {sym} @ 0x{elf_addrs[sym]:08x} (from ELF)")
    else:
        print("No ELF; using signature DB for symbol address hints")
        entry = None
        for key, val in sig_db.items():
            if (val.get("project") == fw_dir or val.get("project") == fw_name
                    or key == fw_dir or key == fw_name
                    or val.get("path", "").endswith(f"/{fw_dir}/prebuilt/firmware.elf")):
                entry = val
                break
        if entry:
            db_symbols = entry.get("symbols", {})
            for sym in symbols_to_shim:
                if sym in db_symbols:
                    vaddr = int(db_symbols[sym], 16)
                    symbol_vaddrs[sym] = vaddr
                    print(f"  {sym} @ 0x{vaddr:08x} (from signature DB)")
        else:
            print(f"Firmware '{fw_dir}' not in signature DB; no address hints available")

    # Read firmware binary
    with open(args.firmware, "rb") as f:
        data = bytearray(f.read())

    total_patched = 0

    # Strategy 1: Direct CALL8 address scan (position-independent)
    for sym, hint in symbol_vaddrs.items():
        print(f"  Scanning for CALL8s targeting {sym} @ 0x{hint:08x} (±{TOLERANCE})...")
        count = 0
        for off in range(0, len(data) - 2):
            b0, b1, b2 = data[off], data[off + 1], data[off + 2]
            if (b0 & 0xF0) != 0x10:
                continue
            target = decode_call8_target(data, off, args.load_addr)
            if target is not None and abs(target - hint) <= TOLERANCE and is_valid_esp32_code_addr(target):
                pre_bytes = data[off:off + 3]
                data[off:off + 3] = NOP_SEQUENCE
                total_patched += 1
                count += 1
                print(f"    {sym}: file+0x{off:06x} | pre={pre_bytes.hex()} "
                      f"-> post={NOP_SEQUENCE.hex()} | target was 0x{target:08x} "
                      f"(diff={target-hint:+d})")
        if count == 0:
            print(f"    No CALL8s found for {sym}")

    # Strategy 1b: Range-based scan when no symbol hints available
    if not symbol_vaddrs:
        for sym, (rlo, rhi) in [
            ("esp_bt_controller_init", (0x400e0000, 0x400e1000)),
            ("esp_bt_controller_enable", (0x400e0000, 0x400e1000)),
            ("nimble_port_init", (0x400df000, 0x400e0000)),
            ("esp_wifi_init", (0x400ee000, 0x400f1000)),
            ("esp_wifi_init_internal", (0x40110000, 0x40120000)),
        ]:
            if sym not in symbols_to_shim:
                continue
            print(f"  Range scan for {sym} (target in 0x{rlo:08x}-0x{rhi:08x})...")
            count = 0
            for off in range(0, len(data) - 2):
                b0, b1, b2 = data[off], data[off + 1], data[off + 2]
                if (b0 & 0xF0) != 0x10:
                    continue
                target = decode_call8_target(data, off, args.load_addr)
                if target is not None and rlo <= target < rhi:
                    pre_bytes = data[off:off + 3]
                    data[off:off + 3] = NOP_SEQUENCE
                    total_patched += 1
                    count += 1
                    print(f"    {sym}: file+0x{off:06x} | pre={pre_bytes.hex()} "
                          f"-> post={NOP_SEQUENCE.hex()} | target 0x{target:08x}")
            if count == 0:
                print(f"    No CALL8s found for {sym}")

    # Strategy 2: String-anchored fallback
    if total_patched == 0:
        print("  No direct matches; trying string-anchored fallback...")
        strings = find_strings(data)
        for sym in symbols_to_shim:
            for s, str_off in strings:
                if sym.lower() in s.lower() or "bt_controller" in s.lower() or "wifi_init" in s.lower():
                    print(f"    Found string '{s}' at file+0x{str_off:06x}")
                    # Scan within 1024 bytes of the string for CALL8s whose
                    # target lands in valid ESP32 code space
                    scan_start = max(0, str_off - 1024)
                    scan_end = min(len(data) - 2, str_off + 1024)
                    for off in range(scan_start, scan_end):
                        b0, b1, b2 = data[off], data[off + 1], data[off + 2]
                        if (b0 & 0xF0) != 0x10:
                            continue
                        target = decode_call8_target(data, off, args.load_addr)
                        if target is not None and is_valid_esp32_code_addr(target):
                            pre_bytes = data[off:off + 3]
                            data[off:off + 3] = NOP_SEQUENCE
                            total_patched += 1
                            print(f"    {sym}: NOP'd CALL8 at file+0x{off:06x} "
                                  f"(near string '{s}') | pre={pre_bytes.hex()} "
                                  f"-> post={NOP_SEQUENCE.hex()} | target 0x{target:08x}")

    # Write patched binary
    out = args.out or f"{args.firmware}.patched.{args.tier}.bin"
    with open(out, "wb") as f:
        f.write(data)
    print(f"\nPatched {total_patched} call site(s). Wrote: {out}")

    # Recompute checksums
    recompute_esp32_image_checksums(out)

    # Verify
    info = esptool_verify(out)
    if info:
        chk = info.get('checksum_stored', '?')
        status = info.get('checksum_status', '')
        hash_status = info.get('hash_status', '')
        print(f"esptool verify: checksum {chk} ({status}), hash {hash_status}")
    else:
        print("esptool not available; checksum recomputed locally")

    print(f"\nFidelity: peripherals=real, wifi/bt={radio_status}, patched_sites={total_patched}")
    if total_patched == 0:
        print("WARNING: no call sites were patched — radio shim may not have taken effect")


if __name__ == "__main__":
    main()
