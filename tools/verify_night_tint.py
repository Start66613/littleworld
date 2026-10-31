"""Check that a pack's baked <clip>.night frames are what LVGL would draw.

lwp_prescale.py --night-tint bakes the pet page's night recolour so the board
never has to call lv_obj_set_style_image_recolor (a non-zero recolor_opa leaves
LVGL's RGB565A8 fast path and cost 24 ms per frame change here). The look only
survives if the baked pixels obey the same lv_color_16_16_mix(tint, pixel, opa)
the runtime call used, so this checks exactly that, frame by frame, and also
that the alpha plane is untouched.

Usage: python tools/verify_night_tint.py build/v46-night/daily.lwp
"""
import sys
from pathlib import Path

import numpy as np

import lwp_prescale as L

# name -> (RRGGBB, opa); keep in step with the --night-tint argument used when
# the pack was built.
TINTS = {
    'couple.seated': (0x797092, 35),
    'couple.wave': (0x797092, 35),
    'couple.rest': (0x797092, 35),
    'dog.idle': (0xd8b99c, 65),
    'dog.sleep': (0xd8b99c, 65),
    'dogbed.empty': (0x937a8c, 35),
    'dogbed.sleep': (0x937a8c, 35),
    'dogbed.look': (0x937a8c, 35),
}


def rgb565(color):
    r, g, b = (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main(path):
    raw, width, height, records, _kind, _trailer = L.read_pack(Path(path))
    by_name = {r['name']: r for r in records}
    checked = failed = 0
    for base, (color, opa) in TINTS.items():
        night_name = base + '.night'
        if base not in by_name or night_name not in by_name:
            print('%-16s missing (day=%s night=%s)'
                  % (base, base in by_name, night_name in by_name))
            failed += 1
            continue
        day, night = by_name[base], by_name[night_name]
        if day['frames'] != night['frames'] or day['anchor_x'] != night['anchor_x'] \
                or day['anchor_y'] != night['anchor_y']:
            print('%-16s geometry differs from its day clip' % base)
            failed += 1
            continue
        for frame in range(day['frames']):
            drgb, da = L.frame_view(raw, width, height, day['offset'], frame)
            nrgb, na = L.frame_view(raw, width, height, night['offset'], frame)
            expect = np.frombuffer(
                L.tint_rgb565(drgb.tobytes(), rgb565(color), opa),
                dtype='<u2')
            got = np.frombuffer(nrgb.tobytes(), dtype='<u2')
            if not np.array_equal(expect, got):
                bad = int((expect != got).sum())
                print('%-16s frame %2d: %d of %d pixels differ from the tint'
                      % (base, frame, bad, got.size))
                failed += 1
            if not np.array_equal(da.reshape(-1), na.reshape(-1)):
                print('%-16s frame %2d: alpha plane changed' % (base, frame))
                failed += 1
            checked += 1
    print('%d frame(s) checked, %d mismatch(es)' % (checked, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'build/v46-night/daily.lwp'))
