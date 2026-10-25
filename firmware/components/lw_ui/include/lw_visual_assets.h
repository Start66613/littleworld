/* Bounded optional RGB565 scene files; caller owns mounting and path policy. */
#pragma once
#include "lvgl.h"
typedef struct { size_t live_bytes,peak_bytes; unsigned refs,reads,hits,failures; } lw_visual_stats_t;
bool lw_visual_assets_configure(const char *directory);
bool lw_visual_assets_available(void);
lv_obj_t *lw_visual_scene_image(lv_obj_t *parent,unsigned theme,int w,int h,bool thumbnail);
lw_visual_stats_t lw_visual_assets_stats(void);
/* Delete all scene widgets first. No hardware or filesystem mutation. */
bool lw_visual_assets_shutdown(void);
