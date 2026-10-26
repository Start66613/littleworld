/* Little World V4 diagnostics report builder. Platform neutral.
 * SPDX-License-Identifier: MIT */
#include "lw_diag_report.h"

#include <stdio.h>
#include <string.h>

static lw_diag_providers_t s_providers;
static bool s_registered;

/* Single error slot, written from whatever task noticed the problem. A torn
 * read would only garble a diagnostic line, but keep the copy bounded anyway. */
static char s_last_error[96];
static uint32_t s_last_error_ms;

static void copy_text(char *dst, size_t len, const char *src)
{
    if (len == 0) {
        return;
    }
    if (src == NULL) {
        snprintf(dst, len, "n/a");
        return;
    }
    snprintf(dst, len, "%s", src);
}

void lw_diag_providers(const lw_diag_providers_t *providers)
{
    if (providers == NULL) {
        memset(&s_providers, 0, sizeof(s_providers));
        s_registered = false;
        return;
    }
    s_providers = *providers;
    s_registered = true;
}

void lw_diag_note_error(const char *text, uint32_t now_ms)
{
    if (text == NULL) {
        s_last_error[0] = '\0';
        s_last_error_ms = now_ms;
        return;
    }
    snprintf(s_last_error, sizeof(s_last_error), "%s", text);
    s_last_error_ms = now_ms;
}

lw_diag_report_t lw_diag_report(void)
{
    lw_diag_report_t r;
    memset(&r, 0, sizeof(r));

    copy_text(r.firmware, sizeof(r.firmware), s_registered ? s_providers.firmware : NULL);
    copy_text(r.build_config, sizeof(r.build_config), s_registered ? s_providers.build_config : NULL);
    copy_text(r.idf, sizeof(r.idf), s_registered ? s_providers.idf : NULL);
    copy_text(r.build_time, sizeof(r.build_time), __DATE__);

    if (s_registered && s_providers.memory != NULL) {
        s_providers.memory(&r.reset_reason, &r.boot_ms, &r.heap_free, &r.heap_min_free, &r.heap_largest,
                           &r.psram_free, &r.psram_largest);
    }

    if (s_registered && s_providers.lv_mem != NULL) {
        size_t used = 0;
        size_t max_used = 0;
        uint8_t frag = 0;
        if (s_providers.lv_mem(&used, &max_used, &frag)) {
            r.lv_available = true;
            r.lv_used = used;
            r.lv_max_used = max_used;
            r.lv_frag_pct = frag;
            copy_text(r.lv_note, sizeof(r.lv_note), "monitored");
        } else {
            copy_text(r.lv_note, sizeof(r.lv_note), "monitor unavailable");
        }
    } else {
        copy_text(r.lv_note, sizeof(r.lv_note), "no LVGL provider");
    }

    if (s_registered && s_providers.resources != NULL) {
        s_providers.resources(r.pack_version, sizeof(r.pack_version), r.resource_status,
                              sizeof(r.resource_status), &r.resource_reads, &r.resource_failures);
    } else {
        copy_text(r.pack_version, sizeof(r.pack_version), NULL);
        copy_text(r.resource_status, sizeof(r.resource_status), NULL);
    }
    if (r.pack_version[0] == '\0') {
        copy_text(r.pack_version, sizeof(r.pack_version), NULL);
    }
    if (r.resource_status[0] == '\0') {
        copy_text(r.resource_status, sizeof(r.resource_status), NULL);
    }

    if (s_registered && s_providers.frames != NULL) {
        s_providers.frames(&r.frame_load_failures, &r.asset_leases, &r.asset_bytes);
    }

    if (s_registered && s_providers.theme != NULL) {
        r.theme = s_providers.theme();
    } else {
        r.theme = -1;
    }

    if (s_registered && s_providers.cache != NULL) {
        s_providers.cache(&r.cache_available, &r.cache_hits, &r.cache_misses, &r.cache_evictions,
                          &r.cache_queue_drops, &r.cache_resident, &r.cache_peak, &r.cache_budget);
    }

    if (s_registered && s_providers.clock != NULL) {
        s_providers.clock(r.clock, sizeof(r.clock));
    }
    if (r.clock[0] == '\0') {
        copy_text(r.clock, sizeof(r.clock), "n/a");
    }

    if (s_registered && s_providers.ui_assets != NULL) {
        s_providers.ui_assets(&r.ui_assets, &r.ui_asset_bytes);
    }

    if (s_registered && s_providers.animation != NULL) {
        s_providers.animation(&r.anim_sprites, &r.anim_running);
    }

    if (s_registered && s_providers.medium != NULL) {
        s_providers.medium(&r.medium_mounted, r.medium_reason, sizeof(r.medium_reason));
    }
    if (r.medium_reason[0] == '\0') {
        copy_text(r.medium_reason, sizeof(r.medium_reason), r.medium_mounted ? "mounted" : "not mounted");
    }

    if (s_registered && s_providers.wifi != NULL) {
        s_providers.wifi(&r.wifi, r.wifi_reason, sizeof(r.wifi_reason));
    } else {
        r.wifi = LW_DIAG_WIFI_ABSENT;
    }
    if (r.wifi_reason[0] == '\0') {
        copy_text(r.wifi_reason, sizeof(r.wifi_reason), "no adapter registered");
    }

    if (s_registered && s_providers.recovery != NULL) {
        s_providers.recovery(&r.safe_mode, r.recovery, sizeof(r.recovery));
    }
    if (r.recovery[0] == '\0') {
        copy_text(r.recovery, sizeof(r.recovery), "not tracked");
    }

    snprintf(r.last_error, sizeof(r.last_error), "%s", s_last_error[0] ? s_last_error : "none");
    r.last_error_ms = s_last_error_ms;
    return r;
}

static const char *wifi_text(lw_diag_wifi_t state)
{
    switch (state) {
    case LW_DIAG_WIFI_COMPILED:
        return "compiled";
    case LW_DIAG_WIFI_STARTED:
        return "started";
    case LW_DIAG_WIFI_FAILED:
        return "failed";
    case LW_DIAG_WIFI_ABSENT:
    default:
        return "absent";
    }
}

size_t lw_diag_text(const lw_diag_report_t *report, char *buf, size_t len)
{
    if (report == NULL || buf == NULL || len == 0) {
        return 0;
    }
    int written = snprintf(
        buf, len,
        "firmware: %s\n"
        "build: %s (%s, idf %s)\n"
        "reset_reason: %u\n"
        "boot_ms: %u\n"
        "heap: free=%u min=%u largest=%u\n"
        "psram: free=%u largest=%u\n"
        "lvgl: %s used=%u max=%u frag=%u%% (%s)\n"
        "pack: %s (%s)\n"
        "resources: reads=%u failures=%u\n"
        "frames: failures=%u leases=%u bytes=%u\n"
        "theme: %d\n"
        "cache: %s hits=%u misses=%u evictions=%u drops=%u resident=%u peak=%u budget=%u\n"
        "clock: %s\n"
        "ui_assets: count=%u bytes=%u\n"
        "animation: sprites=%u running=%u\n"
        "medium: %s (%s)\n"
        "wifi: %s (%s)\n"
        "recovery: %s%s\n"
        "last_error: %s @%ums\n",
        report->firmware, report->build_config, report->build_time, report->idf,
        (unsigned)report->reset_reason, (unsigned)report->boot_ms,
        (unsigned)report->heap_free, (unsigned)report->heap_min_free, (unsigned)report->heap_largest,
        (unsigned)report->psram_free, (unsigned)report->psram_largest,
        report->lv_available ? "available" : "unavailable",
        (unsigned)report->lv_used, (unsigned)report->lv_max_used, (unsigned)report->lv_frag_pct,
        report->lv_note[0] ? report->lv_note : "monitored",
        report->pack_version, report->resource_status,
        (unsigned)report->resource_reads, (unsigned)report->resource_failures,
        (unsigned)report->frame_load_failures, (unsigned)report->asset_leases, (unsigned)report->asset_bytes,
        report->theme,
        report->cache_available ? "open" : "absent", (unsigned)report->cache_hits,
        (unsigned)report->cache_misses, (unsigned)report->cache_evictions,
        (unsigned)report->cache_queue_drops, (unsigned)report->cache_resident,
        (unsigned)report->cache_peak, (unsigned)report->cache_budget, report->clock,
        report->ui_assets, (unsigned)report->ui_asset_bytes, report->anim_sprites, report->anim_running,
        report->medium_mounted ? "mounted" : "absent", report->medium_reason,
        wifi_text(report->wifi), report->wifi_reason,
        report->safe_mode ? "SAFE MODE, " : "", report->recovery,
        report->last_error, (unsigned)report->last_error_ms);
    return written < 0 ? 0 : (size_t)written;
}

/* JSON string escaping. The values are ours (versions, reasons we wrote), but a
 * resource status can carry a path from the card, so do not assume it is clean. */
static size_t json_escape(const char *src, char *dst, size_t len)
{
    size_t out = 0;
    if (len == 0) {
        return 0;
    }
    for (const char *p = src; *p != '\0' && out + 2 < len; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            dst[out++] = '\\';
            dst[out++] = (char)c;
        } else if (c < 0x20) {
            if (out + 6 >= len) {
                break;
            }
            out += (size_t)snprintf(dst + out, len - out, "\\u%04x", (unsigned)c);
        } else {
            dst[out++] = (char)c;
        }
    }
    dst[out] = '\0';
    return out;
}

size_t lw_diag_json(const lw_diag_report_t *report, char *buf, size_t len)
{
    if (report == NULL || buf == NULL || len == 0) {
        return 0;
    }
    char firmware[80];
    char build_config[48];
    char pack_version[64];
    char resource_status[128];
    char medium_reason[128];
    char wifi_reason[128];
    char recovery[192];
    char last_error[192];
    char lv_note[96];
    char clock_text[96];
    json_escape(report->firmware, firmware, sizeof(firmware));
    json_escape(report->build_config, build_config, sizeof(build_config));
    json_escape(report->pack_version, pack_version, sizeof(pack_version));
    json_escape(report->resource_status, resource_status, sizeof(resource_status));
    json_escape(report->medium_reason, medium_reason, sizeof(medium_reason));
    json_escape(report->wifi_reason, wifi_reason, sizeof(wifi_reason));
    json_escape(report->recovery, recovery, sizeof(recovery));
    json_escape(report->last_error, last_error, sizeof(last_error));
    json_escape(report->lv_note, lv_note, sizeof(lv_note));
    json_escape(report->clock, clock_text, sizeof(clock_text));

    int written = snprintf(
        buf, len,
        "{\"firmware\":\"%s\",\"build\":\"%s\",\"idf\":\"%s\",\"reset_reason\":%u,\"boot_ms\":%u,"
        "\"heap_free\":%u,\"heap_min\":%u,\"heap_largest\":%u,\"psram_free\":%u,\"psram_largest\":%u,"
        "\"lv_available\":%s,\"lv_used\":%u,\"lv_max\":%u,\"lv_frag_pct\":%u,\"lv_note\":\"%s\","
        "\"pack\":\"%s\",\"resource\":\"%s\","
        "\"resource_reads\":%u,\"resource_failures\":%u,\"frame_failures\":%u,\"asset_leases\":%u,"
        "\"asset_bytes\":%u,\"theme\":%d,"
        "\"cache_open\":%s,\"cache_hits\":%u,\"cache_misses\":%u,\"cache_evictions\":%u,"
        "\"cache_drops\":%u,\"cache_resident\":%u,\"cache_peak\":%u,\"cache_budget\":%u,"
        "\"clock\":\"%s\",\"ui_assets\":%u,\"ui_asset_bytes\":%u,"
        "\"anim_sprites\":%u,\"anim_running\":%u,"
        "\"medium\":%s,\"medium_reason\":\"%s\",\"wifi\":\"%s\","
        "\"wifi_reason\":\"%s\",\"safe_mode\":%s,\"recovery\":\"%s\","
        "\"last_error\":\"%s\",\"last_error_ms\":%u}",
        firmware, build_config, report->idf, (unsigned)report->reset_reason, (unsigned)report->boot_ms,
        (unsigned)report->heap_free, (unsigned)report->heap_min_free, (unsigned)report->heap_largest,
        (unsigned)report->psram_free, (unsigned)report->psram_largest,
        report->lv_available ? "true" : "false",
        (unsigned)report->lv_used, (unsigned)report->lv_max_used, (unsigned)report->lv_frag_pct, lv_note,
        pack_version, resource_status,
        (unsigned)report->resource_reads, (unsigned)report->resource_failures,
        (unsigned)report->frame_load_failures, (unsigned)report->asset_leases, (unsigned)report->asset_bytes,
        report->theme,
        report->cache_available ? "true" : "false", (unsigned)report->cache_hits,
        (unsigned)report->cache_misses, (unsigned)report->cache_evictions,
        (unsigned)report->cache_queue_drops, (unsigned)report->cache_resident,
        (unsigned)report->cache_peak, (unsigned)report->cache_budget, clock_text,
        report->ui_assets, (unsigned)report->ui_asset_bytes, report->anim_sprites, report->anim_running,
        report->medium_mounted ? "true" : "false", medium_reason,
        wifi_text(report->wifi), wifi_reason, report->safe_mode ? "true" : "false", recovery,
        last_error, (unsigned)report->last_error_ms);
    if (written < 0 || (size_t)written >= len) {
        if (len > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    return (size_t)written;
}
