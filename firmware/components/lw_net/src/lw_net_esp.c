/* Little World - connectivity adapters.
 *
 * Two implementations share this translation unit selection:
 *   - CONFIG_LW_NET_WIFI_ENABLE + esp_wifi.h  -> the real ESP-IDF stack
 *   - otherwise                               -> stubs that report "unavailable"
 *
 * The stub keeps the P4 firmware honest: without the C6 hosted components the
 * UI must show "Wi-Fi 鏈帴绾? instead of pretending to scan.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_net_esp.h"
#include "lw_services.h"

#if defined(ESP_PLATFORM)

#include "esp_log.h"
#include "sdkconfig.h"
#include <stdio.h>

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

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lw_net_wifi";
static struct lw_services *s_services;
static bool s_started;
/* Generation captured when the request was issued. The event handler returns
 * it with the result so the model can drop results of a superseded request. */
static uint32_t s_request_generation;
static uint32_t s_scan_generation;
static bool s_link_up;

static uint32_t now_ms_from_timer(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

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
            lw_services_wifi_report_scan(s_services, s_scan_generation, aps, count, true);
        } else {
            lw_services_wifi_report_scan(s_services, s_scan_generation, NULL, 0, false);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *info = (const wifi_event_sta_disconnected_t *)data;
        const bool was_up = s_link_up;
        s_link_up = false;
        const uint32_t now = now_ms_from_timer();
        if (was_up) {
            /* The association was established: this is a link loss, not a failed attempt. */
            lw_services_wifi_report_lost(s_services, s_request_generation, now);
        } else {
            lw_services_wifi_report_failed(s_services, s_request_generation, map_reason(info->reason), now);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *info = (const ip_event_got_ip_t *)data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info->ip_info.ip));
        s_link_up = true;
        lw_services_wifi_report_connected(s_services, s_request_generation, ip, now_ms_from_timer());
    }
}

/* The transport line is built at run time from the hosted Kconfig values, which
 * keeps the pinout visible in the boot log even though every value is a compile
 * constant. It is inlined on purpose: with -ffunction-sections the linker
 * garbage-collects a standalone describe() before the log call can reference it,
 * because the call arguments are constant and GCC folds them away. */
static void log_transport(void)
{
    char line[128];
#if CONFIG_ESP_HOSTED_ENABLED
    snprintf(line, sizeof(line),
             "hosted esp32c6 sdio clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d rst=%d %dMHz",
             (int)CONFIG_ESP_HOSTED_SDIO_PIN_CLK, (int)CONFIG_ESP_HOSTED_SDIO_PIN_CMD,
             (int)CONFIG_ESP_HOSTED_SDIO_PIN_D0, (int)CONFIG_ESP_HOSTED_SDIO_PIN_D1,
             (int)CONFIG_ESP_HOSTED_SDIO_PIN_D2, (int)CONFIG_ESP_HOSTED_SDIO_PIN_D3,
             (int)CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE,
             (int)(CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ / 1000));
#else
    snprintf(line, sizeof(line), "esp_wifi enabled without ESP-Hosted");
#endif
    ESP_LOGI(TAG, "transport: %s", line);
}

void lw_net_esp_wifi_attach(struct lw_services *services)
{
    s_services = services;
    if (s_started) {
        return;
    }
    log_transport();
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
        ESP_LOGE(TAG, "esp_wifi_init failed (no ESP32-C6 responder on SDIO?)");
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

/* The station can already be associated right here, without waiting for the
 * user to press a button: the ESP32-C6 hosted slave may hold the credential of
 * a previous boot. Reporting it as an unsolicited link keeps the UI honest (it
 * shows the real network) and costs nothing when there is none. A build with
 * the station up but the slave silent stays IDLE, which is the accurate state. */
void lw_net_esp_wifi_probe_connected(void)
{
#if CONFIG_ESP_HOSTED_ENABLED
    if (!s_started) {
        return;
    }
    wifi_ap_record_t ap;
    memset(&ap, 0, sizeof(ap));
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0] != '\0') {
        /* ap.ssid is a 32-byte field that may be exactly full, so it is not
         * guaranteed to be NUL terminated. Copy at most 31 bytes and terminate
         * explicitly; strncpy() trips -Wstringop-truncation with -Werror. */
        char ssid[LW_WIFI_SSID_MAX];
        unsigned n = 0;
        while (n + 1u < (unsigned)sizeof(ssid) && n + 1u < (unsigned)sizeof(ap.ssid) && ap.ssid[n] != '\0') {
            ssid[n] = (char)ap.ssid[n];
            n++;
        }
        ssid[n] = '\0';
        s_link_up = true;
        lw_services_wifi_report_connected(s_services, s_request_generation, ssid, 0);
        ESP_LOGW(TAG, "hosted slave already associated with '%s'", ssid);
    }
#endif
}

/* Availability is only claimed once the station really came up. A host built
 * with CONFIG_LW_NET_WIFI_ENABLE but failing esp_wifi_init() (no C6, wrong
 * SDIO pinout, hosted slave missing) would otherwise be reported as "Wi-Fi
 * ready" and the UI would sit on a scanning animation forever. lw_boot.c
 * publishes the capabilities AFTER lw_net_esp_wifi_attach(), so this flag is
 * the honest answer at that moment. */
bool lw_net_esp_wifi_available(void)
{
    return s_started;
}

/* Slot pins must match the P4<->C6 SDIO wiring of the board, not another P4
 * board: esp_hosted's ESP32-P4 defaults are CLK 18 / CMD 19 / D0 14 / D1 15 /
 * D2 16 / D3 17 / RESET 54. They are printed at boot so a wrong pinout is
 * visible in the log instead of looking like a hanging connect. */
void lw_net_esp_wifi_describe(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }
#if CONFIG_ESP_HOSTED_ENABLED
    snprintf(buf, len,
             "hosted esp32c6 sdio clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d rst=%d %dMHz",
             CONFIG_ESP_HOSTED_SDIO_PIN_CLK, CONFIG_ESP_HOSTED_SDIO_PIN_CMD,
             CONFIG_ESP_HOSTED_SDIO_PIN_D0, CONFIG_ESP_HOSTED_SDIO_PIN_D1,
             CONFIG_ESP_HOSTED_SDIO_PIN_D2, CONFIG_ESP_HOSTED_SDIO_PIN_D3,
             CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE,
             CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ / 1000);
#else
    snprintf(buf, len, "esp_wifi enabled without ESP-Hosted");
#endif
}

bool lw_net_esp_wifi_start_scan(void)
{
    if (!s_started) {
        return false;
    }
    s_scan_generation = lw_services_scan_generation(s_services);
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
    s_request_generation = lw_services_wifi_generation(s_services);
    s_link_up = false;
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

void lw_net_esp_wifi_probe_connected(void)
{
}

void lw_net_esp_wifi_describe(char *buf, size_t len)
{
    if (buf != NULL && len > 0) {
        snprintf(buf, len, "CONFIG_LW_NET_WIFI_ENABLE is off in this build");
    }
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

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_netif_sntp.h"

static const char *TAG_SNTP = "lw_net_sntp";
static struct lw_services *s_time_services;
static bool s_time_started;
/* One sync task at a time: it is the only writer of this flag. */
static bool s_time_waiting;

static uint32_t sntp_now_ms(void)
{
    return now_ms_from_timer();
}

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
    /* start=false: syncing begins only after lw_net_esp_time_request(), i.e.
     * after the station really has an IP address. */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = false;
    if (esp_netif_sntp_init(&config) != ESP_OK) {
        ESP_LOGW(TAG_SNTP, "sntp init failed");
        return;
    }
    s_time_started = true;
    ESP_LOGI(TAG_SNTP, "service ready (idle until the link is up)");
}

/* Runs off the caller's stack: esp_netif_sntp_sync_wait blocks on a semaphore,
 * so the LVGL timer must not wait for the server. Only the wait result decides
 * whether the clock is trusted; the system clock is never sampled as proof. */
static void sntp_wait_task(void *arg)
{
    (void)arg;
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(LW_NET_SNTP_TIMEOUT_MS));
    bool ok = false;
    int64_t utc = 0;
    if (err == ESP_OK) {
        time_t now = 0;
        time(&now);
        utc = (int64_t)now;
        ok = utc > 1700000000LL; /* the server answered; reject a clock that is still unset */
        if (!ok) {
            ESP_LOGW(TAG_SNTP, "sync returned but the clock is still unset");
        }
    } else {
        ESP_LOGW(TAG_SNTP, "sync wait failed: %s", esp_err_to_name(err));
    }
    if (s_time_services != NULL) {
        lw_services_time_report_sync(s_time_services, utc, ok, sntp_now_ms());
    }
    s_time_waiting = false;
    vTaskDelete(NULL);
}

void lw_net_esp_time_request(void)
{
    if (!s_time_started || s_time_waiting) {
        return;
    }
    s_time_waiting = true;
    if (xTaskCreate(sntp_wait_task, "lw_sntp", 4096, NULL, 4, NULL) != pdPASS) {
        s_time_waiting = false;
        ESP_LOGW(TAG_SNTP, "cannot start the sync task");
        if (s_time_services != NULL) {
            lw_services_time_report_sync(s_time_services, 0, false, sntp_now_ms());
        }
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
