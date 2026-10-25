/* Original OS design primitives; no platform dependence. SPDX-License-Identifier: MIT */
#pragma once
#include "lvgl.h"
typedef struct { uint32_t bg,lower,surface,soft,text,muted,line,accent,deep,wood,sky; const char *name; } lw_os_palette_t;
typedef enum { OS_I_HOME,OS_I_ALBUM,OS_I_HEART,OS_I_MUSIC,OS_I_GAME,OS_I_SETTINGS,OS_I_WIFI,OS_I_SUN,OS_I_VOLUME,OS_I_MOON,OS_I_BACK,OS_I_CLOSE,OS_I_LOCK,OS_I_MAIL,OS_I_PLAY,OS_I_CHECK,OS_I_LEAF } lw_os_icon_t;
const lw_os_palette_t *lw_os_palette(unsigned);
lv_obj_t *lw_os_box(lv_obj_t *,int,int,int,int,uint32_t,int,lv_opa_t);
lv_obj_t *lw_os_label(lv_obj_t *,const char *,const lv_font_t *,int,int,int,uint32_t);
lv_obj_t *lw_os_icon(lv_obj_t *,lw_os_icon_t,int,int,int,uint32_t);
lv_obj_t *lw_os_wallpaper(lv_obj_t *,unsigned,int,int,int,int);
void lw_os_animate(lv_obj_t *,int,uint32_t);
