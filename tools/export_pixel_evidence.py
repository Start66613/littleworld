"""Convert actual LVGL framebuffers to PNG locally; never publish character art."""
import argparse
import json
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--root", type=Path, required=True)
args = parser.parse_args()
private = args.root / "private/pixel-duo/screenshots"
files = []
for bmp in sorted(private.glob("*.bmp")):
    with Image.open(bmp) as image:
        if image.size != (1024, 600):
            raise ValueError("Unexpected real framebuffer size")
        target = bmp.with_suffix(".png")
        image.save(target)
    files.append(target.name)
if not files:
    raise SystemExit("No actual pixel-duo screenshot found")
result = json.loads((private / "runtime-results.json").read_text(encoding="utf-8"))
result["screenshots_local_only"] = files
result["screenshot_directory"] = "private/pixel-duo/screenshots"
result["publication_authorized"] = False
public = args.root / "reports/pixel-duo/runtime.json"
public.parent.mkdir(parents=True, exist_ok=True)
public.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"Exported {len(files)} real LVGL screenshots into ignored private directory; metadata report: {public}")
