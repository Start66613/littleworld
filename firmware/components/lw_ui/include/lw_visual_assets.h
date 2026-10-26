/* Bounded optional RGB565 scene files; caller owns mounting and path policy. */
#pragma once
#include "lvgl.h"
typedef struct { size_t live_bytes,peak_bytes; unsigned refs,reads,hits,failures; } lw_visual_stats_t;
bool lw_visual_assets_configure(const char *directory);
/* Optional V4.2 directory; same LWSR0001 format, one bounded auxiliary slot.
 * Configure before creating any scene widgets; caller owns VFS mounting. */
bool lw_visual_aux_configure(const char *directory);
lv_obj_t *lw_visual_rpg_image(lv_obj_t *parent);
bool lw_visual_assets_available(void);
lv_obj_t *lw_visual_scene_image(lv_obj_t *parent,unsigned theme,int w,int h,bool thumbnail);
lw_visual_stats_t lw_visual_assets_stats(void);
/* Delete all scene widgets first. No hardware or filesystem mutation. */
bool lw_visual_assets_shutdown(void);
