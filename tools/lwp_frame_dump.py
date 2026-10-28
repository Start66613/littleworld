"""Extract one LWPX frame to a raw RGB565A8 file for host/render_bench.c.

The layout is exactly what lw_pack.c serves to lv_image: a width*height*2 byte
RGB565 plane followed by a width*height byte A8 plane, no header. That is what
render_bench.c --raw expects, so a real shipped frame can be timed and compared
instead of a synthetic pattern.

    python tools/lwp_frame_dump.py <pack.lwp> <clip> [--frame N] [-o out.raw]
"""
import argparse
import struct
import sys
from pathlib import Path

HEADER = struct.Struct('<8sHHHH')
CLIP = struct.Struct('<32sHhhBBI32H')
CLIP_BYTES = 108


def clips(path):
    raw = path.read_bytes()
    magic, width, height, count, _kind = HEADER.unpack_from(raw, 0)
    if magic != b'LWPX0001':
        raise SystemExit('%s: not an LWPX pack' % path)
    out = []
    for i in range(count):
        name, frames, ax, ay, loop, _pad, offset, *durations = CLIP.unpack_from(
            raw, 16 + CLIP_BYTES * i)
        out.append({'name': name.split(b'\0')[0].decode(), 'frames': frames,
                    'anchor_x': ax, 'anchor_y': ay, 'offset': offset,
                    'durations': durations})
    return raw, width, height, out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('pack', type=Path)
    ap.add_argument('clip')
    ap.add_argument('--frame', type=int, default=0)
    ap.add_argument('-o', '--out', type=Path)
    args = ap.parse_args()

    raw, width, height, table = clips(args.pack)
    rec = next((c for c in table if c['name'] == args.clip), None)
    if rec is None:
        raise SystemExit('no clip %r in %s (have: %s)'
                         % (args.clip, args.pack,
                            ', '.join(c['name'] for c in table)))
    if not 0 <= args.frame < rec['frames']:
        raise SystemExit('frame %d out of range (clip has %d)'
                         % (args.frame, rec['frames']))
    stride = width * height * 3
    base = rec['offset'] + args.frame * stride
    blob = raw[base:base + stride]
    if len(blob) != stride:
        raise SystemExit('%s: frame runs past EOF (truncated pack?)' % args.pack)

    out = args.out or args.pack.with_name('%s-%s-%02d.raw'
                                          % (args.pack.stem, args.clip, args.frame))
    out.write_bytes(blob)
    print('%s %s frame %d -> %s (%dx%d, %d bytes, anchor %d,%d)'
          % (args.pack.name, rec['name'], args.frame, out, width, height,
             len(blob), rec['anchor_x'], rec['anchor_y']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
