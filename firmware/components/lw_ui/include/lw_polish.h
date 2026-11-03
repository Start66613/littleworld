#pragma once
#include "lw_sprite.h"
/* Optional V4.2 exports in the existing asset registry, never filesystem mounts. */
const lw_clip_t *lw_polish_clip(const char *name);
/* AI-generated components, native half-size, alpha image with deletion-bound lease. */
lv_obj_t *lw_polish_art(lv_obj_t *parent,unsigned frame,int x,int y);
/* Read one sheet frame now so a later lw_polish_art() is a cache hit instead of
 * ~300 ms of card time inside the page build. Used by the boot warm-up only. */
bool lw_polish_prefetch(unsigned frame);
void lw_polish_paper(lv_obj_t *object,bool ruled);
void lw_polish_game_badge(lv_obj_t *parent,unsigned game,int x,int y);
