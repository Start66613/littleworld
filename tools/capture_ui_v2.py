"""Lossless conversion of actual LVGL RGB565 screenshots, never concept art."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", type=Path, required=True)
args = parser.parse_args()
base = args.root / "reports/ui-v2"
records = []
for folder in ("baseline", "technical"):
    for bmp in sorted((base / folder).glob("*.bmp")):
        png = bmp.with_suffix(".png")
        with Image.open(bmp) as image:
            if image.size != (1024, 600):
                raise ValueError(f"Unexpected screenshot dimensions: {bmp.name}")
            image.save(png)
        records.append({"path": png.relative_to(args.root).as_posix(), "sha256": hashlib.sha256(png.read_bytes()).hexdigest(),
                        "width": 1024, "height": 600, "source": "real Windows LVGL framebuffer", "classification": folder,
                        "final_artwork": False})
if not records:
    raise SystemExit("No actual simulator screenshots to export")
(base / "SCREENSHOT_MANIFEST.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
print(f"Exported {len(records)} actual simulator screenshots. Baseline and technical fixtures are explicitly labelled.")
