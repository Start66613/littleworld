/* Nine-slice geometry. No LVGL, no allocation, no floating point. SPDX-License-Identifier: MIT */
#include "lw_ui9.h"

#include <stddef.h>

const char *lw_ui9_status_text(lw_ui9_status_t status)
{
    switch (status) {
    case LW_UI9_OK:
        return "ok";
    case LW_UI9_BAD_SOURCE:
        return "source has no area";
    case LW_UI9_BAD_INSETS:
        return "border insets leave no centre";
    case LW_UI9_BAD_TARGET:
        return "target smaller than the corners";
    case LW_UI9_OVERFLOW:
        return "rectangle out of range";
    }
    return "?";
}

bool lw_ui9_insets_valid(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets)
{
    if (src_w == 0 || src_h == 0 || insets == NULL) {
        return false;
    }
    /* The centre must survive: a 16 px border on a 16 px image has nothing to
     * stretch and is an authoring error, not a one-pixel-wide centre. */
    if ((unsigned)insets->left + insets->right >= src_w) {
        return false;
    }
    if ((unsigned)insets->top + insets->bottom >= src_h) {
        return false;
    }
    return true;
}

void lw_ui9_minimum(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets,
                    uint16_t *min_w, uint16_t *min_h)
{
    if (min_w != NULL) {
        *min_w = insets != NULL ? (uint16_t)((unsigned)insets->left + insets->right + 1u) : src_w;
    }
    if (min_h != NULL) {
        *min_h = insets != NULL ? (uint16_t)((unsigned)insets->top + insets->bottom + 1u) : src_h;
    }
}

bool lw_ui9_fits(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets,
                 uint16_t target_w, uint16_t target_h)
{
    if (!lw_ui9_insets_valid(src_w, src_h, insets)) {
        return false;
    }
    uint16_t min_w = 0;
    uint16_t min_h = 0;
    lw_ui9_minimum(src_w, src_h, insets, &min_w, &min_h);
    return target_w >= min_w && target_h >= min_h;
}

lw_ui9_status_t lw_ui9_slice(uint16_t src_w, uint16_t src_h, const lw_ui9_insets_t *insets,
                             int16_t x, int16_t y, uint16_t target_w, uint16_t target_h,
                             lw_ui9_rect_t src[LW_UI9_PIECES], lw_ui9_rect_t dst[LW_UI9_PIECES])
{
    if (src_w == 0 || src_h == 0 || insets == NULL) {
        return LW_UI9_BAD_SOURCE;
    }
    if (!lw_ui9_insets_valid(src_w, src_h, insets)) {
        return LW_UI9_BAD_INSETS;
    }
    if (target_w == 0 || target_h == 0) {
        return LW_UI9_BAD_TARGET;
    }
    uint16_t min_w = 0;
    uint16_t min_h = 0;
    lw_ui9_minimum(src_w, src_h, insets, &min_w, &min_h);
    if (target_w < min_w || target_h < min_h) {
        return LW_UI9_BAD_TARGET;
    }

    /* Column widths in the source. */
    uint16_t s_col[3];
    s_col[0] = insets->left;
    s_col[1] = (uint16_t)(src_w - insets->left - insets->right);
    s_col[2] = insets->right;
    uint16_t s_row[3];
    s_row[0] = insets->top;
    s_row[1] = (uint16_t)(src_h - insets->top - insets->bottom);
    s_row[2] = insets->bottom;

    /* Column widths in the target: corners keep their size, the centre takes
     * whatever is left. Same for rows. */
    uint16_t d_col[3];
    d_col[0] = insets->left;
    d_col[1] = (uint16_t)(target_w - insets->left - insets->right);
    d_col[2] = insets->right;
    uint16_t d_row[3];
    d_row[0] = insets->top;
    d_row[1] = (uint16_t)(target_h - insets->top - insets->bottom);
    d_row[2] = insets->bottom;

    int32_t sx[3];
    int32_t sy[3];
    int32_t dx[3];
    int32_t dy[3];
    sx[0] = 0;
    sx[1] = insets->left;
    sx[2] = src_w - insets->right;
    sy[0] = 0;
    sy[1] = insets->top;
    sy[2] = src_h - insets->bottom;
    dx[0] = x;
    dx[1] = (int32_t)x + insets->left;
    dx[2] = (int32_t)x + target_w - insets->right;
    dy[0] = y;
    dy[1] = (int32_t)y + insets->top;
    dy[2] = (int32_t)y + target_h - insets->bottom;

    for (unsigned row = 0; row < 3; row++) {
        for (unsigned col = 0; col < 3; col++) {
            unsigned i = row * 3u + col;
            int32_t rx = sx[col];
            int32_t ry = sy[row];
            int32_t tx = dx[col];
            int32_t ty = dy[row];
            /* A degenerate corner (inset 0) is legal: it just produces a zero
             * width or height piece, which the caller skips. */
            if (rx < -32768 || rx > 32767 || ry < -32768 || ry > 32767 ||
                tx < -32768 || tx > 32767 || ty < -32768 || ty > 32767) {
                return LW_UI9_OVERFLOW;
            }
            src[i].x = (int16_t)rx;
            src[i].y = (int16_t)ry;
            src[i].w = s_col[col];
            src[i].h = s_row[row];
            dst[i].x = (int16_t)tx;
            dst[i].y = (int16_t)ty;
            dst[i].w = d_col[col];
            dst[i].h = d_row[row];
        }
    }
    return LW_UI9_OK;
}

bool lw_ui9_band_height(uint16_t strip_h, uint16_t *band_h)
{
    if (strip_h == 0 || band_h == NULL) {
        return false;
    }
    if (strip_h % LW_UI9_STATE_COUNT != 0) {
        return false;
    }
    *band_h = (uint16_t)(strip_h / LW_UI9_STATE_COUNT);
    return *band_h > 0;
}

bool lw_ui9_state_rect(uint16_t strip_w, uint16_t strip_h, lw_ui9_button_state_t state,
                       lw_ui9_rect_t *out)
{
    uint16_t band = 0;
    if (out == NULL || strip_w == 0 || !lw_ui9_band_height(strip_h, &band)) {
        return false;
    }
    if ((unsigned)state >= LW_UI9_STATE_COUNT) {
        return false;
    }
    out->x = 0;
    out->y = (int16_t)(band * (uint16_t)state);
    out->w = strip_w;
    out->h = band;
    return true;
}
