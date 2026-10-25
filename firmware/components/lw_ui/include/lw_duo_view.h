/* SPDX-License-Identifier: MIT */
#pragma once
#include "lw_duo.h"
#include "lw_scene.h"
typedef struct { const char *name; lw_clip_t clip; } lw_named_clip_t;
typedef struct { const char *label; const lw_named_clip_t *clips; unsigned count; bool placeholder; } lw_actor_art_t;
typedef struct lw_duo_view lw_duo_view_t;

/* Model and art outlive the view. P1 room is an explicitly technical scene;
 * deleting its parent releases timer, both players and every frame lease. */
lw_duo_view_t *lw_duo_view_create(lv_obj_t *content,lw_duo_t *model,const lw_actor_art_t art[2]);
void lw_duo_view_update(lw_duo_view_t *view,uint32_t dt_ms);
void lw_duo_view_set_animations(lw_duo_view_t *view,bool enabled);
void lw_duo_view_select(lw_duo_view_t *view,unsigned actor);
bool lw_duo_view_request(lw_duo_view_t *view,lw_interaction_t interaction);
lv_obj_t *lw_duo_view_root(lw_duo_view_t *view);
void lw_duo_view_destroy(lw_duo_view_t *view);
