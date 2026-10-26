/* Board-side wiring for the V4 diagnostics report. ESP32-P4 only.
 *
 * The neutral half (lw_diag_report.c) builds the snapshot and formats it; this
 * file answers "what does this board actually know". Every provider here reads
 * a real subsystem or admits it cannot:
 *
 *   LVGL heap   - lv_mem_monitor(). LVGL is built with the C library allocator
 *                 (CONFIG_LV_USE_CLIB_MALLOC=y), whose monitor is a documented
 *                 no-op that zeroes the struct. Reporting "0 B used" would be a
 *                 lie, so this provider returns false and the report says the
 *                 monitor is unavailable. The real numbers in that
 *                 configuration come from the allocator counters below.
 *   resources   - lw_resource_report() plus the file cache counters.
 *   frames      - lw_assets_stats(): leases, live bytes, load failures.
 *   theme       - the live preference the UI is rendering.
 *   medium      - lw_resource_medium_mounted() and its status text.
 *   wifi        - the service snapshot, which is the only place that knows the
 *                 difference between absent / started / connected.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_diag_board.h"

#include "sdkconfig.h"

#ifdef ESP_PLATFORM

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lw_assets.h"
#include "lw_art.h"
#include "lw_os.h"
#include "lw_resource.h"
#include "lw_resource_mount.h"
#include "lw_services.h"
#include "lw_storage.h"
#include "lw_system.h"
#include "lvgl.h"

#if defined(CONFIG_LW_NET_WIFI_ENABLE)
#include "lw_net_esp.h"
#endif

static const char *TAG = "lw_diag";

static lw_system_t *s_system;
static lw_services_t *s_services;
static bool s_ready;

/* Which build this is. The image does not carry the sdkconfig fragment name,
 * so derive it from the switches that actually differ. Kept short: it has to
 * fit next to the firmware version in a 24-byte field. */
static const char *build_config_name(void)
{
#if !defined(CONFIG_LW_BOOT_MODE_OS_V2)
    return "legacy";
#elif defined(CONFIG_LW_NET_WIFI_ENABLE)
    return "os-wifi";
#elif defined(CONFIG_LW_DIAG_STATIC_UI)
    return "diag-static";
#elif defined(CONFIG_LW_BACKLIGHT_FIXED)
    return "diag-fixed";
#elif defined(CONFIG_LW_BACKLIGHT_MINIMAL)
    return "diag-minimal";
#elif !defined(CONFIG_LW_ENABLE_AUDIO)
    return "noaudio";
#else
    return "os";
#endif
}

static void prov_memory(uint32_t *reset_reason, uint32_t *boot_ms, size_t *heap_free, size_t *heap_min,
                        size_t *heap_largest, size_t *psram_free, size_t *psram_largest)
{
    multi_heap_info_t info;
    *reset_reason = (uint32_t)esp_reset_reason();
    *boot_ms = (uint32_t)(esp_timer_get_time() / 1000);
    *heap_free = esp_get_free_heap_size();
    *heap_min = esp_get_minimum_free_heap_size();
    *heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    *psram_free = info.total_free_bytes;
    *psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

static bool prov_lv_mem(size_t *used, size_t *max_used, uint8_t *frag_pct)
{
    /* Only the LVGL task may touch LVGL state. A console command is another
     * task, so take the display lock; without it a snapshot can walk a widget
     * tree that the UI task is deleting. */
    if (!bsp_display_lock(2000)) {
        return false;
    }
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    bool usable = mon.total_size > 0u;
    if (usable) {
        *used = mon.total_size - mon.free_size;
        *max_used = mon.max_used;
        *frag_pct = mon.frag_pct;
    }
    bsp_display_unlock();
    return usable;
}

static void prov_resources(char *version, size_t version_len, char *status, size_t status_len,
                           uint32_t *reads, uint32_t *failures)
{
    const lw_resource_report_t *report = lw_resource_report();
    if (report == NULL) {
        snprintf(version, version_len, "n/a");
        snprintf(status, status_len, "no report");
        return;
    }
    if (report->pack_version != 0u) {
        snprintf(version, version_len, "schema%u-v%u", (unsigned)report->schema,
                 (unsigned)report->pack_version);
    } else {
        snprintf(version, version_len, "none");
    }
    snprintf(status, status_len, "%s%s%s", lw_resource_status_text(report->status),
             report->verified ? " verified" : " unverified",
             report->themes != 0u ? "" : " no-themes");
    /* The cache counters are the only real read/failure numbers we have; the
     * manifest check reads a handful of headers and does not count them. The
     * handle is borrowed from lw_art and may legitimately be NULL (no card). */
    lw_file_cache_t *cache = lw_art_cache();
    if (cache != NULL) {
        lw_file_cache_stats_t stats = lw_file_cache_stats(cache);
        *reads = stats.reads + stats.hits + stats.misses;
        *failures = stats.failures + stats.queue_drops;
    } else {
        *reads = 0u;
        *failures = 0u;
    }
}

static void prov_frames(uint32_t *failures, uint32_t *leases, size_t *bytes)
{
    lw_asset_stats_t stats = lw_assets_stats();
    *failures = stats.failures;
    *leases = stats.live_leases;
    *bytes = stats.live_bytes;
}

static int prov_theme(void)
{
    if (s_system == NULL) {
        return -1;
    }
    return (int)s_system->prefs.theme;
}

static void prov_medium(bool *mounted, char *reason, size_t len)
{
    *mounted = lw_resource_medium_mounted();
    const lw_resource_report_t *report = lw_resource_report();
    if (report != NULL && report->root[0] != '\0') {
        snprintf(reason, len, "%s", report->root);
    } else if (report != NULL) {
        snprintf(reason, len, "%s", lw_resource_status_text(report->status));
    } else {
        snprintf(reason, len, "%s", *mounted ? "mounted" : "not mounted");
    }
}

static void prov_wifi(lw_diag_wifi_t *state, char *reason, size_t len)
{
    if (s_services == NULL) {
        *state = LW_DIAG_WIFI_ABSENT;
        snprintf(reason, len, "no service layer");
        return;
    }
    lw_net_status_t status;
    memset(&status, 0, sizeof(status));
    lw_services_status(s_services, (uint32_t)(esp_timer_get_time() / 1000), &status);
    if (!status.wifi_backend) {
        *state = LW_DIAG_WIFI_ABSENT;
    } else if (status.wifi_state == LW_WIFI_ST_UNSUPPORTED) {
        /* Linked but the adapter refused to start - a real difference the UI
         * also shows, and the one a board test needs to see. */
        *state = LW_DIAG_WIFI_FAILED;
    } else if (status.wifi_state == LW_WIFI_ST_CONNECTED) {
        *state = LW_DIAG_WIFI_STARTED;
    } else {
        *state = LW_DIAG_WIFI_COMPILED;
    }
    const char *detail = NULL;
    if (status.wifi_state == LW_WIFI_ST_CONNECTED) {
        snprintf(reason, len, "connected %s %s", status.ssid[0] ? status.ssid : "?", status.ip);
        return;
    }
    detail = lw_wifi_error_name(status.wifi_error);
    if (detail == NULL || detail[0] == '\0') {
        detail = "idle";
    }
    snprintf(reason, len, "%s (%s)", detail, lw_wifi_state_name(status.wifi_state));
}

void lw_diag_board_bind(lw_system_t *system, lw_services_t *services)
{
    s_system = system;
    s_services = services;
}

#if CONFIG_LW_DIAG_PERIODIC_LOG
/* One line every 30 s, and only when the free heap actually moved. The point is
 * to catch a slow leak over a long board run without turning the console into a
 * log stream: a 20 ms print would cost more frame time than it measures. */
#define DIAG_LOG_PERIOD_US (30 * 1000 * 1000)
#define DIAG_LOG_MIN_DELTA  (8 * 1024)

static void diag_periodic_cb(void *arg)
{
    (void)arg;
    static size_t last_free;
    size_t now_free = esp_get_free_heap_size();
    size_t delta = (now_free > last_free) ? (now_free - last_free) : (last_free - now_free);
    if (last_free != 0u && delta < DIAG_LOG_MIN_DELTA) {
        return;
    }
    last_free = now_free;
    ESP_LOGI(TAG, "heap free %u B, min %u B, psram %u B",
             (unsigned)now_free,
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void start_periodic_log(void)
{
    const esp_timer_create_args_t args = { .callback = &diag_periodic_cb, .name = "diag_periodic" };
    esp_timer_handle_t timer = NULL;
    if (esp_timer_create(&args, &timer) != ESP_OK) {
        ESP_LOGW(TAG, "periodic log timer not created");
        return;
    }
    if (esp_timer_start_periodic(timer, DIAG_LOG_PERIOD_US) != ESP_OK) {
        esp_timer_delete(timer);
        ESP_LOGW(TAG, "periodic log timer not started");
        return;
    }
    ESP_LOGI(TAG, "periodic heap line every 30 s (delta >= 8 KiB)");
}
#endif

void lw_diag_board_init(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    static char s_firmware[40];
    static char s_build[24];
    static char s_idf[24];
    snprintf(s_firmware, sizeof(s_firmware), "%s", desc != NULL ? desc->version : "unknown");
    snprintf(s_build, sizeof(s_build), "%s", build_config_name());
    snprintf(s_idf, sizeof(s_idf), "%s", desc != NULL ? desc->idf_ver : "unknown");

    lw_diag_providers_t providers;
    memset(&providers, 0, sizeof(providers));
    providers.firmware = s_firmware;
    providers.build_config = s_build;
    providers.idf = s_idf;
    providers.memory = prov_memory;
    providers.lv_mem = prov_lv_mem;
    providers.resources = prov_resources;
    providers.frames = prov_frames;
    providers.theme = prov_theme;
    providers.medium = prov_medium;
    providers.wifi = prov_wifi;
    lw_diag_providers(&providers);
    s_ready = true;

    lw_diag_report_t report;
    lw_diag_board_report(&report);
    char line[512];
    lw_diag_text(&report, line, sizeof(line));
    ESP_LOGI(TAG, "diagnostics ready: firmware=%s build=%s", report.firmware, report.build_config);
    (void)line;
#if CONFIG_LW_DIAG_PERIODIC_LOG
    start_periodic_log();
#endif
}

bool lw_diag_board_ready(void)
{
    return s_ready;
}

void lw_diag_board_report(lw_diag_report_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = lw_diag_report();
}

#endif /* ESP_PLATFORM */
