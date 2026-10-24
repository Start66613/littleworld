/* Little World - hand drawn vector icons (LVGL primitives only, no image assets).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Draw the icon for @p app_id inside a square @p badge (centred, @p fg colour). */
void lw_icon_draw(const char *app_id, lv_obj_t *badge, uint32_t fg);

/** Small pet face badge (used on the home screen entry). */
void lw_icon_pet(lv_obj_t *badge, uint32_t fg, uint32_t bg);

#ifdef __cplusplus
}
#endif
