/* Little World - system time service. Pure C, no IDF and no LVGL. SPDX-License-Identifier: MIT */
#include "lw_time_svc.h"

#include <stdio.h>
#include <string.h>

#define SECONDS_PER_DAY 86400

static const char *const kWeekdayShort[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

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

void lw_time_service_init(lw_time_service_t *svc, int32_t tz_offset_min)
{
    memset(svc, 0, sizeof(*svc));
    svc->state = LW_TIME_DEMO;
    svc->error = LW_TIME_ERR_NONE;
    svc->tz_offset_min = clamp_zone(tz_offset_min);
}

void lw_time_service_set_zone(lw_time_service_t *svc, int32_t tz_offset_min)
{
    svc->tz_offset_min = clamp_zone(tz_offset_min);
}

int32_t lw_time_service_zone(const lw_time_service_t *svc)
{
    return svc->tz_offset_min;
}

lw_time_state_t lw_time_state(const lw_time_service_t *svc)
{
    return svc->state;
}

lw_time_error_t lw_time_error(const lw_time_service_t *svc)
{
    return svc->error;
}

const char *lw_time_state_name(lw_time_state_t state)
{
    switch (state) {
    case LW_TIME_DEMO:
        return "demo";
    case LW_TIME_SYNCING:
        return "syncing";
    case LW_TIME_SYNCED:
        return "synced";
    case LW_TIME_STALE:
        return "stale";
    case LW_TIME_FAILED:
        return "failed";
    }
    return "?";
}

const char *lw_time_error_name(lw_time_error_t error)
{
    switch (error) {
    case LW_TIME_ERR_NONE:
        return "none";
    case LW_TIME_ERR_NOT_SUPPORTED:
        return "not-supported";
    case LW_TIME_ERR_TIMEOUT:
        return "timeout";
    case LW_TIME_ERR_NETWORK:
        return "network";
    case LW_TIME_ERR_INVALID:
        return "invalid-time";
    }
    return "?";
}

void lw_time_service_restore(lw_time_service_t *svc, int64_t utc_seconds, uint32_t now_ms)
{
    if (utc_seconds <= 0) {
        return;
    }
    svc->utc_seconds = utc_seconds;
    svc->synced_ms = now_ms;
    svc->rtc_restored = true;
    if (svc->state == LW_TIME_DEMO) {
        svc->state = LW_TIME_STALE;
        svc->error = LW_TIME_ERR_NONE;
    }
}

bool lw_time_service_request_sync(lw_time_service_t *svc, uint32_t now_ms)
{
    if (svc->state == LW_TIME_SYNCING) {
        return false;
    }
    svc->state = LW_TIME_SYNCING;
    svc->error = LW_TIME_ERR_NONE;
    svc->attempt_ms = now_ms;
    svc->attempts++;
    return true;
}

void lw_time_service_on_sync_ok(lw_time_service_t *svc, int64_t utc_seconds, uint32_t now_ms)
{
    if (utc_seconds <= 0) {
        lw_time_service_on_sync_failed(svc, LW_TIME_ERR_INVALID, now_ms);
        return;
    }
    svc->state = LW_TIME_SYNCED;
    svc->error = LW_TIME_ERR_NONE;
    svc->utc_seconds = utc_seconds;
    svc->synced_ms = now_ms;
    svc->attempts = 0;
}

void lw_time_service_on_sync_failed(lw_time_service_t *svc, lw_time_error_t error, uint32_t now_ms)
{
    (void)now_ms;
    svc->error = error == LW_TIME_ERR_NONE ? LW_TIME_ERR_NETWORK : error;
    /* A device that already had real time keeps showing it (as STALE); a device
     * that never had it stays on the DEMO clock instead of jumping to 1970. */
    /* Keep the failure visible: the UI decides whether to warn or stay quiet. */
    svc->state = svc->utc_seconds > 0 ? LW_TIME_STALE : LW_TIME_DEMO;
}

void lw_time_service_on_network_lost(lw_time_service_t *svc, uint32_t now_ms)
{
    (void)now_ms;
    if (svc->state == LW_TIME_SYNCED || svc->state == LW_TIME_STALE) {
        if (svc->utc_seconds > 0) {
            svc->state = LW_TIME_STALE;
        }
    }
    if (svc->state == LW_TIME_SYNCING) {
        svc->state = svc->utc_seconds > 0 ? LW_TIME_STALE : LW_TIME_DEMO;
        svc->error = LW_TIME_ERR_NETWORK;
    }
}

void lw_time_service_tick(lw_time_service_t *svc, uint32_t now_ms)
{
    if (svc->state == LW_TIME_SYNCING && now_ms - svc->attempt_ms >= LW_TIME_SYNC_TIMEOUT_MS) {
        lw_time_service_on_sync_failed(svc, LW_TIME_ERR_TIMEOUT, now_ms);
        return;
    }
    if (svc->state == LW_TIME_SYNCED && now_ms - svc->synced_ms >= LW_TIME_RESYNC_MS) {
        svc->state = LW_TIME_STALE;
    }
}

bool lw_time_service_utc(const lw_time_service_t *svc, uint32_t now_ms, int64_t *out)
{
    if (svc->utc_seconds <= 0) {
        return false;
    }
    if (svc->state != LW_TIME_SYNCED && svc->state != LW_TIME_STALE) {
        return false;
    }
    int64_t value = svc->utc_seconds;
    if (svc->state == LW_TIME_SYNCED) {
        value += (int64_t)((now_ms - svc->synced_ms) / 1000u);
    }
    if (out != NULL) {
        *out = value;
    }
    return true;
}

bool lw_time_service_clock(const lw_time_service_t *svc, uint32_t now_ms, lw_clock_t *out)
{
    int64_t utc = 0;
    if (!lw_time_service_utc(svc, now_ms, &utc)) {
        return false;
    }
    int64_t local = utc + (int64_t)svc->tz_offset_min * 60;
    if (local < 0) {
        local += SECONDS_PER_DAY;
    }
    int64_t days = local / SECONDS_PER_DAY;
    int64_t rem = local % SECONDS_PER_DAY;
    lw_date_t date;
    lw_civil_from_days((int)days, &date);
    if (out != NULL) {
        out->year = date.year;
        out->month = date.month;
        out->day = date.day;
        out->hour = (int)(rem / 3600);
        out->minute = (int)((rem / 60) % 60);
        out->second = (int)(rem % 60);
        /* 1970-01-01 was a Thursday (4 with Sunday = 0). */
        out->weekday = (int)((days + 4) % 7);
        if (out->weekday < 0) {
            out->weekday += 7;
        }
    }
    return true;
}

bool lw_time_service_date(const lw_time_service_t *svc, uint32_t now_ms, lw_date_t *out)
{
    lw_clock_t clock;
    if (!lw_time_service_clock(svc, now_ms, &clock)) {
        return false;
    }
    if (out != NULL) {
        out->year = clock.year;
        out->month = clock.month;
        out->day = clock.day;
    }
    return true;
}

void lw_time_service_hm(const lw_time_service_t *svc, uint32_t now_ms, char out[6])
{
    int64_t utc = 0;
    if (!lw_time_service_utc(svc, now_ms, &utc)) {
        copy_str(out, 6, "--:--");
        return;
    }
    int64_t local = utc + (int64_t)svc->tz_offset_min * 60;
    if (local < 0) {
        local += SECONDS_PER_DAY;
    }
    int64_t second_of_day = local % SECONDS_PER_DAY;
    int hour = (int)(second_of_day / 3600);
    int minute = (int)((second_of_day / 60) % 60);
    out[0] = (char)('0' + (hour / 10) % 10);
    out[1] = (char)('0' + hour % 10);
    out[2] = ':';
    out[3] = (char)('0' + (minute / 10) % 10);
    out[4] = (char)('0' + minute % 10);
    out[5] = '\0';
}
void lw_time_service_date_label(const lw_time_service_t *svc, uint32_t now_ms, char *out, unsigned cap)
{
    lw_clock_t clock;
    if (out == NULL || cap == 0) {
        return;
    }
    if (!lw_time_service_clock(svc, now_ms, &clock)) {
        out[0] = '\0';
        return;
    }
    char buffer[48];
    int weekday = clock.weekday;
    if (weekday < 0 || weekday > 6) {
        weekday = 0;
    }
    snprintf(buffer, sizeof(buffer), "%d月%d日 %s", clock.month, clock.day, kWeekdayShort[weekday]);
    copy_str(out, cap, buffer);
}
