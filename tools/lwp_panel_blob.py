"""Write the baked panels into one binary blob for the firmware.

Layout: 'LWSP0001', u32 count, then per entry u32 w, h, phase, offset, bytes.
phase is 0 = day, 1 = night. The blob is embedded with EMBED_FILES on the board
and loaded from disk by the host build, so both render the same bytes.
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lwp_prescale import read_pack, frame_view, tint_rgb565  # noqa: E402
from lwp_panel_bake import PANELS, NIGHT, SKIN_ATLAS, nine_slice  # noqa: E402

MAGIC = b'LWSP0001'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--skin', default=r'build\v45-runtime\skin.lwp')
    ap.add_argument('--out', default=r'firmware\components\lw_ui\panels\skinpanels.bin')
    ap.add_argument('--mode', default='half', choices=['half', 'floor', 'piece'])
    args = ap.parse_args()

    raw, width, height, records, _k, _t = read_pack(Path(args.skin))
    rec = next(r for r in records if r['name'] == SKIN_ATLAS)
    rgb, alpha = frame_view(raw, width, height, rec['offset'], 0)

    entries = []
    for name, w, h, b in PANELS:
        prgb, pa = nine_slice(w, h, b, width, height, rgb, alpha, args.mode)
        day = prgb.tobytes() + pa.tobytes()
        night_rgb = np.frombuffer(tint_rgb565(prgb.tobytes(), NIGHT[0], NIGHT[1]),
                                  dtype=np.uint8).reshape(prgb.shape)
        night = night_rgb.tobytes() + pa.tobytes()
        entries.append((w, h, 0, day))
        entries.append((w, h, 1, night))

    header = MAGIC + struct.pack('<I', len(entries))
    table = b''
    body = b''
    offset = len(header) + 20 * len(entries)
    for w, h, phase, data in entries:
        table += struct.pack('<IIIII', w, h, phase, offset, len(data))
        body += data
        offset += len(data)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + table + body)
    print('%s: %d panels, %d bytes' % (out, len(entries), len(header) + len(table) + len(body)))


if __name__ == '__main__':
    main()
