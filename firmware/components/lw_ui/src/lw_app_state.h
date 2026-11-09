/* Little World - runtime state: persisted record, clock and helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lw/lw_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load the save record (or defaults) and prepare the clock. */
void lw_app_state_init(void);

lw_state_t *lw_app_state(void);

/** Persist the current state through the platform storage hook. */
void lw_app_state_save(void);

/** Wipe the save and start over (settings, pet, progress). */
void lw_app_state_reset(void);

/** Seconds since the local epoch, using the demo clock when time is unknown. */
uint32_t lw_now(void);

/** True when the displayed date comes from the demo clock rather than a real clock. */
bool lw_time_is_demo(void);

/** Today's calendar date (local). */
lw_date_t lw_today(void);

/** Force the demo clock (used by the screenshot/regression mode). */
void lw_set_demo_clock(bool on);

/* ------------------------------------------------------------------ story */
/* 《拾取回忆》 first-boot story progress: 0 = not started, 1..6 = that many
 * fragments collected, LW_STORY_DONE = finished. Stored under its own
 * platform-storage key ("lwstory") with its own versioned record - the
 * .lwr save format is deliberately not involved. */
#define LW_STORY_FRAGMENTS 6u
#define LW_STORY_DONE 7u
unsigned lw_story_progress(void);
/** Record progress; monotonic - a lower value never overwrites a higher one. */
void lw_story_progress_set(unsigned progress);

#ifdef __cplusplus
}
#endif
