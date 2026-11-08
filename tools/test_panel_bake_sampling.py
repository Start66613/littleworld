"""Unit tests for the nine-slice sampling grid in lwp_panel_bake.py.

numpy only (plus the two tools under test). Run:

    python3 tools/test_panel_bake_sampling.py

What is being pinned down:

1. crop_axis() windows - the mechanism lwp_prescale.py was verified with
   (237 frames, 0 differences) - only ever contain source indices inside
   the piece rectangle, and their length is within one slot of the naive
   scaled length. This is the property the old per-piece lattice anchor
   in lwp_panel_bake.py did not have.
2. On a synthetic atlas whose nine pieces are nine different solid
   colours, a baked panel must contain *only* each piece's own colour:
   any sampling window that slips one source pixel across a piece edge
   shows up as a wrong-colour pixel. (Alpha is 255 everywhere, rgb is
   compared as packed RGB565.)
3. A 1:1 piece (scale 256, the corners' fixed axis) must reproduce its
   source pixels exactly, in order.
4. The three real panel geometries bake to their exact output sizes
   with no transparent holes (the source rect is fully opaque in the
   synthetic atlas).

These tests do NOT prove the bake matches LVGL on the device - there is
no reference screenshot in this environment. They prove the sampling
grid has the properties the prescale verification relied on. See
reports/perf-v4-5/CLOUD_PATCHES.md (P-08) for the old-vs-new differing
pixel counts on the real atlas.
"""

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lwp_panel_bake import nine_slice, piece_window, PANELS, PANEL_RECT  # noqa: E402
from lwp_prescale import crop_axis  # noqa: E402


def pack565(r, g, b):
    v = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F)
    return np.array([v & 0xFF, (v >> 8) & 0xFF], dtype=np.uint8)


def solid_atlas(border=16):
    """512x544 atlas; the PANEL_RECT tile is split into 9 solid pieces.

    Piece colours are chosen far apart in RGB565 so a one-pixel slip
    across an edge cannot alias into the neighbour's colour. The split
    follows the bake border, because nine_slice() derives the source
    pieces from the same border it is given for the destination.
    """
    atlas_w, atlas_h = 512, 544
    rgb = np.zeros((atlas_h, atlas_w, 2), dtype=np.uint8)
    alpha = np.full((atlas_h, atlas_w), 255, dtype=np.uint8)
    xs = [PANEL_RECT[0], PANEL_RECT[0] + border, PANEL_RECT[0] + PANEL_RECT[2] - border]
    ys = [PANEL_RECT[1], PANEL_RECT[1] + border, PANEL_RECT[1] + PANEL_RECT[3] - border]
    xe = [xs[1], xs[2], PANEL_RECT[0] + PANEL_RECT[2]]
    ye = [ys[1], ys[2], PANEL_RECT[1] + PANEL_RECT[3]]
    colours = {}
    n = 0
    for j in range(3):
        for i in range(3):
            c = pack565(4 + n * 3, 8 + n * 6, 28 - n * 3)
            colours[(i, j)] = c
            rgb[ys[j]:ye[j], xs[i]:xe[i]] = c
            n += 1
    return atlas_w, atlas_h, rgb, alpha, colours, border


class CropWindowTest(unittest.TestCase):
    def test_window_stays_inside_piece(self):
        # (atlas_len, src_lo, src_len, scale) covering corners, edges and
        # the heavily stretched middle piece of all three real panels.
        cases = [
            (512, 152, 16, 256), (512, 168, 96, 1218), (512, 264, 16, 256),
            (544, 178, 16, 256), (544, 194, 96, 1210), (544, 290, 16, 256),
            (512, 152, 108, 256), (544, 178, 96, 453),
        ]
        for atlas_len, lo, length, scale in cases:
            for mode in ('half', 'floor'):
                window, _first = crop_axis(atlas_len, lo, length, scale, mode)
                self.assertTrue(window, (atlas_len, lo, length, scale, mode))
                self.assertGreaterEqual(min(window), lo)
                self.assertLessEqual(max(window), lo + length - 1)
                naive = max(1, (length * scale + 0x80) >> 8)
                self.assertLessEqual(abs(len(window) - naive), 1,
                                     (atlas_len, lo, length, scale, mode, len(window), naive))

    def test_piece_window_matches_crop_axis(self):
        window, _ = crop_axis(512, 168, 96, 1218, 'half')
        got = piece_window(512, 168, 96, 468, 1218, 'half', 'crop')
        self.assertEqual(got[:len(window)], window)
        self.assertEqual(len(got), 468)


class NineSliceTest(unittest.TestCase):
    def test_no_colour_bleeds_across_pieces(self):
        for name, w, h, b in PANELS:
            atlas_w, atlas_h, rgb, alpha, colours, _b = solid_atlas(border=b)
            out_rgb, out_a = nine_slice(w, h, b, atlas_w, atlas_h, rgb, alpha, 'half', 'crop')
            sw = [b, w - 2 * b, b]
            sh = [b, h - 2 * b, b]
            dy = 0
            for j in range(3):
                dx = 0
                for i in range(3):
                    block = out_rgb[dy:dy + sh[j], dx:dx + sw[i]]
                    want = colours[(i, j)]
                    self.assertTrue(
                        np.all(block == want),
                        '%s piece (%d,%d): got %s want %s' % (name, i, j, block[0, 0], want))
                    dx += sw[i]
                dy += sh[j]
            self.assertTrue(np.all(out_a == 255), name)

    def test_output_sizes(self):
        atlas_w, atlas_h, rgb, alpha, _c, _b = solid_atlas()
        for _name, w, h, b in PANELS:
            out_rgb, out_a = nine_slice(w, h, b, atlas_w, atlas_h, rgb, alpha, 'half', 'crop')
            self.assertEqual(out_rgb.shape, (h, w, 2))
            self.assertEqual(out_a.shape, (h, w))

    def test_identity_axis_is_exact(self):
        # Encode the source column in the pixel value along a 1:1 axis:
        # with scale 256 the baked piece must reproduce the source order.
        atlas_w, atlas_h = 512, 544
        rgb = np.zeros((atlas_h, atlas_w, 2), dtype=np.uint8)
        for x in range(atlas_w):
            rgb[:, x] = pack565(x & 0x1F, (x >> 5) & 0x3F, 7)
        alpha = np.full((atlas_h, atlas_w), 255, dtype=np.uint8)
        out_rgb, _ = nine_slice(108, 196, 10, atlas_w, atlas_h, rgb, alpha, 'half', 'crop')
        # Left column piece is 10 wide at scale 256 on x: exact source slice.
        left = out_rgb[:, :10]
        want = rgb[PANEL_RECT[1]:PANEL_RECT[1] + 10, PANEL_RECT[0]:PANEL_RECT[0] + 10]
        self.assertTrue(np.all(left[:10] == want))


if __name__ == '__main__':
    unittest.main(verbosity=2)
