"""Bake the nine-slice skin panels into flat images.

The control centre paints a 500x486 panel whose middle piece is a 468x12 strip
of the atlas stretched to 468x454. That stretch is what costs 807 of the 1038 ms
one repaint takes on the board (osperf skinb/skin1x), and it cannot be avoided
at run time: the panel is resizable, so the middle piece has to be scaled.

Since only three panel geometries actually exist in the code - 500x486 b=16,
108x196 b=10 and 254x112 b=12 - they can be resampled once, offline, with the
same fixed-point lattice LVGL uses, and drawn afterwards as one 1:1 image.

This tool only writes the pixels and a report. It compares the opaque part of
each baked panel against the same pixels in a host screenshot, which is the
ground truth for "did the bake keep the look".
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lwp_prescale import read_pack, frame_view, map_axis, crop_axis, tint_rgb565  # noqa: E402

SKIN_ATLAS = 'atlas'   # the runtime name is <pack>.<clip> = v43skin.atlas
# All three geometries are asked for as SK_CC_PANEL, which is rects[11] in
# lw_skin.c, not the big SK_BOOK rect: the 128x128 tile is what the runtime
# stretches to 500x486. Baking rects[10] produced a leather book cover.
PANEL_RECT = (152, 178, 128, 128)
PANELS = [
    ('cc',   500, 486, 16),
    ('tile', 108, 196, 10),
    ('side', 254, 112, 12),
]
NIGHT = (0xb79496, 26)
DUSK = (0xd8a17d, 12)


def lattice(atlas_len, scale, mode):
    """Source index per destination slot of the scaled atlas.

    The extent is the *scaled* one: lv_draw_sw_transform builds its step from the
    destination extent it is asked to fill, and an atlas row stretched 38x covers
    ~20k destination slots, not 544. Using the unscaled length dropped every row
    of the middle piece (the first bake drew only the top and bottom edges).
    """
    dest = max(1, (atlas_len * scale + 0x80) >> 8)
    return map_axis(0, atlas_len, dest, scale, mode)


def piece_axis(src_lo, src_len, dst_len):
    """Stretch one piece on its own: source index per destination pixel."""
    out = []
    for i in range(dst_len):
        j = (i * src_len) // dst_len
        if j > src_len - 1:
            j = src_len - 1
        out.append(src_lo + j)
    return out


def piece_window(atlas_len, src_lo, src_len, dst_len, scale, mode, sampler):
    """Source indices for one nine-slice piece, one per destination pixel.

    sampler='crop' (the default since the cloud audit) builds the piece's
    sampling grid the way tools/lwp_prescale.py - the implementation already
    verified at 237 frames / 0 differences - builds it for a sprite clip:
    crop_axis() takes the window out of the *whole atlas* lattice (whose
    length is the scaled length, (atlas_len*scale+0x80)>>8, and whose bounds
    are judged against that scaled length, not the atlas length), and the
    window - not a truncated scaled offset - decides which source pixels
    the piece gets. The old code anchored the window at
    (src_lo*scale)>>8 inside a per-call lattice, which is off by one source
    pixel wherever the lattice crosses the piece edge on the other side of
    a rounding boundary; that is the 1 px sampling difference the host
    screenshot baseline shows on panel paper texture.

    sampler='atlas' keeps that old behaviour for A/B comparison, and
    sampler='piece' keeps the naive per-piece stretch. Neither is the
    default any more.

    A crop window can be one slot shorter or longer than the destination
    piece (the lattice, not the geometry, decides); extra slots are
    clipped away and a short window's last source pixel is held for the
    remaining slot, matching the clamp map_axis() itself applies at the
    edge of a lattice.
    """
    if sampler == 'piece':
        return piece_axis(src_lo, src_len, dst_len)
    if sampler == 'atlas':
        lat = lattice(atlas_len, scale, mode)
        start = (src_lo * scale) >> 8
        out = []
        for k in range(dst_len):
            t = start + k
            out.append(lat[t] if 0 <= t < len(lat) else lat[-1])
        return out
    window, _first = crop_axis(atlas_len, src_lo, src_len, scale, mode)
    out = []
    for k in range(dst_len):
        out.append(window[k] if k < len(window) else window[-1])
    return out


def nine_slice(panel_w, panel_h, border, atlas_w, atlas_h, rgb, alpha, mode, sampler='crop'):
    """Resample one nine-slice panel the way lv_draw_sw_transform would.

    Each piece is its own draw of the whole atlas with dsc.scale_*, clipped
    to the piece; piece_window() decides which source pixels each piece is
    sampled from.
    """
    sw = [border, PANEL_RECT[2] - 2 * border, border]
    sh = [border, PANEL_RECT[3] - 2 * border, border]
    dw = [border, panel_w - 2 * border, border]
    dh = [border, panel_h - 2 * border, border]

    out_rgb = np.zeros((panel_h, panel_w, 2), dtype=np.uint8)
    out_a = np.zeros((panel_h, panel_w), dtype=np.uint8)
    sy = PANEL_RECT[1]
    dy = 0
    for j in range(3):
        sx = PANEL_RECT[0]
        dx = 0
        for i in range(3):
            src_w, src_h = sw[i], sh[j]
            dst_w, dst_h = dw[i], dh[j]
            scale_x = (dst_w * 256) // src_w
            scale_y = (dst_h * 256) // src_h
            cols = np.asarray(piece_window(atlas_w, sx, src_w, dst_w, scale_x, mode, sampler), dtype=np.intp)
            rows = np.asarray(piece_window(atlas_h, sy, src_h, dst_h, scale_y, mode, sampler), dtype=np.intp)
            out_rgb[dy:dy + dst_h, dx:dx + dst_w] = rgb[rows[:, None], cols[None, :]]
            out_a[dy:dy + dst_h, dx:dx + dst_w] = alpha[rows[:, None], cols[None, :]]
            sx += src_w
            dx += dst_w
        sy += sh[j]
        dy += dh[j]
    return out_rgb, out_a


def to_ppm(path, rgb, a):
    """Write PNG through Pillow when present, otherwise a raw PAM."""
    try:
        from PIL import Image
    except ImportError:
        path.with_suffix('.pam').write_bytes(
            b'P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'
            % (rgb.shape[1], rgb.shape[0]) + b''.join(
                struct.pack('<BBBB', int(rgb[y, x, 1] >> 5) * 8, int(rgb[y, x, 1] & 0x1f) * 8,
                            int(rgb[y, x, 0]) & 0xff, int(a[y, x]))
                for y in range(rgb.shape[0]) for x in range(rgb.shape[1])))
        return
    img = np.zeros((rgb.shape[0], rgb.shape[1], 4), dtype=np.uint8)
    px = rgb.astype(np.uint16)
    v = (px[:, :, 0].astype(np.uint16) | (px[:, :, 1].astype(np.uint16) << 8))
    r = ((v >> 11) & 0x1f) * 255 // 31
    g = ((v >> 5) & 0x3f) * 255 // 63
    b = (v & 0x1f) * 255 // 31
    img[:, :, 0] = r.astype(np.uint8)
    img[:, :, 1] = g.astype(np.uint8)
    img[:, :, 2] = b.astype(np.uint8)
    img[:, :, 3] = a
    Image.fromarray(img, 'RGBA').save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--skin', default=r'build\v45-runtime\skin.lwp')
    ap.add_argument('--shot', default=r'build\v45-assets\shot-skin2\02-control-center.bmp')
    ap.add_argument('--out', default=r'build\v45-assets\baked')
    ap.add_argument('--mode', default='half', choices=['half', 'floor', 'piece'])
    ap.add_argument('--sampler', default='crop', choices=['crop', 'atlas', 'piece'],
                    help="per-piece sampling grid: 'crop' = crop_axis window from the "
                         "whole-atlas lattice (default), 'atlas' = pre-audit lattice + "
                         "truncated offset, 'piece' = naive per-piece stretch")
    args = ap.parse_args()

    raw, width, height, records, _kind, _trailer = read_pack(Path(args.skin))
    rec = next((r for r in records if r['name'] == SKIN_ATLAS), None)
    if rec is None:
        raise SystemExit('atlas %s not found in %s' % (SKIN_ATLAS, args.skin))
    rgb, alpha = frame_view(raw, width, height, rec['offset'], 0)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    shot = None
    if Path(args.shot).exists():
        from PIL import Image
        shot = np.asarray(Image.open(args.shot).convert('RGB'))

    for name, w, h, b in PANELS:
        panel_rgb, panel_a = nine_slice(w, h, b, width, height, rgb, alpha,
                                        'piece' if args.mode == 'piece' else args.mode,
                                        'piece' if args.mode == 'piece' else args.sampler)
        to_ppm(out / ('panel-%s.png' % name), panel_rgb, panel_a)
        for tag, tint in (('day', None), ('night', NIGHT), ('dusk', DUSK)):
            trgb, ta = panel_rgb, panel_a
            if tint is not None:
                tinted = tint_rgb565(trgb.tobytes(), tint[0], tint[1])
                trgb = np.frombuffer(tinted, dtype=np.uint8).reshape(panel_rgb.shape)
            to_ppm(out / ('panel-%s-%s.png' % (name, tag)), trgb, ta)
        if shot is not None and name == 'cc':
            # The control centre panel sits at (484,58) in the 1024x600 frame.
            x0, y0 = 484, 58
            region = shot[y0:y0 + h, x0:x0 + w].astype(np.int16)
            night = tint_rgb565(panel_rgb.tobytes(), NIGHT[0], NIGHT[1])
            nrgb = np.frombuffer(night, dtype=np.uint8).reshape(panel_rgb.shape)
            px = nrgb.astype(np.uint16)
            v = (px[:, :, 0].astype(np.int32) | (px[:, :, 1].astype(np.int32) << 8))
            r = (((v >> 11) & 0x1f) * 255 // 31)
            g = (((v >> 5) & 0x3f) * 255 // 63)
            b2 = ((v & 0x1f) * 255 // 31)
            mine = np.stack([r, g, b2], axis=-1)
            opaque = panel_a >= 255
            delta = np.abs(mine - region).max(axis=-1)
            bad = int(np.count_nonzero((delta > 1) & opaque))
            total = int(np.count_nonzero(opaque))
            worst = int(delta[opaque].max()) if total else 0
            print('cc panel: %d opaque px, %d differ by >1 (worst %d)' % (total, bad, worst))
            if bad:
                ys, xs = np.where((delta > 1) & opaque)
                print('  first few: %s' % [(int(xs[i]), int(ys[i]), int(delta[ys[i], xs[i]]))
                                           for i in range(min(5, bad))])
        print('%s %dx%d b=%d baked' % (name, w, h, b))


if __name__ == '__main__':
    main()
