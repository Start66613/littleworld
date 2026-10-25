/* Little World - connectivity adapters.
 *
 * Two implementations share this translation unit selection:
 *   - CONFIG_LW_NET_WIFI_ENABLE + esp_wifi.h  -> the real ESP-IDF stack
 *   - otherwise                               -> stubs that report "unavailable"
 *
 * The stub keeps the P4 firmware honest: without the C6 hosted components the
 * UI must show "Wi-Fi 未接线" instead of pretending to scan.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_net_esp.h"
#include "lw_services.h"

#if defined(ESP_PLATFORM)

#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_LW_NET_WIFI_ENABLE
#if __has_include("esp_wifi.h")
#define LW_NET_HAVE_WIFI 1
#endif
#endif
#if CONFIG_LW_NET_SNTP_ENABLE
#if __has_include("esp_netif_sntp.h")
#define LW_NET_HAVE_SNTP 1
#endif
#endif

#endif /* ESP_PLATFORM */

#ifdef LW_NET_HAVE_WIFI

#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lw_net_wifi";
static struct lw_services *s_services;
static bool s_started;

static lw_wifi_error_t map_reason(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return LW_WIFI_ERR_NO_AP;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return LW_WIFI_ERR_AUTH_FAILED;
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
        return LW_WIFI_ERR_TIMEOUT;
    default:
        return LW_WIFI_ERR_INTERNAL;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t count = 0;
        esp_wifi_scan_get_ap_num(&count);
        if (count > LW_WIFI_MAX_APS) {
            count = LW_WIFI_MAX_APS;
        }
        wifi_ap_record_t records[LW_WIFI_MAX_APS];
        if (count > 0 && esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
            lw_wifi_ap_t aps[LW_WIFI_MAX_APS];
            memset(aps, 0, sizeof(aps));
            for (uint16_t i = 0; i < count; i++) {
                strncpy(aps[i].ssid, (const char *)records[i].ssid, LW_WIFI_SSID_MAX - 1);
                aps[i].rssi = records[i].rssi;
                aps[i].secured = records[i].authmode != WIFI_AUTH_OPEN;
            }
            lw_services_wifi_report_scan(s_services, aps, count, true);
        } else {
            lw_services_wifi_report_scan(s_services, NULL, 0, false);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *info = (const wifi_event_sta_disconnected_t *)data;
        lw_services_wifi_report_failed(s_services, map_reason(info->reason), (uint32_t)(esp_log_timestamp()));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *info = (const ip_event_got_ip_t *)data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info->ip_info.ip));
        lw_services_wifi_report_connected(s_services, ip, (uint32_t)esp_log_timestamp());
    }
}

void lw_net_esp_wifi_attach(struct lw_services *services)
{
    s_services = services;
    if (s_started) {
        return;
    }
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed");
        return;
    }
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
        return;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "netif station create failed");
        return;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed");
        return;
    }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "station start failed");
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "station ready");
}

bool lw_net_esp_wifi_available(void)
{
    return true;
}

bool lw_net_esp_wifi_start_scan(void)
{
    if (!s_started) {
        return false;
    }
    wifi_scan_config_t scan = {0};
    return esp_wifi_scan_start(&scan, false) == ESP_OK;
}

bool lw_net_esp_wifi_start_connect(const char *ssid, const char *password)
{
    if (!s_started || ssid == NULL) {
        return false;
    }
    wifi_config_t config;
    memset(&config, 0, sizeof(config));
    strncpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)config.sta.password, password, sizeof(config.sta.password) - 1);
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) {
        return false;
    }
    return esp_wifi_connect() == ESP_OK;
}

void lw_net_esp_wifi_stop(void)
{
    if (s_started) {
        esp_wifi_disconnect();
    }
}

#else /* no Wi-Fi stack */

bool lw_net_esp_wifi_available(void)
{
    return false;
}

void lw_net_esp_wifi_attach(struct lw_services *services)
{
    (void)services;
}

bool lw_net_esp_wifi_start_scan(void)
{
    return false;
}

bool lw_net_esp_wifi_start_connect(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    return false;
}

void lw_net_esp_wifi_stop(void)
{
}

#endif /* LW_NET_HAVE_WIFI */

#ifdef LW_NET_HAVE_SNTP

#include <time.h>

#include "esp_netif_sntp.h"

static const char *TAG_SNTP = "lw_net_sntp";
static struct lw_services *s_time_services;
static bool s_time_started;

bool lw_net_esp_time_available(void)
{
    return true;
}

void lw_net_esp_time_attach(struct lw_services *services)
{
    s_time_services = services;
    if (s_time_started) {
        return;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = true;
    if (esp_netif_sntp_init(&config) != ESP_OK) {
        ESP_LOGW(TAG_SNTP, "sntp init failed");
        return;
    }
    s_time_started = true;
}

void lw_net_esp_time_request(void)
{
    if (!s_time_started) {
        return;
    }
    time_t now = 0;
    time(&now);
    if (now > 1700000000) {
        lw_services_time_report_sync(s_time_services, (int64_t)now, true, (uint32_t)esp_log_timestamp());
    }
}

#else /* no SNTP */

bool lw_net_esp_time_available(void)
{
    return false;
}

void lw_net_esp_time_attach(struct lw_services *services)
{
    (void)services;
}

void lw_net_esp_time_request(void)
{
}

#endif /* LW_NET_HAVE_SNTP */

bool lw_net_esp_audio_available(void)
{
    /* The ES8311 path is probed by the BSP in lw_hw_begin(); the audio model is
     * driven by that result in the boot code, not by this adapter. */
    return false;
}
