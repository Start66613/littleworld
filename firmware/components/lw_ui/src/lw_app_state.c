/* SPDX-License-Identifier: MIT */
#include "lw_app_state.h"

#include <string.h>

#include "lw/lw_platform.h"
#include "lw/lw_time.h"

#define LW_SAVE_KEY "littleworld"
/* 2026-10-25 19:30 local, the demo clock base. */
#define LW_DEMO_BASE (lw_days_from_civil(2026, 10, 25) * 86400 + 19 * 3600 + 30 * 60)

static lw_state_t s_state;
static bool s_loaded;
static bool s_demo_forced;
static bool s_demo_clock;

void lw_app_state_init(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    /* When the board has no clock the UI runs on a demo clock anchored at
     * LW_DEMO_BASE; the save record must use the same anchor, otherwise the
     * first pet tick sees a multi-year gap and drains every stat to zero. */
    uint32_t now = (uint32_t)LW_DEMO_BASE;
    bool have_time = false;
    const lw_platform_t *platform = lw_platform_get();
    struct tm tm;
    if (platform != NULL && platform->get_time != NULL && platform->get_time(&tm)) {
        now = (uint32_t)(lw_days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400 + tm.tm_hour * 3600 +
                         tm.tm_min * 60 + tm.tm_sec);
        have_time = true;
    }

    lw_state_defaults(&s_state, now);
    if (!have_time) {
        s_state.settings.demo_clock = 1;
    }

    uint8_t blob[LW_SAVE_BYTES];
    size_t length = sizeof(blob);
    if (platform != NULL && platform->storage_read != NULL &&
        platform->storage_read(LW_SAVE_KEY, blob, &length) && lw_save_decode(&s_state, blob, length) == 0) {
        /* loaded */
    } else {
        lw_app_state_save();
    }

    s_demo_clock = s_state.settings.demo_clock ? true : false;
}

lw_state_t *lw_app_state(void)
{
    return &s_state;
}

void lw_app_state_save(void)
{
    uint8_t blob[LW_SAVE_BYTES];
    if (lw_save_encode(&s_state, blob, sizeof(blob)) != LW_SAVE_BYTES) {
        return;
    }
    const lw_platform_t *platform = lw_platform_get();
    if (platform != NULL && platform->storage_write != NULL) {
        platform->storage_write(LW_SAVE_KEY, blob, sizeof(blob));
    }
}

void lw_app_state_reset(void)
{
    uint32_t now = lw_now();
    lw_state_defaults(&s_state, now);
    s_demo_clock = false;
    lw_app_state_save();
}

void lw_set_demo_clock(bool on)
{
    s_demo_forced = on;
}

bool lw_time_is_demo(void)
{
    if (s_demo_forced) {
        return true;
    }
    if (s_demo_clock) {
        return true;
    }
    const lw_platform_t *platform = lw_platform_get();
    struct tm tm;
    return platform == NULL || platform->get_time == NULL || !platform->get_time(&tm);
}

uint32_t lw_now(void)
{
    if (!lw_time_is_demo()) {
        const lw_platform_t *platform = lw_platform_get();
        struct tm tm;
        if (platform->get_time(&tm)) {
            return (uint32_t)(lw_days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400 +
                              tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
        }
    }

    uint32_t uptime = 0;
    const lw_platform_t *platform = lw_platform_get();
    if (platform != NULL && platform->uptime_seconds != NULL) {
        uptime = platform->uptime_seconds();
    }
    return (uint32_t)LW_DEMO_BASE + uptime;
}

lw_date_t lw_today(void)
{
    lw_date_t date;
    lw_civil_from_days((int)(lw_now() / 86400), &date);
    return date;
}
