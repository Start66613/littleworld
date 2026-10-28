#!/usr/bin/env python3
"""Dump one real sprite frame as raw RGB565A8 plus its union alpha bbox.

RenderBench can only reason about real art if it is fed real art. The synthetic
gradient in the benchmark is the worst possible case for a resampling
comparison: every pixel differs from its neighbour, so any nearest-neighbour
rounding difference shows up. Pixel art has large flat regions, so the honest
way to judge "does baking the 1.125x into the asset change the picture" is to
run the same five configs over an actual frame.

Output layout (little endian, no header):
    RGB565 plane   width*height*2 bytes
    A8 plane       width*height*1 byte
and a JSON sidecar with the union bbox over all frames of the clip.

Usage:
    python tools/bench_frame_dump.py --clip male.idle --out build/bench/male_idle
"""

import argparse
import json
import pathlib
import struct
import sys

from PIL import Image

# Same threshold tools/build_character_pack.py uses, so the bbox here is the
# bbox the packer would have produced.
ALPHA_MIN = 8

CLIPS = {
    "male.idle": ("assets/visual-v4/characters/male/idle", 288),
    "male.blink": ("assets/visual-v4/characters/male/blink", 288),
    "male.wave": ("assets/visual-v4/characters/male/wave", 288),
    "male.walk_down": ("assets/visual-v4-2/characters/male.walk_down", 288),
    "couple.seated": ("assets/visual-v4-2/characters/couple.seated", 282),
    "dog.idle": ("assets/visual-v4-2/characters/dog.idle", 256),
}


def rgb565(im):
    """Pack to RGB565 the same way the runtime packer does."""
    out = bytearray()
    px = im.convert("RGB").load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(out)


def union_bbox(images):
    x1 = y1 = 10 ** 9
    x2 = y2 = -1
    for im in images:
        a = im.getchannel("A")
        bb = a.point(lambda v: 255 if v > ALPHA_MIN else 0).getbbox()
        if bb is None:
            continue
        x1 = min(x1, bb[0])
        y1 = min(y1, bb[1])
        x2 = max(x2, bb[2])
        y2 = max(y2, bb[3])
    if x2 < 0:
        return (0, 0, images[0].size[0], images[0].size[1])
    return (x1, y1, x2 - x1, y2 - y1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clip", default="male.idle")
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    if args.clip not in CLIPS:
        print("unknown clip %s; known: %s" % (args.clip, ", ".join(sorted(CLIPS))), file=sys.stderr)
        return 2

    src_dir, scale = CLIPS[args.clip]
    root = pathlib.Path(__file__).resolve().parent.parent
    files = sorted(pathlib.Path(root / src_dir).glob("*.png"))
    if not files:
        print("no PNG frames under %s" % (root / src_dir), file=sys.stderr)
        return 2

    images = [Image.open(f).convert("RGBA") for f in files]
    w, h = images[0].size
    for im in images:
        if im.size != (w, h):
            print("frame size mismatch in %s" % src_dir, file=sys.stderr)
            return 2

    bbox = union_bbox(images)
    frame = images[args.frame % len(images)]

    out = pathlib.Path(root / args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    raw = out.with_suffix(".rgb565a8")
    with raw.open("wb") as f:
        f.write(rgb565(frame))
        f.write(frame.getchannel("A").tobytes())

    meta = {
        "clip": args.clip,
        "frame": args.frame % len(images),
        "frames": len(images),
        "canvas_w": w,
        "canvas_h": h,
        "scale": scale,
        "bbox": {"x": bbox[0], "y": bbox[1], "w": bbox[2], "h": bbox[3]},
        "raw": str(raw.relative_to(root)).replace("\\", "/"),
        "raw_bytes": raw.stat().st_size,
        "alpha_min": ALPHA_MIN,
    }
    js = out.with_suffix(".json")
    js.write_text(json.dumps(meta, indent=2), encoding="utf-8")

    print("clip      : %s (frame %d of %d)" % (args.clip, meta["frame"], len(images)))
    print("canvas    : %dx%d, scale %d" % (w, h, scale))
    print("union bbox: %dx%d at (%d,%d)" % (bbox[2], bbox[3], bbox[0], bbox[1]))
    print("raw       : %s (%d bytes)" % (meta["raw"], meta["raw_bytes"]))
    print("meta      : %s" % str(js.relative_to(root)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
