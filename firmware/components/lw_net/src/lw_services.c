/* Little World - connectivity service glue. Pure C apart from the adapter calls. */
#include "lw_services.h"

#include <string.h>

#include "lw_net_esp.h"

#define TIME_SYNC_RETRY_MS 30000u

static void copy_str(char *dst, unsigned cap, const char *src)
{
    if (cap == 0) {
        return;
    }
    unsigned i = 0;
    for (; src != NULL && src[i] != '\0' && i + 1u < cap; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void lw_services_init(lw_services_t *services)
{
    memset(services, 0, sizeof(*services));
    services->caps.wifi_backend = lw_net_esp_wifi_available();
    services->caps.time_backend = lw_net_esp_time_available();
    services->caps.audio_backend = lw_net_esp_audio_available();
    lw_wifi_init(&services->wifi, services->caps.wifi_backend);
    lw_time_service_init(&services->time, LW_TIME_DEFAULT_TZ_MINUTES);
    lw_audio_init(&services->audio, services->caps.audio_backend);
}

void lw_services_set_audio_backend(lw_services_t *services, bool present)
{
    if (services == NULL) {
        return;
    }
    services->caps.audio_backend = present;
    lw_audio_set_sink(&services->audio, present);
}

void lw_services_start(lw_services_t *services, uint32_t now_ms)
{
    if (services->started) {
        return;
    }
    services->started = true;
    services->last_poll_ms = now_ms;
    if (services->caps.wifi_backend) {
        lw_net_esp_wifi_attach(services);
        lw_wifi_enable(&services->wifi, true, now_ms);
    }
    if (services->caps.time_backend) {
        lw_net_esp_time_attach(services);
    }
}

static void run_actions(lw_services_t *services, uint32_t now_ms)
{
    char ssid[LW_WIFI_SSID_MAX];
    const char *password = NULL;
    lw_wifi_action_t action = lw_wifi_take_action(&services->wifi, ssid, &password);
    switch (action) {
    case LW_WIFI_ACTION_SCAN:
        lw_net_esp_wifi_start_scan();
        break;
    case LW_WIFI_ACTION_CONNECT:
        lw_net_esp_wifi_start_connect(ssid, password);
        break;
    case LW_WIFI_ACTION_DISCONNECT:
        lw_net_esp_wifi_stop();
        break;
    case LW_WIFI_ACTION_NONE:
        break;
    }
    (void)now_ms;
}

void lw_services_poll(lw_services_t *services, uint32_t now_ms)
{
    services->last_poll_ms = now_ms;
    lw_wifi_tick(&services->wifi, now_ms);
    run_actions(services, now_ms);

    /* Once the link is up, ask for the wall clock; retry while it is missing. */
    if (lw_wifi_state(&services->wifi) == LW_WIFI_ST_CONNECTED && services->caps.time_backend) {
        lw_time_state_t state = lw_time_state(&services->time);
        if (state == LW_TIME_DEMO || state == LW_TIME_FAILED) {
            if (services->sync_retry_at_ms == 0 || now_ms >= services->sync_retry_at_ms) {
                if (lw_time_service_request_sync(&services->time, now_ms)) {
                    lw_net_esp_time_request();
                }
                services->sync_retry_at_ms = now_ms + TIME_SYNC_RETRY_MS;
            }
        }
    } else if (lw_wifi_state(&services->wifi) != LW_WIFI_ST_CONNECTED) {
        services->sync_retry_at_ms = 0;
        lw_time_service_on_network_lost(&services->time, now_ms);
    }
    lw_time_service_tick(&services->time, now_ms);
}

void lw_services_status(const lw_services_t *services, uint32_t now_ms, lw_net_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->wifi_backend = services->caps.wifi_backend;
    out->wifi_enabled = services->wifi.enabled;
    out->wifi_state = lw_wifi_state(&services->wifi);
    out->wifi_error = lw_wifi_error(&services->wifi);
    out->ap_count = lw_wifi_scan_count(&services->wifi);
    copy_str(out->ssid, sizeof(out->ssid), lw_wifi_target(&services->wifi));
    copy_str(out->ip, sizeof(out->ip), lw_wifi_ip(&services->wifi));
    if (out->ap_count > 0) {
        const lw_wifi_ap_t *first = lw_wifi_scan_at(&services->wifi, 0);
        out->rssi = first != NULL ? first->rssi : 0;
    }

    out->time_state = lw_time_state(&services->time);
    out->time_error = lw_time_error(&services->time);
    out->zone_minutes = lw_time_service_zone(&services->time);
    out->time_valid = lw_time_service_utc(&services->time, now_ms, NULL);
    lw_time_service_hm(&services->time, now_ms, out->clock);
    lw_time_service_date_label(&services->time, now_ms, out->date_label, sizeof(out->date_label));

    out->audio_backend = services->caps.audio_backend;
    out->audio_state = lw_audio_state(&services->audio);
    out->volume = lw_audio_volume(&services->audio);
    out->muted = lw_audio_muted(&services->audio);
}

bool lw_services_wifi_enable(lw_services_t *services, bool enabled, uint32_t now_ms)
{
    bool ok = lw_wifi_enable(&services->wifi, enabled, now_ms);
    if (ok) {
        run_actions(services, now_ms);
    }
    return ok;
}

bool lw_services_scan(lw_services_t *services, uint32_t now_ms)
{
    if (!lw_wifi_scan(&services->wifi, now_ms)) {
        return false;
    }
    run_actions(services, now_ms);
    return true;
}

bool lw_services_connect(lw_services_t *services, const char *ssid, const char *password, uint32_t now_ms)
{
    if (!lw_wifi_connect(&services->wifi, ssid, password, now_ms)) {
        return false;
    }
    run_actions(services, now_ms);
    return true;
}

bool lw_services_disconnect(lw_services_t *services, uint32_t now_ms)
{
    if (!lw_wifi_disconnect(&services->wifi, now_ms)) {
        return false;
    }
    run_actions(services, now_ms);
    return true;
}

bool lw_services_request_time_sync(lw_services_t *services, uint32_t now_ms)
{
    if (!lw_time_service_request_sync(&services->time, now_ms)) {
        return false;
    }
    lw_net_esp_time_request();
    return true;
}

void lw_services_set_zone(lw_services_t *services, int32_t zone_minutes)
{
    lw_time_service_set_zone(&services->time, zone_minutes);
}

void lw_services_wifi_report_scan(lw_services_t *services, const lw_wifi_ap_t *aps, unsigned count, bool ok)
{
    lw_wifi_on_scan_done(&services->wifi, services->wifi.scan_generation, aps, count, ok);
}

void lw_services_wifi_report_connected(lw_services_t *services, const char *ip, uint32_t now_ms)
{
    lw_wifi_on_connected(&services->wifi, services->wifi.generation, ip, now_ms);
}

void lw_services_wifi_report_failed(lw_services_t *services, lw_wifi_error_t error, uint32_t now_ms)
{
    lw_wifi_on_connect_failed(&services->wifi, services->wifi.generation, error, now_ms);
}

void lw_services_wifi_report_lost(lw_services_t *services, uint32_t now_ms)
{
    lw_wifi_on_disconnected(&services->wifi, services->wifi.generation, LW_WIFI_ERR_LINK_LOST, now_ms);
}

void lw_services_time_report_sync(lw_services_t *services, int64_t utc_seconds, bool ok, uint32_t now_ms)
{
    if (ok) {
        lw_time_service_on_sync_ok(&services->time, utc_seconds, now_ms);
    } else {
        lw_time_service_on_sync_failed(&services->time, LW_TIME_ERR_NETWORK, now_ms);
    }
}
