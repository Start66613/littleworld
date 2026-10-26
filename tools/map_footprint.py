#!/usr/bin/env python3
"""Static footprint of an ESP-IDF ELF, without esp_idf_size.

esp_idf_size fails on this P4 target ("cannot assign memory region sram_seg to
chip memory type"), so this reads the section headers instead. Section-based
accounting is what the resource budget actually needs: bytes of internal RAM
reserved before any heap exists, bytes of PSRAM reserved the same way, and bytes
of flash the app image occupies.

Usage: python tools/map_footprint.py firmware/build/os/little_world.elf
SPDX-License-Identifier: MIT
"""
import json
import os
import re
import subprocess
import sys

# Where the ESP32-P4 puts each section family. Taken from the linker scripts in
# esp-idf-v5.5.5/components/esp_system/ld/esp32p4/sections.ld.
INTERNAL_PREFIXES = (".iram0", ".dram0", ".rtc.text", ".rtc.force_fast",
                     ".rtc.force_slow", ".rtc_noinit", ".rtc_reserved", ".rtc.data")
PSRAM_PREFIXES = (".ext_ram", ".psram")
FLASH_PREFIXES = (".flash.text", ".flash.rodata", ".flash.appdesc", ".flash.init_array",
                  ".flash.tdata", ".flash.rodata_noload", ".spm.text", ".spm.data")

SIZE_RE = re.compile(r"^(\.\S+)\s+(\d+)\s+(\d+)\s*$")


def size_tool():
    """Find riscv32-esp-elf-size next to the compiler IDF is configured with."""
    idf = os.environ.get("IDF_PATH", r"D:\Espressif\LittleWorld\esp-idf-v5.5.5")
    tools = os.path.join(os.path.dirname(idf), "idf-tools", "tools", "riscv32-esp-elf")
    for root, _dirs, files in os.walk(tools):
        if "riscv32-esp-elf-size.exe" in files:
            return os.path.join(root, "riscv32-esp-elf-size.exe")
    return "riscv32-esp-elf-size"


def main(elf):
    output = subprocess.run([size_tool(), "-A", elf], capture_output=True, text=True)
    if output.returncode != 0:
        print(output.stderr.strip() or "size tool failed")
        return 1

    buckets = {"internal-ram": [], "psram": [], "flash": [], "other": []}
    for line in output.stdout.splitlines():
        match = SIZE_RE.match(line.strip())
        if not match:
            continue
        name, size = match.group(1), int(match.group(2))
        # Debug sections are not loaded and must never enter the budget.
        if name.startswith(".debug") or name in (".comment", ".riscv.attributes"):
            continue
        if size == 0:
            continue
        if name.startswith(INTERNAL_PREFIXES):
            buckets["internal-ram"].append((name, size))
        elif name.startswith(PSRAM_PREFIXES):
            buckets["psram"].append((name, size))
        elif name.startswith(FLASH_PREFIXES):
            buckets["flash"].append((name, size))
        else:
            buckets["other"].append((name, size))

    summary = {}
    for bucket, sections in buckets.items():
        total = sum(s for _, s in sections)
        summary[bucket] = total
        if not sections:
            continue
        print(f"{bucket:<14} {total:>10,} B  ({total / 1024.0:>9,.1f} KiB)")
        for name, value in sorted(sections, key=lambda kv: -kv[1]):
            print(f"    {name:<32} {value:>10,} B")

    print()
    print(f"image on flash   {summary['flash']:>10,} B  "
          f"({summary['flash'] / 1024.0 / 1024.0:.2f} MiB)")
    print(f"internal RAM     {summary['internal-ram']:>10,} B  "
          f"({summary['internal-ram'] / 1024.0:.1f} KiB)")
    print(f"PSRAM static     {summary['psram']:>10,} B  "
          f"({summary['psram'] / 1024.0:.1f} KiB)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
