/* SPDX-License-Identifier: MIT */
#pragma once
#include "lw_scene.h"
#include "lw/lw_pet.h"
#include "lw/lw_time.h"

typedef enum { LW_WORLD_HOME, LW_WORLD_PET } lw_world_kind_t;
typedef struct lw_world lw_world_t;
/* Scene blueprint, deliberately not installed as the default app before art
 * acceptance. Reuses domain values supplied by the existing app controller. */
lw_world_t *lw_world_create(lv_obj_t *content, lw_world_kind_t kind, lw_scene_action_fn action, void *context);
lw_scene_t *lw_world_scene(lw_world_t *world);
bool lw_world_has_art(const lw_world_t *world);
void lw_world_refresh(lw_world_t *world, const lw_pet_t *pet, uint32_t now, lw_date_t anniversary, lw_date_t today);
bool lw_world_motion(lw_world_t *world, const lw_clip_t *clip);
void lw_world_destroy(lw_world_t *world);
