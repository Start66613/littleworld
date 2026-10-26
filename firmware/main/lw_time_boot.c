/* SPDX-License-Identifier: MIT */
#include "lw_time_boot.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "lw_time_boot";

static lw_day_t s_day;
static lw_time_state_t s_last_state;
static int64_t s_last_utc;
static bool s_started;
static char s_report[192];

void lw_time_boot_init(void)
{
    lw_day_init(&s_day, NULL);
    /* Start on the labelled rehearsal clock: the device must show a time from
     * the first frame, and pretending it is real is the failure mode this whole
     * module exists to prevent. */
    lw_day_use_demo(&s_day);
    s_last_state = LW_TIME_DEMO;
    s_last_utc = 0;
    s_started = true;
    ESP_LOGI(TAG, "clock: %s (demo %02u:%02u)", lw_day_source_name(s_day.source),
             (unsigned)(LW_DAY_DEMO_SECONDS / 3600), (unsigned)(LW_DAY_DEMO_SECONDS % 3600 / 60));
}

void lw_time_boot_update(const lw_net_status_t *status, uint32_t now_ms)
{
    if (!s_started || status == NULL) {
        return;
    }
    lw_day_tick(&s_day, now_ms);

    lw_time_state_t state = status->time_state;
    bool state_changed = state != s_last_state;
    s_last_state = state;

    switch (state) {
    case LW_TIME_SYNCED:
        if (status->time_valid && status->utc_seconds > 0) {
            /* A successful sync replaces any manual override: real time is
             * better than typed time. The snapshot carries the UTC second, so
             * this reader never touches services->time directly. */
            if (s_day.source != LW_DAY_SOURCE_REAL || status->utc_seconds != s_last_utc) {
                (void)lw_day_set_real(&s_day, status->utc_seconds, status->zone_minutes, now_ms);
                s_last_utc = status->utc_seconds;
            }
        }
        break;
    case LW_TIME_STALE:
        /* The value survives, the trust does not. Only log the transition: this
         * runs every second. */
        if (state_changed) {
            lw_day_mark_stale(&s_day, now_ms);
            ESP_LOGW(TAG, "clock: link lost, last known time is no longer trusted");
        }
        break;
    case LW_TIME_SYNCING:
        /* Nothing to change: the previous source stays in effect until the
         * round reports back. */
        break;
    case LW_TIME_DEMO:
    case LW_TIME_FAILED:
    default:
        if (s_day.source != LW_DAY_SOURCE_MANUAL && s_day.source != LW_DAY_SOURCE_REAL) {
            lw_day_use_demo(&s_day);
        }
        break;
    }

    if (state_changed) {
        ESP_LOGI(TAG, "clock: time service %s, effective source %s", lw_time_state_name(state),
                 lw_day_source_name(s_day.source));
    }
}

bool lw_time_boot_get_tm(struct tm *out)
{
    if (out == NULL || !s_started) {
        return false;
    }
    /* Only a trusted source answers the platform hook. DEMO is deliberately not
     * returned here: lw_now() then uses its own labelled demo base instead of
     * silently reporting the rehearsal clock as the real one. */
    if (!s_day.trusted) {
        return false;
    }
    uint32_t now_ms = lw_services_now_ms();
    int year = 0;
    int month = 0;
    int day_of_month = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!lw_day_clock(&s_day, now_ms, &year, &month, &day_of_month, &hour, &minute, &second)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->tm_year = year - 1900;
    out->tm_mon = month - 1;
    out->tm_mday = day_of_month;
    out->tm_hour = hour;
    out->tm_min = minute;
    out->tm_sec = second;
    return true;
}

const lw_day_t *lw_time_boot_day(void) { return s_started ? &s_day : NULL; }

void lw_time_boot_line(char *out, size_t len)
{
    if (out == NULL || len == 0) {
        return;
    }
    if (!s_started) {
        (void)snprintf(out, len, "not initialised");
        return;
    }
    uint32_t now_ms = lw_services_now_ms();
    char clock[6] = "--:--";
    lw_day_hm(&s_day, now_ms, clock);
    /* "trusted" is the load-bearing word: the screen may show a rehearsal time,
     * and a diagnostic line that omitted that would be the exact lie this
     * module exists to prevent. */
    (void)snprintf(out, len, "%s %s, %s, %s, theme %d", clock, lw_day_source_name(s_day.source),
                   s_day.trusted ? "trusted" : "not trusted", lw_day_phase_name(s_day.phase),
                   lw_day_theme(s_day.phase));
}

const char *lw_time_boot_report(void)
{
    if (!s_started) {
        return "clock: not initialised";
    }
    uint32_t now_ms = lw_services_now_ms();
    char clock[6] = "--:--";
    lw_day_hm(&s_day, now_ms, clock);
    uint32_t next = lw_day_until_next(&s_day, now_ms);
    (void)snprintf(s_report, sizeof(s_report), "clock %s (%s) %s, phase %s, theme %d, next in %us",
                   clock, lw_day_source_name(s_day.source),
                   s_day.trusted ? "trusted" : "not trusted", lw_day_phase_name(s_day.phase),
                   lw_day_theme(s_day.phase), (unsigned)next);
    return s_report;
}
