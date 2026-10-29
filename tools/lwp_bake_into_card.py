"""Stage a TF-card copy of /littleworld with the prescaled (baked) LWP packs.

The baked packs from tools/lwp_prescale.py draw the same pixels but let LVGL
take its non-transformed RGB565A8 fast path. Swapping them onto the card is the
only way to see that on the board: the firmware reads the packs from
/sdcard/littleworld, and nothing in the build embeds them.

What this does, and nothing else:

  * copies every existing file from the card's littleworld/ into a staging
    directory (the card itself is never written by this script);
  * replaces the four character packs with the baked ones;
  * rewrites littleworld-v4.sha256 in the same order the card already uses,
    recomputing only the digests that changed;
  * updates payloadBytes in littleworld-v4.json to the new total.

The caller then renames the card's littleworld/ to a timestamped backup and
copies the staging directory in.

Usage:
  python tools/lwp_bake_into_card.py --card F:\\ --baked build/v45-assets \
      --stage build/v45-sd

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import sys
from pathlib import Path

# Only the four character/daily packs are baked. skin.lwp and ui.lwp are slice
# atlases: their clip records are rectangles inside one image, so cropping them
# would move every slice. tools/lwp_prescale.py refuses them for the same
# reason.
BAKED = ("male.lwp", "qianqian-base.lwp", "qianqian-wave.lwp", "daily.lwp")

MANIFEST = "littleworld-v4.json"
DIGEST = "littleworld-v4.sha256"

HEADER = struct.Struct("<8sHHHH")     # LWPX0001, w, h, frames, kind
TRAILER_MAGIC = b"LWTR0001"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_digest_order(path: Path) -> list[str]:
    """Return the file order the card's digest already uses."""
    order: list[str] = []
    if not path.is_file():
        return order
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) == 2:
            order.append(parts[1].strip())
    return order


def trailer_kind(path: Path) -> int:
    """Header field at +14 is the trailer kind; 1 means prescaled geometry."""
    with path.open("rb") as handle:
        head = handle.read(HEADER.size)
        if len(head) != HEADER.size:
            return 0
        magic, _w, _h, _frames, kind = HEADER.unpack(head)
        if magic != b"LWPX0001":
            return 0
        return kind


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Stage the card runtime directory with baked LWP packs.")
    parser.add_argument("--card", required=True,
                        help="drive root that holds littleworld/, e.g. F:\\")
    parser.add_argument("--baked", required=True,
                        help="directory holding the prescaled .lwp files")
    parser.add_argument("--stage", required=True,
                        help="empty directory to build the new littleworld/ in")
    args = parser.parse_args()

    card = Path(args.card) / "littleworld"
    baked = Path(args.baked)
    stage = Path(args.stage) / "littleworld"

    if not card.is_dir():
        print(f"no littleworld/ under {args.card}", file=sys.stderr)
        return 2
    for name in BAKED:
        if not (baked / name).is_file():
            print(f"missing baked pack: {baked / name}", file=sys.stderr)
            return 2

    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    order = read_digest_order(card / DIGEST)
    present = sorted(p.name for p in card.iterdir() if p.is_file())
    # Everything the card carries, in the card's own order. The manifest and the
    # digest itself are never listed inside the digest: the packer hashes only
    # the shipped assets, and a digest cannot hash itself.
    order = [n for n in order if n in present and n not in (MANIFEST, DIGEST)]
    order += [n for n in present
              if n not in order and n not in (MANIFEST, DIGEST)]

    copied = 0
    for name in present:
        source = card / name
        if name in BAKED:
            source = baked / name
        shutil.copy2(source, stage / name)
        copied += 1

    # A pack whose trailer is missing would fall back to the slow path and the
    # frame would be drawn 1.125x too large, so refuse that combination here
    # rather than after the copy.
    for name in BAKED:
        kind = trailer_kind(stage / name)
        if kind != 1:
            print(f"{name}: trailer kind {kind}, expected 1 (not baked?)",
                  file=sys.stderr)
            return 3

    lines = [f"{sha256_file(stage / name)}  {name}" for name in order]
    (stage / DIGEST).write_text("\n".join(lines) + "\n", encoding="utf-8")

    payload = sum(p.stat().st_size for p in stage.iterdir() if p.is_file())
    manifest_path = stage / MANIFEST
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    before = manifest.get("payloadBytes")
    manifest["payloadBytes"] = payload
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")

    print(f"staged {copied} file(s) in {stage}")
    for name in BAKED:
        print(f"  {name:<20} {card.joinpath(name).stat().st_size:>10,} -> "
              f"{(stage / name).stat().st_size:>10,} B")
    print(f"  payloadBytes {before:,} -> {payload:,}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
