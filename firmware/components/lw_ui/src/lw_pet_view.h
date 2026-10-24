/* Little World - animated pet drawn with LVGL primitives.
 *
 * Placeholder character art on purpose: a soft round body, ears, eyes, blush
 * and a mouth, animated with breathing / blinking / mood poses. The public
 * interface is what a future sprite-sheet renderer will implement.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "lw/lw_pet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lw_pet_view lw_pet_view_t;

/** Create the pet inside @p parent at (x, y); scale in percent (100 = 220x220). */
lw_pet_view_t *lw_pet_view_create(lv_obj_t *parent, int x, int y, int scale);
void lw_pet_view_free(lw_pet_view_t *view);

/** Apply mood, sleeping state and timers. */
void lw_pet_view_refresh(lw_pet_view_t *view, const lw_pet_t *pet, uint32_t now);

/** Immediate feedback for an interaction (bounce, hearts, wiggle). */
void lw_pet_view_react(lw_pet_view_t *view, lw_pet_action_t action);

#ifdef __cplusplus
}
#endif
