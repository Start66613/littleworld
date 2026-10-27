#pragma once
/* Day phase for the ambience layer.
 *
 * There used to be two of these: this header defined its own
 * `lw_day_phase_t` with LW_DAWN..LW_DEEP_NIGHT, and lw_day.h (the engine's
 * single clock) defined another with LW_DAY_PHASE_DAWN..LW_DAY_PHASE_LATE_NIGHT.
 * Both were five-state and in the same order, so every comparison "worked" in
 * whichever file saw only one of them - and any file that saw both failed to
 * compile. Rather than keep two truths, the Codex names are now aliases of the
 * engine enum: same values, same order, one definition. Code that says
 * `phase >= LW_NIGHT` keeps its original meaning.
 *
 * The phase itself comes from the effective clock (lw_ui_clock -> lw_day), so
 * the room, the theme and the status-bar time can never disagree. A preview
 * (user picked a time band in settings) overrides it for the session only. */
#include "lvgl.h"
#include "lw_day.h"
#include <time.h>

#define LW_DAWN       LW_DAY_PHASE_DAWN
#define LW_DAY        LW_DAY_PHASE_DAY
#define LW_DUSK       LW_DAY_PHASE_DUSK
#define LW_NIGHT      LW_DAY_PHASE_NIGHT
#define LW_DEEP_NIGHT LW_DAY_PHASE_LATE_NIGHT

/* UI-thread only. Preview is session-only; -1 restores the effective clock. */
bool lw_ambience_preview(int minute);
/* True while a preview is in force (the status bar still shows real time). */
bool lw_ambience_previewing(void);
unsigned lw_ambience_minute(void);
bool lw_ambience_clock_valid(void);
bool lw_ambience_localtime(struct tm *out);
lw_day_phase_t lw_ambience_phase(void);
unsigned lw_ambience_wallpaper(void);
/* Lightweight lighting layer, not a decoder or hardware brightness API.
 * room: 0 desktop, 1 daily, 2 RPG. Create after scenery, before UI/actors. */
lv_obj_t *lw_ambience_layer(lv_obj_t *parent,unsigned room,int y);
void lw_ambience_refresh(lv_obj_t *layer);
