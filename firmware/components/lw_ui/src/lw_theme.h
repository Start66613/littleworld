/* Little World - design tokens (palette, fonts, spacing) and shared styles.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(lw_font_18);
LV_FONT_DECLARE(lw_font_22);
LV_FONT_DECLARE(lw_font_28);
LV_FONT_DECLARE(lw_font_44);

#define LW_VERSION "0.2.0-demo"

#define LW_SCREEN_W 1024
#define LW_SCREEN_H 600
#define LW_TOPBAR_H 64
#define LW_DOCK_H   96
#define LW_CONTENT_H (LW_SCREEN_H - LW_TOPBAR_H - LW_DOCK_H)

typedef struct {
    uint32_t bg_top;
    uint32_t bg_bottom;
    uint32_t card;
    uint32_t card_soft;
    uint32_t text;
    uint32_t dim;
    uint32_t line;
    uint32_t accent;      /* main brand accent   */
    uint32_t accent_deep; /* pressed / emphasis  */
    uint32_t lav;
    uint32_t lav_deep;
    uint32_t mint;
    uint32_t gold;
    uint32_t shadow;
    uint32_t on_accent;
} lw_palette_t;

const lw_palette_t *lw_palette(void);
void lw_theme_init(void);
/** 0 = cream (default), 1 = rose, 2 = lavender. Re-applies to the active screen. */
void lw_theme_set_variant(int variant);
int lw_theme_variant(void);

/** Gradient background used by the shell and the full screen apps. */
void lw_theme_paint_background(lv_obj_t *obj);

void lw_style_card(lv_obj_t *obj);
void lw_style_soft_card(lv_obj_t *obj);
void lw_style_label(lv_obj_t *obj, const lv_font_t *font, uint32_t color);

/** Fade + rise transition used when a page is created. */
void lw_theme_enter_anim(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
