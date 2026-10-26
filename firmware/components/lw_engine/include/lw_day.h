/* Little World - one effective clock, one day phase, one theme.
 *
 * The bug this prevents: three parts of the shell each deciding "what time is
 * it" for themselves. The status bar showed the DEMO clock, the anniversary
 * counter used the RTC, and the theme used a compile-time constant, so at 19:30
 * the screen could say "19:30" while the room was painted for noon.
 *
 * Rules encoded here:
 *  - A clock is either REAL (a network or RTC timestamp), MANUAL (the user typed
 *    it), DEMO (a fixed rehearsal clock) or INVALID (nothing at all). Only REAL
 *    may be labelled as the actual time; DEMO is always labelled as a rehearsal.
 *  - The user's manual choice outranks the DEMO clock. A successful sync
 *    replaces it, because real time is better than typed time.
 *  - Phase changes are debounced: a clock sitting on 17:59:59 must not flip the
 *    theme back and forth. The phase only commits after it has been stable for
 *    the hysteresis window.
 *  - Nothing here runs per frame. The caller feeds now_ms; work happens only
 *    when the whole second changes.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Priority, lowest to highest. The numeric order is the tie-break rule. */
typedef enum {
    LW_DAY_SOURCE_INVALID = 0, /* no clock at all: show "--:--" */
    LW_DAY_SOURCE_DEMO = 1,    /* fixed rehearsal clock, never presented as real */
    LW_DAY_SOURCE_MANUAL = 2,  /* the user set it; survives reboots */
    LW_DAY_SOURCE_REAL = 3     /* synced this boot (or restored from RTC) */
} lw_day_source_t;

typedef enum {
    LW_DAY_PHASE_DAWN = 0,       /* 05:00 - 07:59 */
    LW_DAY_PHASE_DAY = 1,        /* 08:00 - 16:59 */
    LW_DAY_PHASE_DUSK = 2,       /* 17:00 - 19:59 */
    LW_DAY_PHASE_NIGHT = 3,      /* 20:00 - 22:59 */
    LW_DAY_PHASE_LATE_NIGHT = 4, /* 23:00 - 04:59 */
    LW_DAY_PHASE_COUNT
} lw_day_phase_t;

/* Theme index into the three shipped scenes. Five phases map onto three
 * paintings, which is a content decision, not a rendering one. */
#define LW_DAY_THEME_MORNING 0
#define LW_DAY_THEME_NIGHT 1
#define LW_DAY_THEME_SUNSET 2

#define LW_DAY_DEMO_SECONDS (19 * 3600 + 30 * 60) /* 19:30, the rehearsal clock */
#define LW_DAY_HYSTERESIS_MS 3000u

typedef struct {
    /* configured boundaries, seconds since local midnight */
    uint32_t dawn_start;
    uint32_t day_start;
    uint32_t dusk_start;
    uint32_t night_start;
    uint32_t late_start;
} lw_day_bounds_t;

typedef struct {
    lw_day_bounds_t bounds;
    lw_day_source_t source;
    bool manual;   /* the user asserted this time */
    bool trusted;  /* may be presented as the current time */
    bool stale;    /* real, but no longer re-synced */
    int32_t tz_offset_min;
    int64_t utc_seconds; /* base timestamp */
    uint32_t base_ms;    /* uptime when utc_seconds was captured */
    bool has_time;       /* false only for INVALID */
    /* debounced phase */
    lw_day_phase_t phase;
    lw_day_phase_t pending;
    uint32_t pending_since_ms;
    uint32_t last_eval_second; /* the whole-second gate */
    uint32_t transitions;
} lw_day_t;

/* Default boundaries: dawn 05:00, day 08:00, dusk 17:00, night 20:00, late 23:00. */
void lw_day_defaults(lw_day_bounds_t *bounds);
bool lw_day_bounds_valid(const lw_day_bounds_t *bounds);

void lw_day_init(lw_day_t *day, const lw_day_bounds_t *bounds);
/* Replace the boundaries. Re-evaluates immediately, bypassing hysteresis: a
 * settings change is an explicit user action, not a clock tick. */
bool lw_day_set_bounds(lw_day_t *day, const lw_day_bounds_t *bounds, uint32_t now_ms);
const lw_day_bounds_t *lw_day_bounds(const lw_day_t *day);

/* --- clock sources ------------------------------------------------------ */

/* No clock at all. */
void lw_day_clear(lw_day_t *day);
/* The rehearsal clock: present, never trusted, never persisted as real. */
void lw_day_use_demo(lw_day_t *day);
/* The user typed a time. Persisted by the caller; this module only remembers it
 * for the running session. */
bool lw_day_set_manual(lw_day_t *day, int year, int month, int day_of_month, int hour, int minute,
                       int32_t tz_offset_min, uint32_t now_ms);
/* A real sync. Replaces any manual override. */
bool lw_day_set_real(lw_day_t *day, int64_t utc_seconds, int32_t tz_offset_min, uint32_t now_ms);
/* The link that provided the time went away: the value stays, the trust does
 * not. A DEMO clock is unaffected because it was never trusted. */
void lw_day_mark_stale(lw_day_t *day, uint32_t now_ms);
/* Explicitly drop a manual override, falling back to DEMO. */
void lw_day_clear_manual(lw_day_t *day, uint32_t now_ms);

/* --- evaluation --------------------------------------------------------- */

/* Advance the clock and the phase. Cheap: returns immediately unless the whole
 * second changed. */
void lw_day_tick(lw_day_t *day, uint32_t now_ms);
/* Re-evaluate now, ignoring the whole-second gate (still honours hysteresis). */
void lw_day_refresh(lw_day_t *day, uint32_t now_ms);

/* Local clock fields for the effective source. False for INVALID. */
bool lw_day_clock(const lw_day_t *day, uint32_t now_ms, int *year, int *month, int *day_of_month,
                  int *hour, int *minute, int *second);
/* "19:30" or "--:--". */
void lw_day_hm(const lw_day_t *day, uint32_t now_ms, char out[6]);
/* Phase for an arbitrary local second-of-day. */
lw_day_phase_t lw_day_phase_at(const lw_day_t *day, uint32_t second_of_day);
/* Seconds until the next phase boundary, for scheduling the next theme change
 * without polling. 0 when there is no clock. */
uint32_t lw_day_until_next(const lw_day_t *day, uint32_t now_ms);
/* Theme index for a phase. */
int lw_day_theme(lw_day_phase_t phase);
/* Human labels. */
const char *lw_day_source_name(lw_day_source_t source);
const char *lw_day_phase_name(lw_day_phase_t phase);
/* True when the phase label must carry a "demo" or "manual" qualifier. */
bool lw_day_needs_qualifier(const lw_day_t *day);

#ifdef __cplusplus
}
#endif
