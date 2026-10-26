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
void lw_os_wallpaper_change(lv_obj_t *,unsigned);
size_t lw_os_scene_live_bytes(void);
void lw_os_animate(lv_obj_t *,int,uint32_t);
typedef enum { LW_MOTION_FULL,LW_MOTION_REDUCED,LW_MOTION_OFF } lw_motion_mode_t;
typedef enum { LW_MOVE_PAGE,LW_MOVE_CONTROL,LW_MOVE_DIALOG,LW_MOVE_TOAST,LW_MOVE_REWARD,LW_MOVE_THEME,LW_MOVE_RETURN,LW_MOVE_BOOT,LW_MOVE_ACTOR,LW_MOVE_PAPER,LW_MOVE_PHOTO,LW_MOVE_CARD,LW_MOVE_LOCK,LW_MOVE_FLAP,LW_MOVE_DISMISS } lw_motion_kind_t;
void lw_os_motion_mode(lw_motion_mode_t);
lw_motion_mode_t lw_os_motion_get(void);
void lw_os_motion(lv_obj_t *,lw_motion_kind_t);
void lw_os_motion_exit(lv_obj_t *);
void lw_os_motion_touch(lv_obj_t *);
void lw_os_motion_settle(lv_obj_t *);
void lw_os_motion_step(lv_obj_t *,int,int,unsigned);
void lw_os_motion_cancel(lv_obj_t *);
void lw_os_motion_slide(lv_obj_t *,int,int,unsigned,unsigned,bool);
void lw_os_motion_stagger(lv_obj_t *,unsigned);
void lw_os_motion_vector(lv_obj_t *,int,int,unsigned,unsigned);
void lw_os_motion_dim(lv_obj_t *,int,int,unsigned);
/* Small foreground layer drawn after actors. Native coordinates, no image cache. */
lv_obj_t *lw_os_foreground(lv_obj_t *,unsigned);
