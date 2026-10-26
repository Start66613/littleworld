"""Convert UI art (PNG) into the on-device ".lwu" container.

Why this exists
---------------
LVGL 9.5 has no nine-slice widget. Scaling a pixel-art panel with
lv_image_set_scale() stretches the corners and blurs the border, which is the
exact artefact the UI pipeline is supposed to prevent. The device therefore
needs the border widths and the button states *inside* the image file, so that
lw_ui_asset_parse() can hand the renderer nine rectangles it can place itself.

This tool is the only supported producer of .lwu files. It refuses anything it
cannot represent exactly - it never rescales, never pads, never quantises a
colour count, and never silently drops a frame.

Layout produced (little endian, 32-byte header + payload):

    0   char   magic[8]  "LWUI0001"
    8   u16    width     one state band, pixels
    10  u16    height    one state band, pixels
    12  u8     kind      0 icon, 1 panel, 2 button
    13  u8     format    0 RGB565, 1 RGB565A8
    14  u16    states
    16  u16    inset_left
    18  u16    inset_top
    20  u16    inset_right
    22  u16    inset_bottom
    24  u16    flags     bit 0 = pixel art
    26  u16    reserved  0
    28  u32    data_bytes

Payload: for each state in order, the RGB565 plane (little endian) followed by
one tightly packed alpha plane. That is byte-for-byte what lw_asset_spec_t's
read callback must fill, so the device never re-orders pixels.

Usage
-----
    python tools/build_ui_assets.py --source assets/ui --out assets/ui/runtime
    python tools/build_ui_assets.py --source assets/ui --out ... --check
    python tools/build_ui_assets.py --spec panel:foo.png:6,6,6,6 --out ...
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - environment guard
    print("Pillow is required: run with the bundled venv python", file=sys.stderr)
    raise SystemExit(2)

MAGIC = b"LWUI0001"
HEADER = struct.Struct("<8sHHBBHHHHHHHI")
KIND_ICON, KIND_PANEL, KIND_BUTTON = 0, 1, 2
FORMAT_RGB565, FORMAT_RGB565A8 = 0, 1
STATE_COUNT = 3
MAX_SIDE = 1024
FLAG_PIXEL_ART = 1


class AssetError(Exception):
    """A refusal with a human explanation; never a silent downgrade."""


@dataclass
class AssetSpec:
    path: Path
    kind: int
    insets: tuple[int, int, int, int] = (0, 0, 0, 0)
    pixel_art: bool = True
    states: int = 1
    alpha: bool = True
    source: str = ""


@dataclass
class AssetResult:
    source: str
    output: str
    kind: str
    width: int
    height: int
    states: int
    format: str
    bytes: int
    sha256: str
    insets: list[int] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


def _pixels(image: Image.Image):
    """Pillow renamed getdata(); use whichever exists so the tool does not start
    emitting deprecation noise on a newer Pillow."""
    getter = getattr(image, "get_flattened_data", None)
    return getter() if getter is not None else image.getdata()


def rgb565_plane(image: Image.Image) -> bytes:
    """RGB565 little endian, alpha ignored (it travels in its own plane)."""
    rgb = image.convert("RGB")
    out = bytearray(rgb.width * rgb.height * 2)
    index = 0
    for r, g, b in _pixels(rgb):
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[index] = value & 0xFF
        out[index + 1] = (value >> 8) & 0xFF
        index += 2
    return bytes(out)


def alpha_plane(image: Image.Image) -> bytes:
    return image.convert("RGBA").getchannel("A").tobytes()


def check_transparency(image: Image.Image, spec: AssetSpec) -> list[str]:
    """Report what the alpha channel actually contains.

    A panel that claims to be transparent but is fully opaque is an authoring
    mistake worth surfacing: the nine-slice will look like a solid rectangle on
    the device and nobody will know why.
    """
    notes: list[str] = []
    if image.mode not in ("RGBA", "LA", "P"):
        notes.append(f"source has no alpha channel (mode {image.mode}); written fully opaque")
        return notes
    alpha = image.convert("RGBA").getchannel("A")
    lo, hi = alpha.getextrema()
    if lo == 255:
        notes.append("alpha channel present but fully opaque")
    elif lo == 0 and hi == 0:
        raise AssetError("image is fully transparent: nothing would be drawn")
    else:
        # Count partially transparent pixels; a pixel-art edge should be either
        # 0 or 255, so anything in between means the source was resampled.
        partial = sum(1 for value in _pixels(alpha) if 0 < value < 255)
        if partial and spec.pixel_art:
            notes.append(f"{partial} semi-transparent pixels: pixel art usually wants hard edges")
    return notes


def validate_insets(width: int, height: int, insets: tuple[int, int, int, int]) -> None:
    left, top, right, bottom = insets
    for name, value in zip(("left", "top", "right", "bottom"), insets):
        if value < 0:
            raise AssetError(f"inset {name} is negative")
    if left + right >= width:
        raise AssetError(f"left+right insets ({left}+{right}) leave no centre in a {width}px wide art")
    if top + bottom >= height:
        raise AssetError(f"top+bottom insets ({top}+{bottom}) leave no centre in a {height}px tall art")


def build_asset(spec: AssetSpec, out_dir: Path) -> AssetResult:
    if not spec.path.exists():
        raise AssetError(f"source not found: {spec.path}")
    try:
        with Image.open(spec.path) as probe:
            probe.load()
            image = probe.copy()
    except OSError as exc:
        raise AssetError(f"cannot decode {spec.path.name}: {exc}") from exc

    if image.width == 0 or image.height == 0:
        raise AssetError("image has zero size")
    if image.width > MAX_SIDE or image.height > MAX_SIDE:
        raise AssetError(f"{image.width}x{image.height} exceeds the {MAX_SIDE}px limit")

    if spec.kind == KIND_BUTTON:
        if image.height % STATE_COUNT != 0:
            raise AssetError(
                f"button strip height {image.height} is not divisible by {STATE_COUNT}: "
                "the pressed band would shift by a pixel"
            )
        band_h = image.height // STATE_COUNT
        width, height = image.width, band_h
        states = STATE_COUNT
        if image.height != band_h * STATE_COUNT:
            raise AssetError("button strip does not divide into equal bands")
    else:
        width, height = image.width, image.height
        states = 1

    if spec.kind in (KIND_PANEL, KIND_BUTTON):
        validate_insets(width, height, spec.insets)
    else:
        if any(spec.insets):
            raise AssetError("an icon has no borders; insets must be 0,0,0,0")
        spec.insets = (0, 0, 0, 0)

    notes = check_transparency(image, spec)

    planes: list[bytes] = []
    for state in range(states):
        band = image.crop((0, state * height, width, (state + 1) * height)) if states > 1 else image
        if band.size != (width, height):
            raise AssetError(f"state {state} cropped to {band.size}, expected {(width, height)}")
        planes.append(rgb565_plane(band))
        if spec.alpha:
            planes.append(alpha_plane(band))

    fmt = FORMAT_RGB565A8 if spec.alpha else FORMAT_RGB565
    payload = b"".join(planes)
    per_state = width * height * (3 if spec.alpha else 2)
    expected = per_state * states
    if len(payload) != expected:
        raise AssetError(f"payload is {len(payload)} B, geometry says {expected} B")

    header = HEADER.pack(
        MAGIC,
        width,
        height,
        spec.kind,
        fmt,
        states,
        *spec.insets,
        FLAG_PIXEL_ART if spec.pixel_art else 0,
        0,
        len(payload),
    )
    if len(header) != 32:
        raise AssetError(f"header is {len(header)} B, expected 32")

    blob = header + payload
    out_dir.mkdir(parents=True, exist_ok=True)
    destination = out_dir / (spec.path.stem + ".lwu")
    destination.write_bytes(blob)

    return AssetResult(
        source=spec.path.as_posix(),
        output=destination.as_posix(),
        kind={KIND_ICON: "icon", KIND_PANEL: "panel", KIND_BUTTON: "button"}[spec.kind],
        width=width,
        height=height,
        states=states,
        format="RGB565A8" if spec.alpha else "RGB565",
        bytes=len(blob),
        sha256=hashlib.sha256(blob).hexdigest(),
        insets=list(spec.insets),
        notes=notes,
    )


def parse_spec(text: str) -> AssetSpec:
    """"name.png:panel:6,6,6,6" or "name.png:icon"."""
    parts = text.split(":")
    if len(parts) < 2:
        raise AssetError(f"--spec needs at least <file>:<kind>, got {text!r}")
    path = Path(parts[0])
    kind_name = parts[1].lower()
    kinds = {"icon": KIND_ICON, "panel": KIND_PANEL, "button": KIND_BUTTON}
    if kind_name not in kinds:
        raise AssetError(f"unknown kind {kind_name!r}; use icon, panel or button")
    insets = (0, 0, 0, 0)
    if len(parts) >= 3 and parts[2]:
        try:
            values = [int(v) for v in parts[2].split(",")]
        except ValueError as exc:
            raise AssetError(f"insets must be four integers, got {parts[2]!r}") from exc
        if len(values) != 4:
            raise AssetError(f"insets need exactly four values, got {len(values)}")
        insets = tuple(values)  # type: ignore[assignment]
    return AssetSpec(path=path, kind=kinds[kind_name], insets=insets)


def discover(source: Path) -> list[AssetSpec]:
    """Every PNG under --source, classified by file name.

    Naming is the manifest: a file called "button-*.png" is a button strip, a
    file called "panel-*.png" is a nine-slice panel, everything else is an icon.
    Insets come from a sidecar .json next to the PNG, because border widths are
    art decisions that belong with the art.
    """
    if not source.exists():
        raise AssetError(f"source directory not found: {source}")
    specs: list[AssetSpec] = []
    for path in sorted(source.rglob("*.png")):
        if "runtime" in path.parts:
            continue
        stem = path.stem.lower()
        if stem.startswith("button-"):
            kind = KIND_BUTTON
        elif stem.startswith("panel-"):
            kind = KIND_PANEL
        elif stem.startswith("icon-"):
            kind = KIND_ICON
        else:
            raise AssetError(
                f"{path.name}: cannot tell what this is; name it icon-*, panel-* or button-*"
            )
        insets = (0, 0, 0, 0)
        sidecar = path.with_suffix(".json")
        if sidecar.exists():
            try:
                # utf-8-sig: a sidecar saved by Notepad or PowerShell 5.1 has a
                # BOM, and refusing it would be a papercut with no upside.
                meta = json.loads(sidecar.read_text(encoding="utf-8-sig"))
            except (OSError, json.JSONDecodeError) as exc:
                raise AssetError(f"{sidecar.name}: {exc}") from exc
            raw = meta.get("insets")
            if raw is not None:
                if not isinstance(raw, list) or len(raw) != 4:
                    raise AssetError(f"{sidecar.name}: insets must be a four element list")
                insets = tuple(int(v) for v in raw)  # type: ignore[assignment]
            if meta.get("pixel_art") is False:
                spec_pixel = False
            else:
                spec_pixel = True
        else:
            spec_pixel = True
        specs.append(AssetSpec(path=path, kind=kind, insets=insets, pixel_art=spec_pixel,
                               source=path.as_posix()))
    if not specs:
        raise AssetError(f"no PNG files under {source}")
    return specs


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build .lwu UI assets from PNG art")
    parser.add_argument("--source", type=Path, help="directory to scan for PNG art")
    parser.add_argument("--spec", action="append", default=[], help="explicit file:kind[:insets]")
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("--json", action="store_true", help="print the report as JSON")
    parser.add_argument("--no-alpha", action="store_true", help="force RGB565 (drop alpha)")
    args = parser.parse_args(argv)

    if not args.source and not args.spec:
        parser.error("give --source or at least one --spec")

    try:
        specs = discover(args.source) if args.source else []
        specs.extend(parse_spec(text) for text in args.spec)
        if not specs:
            raise AssetError("nothing to convert")
        if args.no_alpha:
            for spec in specs:
                spec.alpha = False
        results = []
        for spec in specs:
            if args.check:
                # Build into a scratch dir so validation exercises the real
                # encoder instead of a second, drifting copy of the rules.
                import tempfile

                with tempfile.TemporaryDirectory() as tmp:
                    results.append(build_asset(spec, Path(tmp)))
            else:
                results.append(build_asset(spec, args.out))
    except AssetError as exc:
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False))
        else:
            print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps({"ok": True, "assets": [r.__dict__ for r in results]}, ensure_ascii=False,
                         indent=2))
    else:
        total = sum(r.bytes for r in results)
        for result in results:
            print(f"{Path(result.source).name} -> {Path(result.output).name}")
            print(f"  {result.kind} {result.width}x{result.height} x{result.states} "
                  f"{result.format} insets={result.insets} {result.bytes} B")
            for note in result.notes:
                print(f"  note: {note}")
        print(f"\n{len(results)} asset(s), {total} B total")
        if args.check:
            print("check only: nothing written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
