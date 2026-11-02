/* Bounded optional RGB565 scene files; caller owns mounting and path policy. */
#pragma once
#include "lvgl.h"
typedef struct { size_t live_bytes,peak_bytes; unsigned refs,reads,hits,failures; } lw_visual_stats_t;
bool lw_visual_assets_configure(const char *directory);
/* Optional V4.2 directory; same LWSR0001 format, one bounded auxiliary slot.
 * Configure before creating any scene widgets; caller owns VFS mounting. */
bool lw_visual_aux_configure(const char *directory);
lv_obj_t *lw_visual_rpg_image(lv_obj_t *parent);
lv_obj_t *lw_visual_daily_image(lv_obj_t *parent);
bool lw_visual_assets_available(void);
lv_obj_t *lw_visual_scene_image(lv_obj_t *parent,unsigned theme,int w,int h,bool thumbnail);
const char *lw_visual_daily_name(void);
/* Boot warm-up: read a card into the pool now so the page that wants it later
 * finds it there. Cheap to call and safe to fail - a miss just means the page
 * pays the card read it would have paid anyway. */
bool lw_visual_prefetch_scene(unsigned theme);
bool lw_visual_prefetch_room(const char *name);
lw_visual_stats_t lw_visual_assets_stats(void);
/* Delete all scene widgets first. No hardware or filesystem mutation. */
bool lw_visual_assets_shutdown(void);
