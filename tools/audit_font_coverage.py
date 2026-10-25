"""Audit font glyph coverage against the strings the firmware can display.

Parses the generated lv_font_conv C files (cmap ranges and unicode lists) and
compares them with every non-ASCII character that appears in the C sources.

Usage: python tools/audit_font_coverage.py
Writes reports/release/font_coverage.json and prints the missing characters.
"""
from __future__ import annotations

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(r"D:\LittleWorld-V3")
FONT_DIR = ROOT / "firmware/components/lw_ui/fonts"
SOURCE_DIRS = [
    ROOT / "firmware/components",
    ROOT / "firmware/main",
    ROOT / "host",
    ROOT / "rpg",
    ROOT / "tests",
]
SKIP_PARTS = ("/build/", "/managed_components/", "/fonts/", "/private/")
FONTS = ["lw_font_18", "lw_font_22", "lw_font_28", "lw_font_44", "lw_font_88"]


def covered(path: pathlib.Path) -> set[int]:
    text = path.read_text(encoding="utf-8", errors="replace")
    chars: set[int] = set()
    lists: dict[str, list[int]] = {}
    for name, body in re.findall(r"static const uint16_t (unicode_list_\d+)\[\]\s*=\s*\{([^}]*)\}", text):
        lists[name] = [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
    # A sparse cmap spans a wide range but only lists its real code points;
    # a tiny cmap is contiguous, so the span is the coverage.
    for block in re.findall(r"\.range_start = (\d+), \.range_length = (\d+),.*?\.unicode_list = ([^,]+),", text, re.S):
        start, length, name = int(block[0]), int(block[1]), block[2].strip()
        if name.startswith("unicode_list_"):
            # lv_font_conv stores sparse entries as offsets from range_start.
            chars.update(start + value for value in lists.get(name, []))
        else:
            chars.update(range(start, start + length))
    return chars


def source_chars() -> dict[str, set[str]]:
    found: dict[str, set[str]] = {}
    for base in SOURCE_DIRS:
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix not in (".c", ".h"):
                continue
            posix = path.as_posix()
            if any(part in posix for part in SKIP_PARTS):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            marks = {ch for ch in text if ord(ch) >= 0x7F}
            if marks:
                found[posix] = marks
    return found


def main() -> int:
    report = {"fonts": {}, "missing": {}}
    sources = source_chars()
    all_chars: set[str] = set()
    for marks in sources.values():
        all_chars |= marks
    per_font = {name: covered(FONT_DIR / f"{name}.c") for name in FONTS}
    for name, chars in per_font.items():
        print(f"{name}: {len(chars)} code points")
        report["fonts"][name] = {"codepoints": len(chars)}

    missing = {name: sorted(ch for ch in all_chars if ord(ch) not in chars) for name, chars in per_font.items()}
    # 18/22/28 share one CJK glyph set and must cover every string the UI can
    # show. 44 and 88 are numeric display sizes by design: the UI splits a big
    # value from its unit label so CJK never has to render at 44/88 px. They
    # only have to cover printable ASCII plus the units they do render.
    numeric_only = {"lw_font_44", "lw_font_88"}
    unit_chars = {"°", "℃", "%", "·", ":", "-", "/", "×"}
    for name, chars in missing.items():
        report["missing"][name] = "".join(chars)
        if name in numeric_only:
            required = {chr(c) for c in range(0x20, 0x7F)} | unit_chars
            truly = sorted(ch for ch in chars if ch in required)
            report.setdefault("not_expected", {})[name] = "".join(sorted(ch for ch in chars if ch not in required))
            report["fonts"][name]["scope"] = "numeric-only"
            missing[name] = truly
        status = "OK" if not missing[name] else f"{len(missing[name])} missing"
        print(f"  {name}: {status}" + ("" if not missing[name] else " -> " + "".join(missing[name][:40])))

    offenders = {name: chars for name, chars in missing.items() if chars}
    if offenders:
        for path, marks in sources.items():
            hit = sorted(ch for ch in marks if ord(ch) in {ord(c) for c in offenders.get("lw_font_22", "")})
            if hit:
                print(f"  source {path}: {''.join(hit)}")

    out = ROOT / "reports/release/font_coverage.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"report {out}")
    return 1 if offenders else 0


if __name__ == "__main__":
    sys.exit(main())
