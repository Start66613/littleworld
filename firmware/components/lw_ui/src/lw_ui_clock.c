/* SPDX-License-Identifier: MIT */
#include "lw_ui_clock.h"

#include <string.h>

#include "lvgl.h"

/* The clock is owned by the boot layer and lives for the whole session, so a
 * bare pointer is enough. It is written once at boot, before the UI task can
 * read it, and never reassigned afterwards - which is why no lock is needed. */
static const lw_day_t *s_day;

void lw_ui_clock_attach(const lw_day_t *day)
{
    s_day = day;
}

bool lw_ui_clock_read(lw_ui_clock_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (s_day == NULL) {
        return false;
    }
    /* LVGL's tick is the same millisecond clock the sprite timers and the OS
     * loop use, so a phase change and the clock label can never disagree about
     * which millisecond they are in. */
    uint32_t now_ms = lv_tick_get();
    char hm[6] = "--:--";
    lw_day_hm(s_day, now_ms, hm);
    if (hm[0] == '-' || hm[2] != ':') {
        return false;
    }
    out->hour = (unsigned)((hm[0] - '0') * 10 + (hm[1] - '0'));
    out->minute = (unsigned)((hm[3] - '0') * 10 + (hm[4] - '0'));
    out->source = s_day->source;
    out->trusted = s_day->trusted;
    out->stale = s_day->stale;
    out->manual = s_day->source == LW_DAY_SOURCE_MANUAL;
    out->phase = s_day->phase;
    out->theme = lw_day_theme(s_day->phase);
    out->until_next_s = lw_day_until_next(s_day, now_ms);
    out->needs_qualifier = lw_day_needs_qualifier(s_day);
    return true;
}
