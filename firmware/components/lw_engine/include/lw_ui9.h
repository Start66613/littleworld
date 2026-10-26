/* Little World - nine-slice geometry (pure C, host tested).
 *
 * LVGL 9.5 has no nine-slice widget: lv_image_set_scale() scales the WHOLE
 * bitmap, which is exactly what makes a pixel-art frame look wrong (corners
 * stretch, borders thicken, the centre blurs). The correct treatment is to cut
 * the source into nine pieces and place each one at its own size:
 *
 *      +----+------------------+----+
 *      | 0  |        1         | 2  |   corners never scale
 *      +----+------------------+----+
 *      | 3  |        4         | 5  |   edges stretch on one axis only
 *      +----+------------------+----+
 *      | 6  |        7         | 8  |   centre stretches on both axes
 *      +----+------------------+----+
 *
 * This module only computes rectangles. It never touches LVGL, so the geometry
 * is verifiable on the build machine, and the caller can render the pieces with
 * whatever primitive it has (lv_image with a per-piece source, or a draw layer).
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LW_UI9_PIECES 9
#define LW_UI9_MAX_INSET 512

/* Pixel insets from each source edge. Right/bottom are derived from the source
 * size, so a caller supplies left/top/right/bottom border widths only. */
typedef struct {
    uint16_t left, top, right, bottom;
} lw_ui9_insets_t;

typedef struct {
    int16_t x, y;
    uint16_t w, h;
} lw_ui9_rect_t;

/* Why a nine-slice request was refused. Every one of these is a real authoring
 * mistake: a border wider than the image, a target smaller than the corners. */
typedef enum {
    LW_UI9_OK = 0,
    LW_UI9_BAD_SOURCE,   /* zero width or height */
    LW_UI9_BAD_INSETS,   /* left+right >= width, top+bottom >= height */
    LW_UI9_BAD_TARGET,   /* target smaller than the two corners together */
    LW_UI9_OVERFLOW      /* computed rectangle left the int16 range */
} lw_ui9_status_t;

const char *lw_ui9_status_text(lw_ui9_status_t status);

/* True when the insets leave at least one pixel for each of the three columns
 * and rows, i.e. the source can actually be sliced. */
bool lw_ui9_insets_valid(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets);

/* Fill src[9] with the source rectangles and dst[9] with the target rectangles.
 * Both arrays are indexed row-major: 0 top-left, 1 top-centre, 2 top-right,
 * 3 middle-left, 4 centre, 5 middle-right, 6 bottom-left, 7 bottom-centre,
 * 8 bottom-right.
 *
 * The target is placed at (x, y) with the requested width and height. Corners
 * keep their source size exactly; edges stretch along one axis; the centre
 * stretches along both. No interpolation is implied - a pixel-art frame wants
 * nearest-neighbour, which is the caller's choice, not this module's. */
lw_ui9_status_t lw_ui9_slice(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets,
                             int16_t x, int16_t y, uint16_t target_w, uint16_t target_h,
                             lw_ui9_rect_t src[LW_UI9_PIECES], lw_ui9_rect_t dst[LW_UI9_PIECES]);

/* True when the requested target can be produced. Used by asset validation to
 * reject a manifest whose minimum size is smaller than its own border. */
bool lw_ui9_fits(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets,
                 uint16_t target_w, uint16_t target_h);

/* Smallest target that keeps every corner intact. */
void lw_ui9_minimum(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets,
                    uint16_t *min_w, uint16_t *min_h);

/* --- button states -------------------------------------------------------
 * A pixel-art button is authored as one strip (normal / pressed / disabled)
 * rather than three files, so the state only selects a vertical band. */
typedef enum {
    LW_UI9_STATE_NORMAL = 0,
    LW_UI9_STATE_PRESSED = 1,
    LW_UI9_STATE_DISABLED = 2,
    LW_UI9_STATE_COUNT = 3
} lw_ui9_button_state_t;

/* Height of one band. The strip must divide evenly; a remainder is refused
 * instead of silently shifting the pressed art up by a pixel. */
bool lw_ui9_band_height(uint16_t strip_h, uint16_t *band_h);
/* Source rectangle of one state inside the strip, same width as the strip. */
bool lw_ui9_state_rect(uint16_t strip_w, uint16_t strip_h, lw_ui9_button_state_t state,
                       lw_ui9_rect_t *out);

#ifdef __cplusplus
}
#endif
