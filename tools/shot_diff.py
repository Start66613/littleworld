#!/usr/bin/env python3
"""Compare two screenshot directories produced by `LittleWorld.exe --os-v2-test`.

Both sides are BMPs written by lw_host_screenshot_bmp(), so the pixel layout is
identical and a byte comparison would also work. This walks the pixels instead
because a pure "different / not different" answer is not actionable: the V4.5
baked packs are supposed to be pixel-identical to the originals, and if they are
not we need to know how large the error is and where it sits.

Usage:
    python tools/shot_diff.py <dir_a> <dir_b> [--json out.json] [--top N]

Exit code 0 when every shared file matches exactly, 1 otherwise.
"""
import argparse
import json
import os
import struct
import sys


def read_bmp(path):
    """Return (width, height, pixels) with pixels as a list of (r, g, b) tuples."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        raise SystemExit("%s: not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits not in (24, 32):
        raise SystemExit("%s: unsupported %d bpp" % (path, bits))
    stride = ((width * bits + 31) // 32) * 4
    step = bits // 8
    flip = height > 0
    rows = abs(height)
    out = []
    for y in range(rows):
        src = offset + y * stride
        row = data[src:src + width * step]
        if flip:
            # BMP stores bottom-up; the comparison does not care about order as
            # long as both sides agree, so keep the file order and skip the flip.
            pass
        for x in range(width):
            b, g, r = row[x * step], row[x * step + 1], row[x * step + 2]
            out.append((r, g, b))
    return width, rows, out


def compare(a, b):
    wa, ha, pa = read_bmp(a)
    wb, hb, pb = read_bmp(b)
    if (wa, ha) != (wb, hb):
        return {"size_mismatch": True, "a": [wa, ha], "b": [wb, hb]}
    diff = 0
    worst = 0
    x1 = y1 = 1 << 30
    x2 = y2 = -1
    for i, (ca, cb) in enumerate(zip(pa, pb)):
        if ca == cb:
            continue
        diff += 1
        d = max(abs(ca[0] - cb[0]), abs(ca[1] - cb[1]), abs(ca[2] - cb[2]))
        if d > worst:
            worst = d
        x, y = i % wa, i // wa
        if x < x1: x1 = x
        if y < y1: y1 = y
        if x > x2: x2 = x
        if y > y2: y2 = y
    return {"diff": diff, "total": wa * ha, "worst": worst,
            "bbox": [x1, y1, x2, y2] if diff else None}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--json")
    ap.add_argument("--top", type=int, default=10)
    args = ap.parse_args()

    names = sorted(n for n in os.listdir(args.a) if n.lower().endswith(".bmp"))
    rows = []
    missing = []
    for name in names:
        pa = os.path.join(args.a, name)
        pb = os.path.join(args.b, name)
        if not os.path.exists(pb):
            missing.append(name)
            continue
        r = compare(pa, pb)
        r["name"] = name
        rows.append(r)

    same = [r for r in rows if r.get("diff") == 0 and not r.get("size_mismatch")]
    differ = [r for r in rows if r.get("diff") or r.get("size_mismatch")]
    differ.sort(key=lambda r: -(r.get("diff") or 0))

    print("compared %d files: %d identical, %d differ, %d missing on the right"
          % (len(rows), len(same), len(differ), len(missing)))
    for r in differ[:args.top]:
        if r.get("size_mismatch"):
            print("  %-34s size %s vs %s" % (r["name"], r["a"], r["b"]))
        else:
            print("  %-34s diff %7d / %7d  worst %3d  bbox %s"
                  % (r["name"], r["diff"], r["total"], r["worst"], r["bbox"]))
    if missing:
        print("missing:", ", ".join(missing[:10]))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"identical": len(same), "differ": len(differ),
                       "missing": missing, "rows": rows}, f, indent=2)
        print("wrote", args.json)

    return 0 if not differ and not missing else 1


if __name__ == "__main__":
    sys.exit(main())
