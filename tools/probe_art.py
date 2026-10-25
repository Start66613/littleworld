"""One-off probe: measure the male PNG and the qianqian idle frames.

Prints canvas size, alpha bounding box, feet row and the union box needed for a
cropped character pack. Read-only; writes nothing.
"""
import pathlib
import sys

from PIL import Image

ROOT = pathlib.Path(r"D:\LittleWorld-V3")
MALE = ROOT / "assets/characters/male/male-anchor-aligned.png"
QIAN = ROOT / "private/pixel-duo/qianqian/frames/idle"


def bbox(path: pathlib.Path):
    im = Image.open(path).convert("RGBA")
    a = im.getchannel("A")
    box = a.getbbox()
    # feet: last row with any opaque pixel
    px = a.load()
    feet = None
    cx_sum = 0
    cx_n = 0
    for y in range(im.height - 1, -1, -1):
        row = [x for x in range(im.width) if px[x, y] > 8]
        if row:
            feet = y
            break
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y] > 8:
                cx_sum += x
                cx_n += 1
    cx = round(cx_sum / cx_n) if cx_n else 0
    return im.size, box, feet, cx


def main():
    size, box, feet, cx = bbox(MALE)
    print(f"male       size={size} alpha_box={box} feet={feet} centroid_x={cx}")
    union = None
    for i in range(1, 9):
        p = QIAN / f"idle_{i:02d}.png"
        if not p.exists():
            print(f"missing {p}")
            continue
        size, box, feet, cx = bbox(p)
        print(f"qianqian {i}  size={size} alpha_box={box} feet={feet} centroid_x={cx}")
        if box:
            union = box if union is None else (
                min(union[0], box[0]), min(union[1], box[1]),
                max(union[2], box[2]), max(union[3], box[3]))
    print(f"qianqian idle union box={union}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
