/* Little World V4 diagnostics: one snapshot of everything a board test needs.
 *
 * This file is deliberately platform-neutral. It never touches a driver and
 * never calls LVGL: every subsystem-specific value arrives through a provider
 * callback registered by the boot layer. That keeps the module compilable (and
 * therefore testable) on the build machine, where none of those subsystems
 * exist, and it keeps lw_backend free of a dependency on lw_ui / lw_storage /
 * lw_net, which would otherwise be a cycle.
 *
 * Nothing here prints on its own. The periodic log is off unless
 * CONFIG_LW_DIAG_PERIODIC_LOG is set, because a 20 ms log would cost more
 * frame time than the diagnostics are worth.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi backend state, as far as the diagnostics are concerned. The distinction
 * matters: "compiled in" is not "started", and "started" is not "connected". */
typedef enum {
    LW_DIAG_WIFI_ABSENT = 0,   /* no radio stack in this build */
    LW_DIAG_WIFI_COMPILED,     /* stack linked, station not started yet */
    LW_DIAG_WIFI_STARTED,      /* esp_wifi_start() succeeded */
    LW_DIAG_WIFI_FAILED        /* the stack refused to start */
} lw_diag_wifi_t;

/* Fixed-size on purpose: the report is copied into a console buffer and into a
 * JSON line, so it must not own heap. */
typedef struct {
    char firmware[40];      /* application version string from the image */
    char build_config[24];  /* "os" / "wifi" / "legacy" / ... */
    char build_time[24];    /* compile date, for telling two builds apart */
    char idf[24];

    uint32_t reset_reason;
    uint32_t boot_ms;

    size_t heap_free;
    size_t heap_min_free;
    size_t heap_largest;
    size_t psram_free;
    size_t psram_largest;

    bool lv_available;
    size_t lv_used;
    size_t lv_max_used;
    uint8_t lv_frag_pct;
    /* Why the LVGL numbers are missing, when they are. LVGL can be built
     * against the C library allocator, and then lv_mem_monitor() is a no-op
     * that reports zeros - printing those zeros as "0 B used" would be a lie. */
    char lv_note[56];

    char pack_version[32];
    char resource_status[64];
    uint32_t resource_reads;
    uint32_t resource_failures;
    uint32_t frame_load_failures;
    uint32_t asset_leases;
    size_t asset_bytes;

    int theme;

    bool medium_mounted;
    char medium_reason[64];

    lw_diag_wifi_t wifi;
    char wifi_reason[64];

    char last_error[96];
    uint32_t last_error_ms;
} lw_diag_report_t;

/* Everything the boot layer knows and this module cannot. Any member may be
 * NULL; the report then says "n/a" instead of inventing a value. */
typedef struct {
    const char *firmware;
    const char *build_config;
    const char *idf;
    /* Reset reason, uptime and the allocator counters. */
    void (*memory)(uint32_t *reset_reason, uint32_t *boot_ms, size_t *heap_free, size_t *heap_min,
                   size_t *heap_largest, size_t *psram_free, size_t *psram_largest);
    /* LVGL heap, only valid on the LVGL thread. */
    bool (*lv_mem)(size_t *used, size_t *max_used, uint8_t *frag_pct);
    /* Resource pack: version string plus read/failure counters. */
    void (*resources)(char *version, size_t version_len, char *status, size_t status_len,
                      uint32_t *reads, uint32_t *failures);
    /* Character/animation frame loading. */
    void (*frames)(uint32_t *failures, uint32_t *leases, size_t *bytes);
    /* Current theme index. */
    int (*theme)(void);
    /* Storage medium: mounted plus a human-readable reason when it is not. */
    void (*medium)(bool *mounted, char *reason, size_t reason_len);
    /* Wi-Fi: state plus the adapter's own explanation. */
    void (*wifi)(lw_diag_wifi_t *state, char *reason, size_t reason_len);
} lw_diag_providers_t;

/* Registers the providers. Call once, before the first lw_diag_report(). The
 * struct is copied, but the strings it points at must outlive the session. */
void lw_diag_providers(const lw_diag_providers_t *providers);

/* Fills a complete snapshot. Cheap enough for a console command (a few
 * heap_caps_get_info calls); never call it from a per-frame path. */
lw_diag_report_t lw_diag_report(void);

/* Records the most recent error. Kept as a single slot: the point is to know
 * what went wrong last, not to build a log inside RAM. Safe to call from any
 * task. */
void lw_diag_note_error(const char *text, uint32_t now_ms);

/* Human-readable block, one "key: value" per line. Returns the number of bytes
 * that would have been written (like snprintf). */
size_t lw_diag_text(const lw_diag_report_t *report, char *buf, size_t len);

/* One-line JSON object, for the PC collection script. Returns bytes written,
 * or 0 when the buffer is too small. Escapes what it must; the values it emits
 * are all ASCII by construction. */
size_t lw_diag_json(const lw_diag_report_t *report, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
