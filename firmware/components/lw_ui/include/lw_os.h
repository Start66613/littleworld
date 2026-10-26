/* Independent OS prototype entry; stable shell stays available. SPDX-License-Identifier: MIT */
#pragma once
#include "lw_system.h"
#include "lw_duo_view.h"
#include "lw_rpg_view.h"
typedef enum { OS_DESKTOP,OS_WORLD,OS_SETTINGS,OS_THEMES,OS_ALBUM,OS_MEMORIES,OS_MUSIC,OS_GAMES,OS_PET,OS_RPG } lw_os_page_t;
typedef enum { OS_OVER_NONE,OS_OVER_CONTROL,OS_OVER_WIFI,OS_OVER_PASSWORD,OS_OVER_LOCK,OS_OVER_LETTER,OS_OVER_EDIT } lw_os_overlay_t;
typedef enum {
    OC_HOME,OC_WORLD,OC_ALBUM,OC_MEMORIES,OC_MUSIC,OC_GAMES,OC_SETTINGS,OC_CONTROL,OC_CLOSE,OC_BACK,
    OC_WIFI,OC_WIFI_TOGGLE,OC_SCAN,OC_AP0,OC_AP1,OC_AP2,OC_PASSWORD,OC_CONNECT,OC_DISCONNECT,OC_FORGET,
    OC_BRIGHTNESS,OC_VOLUME,OC_MUTE,OC_DND,OC_THEMES,OC_THEME0,OC_THEME1,OC_THEME2,OC_APPLY,OC_CANCEL,
    OC_LOCK,OC_UNLOCK,OC_EDITOR,OC_ENVELOPE,OC_WIDGETS,OC_WALLPAPER,OC_PLAY,OC_SCREEN_OFF,
    OC_NETWORK,OC_DISPLAY,OC_SOUND,OC_PERSONAL,OC_TIME,OC_STORAGE,OC_ABOUT,OC_DEVELOPER,
    OC_GREET,OC_LETTER,OC_REST,OC_BRIGHT_VALUE,OC_VOLUME_VALUE,OC_WIFI_STATUS,OC_KEYBOARD,OC_PET,OC_REFRESH,OC_LOCK_CLOCK,OC_LOCK_DATE,OC_LOCK_LOVE,OC_RPG,
    OC_MOTION,OC_MOTION_FULL,OC_MOTION_REDUCED,OC_MOTION_OFF,OC_SETTINGS_BACK,OC_ANNIVERSARY,OC_WINDOW,OC_SOFA,OC_ROOM_PHOTO,OC_DOCK_TOGGLE,OC_TIME_AUTO,OC_DAWN,OC_DAY,OC_DUSK,OC_NIGHT,OC_DEEP_NIGHT,OC_COUNT
} lw_os_control_t;
bool lw_os_start(lw_system_t *,const lw_actor_art_t [2]);
void lw_os_stop(void);
void lw_os_open(lw_os_page_t);
void lw_os_show(lw_os_overlay_t);
lv_obj_t *lw_os_control(lw_os_control_t);
lv_obj_t *lw_os_root(void);
lw_os_page_t lw_os_page(void);
lw_os_overlay_t lw_os_overlay(void);
void lw_os_notify(const char *);
/* Configure before start; backend must outlive the OS. NULL = session-only. */
void lw_os_rpg_storage(const lw_rpg_view_host_t *);
const lw_rpg_state_t *lw_os_rpg_state(void);
bool lw_os_rpg_has_art(void);
lv_obj_t *lw_os_rpg_actor(unsigned);
/* UI-thread capability snapshot; no driver initialization. Call before start
 * or after a backend capability change. NULL derives existing backend callbacks. */
typedef enum { LW_SERVICE_UNAVAILABLE,LW_SERVICE_SIMULATED,LW_SERVICE_READY } lw_service_state_t;
typedef struct { lw_service_state_t brightness,volume,wifi,audio,storage; } lw_os_capabilities_t;
void lw_os_capabilities(const lw_os_capabilities_t *);
bool lw_os_volume_available(void);
void lw_os_boot(void);
bool lw_os_boot_active(void);
lv_obj_t *lw_os_control_panel(void);
const lw_duo_t *lw_os_duo_state(void);
const char *lw_os_actor_clip(unsigned);
uint16_t lw_os_actor_frame(unsigned);
bool lw_os_dock_expanded(void);
/* UI-only attention hint; no saved preference or backend operation. */
void lw_os_scene_focus(bool focused);
