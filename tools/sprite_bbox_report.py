#!/usr/bin/env python3
"""Measure how much of each sprite frame is actually opaque.

V4.5 P0 needs a number, not a feeling: the sprite canvases are 256x256
(male/qianqian) and 320x320 (daily couple), but the characters only occupy a
fraction of that. Every transparent pixel still costs a destination read, a
mask read and a colour mix in LVGL's software blend, so the padding is pure
waste in the hottest loop we have.

Reads the PNG frame sequences under assets/ (the sources the .lwp packs are
built from), so it measures the real artwork rather than a guess.

Usage:
  python tools/sprite_bbox_report.py [--json out.json]
"""
import argparse
import json
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# clip name -> (source dir, canvas w, canvas h, drawn scale in 1/256 units)
CLIPS = {
    "male.idle":        ("assets/visual-v4/characters/male/idle",      256, 256, 288),
    "male.blink":       ("assets/visual-v4/characters/male/blink",     256, 256, 288),
    "male.wave":        ("assets/visual-v4/characters/male/wave",      256, 256, 288),
    "male.walk_down":   ("assets/visual-v4-2/characters/male.walk_down",  256, 256, 288),
    "male.walk_left":   ("assets/visual-v4-2/characters/male.walk_left",  256, 256, 288),
    "male.walk_right":  ("assets/visual-v4-2/characters/male.walk_right", 256, 256, 288),
    "male.walk_up":     ("assets/visual-v4-2/characters/male.walk_up",    256, 256, 288),
    "couple.seated":    ("assets/visual-v4-2/characters/couple.seated",   320, 320, 282),
    "couple.wave":      ("assets/visual-v4-2/characters/couple.wave",     320, 320, 282),
    "couple.rest":      ("assets/visual-v4-2/characters/couple.rest",     320, 320, 282),
    "dog.idle":         ("assets/visual-v4-2/characters/dog.idle",        320, 320, 256),
    "dog.sleep":        ("assets/visual-v4-2/characters/dog.sleep",       320, 320, 256),
}

ALPHA_MIN = 8   # same threshold tools/build_character_pack.py uses


def union_bbox(paths):
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for p in paths:
        im = Image.open(p).convert("RGBA")
        alpha = im.getchannel("A")
        box = alpha.point(lambda v: 255 if v > ALPHA_MIN else 0).getbbox()
        if not box:
            continue
        x0 = min(x0, box[0]); y0 = min(y0, box[1])
        x1 = max(x1, box[2]); y1 = max(y1, box[3])
    if x1 < 0:
        return None
    return (x0, y0, x1, y1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    args = ap.parse_args()

    rows = []
    for name, (rel, cw, ch, scale) in CLIPS.items():
        d = os.path.join(ROOT, rel.replace("/", os.sep))
        if not os.path.isdir(d):
            rows.append({"clip": name, "missing": True})
            continue
        frames = sorted(
            os.path.join(d, f) for f in os.listdir(d)
            if f.lower().endswith(".png")
        )
        if not frames:
            rows.append({"clip": name, "missing": True})
            continue
        box = union_bbox(frames)
        if not box:
            rows.append({"clip": name, "empty": True})
            continue
        bw = box[2] - box[0]
        bh = box[3] - box[1]
        # on-screen destination area = bbox scaled by the draw scale
        dw = (bw * scale) // 256
        dh = (bh * scale) // 256
        full_w = (cw * scale) // 256
        full_h = (ch * scale) // 256
        canvas_px = full_w * full_h
        content_px = dw * dh
        rows.append({
            "clip": name,
            "frames": len(frames),
            "canvas": [cw, ch],
            "scale": scale,
            "bbox": list(box),
            "bbox_px": [bw, bh],
            "drawn_px": [dw, dh],
            "canvas_draw_px": canvas_px,
            "content_draw_px": content_px,
            "padding_ratio": round(canvas_px / content_px, 3) if content_px else None,
            "waste_pct": round(100.0 * (1 - content_px / canvas_px), 1) if canvas_px else None,
        })

    ok = [r for r in rows if "padding_ratio" in r]
    print(f"{'clip':<18}{'canvas':>10}{'scale':>6}{'bbox':>12}{'drawn':>11}"
          f"{'draw px':>10}{'content px':>12}{'waste':>8}")
    for r in rows:
        if "padding_ratio" not in r:
            print(f"{r['clip']:<18}  (source frames not present in this checkout)")
            continue
        b = r["bbox"]
        print(f"{r['clip']:<18}{r['canvas'][0]}x{r['canvas'][1]:<5}{r['scale']:>6}"
              f"{b[2]-b[0]}x{b[3]-b[1]:<7}{r['drawn_px'][0]}x{r['drawn_px'][1]:<6}"
              f"{r['canvas_draw_px']:>10}{r['content_draw_px']:>12}"
              f"{r['waste_pct']:>7}%")

    if ok:
        tot_canvas = sum(r["canvas_draw_px"] for r in ok)
        tot_content = sum(r["content_draw_px"] for r in ok)
        print()
        print(f"measured clips: {len(ok)}")
        print(f"total canvas draw px : {tot_canvas:,}")
        print(f"total content draw px: {tot_content:,}")
        print(f"overall waste        : {100.0*(1-tot_content/tot_canvas):.1f}%")
        print(f"potential speed-up   : {tot_canvas/tot_content:.2f}x fewer blended px")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(rows, f, ensure_ascii=False, indent=2)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
