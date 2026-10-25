"""Local-only PNG contract preflight. Never grants visual acceptance or uploads.
Requires Pillow (producer's D-drive venv is usable without changing it).
"""
import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
from PIL import Image


def safe_file(root, entry):
    name = entry.get("path", "")
    if not isinstance(name, str) or not name or PureWindowsPath(name).is_absolute():
        raise ValueError("asset path must be relative")
    normalized = name.replace("\\", "/")
    if any(part in ("..", ".env", "private", "secrets") for part in normalized.split("/")):
        raise ValueError("unsafe or private asset path")
    file = (root / normalized).resolve()
    if not file.is_relative_to(root) or not file.is_file():
        raise ValueError("file is missing or escapes the asset root")
    expected = entry.get("sha256", "")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected):
        raise ValueError("actual SHA-256 required")
    if hashlib.sha256(file.read_bytes()).hexdigest() != expected.lower():
        raise ValueError("SHA-256 mismatch")
    return file


def inspect(manifest, root):
    root = Path(root).resolve()
    assets = manifest.get("assets", [])
    report = {"contract_version": "1.0.0", "technical_status": "waiting", "visual_status": "not_reviewed",
              "accepted_for_integration": False, "assets": [], "errors": [],
              "note": "Technical checks do not prove character consistency, jitter, edge quality or visual approval."}
    if not isinstance(assets, list):
        report["errors"].append("assets must be an array")
        report["technical_status"] = "fail"
        return report
    if not assets:
        report["errors"].append("No asset files delivered; waiting for DSH and ASSET_REPORT.md.")
        return report
    if manifest.get("schema") != "littleworld.assets/2" or manifest.get("contract_version") != "1.0.0":
        report["errors"].append("Expected littleworld.assets/2 and contract_version 1.0.0")
    if not manifest.get("asset_version") or not manifest.get("producer"):
        report["errors"].append("asset_version and producer are required")
    ids = set()
    raw_total = 0
    for asset in assets:
        result = {"id": asset.get("id"), "errors": [], "frames": []}
        report["assets"].append(result)
        images = []
        try:
            asset_id = asset.get("id", "")
            if not isinstance(asset_id, str) or not re.fullmatch(r"[a-z][a-z0-9_.-]*", asset_id) or asset_id in ids:
                raise ValueError("invalid or duplicate id")
            ids.add(asset_id)
            kind = asset.get("kind")
            if kind not in ("background", "sprite", "decoration", "icon", "concept"):
                raise ValueError("unknown asset kind")
            if asset.get("status") not in ("candidate", "ready"):
                raise ValueError("status must be candidate or ready")
            if not asset.get("license") or not asset.get("provenance"):
                raise ValueError("license and provenance are required, subject to human verification")
            width, height = asset.get("width"), asset.get("height")
            if type(width) is not int or type(height) is not int or not (1 <= width <= 4096 and 1 <= height <= 4096):
                raise ValueError("invalid dimensions")
            if kind == "background" and (width, height) != (1024, 600):
                raise ValueError("background must be exactly 1024x600; no stretch")
            if kind == "sprite" and ((width, height) != (256, 256) or asset.get("anchor") != [128, 236]):
                raise ValueError("sprite must be 256x256 with anchor [128,236]")
            if kind == "icon" and (width, height) != (48, 48):
                raise ValueError("dock icon must be 48x48")
            if kind == "decoration" and (width > 240 or height > 160):
                raise ValueError("decoration must be cropped to at most 240x160")
            files = asset.get("files")
            if not isinstance(files, list) or not (1 <= len(files) <= 32):
                raise ValueError("1..32 numbered files required")
            if kind != "sprite" and len(files) != 1:
                raise ValueError("only sprites can have multiple runtime frames")
            if kind == "sprite" and (not isinstance(asset.get("loop"), bool) or asset.get("fps") not in (4, 8)):
                raise ValueError("sprite loop boolean and fps 4 or 8 required")
            for index, entry in enumerate(files):
                file = safe_file(root, entry)
                with Image.open(file) as source:
                    if source.format != "PNG" or source.size != (width, height) or source.mode not in ("RGB", "RGBA"):
                        raise ValueError(f"frame {index}: expected full-size RGB/RGBA PNG")
                    image = source.convert("RGBA")
                alpha = image.getchannel("A")
                extrema = alpha.getextrema()
                if kind == "background":
                    if extrema != (255, 255):
                        raise ValueError("background must be fully opaque")
                else:
                    if extrema[0] != 0 or extrema[1] == 0 or asset.get("alpha") != "straight":
                        raise ValueError("transparent asset needs real alpha, visible content and alpha=straight")
                if kind == "sprite":
                    if not file.name.endswith(f"{index:03}.png"):
                        raise ValueError("sprite files must be numbered from 000 in playback order")
                    duration = entry.get("duration_ms")
                    if type(duration) is not int or not 20 <= duration <= 2000:
                        raise ValueError("sprite needs a valid duration_ms per frame")
                    box = alpha.getbbox()
                    if box[0] < 4 or box[1] < 4 or box[2] > width - 4 or box[3] > height - 4:
                        raise ValueError("sprite clips the 4px transparent safety edge")
                images.append(image)
                result["frames"].append({"path": entry["path"], "alpha_extrema": extrema,
                                         "alpha_bbox": alpha.getbbox(), "rgba_decode_bytes": width * height * 4})
            sheet = asset.get("sheet")
            if sheet:
                columns, rows = sheet.get("columns"), sheet.get("rows")
                if type(columns) is not int or type(rows) is not int or not 1 <= columns <= 4 or rows != (len(files) + columns - 1) // columns or sheet.get("frame_count") != len(files):
                    raise ValueError("invalid sheet grid or frame count")
                file = safe_file(root, sheet)
                with Image.open(file) as source:
                    if source.format != "PNG" or source.size != (columns * width, rows * height):
                        raise ValueError("sheet size does not match grid")
                    grid = source.convert("RGBA")
                for i in range(columns * rows):
                    x, y = (i % columns) * width, (i // columns) * height
                    cell = grid.crop((x, y, x + width, y + height))
                    if i < len(files) and cell.tobytes() != images[i].tobytes():
                        raise ValueError("sheet order/pixels differ from numbered frames")
                    if i >= len(files) and cell.getchannel("A").getextrema() != (0, 0):
                        raise ValueError("unused sheet cells must be transparent")
            if kind != "concept":
                per_frame = width * height * (2 if kind == "background" else 3)
                raw_total += per_frame * len(files)
                result["runtime_frame_bytes"] = per_frame
                if per_frame > 1250000:
                    raise ValueError("runtime single-frame budget exceeded")
            result["manual_review_required"] = ["character identity and anatomy", "matte/fringe on three backgrounds",
                                                "fixed foot anchor and motion jitter", "scene layout and touch alignment"]
        except (ValueError, OSError, TypeError, KeyError) as error:
            result["errors"].append(str(error))
    report["all_frames_raw_storage_bytes"] = raw_total
    report["technical_status"] = "fail" if report["errors"] or any(a["errors"] for a in report["assets"]) else "pass"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = inspect(json.loads(args.manifest.read_text(encoding="utf-8-sig")), args.root)
    except (OSError, ValueError) as error:
        report = {"technical_status": "fail", "visual_status": "not_reviewed", "accepted_for_integration": False, "errors": [str(error)]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Asset preflight: {report['technical_status']}; visual acceptance: not granted; report: {args.output}")
    return {"pass": 0, "fail": 1, "waiting": 2}[report["technical_status"]]


if __name__ == "__main__":
    raise SystemExit(main())
