#!/usr/bin/env python3
"""Offline checks for ESP-IDF firmware images produced by the Arcade build.

Reads the ESP-IDF image header (esp_app_format.h) and reports the chip target,
the supported chip-revision window and the flash settings, without touching a
device. This is the check that proves a build is allowed to boot on an
ESP32-P4 v3.2 (revision 302) board.

Usage:
    python tools/image_check.py --expect-rev 302 app.bin [bootloader.bin ...]
Exit code 0 = every image matched every expectation.
"""
import argparse
import hashlib
import json
import os
import struct
import sys

CHIP_NAMES = {
    0x0000: "esp32",
    0x0002: "esp32s2",
    0x0005: "esp32c3",
    0x0009: "esp32s3",
    0x000C: "esp32c2",
    0x000D: "esp32c6",
    0x0010: "esp32h2",
    0x0012: "esp32p4",
    0x0014: "esp32c61",
    0x0017: "esp32c5",
    0x0019: "esp32h21",
    0x001C: "esp32h4",
}
FLASH_MODES = {0: "QIO", 1: "QOUT", 2: "DIO", 3: "DOUT", 4: "FAST_READ", 5: "SLOW_READ"}
FLASH_SIZES = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB", 5: "32MB", 6: "64MB", 7: "128MB"}
HEADER_SIZE = 24
MAGIC = 0xE9


def rev_str(full):
    return "v%d.%d (%d)" % (full // 100, full % 100, full)


def parse(path):
    with open(path, "rb") as handle:
        data = handle.read(HEADER_SIZE)
    if len(data) < HEADER_SIZE:
        raise ValueError("%s: shorter than an image header" % path)
    if data[0] != MAGIC:
        raise ValueError("%s: bad magic 0x%02X (not an ESP-IDF image)" % (path, data[0]))

    magic, segments, spi_mode, spi_size_freq = struct.unpack_from("<BBBB", data, 0)
    entry = struct.unpack_from("<I", data, 4)[0]
    chip_id = struct.unpack_from("<H", data, 12)[0]
    min_rev = struct.unpack_from("<H", data, 15)[0]
    max_rev = struct.unpack_from("<H", data, 17)[0]

    return {
        "path": os.path.abspath(path),
        "name": os.path.basename(path),
        "size": os.path.getsize(path),
        "sha256": hashlib.sha256(open(path, "rb").read()).hexdigest(),
        "magic": magic,
        "segments": segments,
        "chip_id": chip_id,
        "chip": CHIP_NAMES.get(chip_id, "unknown(0x%04X)" % chip_id),
        "min_chip_rev_full": min_rev,
        "max_chip_rev_full": max_rev,
        "flash_mode_field": spi_mode,
        "flash_mode": FLASH_MODES.get(spi_mode, str(spi_mode)),
        "flash_size_field": (spi_size_freq >> 4) & 0x0F,
        "flash_size": FLASH_SIZES.get((spi_size_freq >> 4) & 0x0F, "unknown"),
        "flash_freq_field": spi_size_freq & 0x0F,
        "entry_point": "0x%08x" % entry,
    }


def parse_partition_table(path):
    """Parse a partition table binary (magic 0xAA50, 32-byte entries)."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 32 or data[0] != 0xAA or data[1] != 0x50:
        raise ValueError("%s: not a partition table" % path)
    entries = []
    for offset in range(0, len(data) - 31, 32):
        magic, ptype, subtype, addr, size, label, flags = struct.unpack_from("<HBBII16sI", data, offset)
        # ESP_PARTITION_MAGIC is 0x50AA; on disk the two bytes are AA 50 (little endian).
        if magic != 0x50AA:
            break
        entries.append({
            "name": label.split(b"\x00")[0].decode("ascii", "replace"),
            "type": ptype,
            "subtype": subtype,
            "offset": addr,
            "size": size,
        })
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--expect-chip", default="esp32p4")
    parser.add_argument("--expect-rev", type=int, default=302,
                        help="chip revision that must fall inside [min,max] (default 302 = v3.2)")
    parser.add_argument("--json", help="write the parsed report to this path")
    parser.add_argument("images", nargs="+")
    args = parser.parse_args()

    failures = 0
    report = []
    for path in args.images:
        try:
            with open(path, "rb") as handle:
                head = handle.read(2)
            if head[:2] == b"\xaa\x50":
                entries = parse_partition_table(path)
                print("INFO  %s (partition table)" % os.path.basename(path))
                for entry in entries:
                    print("      %-10s type=0x%02X subtype=0x%02X offset=0x%06X size=0x%06X"
                          % (entry["name"], entry["type"], entry["subtype"], entry["offset"], entry["size"]))
                report.append({"name": os.path.basename(path), "kind": "partition_table", "entries": entries,
                               "sha256": hashlib.sha256(open(path, "rb").read()).hexdigest()})
                continue
            info = parse(path)
        except (OSError, ValueError) as error:
            print("FAIL  %s" % error)
            failures += 1
            continue

        report.append(info)
        ok_chip = info["chip"] == args.expect_chip
        ok_rev = info["min_chip_rev_full"] <= args.expect_rev <= info["max_chip_rev_full"]
        status = "PASS" if (ok_chip and ok_rev) else "FAIL"

        print("%s  %s" % (status, info["name"]))
        print("      chip            : %s (id 0x%04X) %s" % (info["chip"], info["chip_id"],
                                                             "" if ok_chip else "<-- expected %s" % args.expect_chip))
        print("      chip revision   : %s .. %s %s" % (rev_str(info["min_chip_rev_full"]),
                                                       rev_str(info["max_chip_rev_full"]),
                                                       "" if ok_rev else "<-- does not cover v%d.%d" % (args.expect_rev // 100, args.expect_rev % 100)))
        print("      flash           : %s, mode field %s" % (info["flash_size"], info["flash_mode"]))
        print("      size / sha256   : %d bytes / %s" % (info["size"], info["sha256"]))
        if not (ok_chip and ok_rev):
            failures += 1

    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(report, handle, ensure_ascii=False, indent=2)
        print("\nreport: %s" % args.json)

    print("\n%d image(s), %d failure(s)" % (len(args.images), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
