#pragma once
#include "lvgl.h"
#include <time.h>
typedef enum {LW_DAWN,LW_DAY,LW_DUSK,LW_NIGHT,LW_DEEP_NIGHT} lw_day_phase_t;
/* UI-thread only. Preview is session-only; -1 restores the platform clock. */
bool lw_ambience_preview(int minute);
unsigned lw_ambience_minute(void);
bool lw_ambience_clock_valid(void);
bool lw_ambience_localtime(struct tm *out);
lw_day_phase_t lw_ambience_phase(void);
unsigned lw_ambience_wallpaper(void);
/* Lightweight lighting layer, not a decoder or hardware brightness API.
 * room: 0 desktop, 1 daily, 2 RPG. Create after scenery, before UI/actors. */
lv_obj_t *lw_ambience_layer(lv_obj_t *parent,unsigned room,int y);
void lw_ambience_refresh(lv_obj_t *layer);
