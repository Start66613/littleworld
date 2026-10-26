"""Generate the small UI art fixtures the asset-pipeline tests run against.

These are deliberately tiny and synthetic: a 24x24 panel with a 6 px border,
a 32x24 three-state button strip, and a 16x16 icon with a transparent corner.
They are committed as PNGs so the tests never depend on a random generator, but
this script is what produced them and can reproduce them byte for byte.

Usage: python tools/make_ui_fixtures.py
"""

from __future__ import annotations

import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover
    print("Pillow is required", file=sys.stderr)
    raise SystemExit(2)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "fixtures" / "ui"

# A deliberately small palette so a reviewer can see at a glance that no
# resampling happened: every colour is exact.
PAPER = (247, 239, 226, 255)
INK = (74, 58, 52, 255)
ACCENT = (196, 108, 96, 255)
CLEAR = (0, 0, 0, 0)


def panel(path: Path) -> None:
    """24x24, 6 px border, rounded by one clipped corner pixel."""
    image = Image.new("RGBA", (24, 24), CLEAR)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 23, 23), fill=PAPER, outline=INK)
    draw.rectangle((6, 6, 17, 17), fill=PAPER, outline=None)
    # Four corner dots: if a corner is ever scaled these become ellipses.
    for x, y in ((2, 2), (21, 2), (2, 21), (21, 21)):
        draw.point((x, y), fill=ACCENT)
    image.save(path)


def button(path: Path) -> None:
    """32 wide x 72 tall: three 24 px bands (normal / pressed / disabled)."""
    image = Image.new("RGBA", (32, 72), CLEAR)
    draw = ImageDraw.Draw(image)
    for band in range(3):
        top = band * 24
        fill = PAPER if band == 0 else (232, 220, 206, 255) if band == 1 else (210, 204, 196, 255)
        draw.rectangle((0, top, 31, top + 23), fill=fill, outline=INK)
        # One mark per band so the tests can prove which band was selected.
        draw.rectangle((6, top + 8, 25, top + 15), fill=ACCENT if band == 0 else INK)
    image.save(path)


def icon(path: Path) -> None:
    """16x16 heart-ish glyph with a fully transparent corner."""
    image = Image.new("RGBA", (16, 16), CLEAR)
    draw = ImageDraw.Draw(image)
    draw.ellipse((2, 3, 7, 8), fill=ACCENT)
    draw.ellipse((8, 3, 13, 8), fill=ACCENT)
    draw.polygon([(2, 7), (13, 7), (8, 14)], fill=ACCENT)
    image.save(path)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    panel(OUT / "panel-card.png")
    button(OUT / "button-primary.png")
    icon(OUT / "icon-heart.png")
    (OUT / "panel-card.json").write_text('{\n  "insets": [6, 6, 6, 6],\n  "pixel_art": true\n}\n',
                                         encoding="utf-8")
    (OUT / "button-primary.json").write_text('{\n  "insets": [6, 6, 6, 6],\n  "pixel_art": true\n}\n',
                                             encoding="utf-8")
    (OUT / "icon-heart.json").write_text('{\n  "pixel_art": true\n}\n', encoding="utf-8")
    for path in sorted(OUT.glob("*.png")):
        print(f"{path.name} {path.stat().st_size} B")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
