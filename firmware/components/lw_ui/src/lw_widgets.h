/* Little World - shared widgets (cards, labels, buttons, toggles, bars).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *lw_card(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *lw_soft_card(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *lw_text(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y);
lv_obj_t *lw_text_wrap(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y,
                       int width);
lv_obj_t *lw_button(lv_obj_t *parent, const char *text, int w, int h, uint32_t bg, uint32_t fg,
                    const lv_font_t *font);
lv_obj_t *lw_pill(lv_obj_t *parent, const char *text, uint32_t bg, uint32_t fg);
lv_obj_t *lw_icon_button(lv_obj_t *parent, const char *symbol, int size, uint32_t bg, uint32_t fg);

/** iOS style switch. Clicking it flips the visual state; the caller stores the value. */
lv_obj_t *lw_toggle(lv_obj_t *parent, int on, int x, int y);
void lw_toggle_set(lv_obj_t *toggle, int on);
int lw_toggle_get(lv_obj_t *toggle);

/** Progress bar with a label drawn by the caller. */
lv_obj_t *lw_bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);
void lw_bar_set(lv_obj_t *bar, int percent);

/** Rounded badge used as an icon background. */
lv_obj_t *lw_icon_badge(lv_obj_t *parent, int x, int y, int size, uint32_t bg, int radius);

#ifdef __cplusplus
}
#endif
