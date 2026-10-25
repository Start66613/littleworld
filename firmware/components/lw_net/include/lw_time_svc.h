/* Little World - system time service (pure C, host tested).
 *
 * Owns the wall clock used by the shell: mostly the DEMO clock, upgraded to
 * real time after a successful SNTP sync, and demoted to STALE when the link
 * that provided the time goes away.
 *
 * The timezone offset is deliberately NOT part of the 24-byte preferences
 * blob: it lives in its own record (lwo2.time) so the preferences format and
 * its CRC stay untouched for the already-shipped firmware.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lw/lw_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LW_TIME_DEFAULT_TZ_MINUTES 480 /* UTC+08:00 */
#define LW_TIME_SYNC_TIMEOUT_MS 20000u
#define LW_TIME_RESYNC_MS (6u * 60u * 60u * 1000u)

typedef enum {
    LW_TIME_DEMO = 0, /* no sync yet; the shell shows the DEMO clock */
    LW_TIME_SYNCING,
    LW_TIME_SYNCED,
    LW_TIME_STALE, /* had a sync, then lost it (link down or too old) */
    LW_TIME_FAILED
} lw_time_state_t;

typedef enum {
    LW_TIME_ERR_NONE = 0,
    LW_TIME_ERR_NOT_SUPPORTED,
    LW_TIME_ERR_TIMEOUT,
    LW_TIME_ERR_NETWORK,
    LW_TIME_ERR_INVALID
} lw_time_error_t;

typedef struct {
    lw_time_state_t state;
    lw_time_error_t error;
    bool rtc_restored;      /* the clock came from the RTC at boot */
    int32_t tz_offset_min;  /* minutes east of UTC, -840..840 */
    int64_t utc_seconds;    /* last known UTC second */
    uint32_t synced_ms;     /* uptime of the last successful sync */
    uint32_t attempt_ms;    /* uptime when the current sync started */
    unsigned attempts;      /* sync attempts in this round */
} lw_time_service_t;

typedef struct {
    int year, month, day;
    int hour, minute, second;
    int weekday; /* 0 = Sunday */
} lw_clock_t;

void lw_time_service_init(lw_time_service_t *svc, int32_t tz_offset_min);
void lw_time_service_set_zone(lw_time_service_t *svc, int32_t tz_offset_min);
int32_t lw_time_service_zone(const lw_time_service_t *svc);
lw_time_state_t lw_time_state(const lw_time_service_t *svc);
lw_time_error_t lw_time_error(const lw_time_service_t *svc);
const char *lw_time_state_name(lw_time_state_t state);
const char *lw_time_error_name(lw_time_error_t error);

/** Restore a persisted UTC second (RTC or NVS). Marks the clock STALE: it is a
 *  real timestamp but nothing vouched for it this boot. */
void lw_time_service_restore(lw_time_service_t *svc, int64_t utc_seconds, uint32_t now_ms);

/** Start an SNTP round. Refused while another one is in flight. */
bool lw_time_service_request_sync(lw_time_service_t *svc, uint32_t now_ms);
void lw_time_service_on_sync_ok(lw_time_service_t *svc, int64_t utc_seconds, uint32_t now_ms);
void lw_time_service_on_sync_failed(lw_time_service_t *svc, lw_time_error_t error, uint32_t now_ms);
/** The link went away: a synced clock becomes STALE, the DEMO clock stays DEMO. */
void lw_time_service_on_network_lost(lw_time_service_t *svc, uint32_t now_ms);
/** Timeouts and the periodic resync. */
void lw_time_service_tick(lw_time_service_t *svc, uint32_t now_ms);

/** Local clock fields. False when there is no usable time (DEMO/FAILED). */
bool lw_time_service_clock(const lw_time_service_t *svc, uint32_t now_ms, lw_clock_t *out);
/** Local calendar date. False when there is no usable time. */
bool lw_time_service_date(const lw_time_service_t *svc, uint32_t now_ms, lw_date_t *out);
/** UTC seconds right now, extrapolated from the last sync with the uptime. */
bool lw_time_service_utc(const lw_time_service_t *svc, uint32_t now_ms, int64_t *out);
/** "19:30" style label; falls back to "--:--" without time. */
void lw_time_service_hm(const lw_time_service_t *svc, uint32_t now_ms, char out[6]);
/** "10月25日 周日" style label; empty string without time. */
void lw_time_service_date_label(const lw_time_service_t *svc, uint32_t now_ms, char *out, unsigned cap);

#ifdef __cplusplus
}
#endif
