#pragma once
#include "lw_sprite.h"
/* Optional V4.2 exports in the existing asset registry, never filesystem mounts. */
const lw_clip_t *lw_polish_clip(const char *name);
lv_obj_t *lw_polish_prop(lv_obj_t *parent,unsigned frame,int foot_x,int foot_y);
void lw_polish_paper(lv_obj_t *object,bool ruled);
void lw_polish_game_badge(lv_obj_t *parent,unsigned game,int x,int y);
