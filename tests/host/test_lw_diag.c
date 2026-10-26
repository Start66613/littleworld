/* Host tests for the V4 diagnostics report builder.
 *
 * The builder is the only part of the diagnostics system that can run off the
 * board, so it carries the contract: a snapshot with no providers must still be
 * valid and honest ("n/a", wifi absent), every provider must be able to fill
 * its own section, the text block must be parseable, and the JSON must survive
 * quotes, backslashes and control characters coming from a card path.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_diag_report.h"

#include <stdio.h>
#include <string.h>

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        s_checks++;                                                                     \
        if (!(cond)) {                                                                  \
            s_failures++;                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                               \
    } while (0)

#define CHECK_STR_CONTAINS(haystack, needle)                                            \
    do {                                                                                \
        s_checks++;                                                                     \
        if (strstr((haystack), (needle)) == NULL) {                                      \
            s_failures++;                                                               \
            printf("FAIL %s:%d: '%s' not in '%s'\n", __FILE__, __LINE__, (needle), (haystack)); \
        }                                                                               \
    } while (0)

#define CHECK_STR_EQUAL(a, b)                                                           \
    do {                                                                                \
        s_checks++;                                                                     \
        if (strcmp((a), (b)) != 0) {                                                     \
            s_failures++;                                                               \
            printf("FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__, (a), (b));          \
        }                                                                               \
    } while (0)

/* ---------------------------------------------------------- fake providers -- */

static void fake_memory(uint32_t *reset_reason, uint32_t *boot_ms, size_t *heap_free, size_t *heap_min,
                        size_t *heap_largest, size_t *psram_free, size_t *psram_largest)
{
    *reset_reason = 3;
    *boot_ms = 8123;
    *heap_free = 200000;
    *heap_min = 150000;
    *heap_largest = 65536;
    *psram_free = 27000000;
    *psram_largest = 20000000;
}

static bool fake_lv(size_t *used, size_t *max_used, uint8_t *frag)
{
    *used = 40960;
    *max_used = 65536;
    *frag = 17;
    return true;
}

static void fake_resources(char *version, size_t version_len, char *status, size_t status_len,
                           uint32_t *reads, uint32_t *failures)
{
    snprintf(version, version_len, "lw4.1-2026.10");
    snprintf(status, status_len, "verified \"v4\" root=/sdcard/littleworld");
    *reads = 1234;
    *failures = 7;
}

static void fake_frames(uint32_t *failures, uint32_t *leases, size_t *bytes)
{
    *failures = 3;
    *leases = 2;
    *bytes = 442368;
}

static int fake_theme(void) { return 2; }

static void fake_cache(bool *available, uint32_t *hits, uint32_t *misses, uint32_t *evictions,
                       uint32_t *queue_drops, size_t *resident, size_t *peak, size_t *budget)
{
    *available = true;
    *hits = 900;
    *misses = 40;
    *evictions = 6;
    *queue_drops = 2;
    *resident = 884736;
    *peak = 1048576;
    *budget = 1048576;
}

static void fake_clock(char *text, size_t len)
{
    snprintf(text, len, "19:30 demo, not trusted, night, theme 1");
}

static void fake_ui_assets(unsigned *count, size_t *bytes)
{
    *count = 5;
    *bytes = 12288;
}

static void fake_animation(unsigned *sprites, unsigned *running)
{
    *sprites = 2;
    *running = 2;
}

static void fake_medium(bool *mounted, char *reason, size_t len)
{
    *mounted = true;
    snprintf(reason, len, "sdcard at /sdcard");
}

static void fake_wifi(lw_diag_wifi_t *state, char *reason, size_t len)
{
    *state = LW_DIAG_WIFI_STARTED;
    snprintf(reason, len, "hosted esp32c6 fw 1.4.7");
}

static void fake_wifi_failed(lw_diag_wifi_t *state, char *reason, size_t len)
{
    *state = LW_DIAG_WIFI_FAILED;
    snprintf(reason, len, "esp_wifi_start timeout after 5000 ms");
}

static void fake_recovery_clean(bool *safe_mode, char *text, size_t len)
{
    *safe_mode = false;
    snprintf(text, len, "boot 12, previous power-on, streak 0");
}

static void fake_recovery_degraded(bool *safe_mode, char *text, size_t len)
{
    *safe_mode = true;
    snprintf(text, len, "previous task_wdt after 4200 ms, streak 2");
}

/* ------------------------------------------------------------------ tests -- */

static void test_defaults_without_providers(void)
{
    lw_diag_providers(NULL);
    lw_diag_report_t r = lw_diag_report();
    /* With no provider the report must not invent values. */
    CHECK_STR_EQUAL(r.firmware, "n/a");
    CHECK_STR_EQUAL(r.build_config, "n/a");
    CHECK_STR_EQUAL(r.idf, "n/a");
    CHECK(r.lv_available == false);
    CHECK(r.lv_used == 0u);
    CHECK_STR_EQUAL(r.lv_note, "no LVGL provider");
    CHECK_STR_EQUAL(r.pack_version, "n/a");
    CHECK_STR_EQUAL(r.resource_status, "n/a");
    CHECK(r.theme == -1);
    CHECK(r.medium_mounted == false);
    CHECK(r.wifi == LW_DIAG_WIFI_ABSENT);
    CHECK_STR_EQUAL(r.last_error, "none");
    /* Sections without a provider must admit they are unknown, not report zero
     * as if a cache existed with no hits. */
    CHECK(r.cache_available == false);
    CHECK_STR_EQUAL(r.clock, "n/a");
    CHECK(r.ui_assets == 0u);
    CHECK(r.anim_sprites == 0u);
    CHECK(r.build_time[0] != '\0');   /* __DATE__ is always present */
    /* No memory provider: the allocator numbers must be zero, not garbage. */
    CHECK(r.reset_reason == 0u);
    CHECK(r.heap_free == 0u);
    CHECK(r.psram_free == 0u);
}

static void test_full_providers(void)
{
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.firmware = "4.1.0-dsh";
    p.build_config = "os";
    p.idf = "v5.5.5";
    p.memory = fake_memory;
    p.lv_mem = fake_lv;
    p.resources = fake_resources;
    p.frames = fake_frames;
    p.theme = fake_theme;
    p.cache = fake_cache;
    p.clock = fake_clock;
    p.ui_assets = fake_ui_assets;
    p.animation = fake_animation;
    p.medium = fake_medium;
    p.wifi = fake_wifi;
    p.recovery = fake_recovery_clean;
    lw_diag_providers(&p);

    lw_diag_note_error("frame 5 of qianqian-wave.lwp read short", 4242);
    lw_diag_report_t r = lw_diag_report();

    CHECK_STR_EQUAL(r.firmware, "4.1.0-dsh");
    CHECK_STR_EQUAL(r.build_config, "os");
    CHECK_STR_EQUAL(r.idf, "v5.5.5");
    CHECK(r.reset_reason == 3u);
    CHECK(r.boot_ms == 8123u);
    CHECK(r.heap_free == 200000u);
    CHECK(r.heap_min_free == 150000u);
    CHECK(r.heap_largest == 65536u);
    CHECK(r.psram_free == 27000000u);
    CHECK(r.psram_largest == 20000000u);
    CHECK(r.lv_available);
    CHECK(r.lv_used == 40960u);
    CHECK(r.lv_max_used == 65536u);
    CHECK(r.lv_frag_pct == 17u);
    CHECK_STR_EQUAL(r.lv_note, "monitored");
    CHECK_STR_EQUAL(r.pack_version, "lw4.1-2026.10");
    CHECK(r.resource_reads == 1234u);
    CHECK(r.resource_failures == 7u);
    CHECK(r.frame_load_failures == 3u);
    CHECK(r.asset_leases == 2u);
    CHECK(r.asset_bytes == 442368u);
    CHECK(r.theme == 2);
    CHECK(r.cache_available);
    CHECK(r.cache_hits == 900u);
    CHECK(r.cache_misses == 40u);
    CHECK(r.cache_evictions == 6u);
    CHECK(r.cache_queue_drops == 2u);
    CHECK(r.cache_resident == 884736u);
    CHECK(r.cache_peak == 1048576u);
    CHECK(r.cache_budget == 1048576u);
    CHECK_STR_EQUAL(r.clock, "19:30 demo, not trusted, night, theme 1");
    CHECK(r.ui_assets == 5u);
    CHECK(r.ui_asset_bytes == 12288u);
    CHECK(r.anim_sprites == 2u);
    CHECK(r.anim_running == 2u);
    CHECK(r.medium_mounted);
    CHECK_STR_EQUAL(r.medium_reason, "sdcard at /sdcard");
    CHECK(r.wifi == LW_DIAG_WIFI_STARTED);
    CHECK_STR_EQUAL(r.wifi_reason, "hosted esp32c6 fw 1.4.7");
    CHECK(r.safe_mode == false);
    CHECK_STR_EQUAL(r.recovery, "boot 12, previous power-on, streak 0");
    CHECK_STR_EQUAL(r.last_error, "frame 5 of qianqian-wave.lwp read short");
    CHECK(r.last_error_ms == 4242u);
}

static void test_recovery_degraded_is_reported(void)
{
    /* Safe mode is the one state a field report must never hide: the board is
     * up but degraded, so the text block and the JSON both have to say so. */
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.recovery = fake_recovery_degraded;
    lw_diag_providers(&p);
    lw_diag_report_t r = lw_diag_report();
    CHECK(r.safe_mode);
    CHECK_STR_EQUAL(r.recovery, "previous task_wdt after 4200 ms, streak 2");

    char buf[1024];
    lw_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_CONTAINS(buf, "recovery: SAFE MODE, previous task_wdt");
    CHECK_STR_CONTAINS(buf, "streak 2");

    char json[2048];
    CHECK(lw_diag_json(&r, json, sizeof(json)) > 0u);
    CHECK_STR_CONTAINS(json, "\"safe_mode\":true");
    CHECK_STR_CONTAINS(json, "\"recovery\":\"previous task_wdt");

    /* And the clean case must not leak the SAFE MODE prefix. */
    memset(&p, 0, sizeof(p));
    p.recovery = fake_recovery_clean;
    lw_diag_providers(&p);
    r = lw_diag_report();
    lw_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_CONTAINS(buf, "recovery: boot 12");
    CHECK(strstr(buf, "SAFE MODE") == NULL);
    CHECK(lw_diag_json(&r, json, sizeof(json)) > 0u);
    CHECK_STR_CONTAINS(json, "\"safe_mode\":false");
}

static void test_recovery_without_provider(void)
{
    /* A build without the recovery path must say "not tracked", not "safe". */
    lw_diag_providers(NULL);
    lw_diag_report_t r = lw_diag_report();
    CHECK(r.safe_mode == false);
    CHECK_STR_EQUAL(r.recovery, "not tracked");
}

static void test_lv_unavailable_is_reported(void)
{
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.firmware = "4.1.0-dsh";
    p.lv_mem = NULL;   /* e.g. called from a non-LVGL task */
    lw_diag_providers(&p);
    lw_diag_report_t r = lw_diag_report();
    CHECK(r.lv_available == false);
    CHECK_STR_EQUAL(r.lv_note, "no LVGL provider");
}

static bool fake_lv_unavailable(size_t *used, size_t *max_used, uint8_t *frag)
{
    (void)used;
    (void)max_used;
    (void)frag;
    return false;
}

static void test_lv_provider_refuses(void)
{
    /* LVGL built on the C library allocator: lv_mem_monitor() reports zeros and
     * the provider must say "no" rather than let a zero pass as a measurement. */
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.lv_mem = fake_lv_unavailable;
    lw_diag_providers(&p);
    lw_diag_report_t r = lw_diag_report();
    CHECK(r.lv_available == false);
    CHECK_STR_EQUAL(r.lv_note, "monitor unavailable");

    char buf[1024];
    lw_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_CONTAINS(buf, "lvgl: unavailable");
    CHECK_STR_CONTAINS(buf, "monitor unavailable");

    char json[2048];
    CHECK(lw_diag_json(&r, json, sizeof(json)) > 0u);
    CHECK_STR_CONTAINS(json, "\"lv_available\":false");
    CHECK_STR_CONTAINS(json, "monitor unavailable");
}

static void test_wifi_failure_reason(void)
{
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.wifi = fake_wifi_failed;
    lw_diag_providers(&p);
    lw_diag_report_t r = lw_diag_report();
    CHECK(r.wifi == LW_DIAG_WIFI_FAILED);
    CHECK_STR_CONTAINS(r.wifi_reason, "timeout");
}

static void test_text_block(void)
{
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.firmware = "4.1.0-dsh";
    p.build_config = "wifi";
    p.idf = "v5.5.5";
    p.memory = fake_memory;
    p.lv_mem = fake_lv;
    p.resources = fake_resources;
    p.frames = fake_frames;
    p.theme = fake_theme;
    p.medium = fake_medium;
    p.wifi = fake_wifi;
    p.recovery = fake_recovery_clean;
    lw_diag_providers(&p);
    lw_diag_note_error("none", 0);
    lw_diag_report_t r = lw_diag_report();

    char buf[1024];
    size_t n = lw_diag_text(&r, buf, sizeof(buf));
    CHECK(n > 0u);
    CHECK(n < sizeof(buf));
    CHECK(strlen(buf) == n);
    CHECK_STR_CONTAINS(buf, "firmware: 4.1.0-dsh");
    CHECK_STR_CONTAINS(buf, "build: wifi");
    CHECK_STR_CONTAINS(buf, "reset_reason: ");
    CHECK_STR_CONTAINS(buf, "heap: free=");
    CHECK_STR_CONTAINS(buf, "psram: free=");
    CHECK_STR_CONTAINS(buf, "lvgl: available");
    CHECK_STR_CONTAINS(buf, "pack: lw4.1-2026.10");
    CHECK_STR_CONTAINS(buf, "frames: failures=3");
    CHECK_STR_CONTAINS(buf, "theme: 2");
    CHECK_STR_CONTAINS(buf, "medium: mounted");
    CHECK_STR_CONTAINS(buf, "wifi: started");
    CHECK_STR_CONTAINS(buf, "last_error: none");
}

static void test_text_truncation(void)
{
    lw_diag_providers(NULL);
    lw_diag_report_t r = lw_diag_report();
    char small[32];
    size_t n = lw_diag_text(&r, small, sizeof(small));
    /* snprintf semantics: the return is what would have been written. */
    CHECK(n >= sizeof(small));
    CHECK(strlen(small) == sizeof(small) - 1);
}

static void test_json_roundtrip(void)
{
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.firmware = "4.1.0-dsh";
    p.build_config = "os";
    p.idf = "v5.5.5";
    p.memory = fake_memory;
    p.lv_mem = fake_lv;
    p.resources = fake_resources;   /* contains embedded quotes on purpose */
    p.frames = fake_frames;
    p.theme = fake_theme;
    p.cache = fake_cache;
    p.clock = fake_clock;
    p.ui_assets = fake_ui_assets;
    p.animation = fake_animation;
    p.medium = fake_medium;
    p.wifi = fake_wifi;
    p.recovery = fake_recovery_clean;
    lw_diag_providers(&p);
    lw_diag_note_error("bad \"frame\" \\ path", 99);
    lw_diag_report_t r = lw_diag_report();

    char buf[2048];
    size_t n = lw_diag_json(&r, buf, sizeof(buf));
    CHECK(n > 0u);
    CHECK(strlen(buf) == n);
    CHECK(buf[0] == '{');
    CHECK(buf[n - 1] == '}');
    /* The quotes from the resource status and the error must be escaped. */
    CHECK_STR_CONTAINS(buf, "\\\"v4\\\"");
    CHECK_STR_CONTAINS(buf, "\\\"frame\\\"");
    CHECK_STR_CONTAINS(buf, "\"firmware\":\"4.1.0-dsh\"");
    CHECK_STR_CONTAINS(buf, "\"medium\":true");
    CHECK_STR_CONTAINS(buf, "\"theme\":2");
    CHECK_STR_CONTAINS(buf, "\"cache_open\":true");
    CHECK_STR_CONTAINS(buf, "\"cache_hits\":900");
    CHECK_STR_CONTAINS(buf, "\"clock\":\"19:30 demo, not trusted, night, theme 1\"");
    CHECK_STR_CONTAINS(buf, "\"anim_running\":2");
    CHECK_STR_CONTAINS(buf, "\"wifi\":\"started\"");
    /* No raw newline may appear inside the object: it must stay one line. */
    CHECK(strchr(buf, '\n') == NULL);
    /* Balanced braces: exactly one open at the start, one close at the end. */
    int opens = 0;
    int closes = 0;
    for (const char *c = buf; *c; c++) {
        if (*c == '{') { opens++; }
        if (*c == '}') { closes++; }
    }
    CHECK(opens == 1);
    CHECK(closes == 1);
}

static void test_json_small_buffer_refuses(void)
{
    lw_diag_providers(NULL);
    lw_diag_report_t r = lw_diag_report();
    char tiny[16];
    size_t n = lw_diag_json(&r, tiny, sizeof(tiny));
    CHECK(n == 0u);
    CHECK(tiny[0] == '\0');
}

static void test_control_characters_escaped(void)
{
    lw_diag_providers_t p;
    memset(&p, 0, sizeof(p));
    p.firmware = "a\tb";   /* a tab must become \u0009, never a raw byte */
    lw_diag_providers(&p);
    lw_diag_report_t r = lw_diag_report();
    char buf[1024];
    size_t n = lw_diag_json(&r, buf, sizeof(buf));
    CHECK(n > 0u);
    CHECK_STR_CONTAINS(buf, "\\u0009");
    CHECK(strchr(buf, '\t') == NULL);
}

static void test_error_slot_overwrite(void)
{
    lw_diag_providers(NULL);
    lw_diag_note_error("first", 10);
    lw_diag_report_t r1 = lw_diag_report();
    CHECK_STR_EQUAL(r1.last_error, "first");
    lw_diag_note_error("second", 20);
    lw_diag_report_t r2 = lw_diag_report();
    CHECK_STR_EQUAL(r2.last_error, "second");
    CHECK(r2.last_error_ms == 20u);
    lw_diag_note_error(NULL, 30);
    lw_diag_report_t r3 = lw_diag_report();
    CHECK_STR_EQUAL(r3.last_error, "none");
    CHECK(r3.last_error_ms == 30u);
}

static void test_long_error_is_truncated_not_overflowed(void)
{
    lw_diag_providers(NULL);
    char long_text[400];
    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    lw_diag_note_error(long_text, 1);
    lw_diag_report_t r = lw_diag_report();
    CHECK(strlen(r.last_error) == sizeof(r.last_error) - 1);
    lw_diag_note_error("", 0);
}

int main(void)
{
    test_defaults_without_providers();
    test_full_providers();
    test_lv_unavailable_is_reported();
    test_lv_provider_refuses();
    test_wifi_failure_reason();
    test_recovery_degraded_is_reported();
    test_recovery_without_provider();
    test_text_block();
    test_text_truncation();
    test_json_roundtrip();
    test_json_small_buffer_refuses();
    test_control_characters_escaped();
    test_error_slot_overwrite();
    test_long_error_is_truncated_not_overflowed();

    printf("\n%d checks, %d failure(s)\n", s_checks, s_failures);
    if (s_failures != 0) {
        printf("DIAG REPORT TESTS FAILED\n");
        return 1;
    }
    printf("DIAG REPORT TESTS PASSED\n");
    return 0;
}
