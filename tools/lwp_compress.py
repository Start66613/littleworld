"""Rewrite a baked LWPX pack with deflate-compressed frames.

Why: the RPG page walks at 50 ms per frame and one frame of the prescaled art is
97-170 KiB. The card delivers ~1.9 MB/s, so two actors walking ask for more than
the card has and every step is a demand read of ~30 ms. A zlib round trip on the
real frames measures 8.1x (male walk_right, 98,229 -> 12,072 B), which puts the
whole walk set in the frame cache and turns the card into a non-issue.

Container rules (all backwards compatible - nothing moves in the file):
  * header stays "LWPX0001" + u16 width/height/clips + u16 kind, kind stays 1
  * clip records keep their 108-byte layout, offsets point at the first byte of
    the clip's first block
  * frames become fixed STRIDE-sized deflate blocks, zero padded. A fixed stride
    is what lets the loader keep its existing offset validation, its file cache
    keying and its prefetch arithmetic: only the block size and the decoder
    change.
  * the geometry trailer stays at EOF; its reserved u16 (offset +10, which the
    loader already reads and no pack has ever used) carries STRIDE. Zero means
    "frames are raw", which is what every pre-V4.6 pack says.

Usage:
  python tools/lwp_compress.py build/v45-assets/male.lwp build/v45-assets/male.lwp
  python tools/lwp_compress.py --inspect build/v45-assets/male.lwp
"""

import argparse
import struct
import sys
import zlib

HEADER = struct.Struct('<8sHHHH')
CLIP = struct.Struct('<32sHhhBBI32H')
CLIP_BYTES = 108
TRAILER_MAGIC = b'LWTR0001'
TRAILER_HEADER = struct.Struct('<8sHH')
TRAILER_RECORD = struct.Struct('<HHHHH')
MAGIC = b'LWPX0001'
ALIGN = 1024


def read_pack(path):
    raw = open(path, 'rb').read()
    magic, w, h, clips, kind = HEADER.unpack_from(raw, 0)
    if magic != MAGIC:
        raise SystemExit('%s: not an LWPX pack' % path)
    if kind != 1:
        raise SystemExit('%s: expected a geometry trailer (kind 1), got %u' % (path, kind))
    records = [CLIP.unpack_from(raw, 16 + i * CLIP_BYTES) for i in range(clips)]

    trailer_bytes = 12 + 10 * clips
    trailer_at = len(raw) - trailer_bytes
    tmagic, tclips, _reserved = TRAILER_HEADER.unpack_from(raw, trailer_at)
    if tmagic != TRAILER_MAGIC or tclips != clips:
        raise SystemExit('%s: bad geometry trailer' % path)
    boxes = [TRAILER_RECORD.unpack_from(raw, trailer_at + 12 + i * 10) for i in range(clips)]
    frames_at = min(r[6] for r in records)
    return raw, w, h, clips, records, boxes, frames_at, trailer_at


def frame_bytes(raw, w, h, records, frames_at):
    per = w * h * 3
    total = sum(r[1] for r in records)
    expected = total * per
    if frames_at + expected != len(raw) - (12 + 10 * len(records)):
        raise SystemExit('pack is not stride-consistent: %d frames at %d of %d bytes '
                         'does not reach the trailer' % (total, frames_at, per))
    return per, total


def compress_frame(buf):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(buf) + c.flush()


def build(src, dst):
    raw, w, h, clips, records, boxes, frames_at, trailer_at = read_pack(src)
    per, total = frame_bytes(raw, w, h, records, frames_at)

    frames = []
    first = frames_at
    for i, rec in enumerate(records):
        if rec[6] != first:
            raise SystemExit('clip %d does not follow its predecessor; refusing to re-layout'
                             % i)
        blobs = []
        for k in range(rec[1]):
            frames.append(raw[first + k * per:first + (k + 1) * per])
            blobs.append(None)
        first += rec[1] * per

    packed = [compress_frame(f) for f in frames]
    worst = max(len(p) for p in packed)
    stride = ((worst + ALIGN - 1) // ALIGN) * ALIGN

    # Round trip before anything is written: a pack that decodes to different
    # pixels is worse than a slow one.
    for frame, blob in zip(frames, packed):
        back = zlib.decompress(blob, -15)
        if back != frame:
            raise SystemExit('round trip mismatch, refusing to write')

    out = bytearray()
    out += HEADER.pack(MAGIC, w, h, clips, 1)
    out += bytes(CLIP_BYTES * clips)
    blocks_at = len(out)
    strides = []
    total_frames = 0
    for rec in records:
        strides.append(total_frames)
        total_frames += rec[1]
    for blob in packed:
        out += blob
        out += b'\0' * (stride - len(blob))

    first = 0
    for i, rec in enumerate(records):
        body = bytearray(CLIP.pack(rec[0], rec[1], rec[2], rec[3], rec[4], rec[5],
                                   blocks_at + strides[i] * stride, *rec[7:]))
        out[16 + i * CLIP_BYTES:16 + (i + 1) * CLIP_BYTES] = body
    out += TRAILER_HEADER.pack(TRAILER_MAGIC, clips, stride)
    for box in boxes:
        out += TRAILER_RECORD.pack(*box)

    if dst == src:
        backup = src + '.raw'
        open(backup, 'wb').write(raw)
    open(dst, 'wb').write(out)

    raw_total = len(frames) * per
    print('%-28s frames=%3d raw=%9d B  compressed=%9d B blocks   stride=%6d B  '
          'stored=%9d B  ratio=%.2fx'
          % (dst.split('\\')[-1], len(frames), raw_total, sum(len(p) for p in packed),
             stride, len(out), raw_total / float(len(out) - 16 - CLIP_BYTES * clips
                                                 - 12 - 10 * clips)))
    return stride


def inspect(path):
    raw, w, h, clips, records, boxes, frames_at, trailer_at = read_pack(path)
    _, _, reserved = TRAILER_HEADER.unpack_from(raw, trailer_at)
    print('%s canvas=%dx%d clips=%d stride=%d' % (path, w, h, clips, reserved))
    for i, rec in enumerate(records):
        print('  %-16s frames=%2d offset=%8d box=%dx%d' %
              (rec[0].rstrip(b'\0').decode(), rec[1], rec[6], boxes[i][2], boxes[i][3]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source')
    ap.add_argument('dest', nargs='?')
    ap.add_argument('--inspect', action='store_true')
    args = ap.parse_args()
    if args.inspect:
        inspect(args.source)
        return 0
    build(args.source, args.dest or args.source)
    return 0


if __name__ == '__main__':
    sys.exit(main())
