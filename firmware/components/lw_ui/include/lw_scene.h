/* SPDX-License-Identifier: MIT */
#pragma once
#include "lw_sprite.h"

typedef enum { LW_LAYER_BACKGROUND, LW_LAYER_MIDDLE, LW_LAYER_CHARACTER, LW_LAYER_FOREGROUND,
               LW_LAYER_HOTSPOTS, LW_LAYER_OVERLAY, LW_LAYER_COUNT } lw_scene_layer_t;
typedef enum { LW_HIT_PET, LW_HIT_FEED, LW_HIT_PLAY, LW_HIT_REST, LW_HIT_ANNIVERSARY, LW_HIT_LETTER } lw_scene_action_t;
typedef struct { int x,y,w,h; lw_scene_action_t action; } lw_hotspot_t;
typedef void (*lw_scene_action_fn)(lw_scene_action_t action, void *context);
typedef struct lw_scene lw_scene_t;

/* parent is a viewport with top-left at screen (0, screen_y). No business state. */
lw_scene_t *lw_scene_create(lv_obj_t *parent, int screen_y, int height, lw_scene_action_fn action, void *context);
lv_obj_t *lw_scene_root(lw_scene_t *scene);
lv_obj_t *lw_scene_layer(lw_scene_t *scene, lw_scene_layer_t layer);
bool lw_scene_image(lw_scene_t *scene, lw_scene_layer_t layer, const char *asset_id, int screen_x, int screen_y);
bool lw_scene_hotspot(lw_scene_t *scene, const lw_hotspot_t *hotspot);
lw_sprite_t *lw_scene_sprite(lw_scene_t *scene, int screen_anchor_x, int screen_anchor_y, uint16_t scale_256);
void lw_scene_destroy(lw_scene_t *scene);
