"""Compare panel-bake samplers on the real skin atlas (numpy only).

    python3 tools/panel_bake_diff.py [--skin assets/visual-v4-3/runtime/skin.lwp]

For each of the three baked panel geometries this reports how much
the sampling-grid fix changes the output: differing pixel count
(RGBA compared after RGB565 expansion), how many of those differ by
more than 8/255 in some channel, and the mean/max absolute channel
delta. A large count with a small mean is the signature of a one-slot
sampling phase shift over a noisy texture (the panel paper), not of
different artwork.

The panel PNGs committed under reports/perf-v4-5/panel-bake/ are NOT
used as a reference: measured 2026-11-08, both samplers differ from
them on ~91% of pixels with a mean delta of ~46/255, i.e. they were
baked from a different atlas generation than the skin.lwp in this
tree. There is no ground truth in this environment, so this script
deliberately does not claim either sampler is "aligned" with LVGL -
it only measures the change. Alignment can only be decided by the
156-screenshot host regression on Windows (tools/shot_diff.py).
"""

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lwp_panel_bake import nine_slice, PANELS, SKIN_ATLAS  # noqa: E402
from lwp_prescale import read_pack, frame_view  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def to_rgba(rgb, a):
    px = rgb.astype(np.uint16)
    v = (px[:, :, 0].astype(np.uint32) | (px[:, :, 1].astype(np.uint32) << 8))
    out = np.zeros((rgb.shape[0], rgb.shape[1], 4), dtype=np.uint8)
    out[:, :, 0] = (((v >> 11) & 0x1F) * 255 // 31).astype(np.uint8)
    out[:, :, 1] = (((v >> 5) & 0x3F) * 255 // 63).astype(np.uint8)
    out[:, :, 2] = ((v & 0x1F) * 255 // 31).astype(np.uint8)
    out[:, :, 3] = a
    return out


def diff_count(rgba_a, rgba_b):
    return int(np.count_nonzero(np.any(rgba_a != rgba_b, axis=-1)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--skin', default=str(ROOT / 'assets/visual-v4-3/runtime/skin.lwp'))
    args = ap.parse_args()

    raw, width, height, records, _kind, _trailer = read_pack(Path(args.skin))
    rec = next((r for r in records if r['name'] == SKIN_ATLAS), None)
    if rec is None:
        raise SystemExit('atlas %s not found in %s' % (SKIN_ATLAS, args.skin))
    rgb, alpha = frame_view(raw, width, height, rec['offset'], 0)

    for name, w, h, b in PANELS:
        baked = {}
        for sampler in ('atlas', 'crop'):
            prgb, pa = nine_slice(w, h, b, width, height, rgb, alpha, 'half', sampler)
            baked[sampler] = to_rgba(prgb, pa)
        delta = np.abs(baked['atlas'].astype(np.int16)
                       - baked['crop'].astype(np.int16)).max(axis=-1)
        print('%s %dx%d: atlas-vs-crop differ %d / %d px; >8/255: %d px; '
              'mean |delta| %.3f; max %d' % (
                  name, w, h, diff_count(baked['atlas'], baked['crop']), w * h,
                  int(np.count_nonzero(delta > 8)), float(delta.mean()), int(delta.max())))


if __name__ == '__main__':
    main()
