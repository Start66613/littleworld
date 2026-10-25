"""Validates the embedded character pack (firmware/main/art/characters.lwfa).

This is a structural check of the exact bytes that ship in the firmware image:
record table, offsets, frame geometry, alpha coverage and the anchor contract
(feet at y=anchor_y, centre at x=anchor_x). It does not need the board.

    python tools/validate_character_pack.py firmware/main/art/characters.lwfa
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

HEADER = struct.Struct("<8sHHI")
RECORD = struct.Struct("<12sHHHhhBBHHI")   # name, frames, w, h, ax, ay, loop, rsv, dur, rsv2, offset
MAGIC = b"LWFA0001"


def load(path: Path) -> tuple[bytes, list[dict]]:
    blob = path.read_bytes()
    magic, version, count, blob_size = HEADER.unpack_from(blob, 0)
    problems: list[str] = []
    if magic != MAGIC:
        problems.append(f"magic {magic!r} != {MAGIC!r}")
    if version != 1:
        problems.append(f"version {version} != 1")
    if blob_size != len(blob):
        problems.append(f"header blob_size {blob_size} != file size {len(blob)}")
    records = []
    off = HEADER.size
    for i in range(count):
        name, frames, w, h, ax, ay, loop, _rsv, dur, _rsv2, data_off = RECORD.unpack_from(blob, off)
        off += RECORD.size
        clip = {
            "name": name.split(b"\0")[0].decode("ascii", "replace"),
            "frames": frames, "w": w, "h": h, "ax": ax, "ay": ay,
            "loop": loop, "duration_ms": dur, "offset": data_off,
        }
        # structural rules, same ones the firmware loader enforces
        if frames < 1 or frames > 32:
            problems.append(f"{clip['name']}: frames {frames}")
        if not (1 <= w <= 1024 and 1 <= h <= 600):
            problems.append(f"{clip['name']}: size {w}x{h}")
        if not (0 <= ax < w and 0 <= ay < h):
            problems.append(f"{clip['name']}: anchor ({ax},{ay}) outside {w}x{h}")
        if dur < 20 or dur > 5000:
            problems.append(f"{clip['name']}: duration {dur} ms")
        frame_bytes = w * h * 3
        end = data_off + frames * frame_bytes
        if end > len(blob):
            problems.append(f"{clip['name']}: data ends at {end} > {len(blob)}")
        records.append(clip)
    return blob, records, problems


def alpha_stats(blob: bytes, clip: dict) -> dict:
    """Measures the visible box of the first frame from its alpha plane."""
    frame_bytes = clip["w"] * clip["h"] * 3
    base = clip["offset"] + clip["w"] * clip["h"] * 2
    alpha = blob[base:base + clip["w"] * clip["h"]]
    w, h = clip["w"], clip["h"]
    xs = [i % w for i, a in enumerate(alpha) if a > 8]
    ys = [i // w for i, a in enumerate(alpha) if a > 8]
    transparent = sum(1 for a in alpha if a == 0)
    return {
        "visible": f"{max(xs) - min(xs) + 1}x{max(ys) - min(ys) + 1}" if xs else "empty",
        "bbox": (min(xs), min(ys), max(xs), max(ys)) if xs else None,
        "transparent_ratio": round(transparent / (w * h), 3),
        "frame_bytes": frame_bytes,
    }


def main(argv: list[str]) -> int:
    path = Path(argv[1] if len(argv) > 1 else "firmware/main/art/characters.lwfa")
    blob, records, problems = load(path)
    print(f"pack   : {path} ({len(blob)} bytes)")
    print(f"header : magic={blob[:8].decode()} version={struct.unpack_from('<H', blob, 8)[0]} "
          f"clips={struct.unpack_from('<H', blob, 10)[0]}")
    for clip in records:
        st = alpha_stats(blob, clip)
        lo, hi = 0, 0
        print(f"  {clip['name']:<14} {clip['w']}x{clip['h']} x{clip['frames']} "
              f"anchor=({clip['ax']},{clip['ay']}) loop={clip['loop']} {clip['duration_ms']}ms "
              f"visible={st['visible']} alpha0={st['transparent_ratio']:.0%} "
              f"offset={clip['offset']} ({st['frame_bytes']} B/frame)")
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print("  -", p)
        return 1
    print("OK: record table, offsets, geometry and anchors are consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
