"""Build the Little World character pack (LWFA v1) for the ESP32-P4 firmware.

Inputs
  male     assets/characters/male/male-anchor-aligned.png   (256x256 RGBA, Codex)
  qianqian private/pixel-duo/qianqian/frames/idle/frame_XXX.rgb565a8  (original art)

Outputs
  firmware/main/art/characters.lwfa      binary pack embedded into the app
  reports/art/character_pack.json        provenance + hashes + sizes
  reports/art/character_pack_preview.png visual check sheet

Layout (little endian):
  header  'LWFA0001' u8[8], u16 version=1, u16 clip_count, u32 blob_size
  record  char[12] name, u16 frames, u16 w, u16 h, i16 anchor_x, i16 anchor_y,
          u8 loop, u8 reserved, u16 duration_ms, u16 reserved2, u32 data_offset
  data    per frame: w*h*2 bytes RGB565 (LE) then w*h bytes alpha

Usage: python tools/build_character_pack.py
"""
from __future__ import annotations

import hashlib
import json
import pathlib
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(r"D:\LittleWorld-V3")
MALE_PNG = ROOT / "assets/characters/male/male-anchor-aligned.png"
QIAN_DIR = ROOT / "private/pixel-duo/qianqian/frames/idle"
OUT_BIN = ROOT / "firmware/main/art/characters.lwfa"
OUT_JSON = ROOT / "reports/art/character_pack.json"
OUT_PREVIEW = ROOT / "reports/art/character_pack_preview.png"
PAD = 2
QIAN_FRAMES = 8


def load_rgba(path: pathlib.Path) -> tuple[np.ndarray, np.ndarray]:
    im = Image.open(path).convert("RGBA")
    a = np.asarray(im, dtype=np.uint8)
    return a[:, :, :3].copy(), a[:, :, 3].copy()


def load_rgb565a8(path: pathlib.Path) -> tuple[np.ndarray, np.ndarray]:
    raw = path.read_bytes()
    w = h = 256
    need = w * h * 3
    if len(raw) != need:
        raise SystemExit(f"{path} is {len(raw)} bytes, expected {need}")
    rgb = np.frombuffer(raw[: w * h * 2], dtype="<u2").reshape(h, w).copy()
    alpha = np.frombuffer(raw[w * h * 2 :], dtype=np.uint8).reshape(h, w).copy()
    return rgb, alpha


def to_rgb565(rgb: np.ndarray, alpha: np.ndarray) -> np.ndarray:
    r = (rgb[:, :, 0].astype(np.uint16) >> 3) & 0x1F
    g = (rgb[:, :, 1].astype(np.uint16) >> 2) & 0x3F
    b = (rgb[:, :, 2].astype(np.uint16) >> 3) & 0x1F
    out = (r << 11) | (g << 5) | b
    out[alpha == 0] = 0  # keep fully transparent pixels black, no colour fringe
    return out.astype("<u2")


def union_box(frames: list[tuple[np.ndarray, np.ndarray]]) -> tuple[int, int, int, int]:
    box = None
    for _, alpha in frames:
        ys, xs = np.nonzero(alpha > 8)
        if len(xs) == 0:
            continue
        b = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
        box = b if box is None else (
            min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    if box is None:
        raise SystemExit("no opaque pixels found")
    return box


def crop_box(box: tuple[int, int, int, int], w: int, h: int) -> tuple[int, int, int, int]:
    x0 = max(0, box[0] - PAD)
    y0 = max(0, box[1] - PAD)
    x1 = min(w, box[2] + PAD)
    y1 = min(h, box[3] + PAD)
    return x0, y0, x1, y1


def build_character(name: str, frames: list[tuple[np.ndarray, np.ndarray]],
                    durations: list[int], loop: bool) -> dict:
    h, w = frames[0][1].shape
    box = union_box(frames)
    x0, y0, x1, y1 = crop_box(box, w, h)
    cw, ch = x1 - x0, y1 - y0
    # anchor: horizontal centre of the union box, vertical last opaque row
    anchor_x = int(round((box[0] + box[2] - 1) / 2)) - x0
    anchor_y = int(box[3] - 1) - y0
    payload = bytearray()
    previews = []
    first_alpha = frames[0][1]
    for rgb, alpha in frames:
        if isinstance(rgb, np.ndarray) and rgb.dtype == np.uint8:
            rgb565 = to_rgb565(rgb, alpha)
        else:
            rgb565 = rgb.astype("<u2")
        sub565 = rgb565[y0:y1, x0:x1]
        suba = alpha[y0:y1, x0:x1]
        payload += sub565.tobytes()
        payload += suba.tobytes()
        previews.append((sub565.copy(), suba.copy()))
    del first_alpha
    return {
        "name": name, "frames": len(frames), "w": cw, "h": ch,
        "anchor_x": anchor_x, "anchor_y": anchor_y, "loop": loop,
        "durations": durations, "payload": bytes(payload),
        "box": [x0, y0, x1, y1], "union": list(box), "previews": previews,
    }


def write_preview(chars: list[dict], path: pathlib.Path) -> None:
    scale = 2
    margin = 12
    total_w = sum(c["w"] * scale for c in chars) + margin * (len(chars) + 1)
    total_h = max(c["h"] for c in chars) * scale + margin * 2
    sheet = Image.new("RGBA", (total_w, total_h), (236, 231, 226, 255))
    x = margin
    for c in chars:
        f565, fa = c["previews"][0]
        rgb = np.zeros((c["h"], c["w"], 3), dtype=np.uint8)
        v = f565.astype(np.uint16)
        rgb[:, :, 0] = ((v >> 11) & 0x1F) * 255 // 31
        rgb[:, :, 1] = ((v >> 5) & 0x3F) * 255 // 63
        rgb[:, :, 2] = (v & 0x1F) * 255 // 31
        frame = Image.fromarray(np.dstack([rgb, fa]), "RGBA")
        frame = frame.resize((c["w"] * scale, c["h"] * scale), Image.NEAREST)
        sheet.alpha_composite(frame, (x, margin))
        x += c["w"] * scale + margin
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def main() -> int:
    if not MALE_PNG.exists():
        raise SystemExit(f"missing {MALE_PNG}")
    qian_frames = []
    for i in range(QIAN_FRAMES):
        p = QIAN_DIR / f"frame_{i:03d}.rgb565a8"
        if not p.exists():
            raise SystemExit(f"missing {p}")
        qian_frames.append(load_rgb565a8(p))

    male_rgb, male_alpha = load_rgba(MALE_PNG)
    chars = [
        build_character("male", [(male_rgb, male_alpha)], [0], True),
        build_character("qianqian", qian_frames, [125] * QIAN_FRAMES, True),
    ]

    header = b"LWFA0001" + struct.pack("<HHI", 1, len(chars), 0)
    offset = len(header) + 32 * len(chars)
    records = bytearray()
    blob = bytearray()
    meta = []
    for c in chars:
        if len(c["name"]) > 11:
            raise SystemExit("name too long")
        name = c["name"].encode("ascii").ljust(12, b"\0")
        # 32-byte record: u16 duration_ms at +24, u32 data offset at +28 (see the header comment)
        duration = c["durations"][0] if c["durations"] else 0
        if duration < 20:
            duration = 500
        records += struct.pack("<12sHHHhhBBHHI", name, c["frames"], c["w"], c["h"],
                               c["anchor_x"], c["anchor_y"], 1 if c["loop"] else 0, 0,
                               duration, 0, offset)
        blob += c["payload"]
        offset += len(c["payload"])
        meta.append({k: c[k] for k in ("name", "frames", "w", "h", "anchor_x", "anchor_y",
                                       "loop", "box", "union")} | {"bytes": len(c["payload"])})
    total = len(header) + len(records) + len(blob)
    header = b"LWFA0001" + struct.pack("<HHI", 1, len(chars), total)
    data = header + bytes(records) + bytes(blob)

    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_BIN.write_bytes(data)
    write_preview(chars, OUT_PREVIEW)

    report = {
        "schema": "littleworld.character-pack/1",
        "pack": str(OUT_BIN.relative_to(ROOT)).replace("\\", "/"),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "clip_count": len(chars),
        "crop_pad": PAD,
        "characters": meta,
        "sources": {
            "male": {"path": "assets/characters/male/male-anchor-aligned.png",
                     "sha256": hashlib.sha256(MALE_PNG.read_bytes()).hexdigest(),
                     "origin": "origin/agent/codex-meowa-challenge assets/meowa-challenge/male-anchor-aligned.png"},
            "qianqian": {"path": "private/pixel-duo/qianqian/frames/idle",
                         "frames": QIAN_FRAMES,
                         "origin": "original DesktopPet qianqian idle frames, RGB565+A8"},
        },
    }
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"pack  {OUT_BIN}  {len(data)} bytes  sha256 {report['sha256'][:16]}...")
    for m in meta:
        print(f"  {m['name']:<9} {m['w']:>3}x{m['h']:<3} frames={m['frames']} "
              f"anchor=({m['anchor_x']},{m['anchor_y']}) bytes={m['bytes']}")
    print(f"total embedded {len(data)} B ({len(data)/1024:.0f} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
