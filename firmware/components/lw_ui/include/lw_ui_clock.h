/* Little World - what the UI is allowed to know about the clock.
 *
 * The UI must never decide what time it is; it renders what the effective clock
 * says and labels it honestly. This header is the read-only window onto that
 * clock, so a page can show "19:30 · demo", pick a theme for the current phase,
 * or tell the user that the time is an estimate because the link dropped.
 *
 * Who fills it: the boot layer attaches the one lw_day instance (lw_time_boot.c)
 * once, before the first frame. Until then every read reports "no clock" rather
 * than a plausible-looking 00:00, because a page that draws a fake midnight is
 * exactly the bug this system exists to prevent.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lw_day.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Wall clock as the user should see it. */
    unsigned hour;
    unsigned minute;
    /* Where it came from and whether it may be presented as the real time. */
    lw_day_source_t source;
    bool trusted;
    bool stale;          /* was real, lost its link */
    bool manual;         /* the user set it by hand */
    /* Where in the day we are, and the theme that follows from it. */
    lw_day_phase_t phase;
    int theme;
    /* Seconds until the next phase boundary; 0 when the clock is not running. */
    uint32_t until_next_s;
    /* True when the value is a rehearsal and the UI must say so. */
    bool needs_qualifier;
} lw_ui_clock_t;

/* Attach the process-wide clock. Called once by the boot layer. Passing NULL
 * detaches, which makes every read fail. */
void lw_ui_clock_attach(const lw_day_t *day);

/* Fill @p out from the attached clock. Returns false when nothing is attached
 * or the clock has no value yet; @p out is zeroed in that case. */
bool lw_ui_clock_read(lw_ui_clock_t *out);

#ifdef __cplusplus
}
#endif
