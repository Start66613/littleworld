/* One effective clock and one day phase. No LVGL, no allocation.
 * SPDX-License-Identifier: MIT */
#include "lw_day.h"

#include "lw/lw_time.h"

#include <string.h>

#define SECONDS_PER_DAY 86400

static void copy_str(char *dst, unsigned cap, const char *src)
{
    if (cap == 0) {
        return;
    }
    unsigned i = 0;
    for (; src[i] != '\0' && i + 1u < cap; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int32_t clamp_zone(int32_t minutes)
{
    if (minutes < -840) {
        return -840;
    }
    if (minutes > 840) {
        return 840;
    }
    return minutes;
}

void lw_day_defaults(lw_day_bounds_t *bounds)
{
    if (bounds == NULL) {
        return;
    }
    bounds->dawn_start = 5u * 3600u;
    bounds->day_start = 8u * 3600u;
    bounds->dusk_start = 17u * 3600u;
    bounds->night_start = 20u * 3600u;
    bounds->late_start = 23u * 3600u;
}

bool lw_day_bounds_valid(const lw_day_bounds_t *bounds)
{
    if (bounds == NULL) {
        return false;
    }
    /* Strictly increasing and inside one day. Equal boundaries would make a
     * phase unreachable, which would look like a stuck theme. */
    if (bounds->dawn_start == 0 || bounds->dawn_start >= bounds->day_start) {
        return false;
    }
    if (bounds->day_start >= bounds->dusk_start) {
        return false;
    }
    if (bounds->dusk_start >= bounds->night_start) {
        return false;
    }
    if (bounds->night_start >= bounds->late_start) {
        return false;
    }
    return bounds->late_start < SECONDS_PER_DAY;
}

void lw_day_init(lw_day_t *day, const lw_day_bounds_t *bounds)
{
    if (day == NULL) {
        return;
    }
    memset(day, 0, sizeof(*day));
    day->source = LW_DAY_SOURCE_INVALID;
    day->phase = LW_DAY_PHASE_DAY;
    day->pending = LW_DAY_PHASE_DAY;
    day->tz_offset_min = 480;
    if (bounds != NULL && lw_day_bounds_valid(bounds)) {
        day->bounds = *bounds;
    } else {
        lw_day_defaults(&day->bounds);
    }
}

const lw_day_bounds_t *lw_day_bounds(const lw_day_t *day)
{
    return day != NULL ? &day->bounds : NULL;
}

static void commit_now(lw_day_t *day, uint32_t now_ms);
static bool local_seconds(const lw_day_t *day, uint32_t now_ms, int64_t *local);

bool lw_day_set_bounds(lw_day_t *day, const lw_day_bounds_t *bounds, uint32_t now_ms)
{
    if (day == NULL || !lw_day_bounds_valid(bounds)) {
        return false;
    }
    day->bounds = *bounds;
    /* An explicit settings change bypasses hysteresis: the user expects the
     * room to change now, not three seconds from now. */
    commit_now(day, now_ms);
    return true;
}

/* An explicit source change is a discrete event, not a clock tick: the user (or
 * a fresh sync) expects the room to match the new time immediately. Hysteresis
 * only exists to stop a ticking clock from oscillating on a boundary. */
static void commit_now(lw_day_t *day, uint32_t now_ms)
{
    int64_t local = 0;
    if (!local_seconds(day, now_ms, &local)) {
        return;
    }
    int64_t sod = local % SECONDS_PER_DAY;
    if (sod < 0) {
        sod += SECONDS_PER_DAY;
    }
    lw_day_phase_t phase = lw_day_phase_at(day, (uint32_t)sod);
    if (phase != day->phase) {
        day->transitions++;
    }
    day->phase = phase;
    day->pending = phase;
    day->pending_since_ms = now_ms;
}

void lw_day_clear(lw_day_t *day)
{
    if (day == NULL) {
        return;
    }
    day->source = LW_DAY_SOURCE_INVALID;
    day->manual = false;
    day->trusted = false;
    day->stale = false;
    day->has_time = false;
    day->utc_seconds = 0;
}

void lw_day_use_demo(lw_day_t *day)
{
    if (day == NULL) {
        return;
    }
    /* A manual time outranks the rehearsal clock; do not silently drop it. */
    if (day->source == LW_DAY_SOURCE_MANUAL || day->source == LW_DAY_SOURCE_REAL) {
        return;
    }
    day->source = LW_DAY_SOURCE_DEMO;
    day->manual = false;
    day->trusted = false;
    day->stale = false;
    day->has_time = true;
    /* The demo clock is a local time-of-day, expressed as a UTC base with the
     * local offset removed, so the read path stays single. */
    day->utc_seconds = (int64_t)LW_DAY_DEMO_SECONDS - (int64_t)day->tz_offset_min * 60;
    day->base_ms = 0;
}

bool lw_day_set_manual(lw_day_t *day, int year, int month, int day_of_month, int hour, int minute,
                       int32_t tz_offset_min, uint32_t now_ms)
{
    if (day == NULL) {
        return false;
    }
    lw_date_t date = {year, month, day_of_month};
    if (!lw_date_valid(date)) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    int64_t local = (int64_t)lw_days_from_civil(year, month, day_of_month) * SECONDS_PER_DAY +
                    (int64_t)hour * 3600 + (int64_t)minute * 60;
    day->tz_offset_min = clamp_zone(tz_offset_min);
    day->utc_seconds = local - (int64_t)day->tz_offset_min * 60;
    day->base_ms = now_ms;
    day->source = LW_DAY_SOURCE_MANUAL;
    day->manual = true;
    /* A time the user typed is a wall-clock assertion: it may be presented as
     * the current time, with the "manual" qualifier. */
    day->trusted = true;
    day->stale = false;
    day->has_time = true;
    commit_now(day, now_ms);
    return true;
}

bool lw_day_set_real(lw_day_t *day, int64_t utc_seconds, int32_t tz_offset_min, uint32_t now_ms)
{
    if (day == NULL || utc_seconds <= 0) {
        return false;
    }
    day->utc_seconds = utc_seconds;
    day->tz_offset_min = clamp_zone(tz_offset_min);
    day->base_ms = now_ms;
    day->source = LW_DAY_SOURCE_REAL;
    day->manual = false;
    day->trusted = true;
    day->stale = false;
    day->has_time = true;
    commit_now(day, now_ms);
    return true;
}

void lw_day_mark_stale(lw_day_t *day, uint32_t now_ms)
{
    (void)now_ms;
    if (day == NULL) {
        return;
    }
    if (day->source == LW_DAY_SOURCE_REAL) {
        day->stale = true;
        day->trusted = false;
    }
}

void lw_day_clear_manual(lw_day_t *day, uint32_t now_ms)
{
    if (day == NULL || day->source != LW_DAY_SOURCE_MANUAL) {
        return;
    }
    day->source = LW_DAY_SOURCE_INVALID;
    day->manual = false;
    day->trusted = false;
    day->has_time = false;
    lw_day_use_demo(day);
    commit_now(day, now_ms);
}

/* Local seconds for the effective source. DEMO and MANUAL are frozen (a
 * rehearsal clock that drifted into the next phase during a demo would be a
 * surprise); REAL advances with uptime, stale or not, because the last known
 * value is still the best estimate. */
static bool local_seconds(const lw_day_t *day, uint32_t now_ms, int64_t *local)
{
    if (day == NULL || local == NULL || !day->has_time) {
        return false;
    }
    int64_t utc = day->utc_seconds;
    if (day->source == LW_DAY_SOURCE_REAL) {
        utc += (int64_t)((now_ms - day->base_ms) / 1000u);
    }
    *local = utc + (int64_t)day->tz_offset_min * 60;
    return true;
}

lw_day_phase_t lw_day_phase_at(const lw_day_t *day, uint32_t second_of_day)
{
    const lw_day_bounds_t *b = day != NULL ? &day->bounds : NULL;
    if (b == NULL) {
        return LW_DAY_PHASE_DAY;
    }
    uint32_t s = second_of_day % SECONDS_PER_DAY;
    if (s >= b->late_start || s < b->dawn_start) {
        return LW_DAY_PHASE_LATE_NIGHT;
    }
    if (s < b->day_start) {
        return LW_DAY_PHASE_DAWN;
    }
    if (s < b->dusk_start) {
        return LW_DAY_PHASE_DAY;
    }
    if (s < b->night_start) {
        return LW_DAY_PHASE_DUSK;
    }
    return LW_DAY_PHASE_NIGHT;
}

void lw_day_refresh(lw_day_t *day, uint32_t now_ms)
{
    if (day == NULL) {
        return;
    }
    int64_t local = 0;
    if (!local_seconds(day, now_ms, &local)) {
        return;
    }
    /* Floor-divide so a pre-1970 local time still lands in [0, 86400). */
    int64_t sod = local % SECONDS_PER_DAY;
    if (sod < 0) {
        sod += SECONDS_PER_DAY;
    }
    lw_day_phase_t phase = lw_day_phase_at(day, (uint32_t)sod);
    if (phase == day->phase) {
        day->pending = phase;
        day->pending_since_ms = now_ms;
        return;
    }
    /* Debounce: only commit once the new phase has been stable. A clock sitting
     * on a boundary must not flip the room back and forth. */
    if (day->pending != phase) {
        day->pending = phase;
        day->pending_since_ms = now_ms;
        return;
    }
    if ((uint32_t)(now_ms - day->pending_since_ms) < LW_DAY_HYSTERESIS_MS) {
        return;
    }
    day->phase = phase;
    day->transitions++;
}

void lw_day_tick(lw_day_t *day, uint32_t now_ms)
{
    if (day == NULL) {
        return;
    }
    /* Whole-second gate: a 20 ms LVGL timer must not do date maths 50 times a
     * second. */
    uint32_t second = now_ms / 1000u;
    if (second == day->last_eval_second) {
        return;
    }
    day->last_eval_second = second;
    lw_day_refresh(day, now_ms);
}

bool lw_day_clock(const lw_day_t *day, uint32_t now_ms, int *year, int *month, int *day_of_month,
                  int *hour, int *minute, int *second)
{
    int64_t local = 0;
    if (!local_seconds(day, now_ms, &local)) {
        return false;
    }
    int64_t days = local / SECONDS_PER_DAY;
    int64_t rem = local % SECONDS_PER_DAY;
    if (rem < 0) {
        rem += SECONDS_PER_DAY;
        days -= 1;
    }
    lw_date_t date;
    lw_civil_from_days((int)days, &date);
    if (year != NULL) {
        *year = date.year;
    }
    if (month != NULL) {
        *month = date.month;
    }
    if (day_of_month != NULL) {
        *day_of_month = date.day;
    }
    if (hour != NULL) {
        *hour = (int)(rem / 3600);
    }
    if (minute != NULL) {
        *minute = (int)((rem / 60) % 60);
    }
    if (second != NULL) {
        *second = (int)(rem % 60);
    }
    return true;
}

void lw_day_hm(const lw_day_t *day, uint32_t now_ms, char out[6])
{
    int hour = 0;
    int minute = 0;
    if (out == NULL) {
        return;
    }
    if (!lw_day_clock(day, now_ms, NULL, NULL, NULL, &hour, &minute, NULL)) {
        copy_str(out, 6, "--:--");
        return;
    }
    out[0] = (char)('0' + (hour / 10) % 10);
    out[1] = (char)('0' + hour % 10);
    out[2] = ':';
    out[3] = (char)('0' + (minute / 10) % 10);
    out[4] = (char)('0' + minute % 10);
    out[5] = '\0';
}

uint32_t lw_day_until_next(const lw_day_t *day, uint32_t now_ms)
{
    int64_t local = 0;
    if (day == NULL || !local_seconds(day, now_ms, &local)) {
        return 0;
    }
    int64_t sod = local % SECONDS_PER_DAY;
    if (sod < 0) {
        sod += SECONDS_PER_DAY;
    }
    uint32_t s = (uint32_t)sod;
    const lw_day_bounds_t *b = &day->bounds;
    const uint32_t edges[5] = {b->dawn_start, b->day_start, b->dusk_start, b->night_start,
                               b->late_start};
    for (unsigned i = 0; i < 5; i++) {
        if (edges[i] > s) {
            return edges[i] - s;
        }
    }
    return (uint32_t)SECONDS_PER_DAY - s + b->dawn_start;
}

int lw_day_theme(lw_day_phase_t phase)
{
    switch (phase) {
    case LW_DAY_PHASE_DAWN:
    case LW_DAY_PHASE_DAY:
        return LW_DAY_THEME_MORNING;
    case LW_DAY_PHASE_DUSK:
        return LW_DAY_THEME_SUNSET;
    case LW_DAY_PHASE_NIGHT:
    case LW_DAY_PHASE_LATE_NIGHT:
        return LW_DAY_THEME_NIGHT;
    default:
        return LW_DAY_THEME_MORNING;
    }
}

const char *lw_day_source_name(lw_day_source_t source)
{
    switch (source) {
    case LW_DAY_SOURCE_INVALID:
        return "invalid";
    case LW_DAY_SOURCE_DEMO:
        return "demo";
    case LW_DAY_SOURCE_MANUAL:
        return "manual";
    case LW_DAY_SOURCE_REAL:
        return "real";
    }
    return "?";
}

const char *lw_day_phase_name(lw_day_phase_t phase)
{
    switch (phase) {
    case LW_DAY_PHASE_DAWN:
        return "dawn";
    case LW_DAY_PHASE_DAY:
        return "day";
    case LW_DAY_PHASE_DUSK:
        return "dusk";
    case LW_DAY_PHASE_NIGHT:
        return "night";
    case LW_DAY_PHASE_LATE_NIGHT:
        return "late-night";
    default:
        return "?";
    }
}

bool lw_day_needs_qualifier(const lw_day_t *day)
{
    if (day == NULL) {
        return true;
    }
    /* Only REAL time may be shown bare. Everything else has to say so, or the
     * demo clock becomes a lie the moment a user trusts it. A STALE clock is
     * still the REAL source but nobody vouched for it this boot, so it needs
     * the qualifier too. */
    return day->source != LW_DAY_SOURCE_REAL || day->stale || !day->trusted;
}
