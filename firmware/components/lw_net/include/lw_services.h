/* Little World - connectivity service glue.
 *
 * Owns the Wi-Fi, time and audio models, performs the side effects they ask
 * for through the adapter in lw_net_esp.c, and exposes one snapshot for the UI.
 * No LVGL here: the boot code calls lw_services_poll() from a timer.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lw_audio.h"
#include "lw_time_svc.h"
#include "lw_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wifi_backend;   /* an adapter is present */
    bool wifi_enabled;   /* radio on */
    bool time_backend;   /* SNTP available */
    bool audio_backend;  /* codec path available */
} lw_services_caps_t;

typedef struct {
    lw_wifi_state_t wifi_state;
    lw_wifi_error_t wifi_error;
    bool wifi_enabled;
    bool wifi_backend;
    char ssid[LW_WIFI_SSID_MAX];
    char ip[16];
    int8_t rssi;
    unsigned ap_count;

    lw_time_state_t time_state;
    lw_time_error_t time_error;
    bool time_valid;
    int32_t zone_minutes;
    char clock[6];       /* "19:30" or "--:--" */
    char date_label[32]; /* "10月25日 周日" or "" */

    bool audio_backend;
    lw_audio_state_t audio_state;
    unsigned volume;
    bool muted;
} lw_net_status_t;

typedef struct lw_services {
    lw_wifi_t wifi;
    lw_time_service_t time;
    lw_audio_t audio;
    lw_services_caps_t caps;
    bool started;
    uint32_t last_poll_ms;
    uint32_t sync_retry_at_ms;
} lw_services_t;

/** Reads the adapters' capabilities and initialises the models. */
void lw_services_init(lw_services_t *services);
/** Starts the adapters that are available. Safe to call once. */
void lw_services_start(lw_services_t *services, uint32_t now_ms);
void lw_services_poll(lw_services_t *services, uint32_t now_ms);
void lw_services_status(const lw_services_t *services, uint32_t now_ms, lw_net_status_t *out);

/* Requests. They return false when the layer is unavailable; the snapshot
 * carries the reason through wifi_error / time_error. */
bool lw_services_wifi_enable(lw_services_t *services, bool enabled, uint32_t now_ms);
bool lw_services_scan(lw_services_t *services, uint32_t now_ms);
bool lw_services_connect(lw_services_t *services, const char *ssid, const char *password, uint32_t now_ms);
bool lw_services_disconnect(lw_services_t *services, uint32_t now_ms);
bool lw_services_request_time_sync(lw_services_t *services, uint32_t now_ms);
void lw_services_set_zone(lw_services_t *services, int32_t zone_minutes);

/** Adapter callbacks (called from the ESP event loop; never touch LVGL). */
void lw_services_wifi_report_scan(lw_services_t *services, const lw_wifi_ap_t *aps, unsigned count, bool ok);
void lw_services_wifi_report_connected(lw_services_t *services, const char *ip, uint32_t now_ms);
void lw_services_wifi_report_failed(lw_services_t *services, lw_wifi_error_t error, uint32_t now_ms);
void lw_services_wifi_report_lost(lw_services_t *services, uint32_t now_ms);
void lw_services_time_report_sync(lw_services_t *services, int64_t utc_seconds, bool ok, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
