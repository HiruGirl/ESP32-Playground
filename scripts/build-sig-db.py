#!/usr/bin/env python3
"""Build radio signature database from prebuilt ELFs.

Scans each example ELF for:
  - IDF version from .flash.appdesc section
  - Radio init function addresses (esp_wifi_init, esp_bt_controller_init,
    esp_bt_controller_enable, NimBLE init) from symbol table

Writes: work/radio-sig-db.json
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, '/usr/local/lib/python3.11/dist-packages')
from elftools.elf.elffile import ELFFile

EXAMPLES_DIR = Path("examples")
OUTPUT = Path("work/radio-sig-db.json")

RADIO_SYMBOLS = [
    "esp_wifi_init",
    "esp_wifi_init_internal",
    "esp_bt_controller_init",
    "esp_bt_controller_enable",
    "esp_bt_controller_get_status",
    "nimble_port_init",
    "NimBLEDevice::init",
]


def read_app_desc(elf_path):
    """Extract IDF version and project name from .flash.appdesc."""
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        for section in elf.iter_sections():
            if section.name == ".flash.appdesc":
                data = section.data()
                # ESP32 app_desc layout (empirically determined from ELF):
                # 0x00: magic_word (4 bytes)
                # 0x10: version (32 bytes)
                # 0x30: project_name (16 bytes)
                # 0x50: time (16 bytes)
                # 0x60: date (16 bytes)
                # 0x70: idf_ver (32 bytes)
                idf_ver = data[0x70:0x90].split(b"\x00")[0].decode("ascii", errors="replace")
                project = data[0x30:0x40].split(b"\x00")[0].decode("ascii", errors="replace")
                return {"idf_ver": idf_ver, "project": project}
    return None


def find_symbols(elf_path):
    """Find radio symbol addresses from .symtab."""
    symbols = {}
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        for section in elf.iter_sections():
            if section.name == ".symtab":
                for sym in section.iter_symbols():
                    name = sym.name
                    if name in RADIO_SYMBOLS and sym.entry.st_value != 0:
                        symbols[name] = hex(sym.entry.st_value)
    return symbols


def main():
    db = {}
    for example_dir in sorted(EXAMPLES_DIR.iterdir()):
        elf_path = example_dir / "prebuilt" / "firmware.elf"
        if not elf_path.exists():
            continue
        app_desc = read_app_desc(elf_path)
        symbols = find_symbols(elf_path)
        if app_desc is None:
            print(f"WARNING: no appdesc in {elf_path}", file=sys.stderr)
            continue
        entry = {
            "path": str(elf_path),
            "idf_ver": app_desc["idf_ver"],
            "project": app_desc["project"],
            "symbols": symbols,
        }
        db[example_dir.name] = entry
        has_radio = bool(symbols)
        print(f"{example_dir.name:30s} idf={app_desc['idf_ver']:10s} radio_symbols={len(symbols)} {'(HAS RADIO)' if has_radio else ''}")
        for name, addr in symbols.items():
            print(f"    {name}: {addr}")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(db, indent=2))
    print(f"\nWrote {OUTPUT} ({len(db)} examples)")


if __name__ == "__main__":
    main()
