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

bool lw_ui_set_anniversary(lw_date_t date)
{
    /* lw_date_valid() is the same gate the save decoder uses, so a date that
     * gets in here is one the next boot will accept back. */
    if (!lw_date_valid(date)) {
        return false;
    }
    lw_state_t *state = lw_app_state();
    if (state->anniversary.year == date.year && state->anniversary.month == date.month &&
        state->anniversary.day == date.day) {
        return false;
    }
    state->anniversary = date;
    lw_app_state_save();
    return true;
}

lw_date_t lw_ui_anniversary(void)
{
    return lw_app_state()->anniversary;
}

/* ------------------------------------------------------------------ story */
/* Own tiny record, own key, own version: 'LWST' v1, 16 bytes.
 *   [0..3] magic, [4] version, [5] progress 0..7, [6..11] zero,
 *   [12..15] crc32 of [0..11], little-endian (memcpy, so endian-safe).
 * A corrupt or missing record simply means "story not played yet",
 * which is the safe failure direction for a gift. */
#define LW_STORY_KEY "lwstory"
#define LW_STORY_BYTES 16
static uint8_t s_story_progress;
static bool s_story_loaded;

static void story_save(void)
{
    uint8_t blob[LW_STORY_BYTES];
    memset(blob, 0, sizeof(blob));
    memcpy(blob, "LWST", 4);
    blob[4] = 1;
    blob[5] = s_story_progress;
    uint32_t crc = lw_crc32(blob, 12);
    memcpy(blob + 12, &crc, 4);
    const lw_platform_t *platform = lw_platform_get();
    if (platform != NULL && platform->storage_write != NULL) {
        platform->storage_write(LW_STORY_KEY, blob, sizeof(blob));
    }
}

unsigned lw_story_progress(void)
{
    if (!s_story_loaded) {
        s_story_loaded = true;
        uint8_t blob[LW_STORY_BYTES];
        size_t length = sizeof(blob);
        const lw_platform_t *platform = lw_platform_get();
        if (platform != NULL && platform->storage_read != NULL &&
            platform->storage_read(LW_STORY_KEY, blob, &length) && length == LW_STORY_BYTES &&
            memcmp(blob, "LWST", 4) == 0 && blob[4] == 1 && blob[5] <= LW_STORY_DONE) {
            uint32_t crc;
            memcpy(&crc, blob + 12, 4);
            if (crc == lw_crc32(blob, 12)) {
                s_story_progress = blob[5];
            }
        }
    }
    return s_story_progress;
}

void lw_story_progress_set(unsigned progress)
{
    if (progress > LW_STORY_DONE) {
        progress = LW_STORY_DONE;
    }
    if (progress <= lw_story_progress()) {
        return;
    }
    s_story_progress = (uint8_t)progress;
    story_save();
}
