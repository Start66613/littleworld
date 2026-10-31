"""Re-pack an LWPX character pack so the sprite draws at 1:1 with no transform.

Why this exists
---------------
V4.4 measured 117 ms per RPG frame and 251 ms per pet frame with zero cache
misses: the cost is LVGL's software image transform, not I/O and not blending
area. lv_draw_sw_img.c:223 decides the path:

    bool transformed = draw_dsc->rotation != 0 ||
                       draw_dsc->scale_x != LV_SCALE_NONE ||
                       draw_dsc->scale_y != LV_SCALE_NONE;

Every character sprite in Little World is created at scale 288 or 282 (the
V4.3 proportion correction), i.e. transformed == true, which sends every frame
through transform_and_recolor(): a per-draw lv_malloc, a per-pixel 8.8 fixed
point resample, and a second blend. Drawing the same art at LV_SCALE_NONE takes
the RGB565A8 fast path and measured 3.9-4.3x cheaper in host/render_bench.c
while blending exactly the same canvas.

This tool bakes that correction into the pixels instead: crop every clip to one
shared content box, resample to the final on-screen size using LVGL's own
fixed-point mapping, and record the result in the LWTR0001 geometry trailer
with prescaled=1. At run time lw_asset_draw_geometry() then returns
scale=LV_SCALE_NONE, offset=0, so lv_image never enters the transform path.

Shared box, not per-clip box
---------------------------
LWPX stores one width/height for the whole pack and every frame is
width*height*3 bytes. Per-clip boxes would therefore need per-clip canvas
sizes, which the format cannot express, so the box is the union over all clips
in the pack. That is also what tools/build_character_pack.py already does for
the embedded LWFA fallback, so both asset paths then agree.

Exactness
---------
The resample reproduces lv_draw_sw_transform.c:191-219 for angle==0 and
pivot==0, antialias off (lw_sprite_set_pixel_mode() always turns antialias off):

    xs1_ups = x1 * 65536 / scale          (dest_area_limited.x1 == 0)
    xs_step = 256 * (xs2_ups - xs1_ups) / (dest_w - 1)
    xs_ups  = xs1_ups + 0x80 + ((xs_step * x) >> 8)
    src_x   = xs_ups >> 8

The 0x80 is the half-pixel bias that made a naive nearest-neighbour copy differ
by thousands of pixels; `--round trunc` exists only to reproduce that failure.

Usage
-----
    python tools/lwp_prescale.py <in.lwp> <out.lwp> --scale 288 [--round half]
    python tools/lwp_prescale.py <in.lwp> --inspect

Writes nothing unless the whole pack is processed, so a failure cannot leave a
half-rewritten asset behind.
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

HEADER = struct.Struct('<8sHHHH')
CLIP = struct.Struct('<32sHhhBBI32H')
CLIP_BYTES = 108
TRAILER_MAGIC = b'LWTR0001'
TRAILER_HEADER = struct.Struct('<8sHH')
TRAILER_RECORD = struct.Struct('<HHHHH')
ALPHA_MIN = 8


def read_pack(path):
    raw = path.read_bytes()
    magic, width, height, clips, trailer_kind = HEADER.unpack_from(raw, 0)
    if magic != b'LWPX0001':
        raise SystemExit('%s: not an LWPX pack (%r)' % (path, magic))
    records = []
    for i in range(clips):
        off = 16 + CLIP_BYTES * i
        name, frames, ax, ay, loop, _pad, offset, *durations = CLIP.unpack_from(raw, off)
        records.append({'name': name.split(b'\0')[0].decode(),
                        'frames': frames, 'anchor_x': ax, 'anchor_y': ay,
                        'loop': loop, 'offset': offset,
                        'durations': durations})
    trailer = None
    if trailer_kind == 1:
        start = len(raw) - TRAILER_HEADER.size - TRAILER_RECORD.size * clips
        magic8, tclips, _reserved = TRAILER_HEADER.unpack_from(raw, start)
        if magic8 != TRAILER_MAGIC or tclips != clips:
            raise SystemExit('%s: malformed geometry trailer' % path)
        trailer = []
        for i in range(clips):
            trailer.append(TRAILER_RECORD.unpack_from(raw, start + TRAILER_HEADER.size
                                                      + TRAILER_RECORD.size * i))
    return raw, width, height, records, trailer_kind, trailer


def frame_view(raw, width, height, offset, frame):
    """RGB565 plane then A8 plane, as lw_pack.c reads it."""
    pixels = width * height
    base = offset + frame * pixels * 3
    rgb = np.frombuffer(raw, dtype=np.uint8, count=pixels * 2, offset=base)
    alpha = np.frombuffer(raw, dtype=np.uint8, count=pixels, offset=base + pixels * 2)
    return rgb.reshape(height, width, 2), alpha.reshape(height, width)


def union_box(raw, width, height, records):
    """Union of every frame's alpha bbox across every clip in the pack."""
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for rec in records:
        for frame in range(rec['frames']):
            _rgb, alpha = frame_view(raw, width, height, rec['offset'], frame)
            ys, xs = np.nonzero(alpha > ALPHA_MIN)
            if not xs.size:
                continue
            x0 = min(x0, int(xs.min()))
            x1 = max(x1, int(xs.max()))
            y0 = min(y0, int(ys.min()))
            y1 = max(y1, int(ys.max()))
    if x1 < 0:
        raise SystemExit('pack has no opaque pixels; nothing to crop')
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1


def map_axis(src_lo, src_len, dest_len, scale, mode):
    """LVGL's fixed-point source index for each destination pixel.

    Mirrors lv_draw_sw_transform.c:191-219 with pivot 0 and angle 0. Kept as a
    list rather than a formula because the per-pixel step is integer-divided
    once per row/column, not recomputed each pixel.
    """
    def up(x):
        return (x * 256 * 256) // scale        # transform_point_upscaled, pivot 0

    x1_ups = up(0)
    x2_ups = up(dest_len - 1)
    step = 0 if dest_len <= 1 else (256 * (x2_ups - x1_ups)) // (dest_len - 1)
    bias = 0x80 if mode == 'half' else 0
    out = []
    for x in range(dest_len):
        u = x1_ups + bias + ((step * x) >> 8)
        idx = u >> 8
        if idx < 0:
            idx = 0
        if idx >= src_len:
            idx = src_len - 1
        out.append(idx + src_lo)
    return out


def crop_axis(canvas_len, src_lo, src_len, scale, mode):
    """The slice of LVGL's canvas-wide lattice that covers one bbox.

    The first version built a fresh lattice per bbox, anchored at the bbox
    origin. That is not what LVGL does: lv_draw_sw_transform() derives its step
    from the whole destination extent, so a sub-window resampled on its own
    grid picks slightly different source columns and rows. The bake must be a
    crop of the pixels LVGL would have drawn, not an independent resample -
    that is what makes prescaled output identical to the shipped output.

    The second version sized the window with scaled(src_len), which is off by
    one pixel wherever the lattice happens to cross the bbox edge on the other
    side of a rounding boundary: qianqian-base sleep_turn lost its last column
    that way. So the window is derived from the lattice itself - first and last
    destination index whose source column falls inside the bbox - and the
    caller anchors the bake to the window start, not to the bbox start.
    """
    full_dest = max(1, scaled(canvas_len, scale))
    full = map_axis(0, canvas_len, full_dest, scale, mode)
    first = None
    last = None
    for d, src in enumerate(full):
        if src_lo <= src <= src_lo + src_len - 1:
            if first is None:
                first = d
            last = d
    if first is None:
        # bbox fell between two sampled columns; take the nearest window.
        first = min(max(0, scaled(src_lo, scale)), full_dest - 1)
        last = min(full_dest - 1, first + max(0, scaled(src_len, scale) - 1))
    return full[first:last + 1], first


def resample(raw, width, height, rec, frame, window, scale, mode):
    """Crop one frame to the lattice window; returns RGB565+A8 bytes."""
    xs, _d0x = crop_axis(width, window[0], window[2], scale, mode)
    ys, _d0y = crop_axis(height, window[1], window[3], scale, mode)
    rgb, alpha = frame_view(raw, width, height, rec['offset'], frame)
    xs = np.asarray(xs, dtype=np.intp)
    ys = np.asarray(ys, dtype=np.intp)
    # Index with the row and column maps separately so the gather is two
    # contiguous passes instead of one 2-D fancy index.
    out_rgb = rgb[ys][:, xs].reshape(-1)
    out_a = alpha[ys][:, xs].reshape(-1)
    return out_rgb.tobytes() + out_a.tobytes()


def scaled(v, scale):
    return (v * scale + 0x80) >> 8


def safe_clip_box(raw, width, height, rec):
    """This clip's own alpha union, or None when nothing in it is opaque."""
    try:
        return union_box(raw, width, height, [rec])
    except SystemExit:
        return None


def tint_rgb565(rgb_bytes, color, opa):
    """Recolour one RGB565 plane exactly the way LVGL does on the board.

    lv_color_16_16_mix(tint, pixel, opa) mixes in the packed 16-bit domain with
    a 5-bit multiplier, so replicating it here is what makes a baked night
    variant pixel-identical to the runtime lv_obj_set_style_image_recolor call
    it replaces. That call is expensive because a non-zero recolor_opa takes
    the image out of LVGL's RGB565A8 fast path and into the per-pixel transform
    path - 24 ms per frame change on this board, measured.
    """
    px = np.frombuffer(rgb_bytes, dtype='<u2').astype(np.uint32)
    if opa <= 0:
        return rgb_bytes
    if opa >= 255:
        return np.full(px.shape, color, dtype='<u2').tobytes()
    mix = ((opa + 4) >> 3) & 0x1F
    bg = (px | (px << 16)) & 0x7E0F81F
    fg = (np.uint32(color) | (np.uint32(color) << 16)) & 0x7E0F81F
    out = ((((fg - bg) * mix) >> 5) + bg) & 0x7E0F81F
    out = (out >> 16) | out
    return out.astype('<u2').tobytes()


def build(raw, width, height, records, scale, mode, per_clip=False, night_tint=None):
    box = union_box(raw, width, height, records)
    # The window is measured on LVGL's lattice, so the baked canvas is a whole
    # number of destination pixels wide and the anchor is expressed in the same
    # grid the runtime will use. Using scaled(bbox) here instead was the bug
    # that dropped qianqian-base sleep_turn's last column.
    xs, dx0 = crop_axis(width, box[0], box[2], scale, mode)
    ys, dy0 = crop_axis(height, box[1], box[3], scale, mode)
    dest_w = len(xs)
    dest_h = len(ys)
    frame_bytes = dest_w * dest_h * 3
    plane_bytes = dest_w * dest_h * 2
    emitted = []
    for rec in records:
        # The anchor must be expressed in the baked image's pixel grid: the
        # sprite subtracts it directly from the on-screen anchor.
        ax = scaled(rec['anchor_x'], scale) - dx0
        ay = scaled(rec['anchor_y'], scale) - dy0
        payload = bytearray()
        for frame in range(rec['frames']):
            payload += resample(raw, width, height, rec, frame, box, scale, mode)
        # The pack has one canvas, so every clip's frame bytes are dest_w x
        # dest_h. The trailer can still say that this clip only occupies a
        # smaller box inside it, which is what stops a 54x47 dog from being
        # blended over a 276x252 union window every frame.
        bx, by, bw, bh = 0, 0, dest_w, dest_h
        if per_clip:
            own = safe_clip_box(raw, width, height, rec)
            if own is not None:
                cxs, cx0 = crop_axis(width, own[0], own[2], scale, mode)
                cys, cy0 = crop_axis(height, own[1], own[3], scale, mode)
                bx, by, bw, bh = cx0 - dx0, cy0 - dy0, len(cxs), len(cys)
        emitted.append({'name': rec['name'], 'frames': rec['frames'], 'ax': ax,
                        'ay': ay, 'loop': rec['loop'],
                        'durations': rec['durations'],
                        'payload': bytes(payload), 'bbox': (bx, by, bw, bh)})
        if night_tint and rec['name'] in night_tint:
            color, opa = night_tint[rec['name']]
            tinted = bytearray()
            for frame in range(rec['frames']):
                start = frame * frame_bytes
                tinted += tint_rgb565(payload[start:start + plane_bytes], color, opa)
                tinted += payload[start + plane_bytes:start + frame_bytes]
            # Same anchor, same box: only the colour plane differs, so the
            # sprite geometry is identical to the day clip it shadows.
            emitted.append({'name': rec['name'] + '.night',
                            'frames': rec['frames'], 'ax': ax, 'ay': ay,
                            'loop': rec['loop'],
                            'durations': rec['durations'],
                            'payload': bytes(tinted), 'bbox': (bx, by, bw, bh)})

    offset = 16 + CLIP_BYTES * len(emitted)
    out = bytearray()
    # Trailer kind 1 in the header's reserved u16 is what makes lw_pack.c look
    # for the geometry block at EOF.
    out += HEADER.pack(b'LWPX0001', dest_w, dest_h, len(emitted), 1)
    for item in emitted:
        out += CLIP.pack(item['name'].encode(), item['frames'], item['ax'],
                         item['ay'], item['loop'], 0, offset, *item['durations'])
        offset += len(item['payload'])
    for item in emitted:
        out += item['payload']
    out += TRAILER_HEADER.pack(TRAILER_MAGIC, len(emitted), 0)
    for item in emitted:
        bbox = item['bbox']
        out += TRAILER_RECORD.pack(bbox[0], bbox[1], bbox[2], bbox[3], 1)
    return bytes(out), box, dest_w, dest_h


def inspect(path):
    raw, width, height, records, kind, trailer = read_pack(path)
    print('%s: %dx%d, %d clips, trailer kind %d'
          % (path, width, height, len(records), kind))
    for i, rec in enumerate(records):
        extra = ''
        if trailer:
            extra = ' bbox %d,%d %dx%d prescaled %d' % trailer[i]
        print('  %-16s frames %2d anchor %d,%d loop %d offset %d%s'
              % (rec['name'], rec['frames'], rec['anchor_x'], rec['anchor_y'],
                 rec['loop'], rec['offset'], extra))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('source', type=Path)
    ap.add_argument('dest', type=Path, nargs='?')
    ap.add_argument('--scale', type=int, default=0,
                    help='on-screen scale the sprite is created with (256=1x)')
    ap.add_argument('--round', choices=('half', 'trunc'), default='half',
                    help='half reproduces LVGL; trunc is the naive variant kept '
                         'for comparison runs')
    ap.add_argument('--inspect', action='store_true')
    ap.add_argument('--per-clip', action='store_true',
                    help='record each clip\'s own content box in the trailer; the '
                         'pack canvas stays the union window, but the runtime '
                         'blends only the box. Needs firmware that reads a '
                         'prescaled sub-window (V4.5 onwards).')
    ap.add_argument('--night-tint', default='',
                    help='comma separated name=RRGGBB:opa entries; each listed '
                         'clip is also written as <name>.night with the tint '
                         'baked in, so the night look costs nothing per frame')
    args = ap.parse_args()

    night_tint = {}
    for item in filter(None, (s.strip() for s in args.night_tint.split(','))):
        name, _, spec = item.partition('=')
        rgb, _, opa = spec.partition(':')
        r, g, b = int(rgb[0:2], 16), int(rgb[2:4], 16), int(rgb[4:6], 16)
        color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        night_tint[name.strip()] = (color, int(opa or 255))

    if args.inspect or args.dest is None:
        inspect(args.source)
        if args.dest is None:
            return 0
    if not args.scale:
        raise SystemExit('--scale is required when writing a pack')

    raw, width, height, records, kind, trailer = read_pack(args.source)
    if trailer and any(t[4] for t in trailer):
        # Baked packs must be regenerated from the original art, never stacked:
        # a second pass would resample already-resampled pixels and compound the
        # one-pixel rounding the first pass was careful to match.
        raise SystemExit('%s: already prescaled; rebuild from the original pack'
                         % args.source)
    data, box, dest_w, dest_h = build(raw, width, height, records, args.scale,
                                      args.round, args.per_clip, night_tint)
    args.dest.write_bytes(data)
    before = width * height * 3 * sum(r['frames'] for r in records)
    after = dest_w * dest_h * 3 * sum(r['frames'] for r in records)
    print('%s -> %s' % (args.source.name, args.dest.name))
    print('  content box %dx%d at (%d,%d) of %dx%d'
          % (box[2], box[3], box[0], box[1], width, height))
    print('  baked canvas %dx%d (scale %d, %s rounding)'
          % (dest_w, dest_h, args.scale, args.round))
    print('  payload %d -> %d bytes (%.1f%% of the original)'
          % (before, after, 100.0 * after / before if before else 0.0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
