/* Startup orchestration for the ESP32-P4 build.
 *
 * This file owns the decision between the integrated OS V2 desktop and the
 * legacy application shell, the platform storage hooks shared by both, and the
 * real persistence adapters. UI internals stay in lw_ui.
 *
 * SPDX-License-Identifier: MIT */
#include <string.h>

#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
/* uxTaskGetStackHighWaterMark() below turns the boot build's stack budget into
 * one log line per phase instead of a guess after a panic. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lw/lw_platform.h"
#include "lw_hw.h"
#include "lw_recovery.h"
#include "lw_services.h"
#include "lw_store_nvs.h"
#include "lw_ui.h"

#if CONFIG_LW_BOOT_MODE_OS_V2
#include "lw_content_boot.h"
#include "lw_time_boot.h"
#include "lw_ui_clock.h"
#include "lw_diag.h"
#include "lw_diag_board.h"
#include "lvgl.h"
#include "lw_art.h"
#include "lw_os.h"
#include "lw_resource_mount.h"
#include "lw_store.h"
#endif

static const char *TAG = "littleworld";

/* ---------------------------------------------------------- recovery ----- */

/* RTC no-init memory: survives a panic or a watchdog reset, cleared by a power
 * cycle. That is the exact lifetime a boot-loop counter needs, and unlike NVS it
 * costs no flash write on a board that may be crashing before NVS is ready. */
static RTC_NOINIT_ATTR lw_recovery_t s_recovery_record;
static lw_recovery_decision_t s_recovery;

/* Static-assert the reset-reason table against the real header: if ESP-IDF ever
 * renumbers it, the policy would silently misclassify a crash as a clean boot,
 * which is the one failure that makes safe mode useless. */
_Static_assert((int)LW_RESET_PANIC == (int)ESP_RST_PANIC, "reset reason table out of sync");
_Static_assert((int)LW_RESET_TASK_WDT == (int)ESP_RST_TASK_WDT, "reset reason table out of sync");
_Static_assert((int)LW_RESET_BROWNOUT == (int)ESP_RST_BROWNOUT, "reset reason table out of sync");
_Static_assert((int)LW_RESET_POWERON == (int)ESP_RST_POWERON, "reset reason table out of sync");

static void recovery_begin(void)
{
    s_recovery = lw_recovery_begin(&s_recovery_record, (uint32_t)esp_reset_reason());
    char line[192];
    lw_recovery_describe(&s_recovery_record, &s_recovery, line, sizeof(line));
    if (s_recovery.safe_mode) {
        ESP_LOGW(TAG, "%s", line);
        ESP_LOGW(TAG, "recovery: degraded boot, previous uptime %u ms",
                 (unsigned)s_recovery.previous_uptime_ms);
    } else {
        ESP_LOGI(TAG, "%s", line);
    }
}

/* Keeps the record's uptime current so the next boot can say how long the
 * crashed run survived. One RTC write per second, no flash. */
static void recovery_tick_cb(void *arg)
{
    (void)arg;
    lw_recovery_tick(&s_recovery_record, (uint32_t)(esp_timer_get_time() / 1000));
}

/* ------------------------------------------------------------ platform --- */

extern const uint8_t characters_lwfa_start[] asm("_binary_characters_lwfa_start");
extern const uint8_t characters_lwfa_end[] asm("_binary_characters_lwfa_end");

static bool platform_get_time(struct tm *out)
{
#if CONFIG_LW_BOOT_MODE_OS_V2
    /* The effective clock lives in lw_time_boot: it answers only when the time
     * is REAL or MANUAL, so the UI's demo fallback stays honest. */
    return lw_time_boot_get_tm(out);
#else
    (void)out;
    return false;
#endif
}

static bool platform_storage_read(const char *key, void *buffer, size_t *length)
{
    nvs_handle_t handle;
    if (nvs_open("littleworld", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t size = *length;
    esp_err_t err = nvs_get_blob(handle, key, buffer, &size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }
    *length = size;
    return true;
}

static bool platform_storage_write(const char *key, const void *buffer, size_t length)
{
    nvs_handle_t handle;
    if (nvs_open("littleworld", NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, key, buffer, length);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

static void platform_log(const char *message)
{
    ESP_LOGI(TAG, "%s", message);
}

static uint32_t platform_uptime(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static const lw_platform_t s_platform = {
    .get_time = platform_get_time,
    .storage_read = platform_storage_read,
    .storage_write = platform_storage_write,
    .uptime_seconds = platform_uptime,
    .log = platform_log,
};

#if CONFIG_LW_BOOT_MODE_OS_V2

/* ------------------------------------------------------------ storage --- */

/* Separate namespaces: the legacy "littleworld" blob keeps the pet and the
 * anniversary records exactly as they were written by earlier builds. */
static const lw_store_nvs_key_t s_prefs_key = {"lwo2.sys", "prefs"};
static const lw_store_nvs_key_t s_rpg_key = {"lwo2.rpg", "save"};

#define LW_KIND_PREFS 1u
#define LW_KIND_RPG 2u
#define LW_RECORD_VERSION 1u

static lw_store_io_t s_prefs_io;
static lw_store_io_t s_rpg_io;

static bool prefs_load(void *context, uint8_t out[LW_SYS_PREF_BYTES])
{
    (void)context;
    lw_store_status_t status = lw_store_load(&s_prefs_io, LW_KIND_PREFS, LW_RECORD_VERSION, out,
                                            LW_SYS_PREF_BYTES);
    if (status != LW_STORE_OK) {
        ESP_LOGW(TAG, "prefs: %s, using defaults", lw_store_status_name(status));
    }
    return status == LW_STORE_OK;
}

static bool prefs_store(void *context, const uint8_t bytes[LW_SYS_PREF_BYTES])
{
    (void)context;
    lw_store_status_t status = lw_store_save(&s_prefs_io, LW_KIND_PREFS, LW_RECORD_VERSION, bytes,
                                             LW_SYS_PREF_BYTES);
    if (status != LW_STORE_OK) {
        /* The service keeps its dirty flag so the next attempt can retry. */
        ESP_LOGW(TAG, "prefs: save failed (%s)", lw_store_status_name(status));
        return false;
    }
    return true;
}

static bool rpg_load(void *context, uint8_t out[LW_RPG_SAVE_BYTES])
{
    (void)context;
    lw_store_status_t status = lw_store_load(&s_rpg_io, LW_KIND_RPG, LW_RECORD_VERSION, out,
                                             LW_RPG_SAVE_BYTES);
    if (status != LW_STORE_OK && status != LW_STORE_MISSING) {
        ESP_LOGW(TAG, "rpg: %s, starting without a save", lw_store_status_name(status));
    }
    return status == LW_STORE_OK;
}

static bool rpg_store(void *context, const uint8_t bytes[LW_RPG_SAVE_BYTES])
{
    (void)context;
    lw_store_status_t status = lw_store_save(&s_rpg_io, LW_KIND_RPG, LW_RECORD_VERSION, bytes,
                                             LW_RPG_SAVE_BYTES);
    if (status != LW_STORE_OK) {
        ESP_LOGW(TAG, "rpg: save failed (%s)", lw_store_status_name(status));
        return false;
    }
    return true;
}

static void rpg_reward(void *context, uint32_t id)
{
    (void)context;
    (void)id;
}

/* --------------------------------------------------------- services ----- */

static lw_system_t s_system;
static lw_services_t s_services;
static esp_timer_handle_t s_net_timer;

/* Connectivity poll: runs off the LVGL thread so a slow scan can never stall a
 * frame. The models are lock-free by construction (the UI only reads a
 * snapshot), so this stays a plain timer callback. */
static void net_poll(void *arg)
{
    (void)arg;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    lw_services_poll(&s_services, now_ms);
}

static void start_connectivity(uint32_t now_ms, bool codec)
{
    lw_services_init(&s_services);
    lw_services_start(&s_services, now_ms);
    lw_services_set_audio_backend(&s_services, codec);

    lw_net_status_t status;
    lw_services_status(&s_services, now_ms, &status);
    ESP_LOGI(TAG, "connectivity: wifi=%s time=%s audio=%s",
             status.wifi_backend ? "backend" : "no-backend",
             lw_time_state_name(status.time_state),
             status.audio_backend ? "codec" : "no-codec");

    if (s_net_timer != NULL) {
        return;
    }
    const esp_timer_create_args_t args = {
        .callback = net_poll,
        .name = "lw_net_poll",
    };
    if (esp_timer_create(&args, &s_net_timer) == ESP_OK) {
        esp_timer_start_periodic(s_net_timer, 500000);
    }
}

/* ------------------------------------------------- UI capability bridge -- */

/* The OS asks what the hardware can actually do. Only this file knows: the
 * board support package reports the panel/codec, the service layer reports the
 * radio. The UI must never infer a capability from a failed call, so the values
 * are pushed explicitly, and only from the LVGL task (or before the UI exists). */
static lw_hw_caps_t s_hw_caps;
static lw_os_capabilities_t s_published_caps;
static bool s_caps_published;

static lw_os_capabilities_t build_capabilities(void)
{
    lw_os_capabilities_t caps;
    caps.brightness = s_hw_caps.brightness ? LW_SERVICE_READY : LW_SERVICE_UNAVAILABLE;
    caps.volume = s_hw_caps.audio ? LW_SERVICE_READY : LW_SERVICE_UNAVAILABLE;
    caps.wifi = s_services.caps.wifi_backend ? LW_SERVICE_READY : LW_SERVICE_UNAVAILABLE;
    caps.audio = LW_SERVICE_UNAVAILABLE;   /* no PCM source or decoder in this round */
    caps.storage = LW_SERVICE_UNAVAILABLE; /* the TF card is out of scope */
    return caps;
}

static bool caps_differ(const lw_os_capabilities_t *a, const lw_os_capabilities_t *b)
{
    return memcmp(a, b, sizeof(*a)) != 0;
}

/* LVGL task boundary: the timer below is the only place that publishes a change
 * after the UI is running. */
static void publish_capabilities(void)
{
    lw_os_capabilities_t caps = build_capabilities();
    if (s_caps_published && !caps_differ(&caps, &s_published_caps)) {
        return;
    }
    s_published_caps = caps;
    s_caps_published = true;
    lw_os_capabilities(&caps);
    ESP_LOGI(TAG, "capabilities: brightness=%d volume=%d wifi=%d audio=%d storage=%d", (int)caps.brightness,
             (int)caps.volume, (int)caps.wifi, (int)caps.audio, (int)caps.storage);
}

/* Feed the effective clock from the service snapshot. Runs on the LVGL task,
 * like publish_capabilities, because lw_time_boot_get_tm() is read from the same
 * task through lw_platform. */
static void publish_time(void)
{
    uint32_t now_ms = lw_services_now_ms();
    lw_net_status_t status;
    lw_services_status(&s_services, now_ms, &status);
    lw_time_boot_update(&status, now_ms);
}

static void caps_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    publish_capabilities();
    publish_time();
}
static bool hw_brightness(void *context, unsigned value)
{
    (void)context;
    return lw_hw_brightness_set(value);
}

static bool hw_volume(void *context, unsigned value, bool muted)
{
    (void)context;
    return lw_hw_volume_set(value, muted);
}

/* Internal-RAM head-room at a boot phase: free bytes and the largest single
 * block. Everything the console REPL needs (UART driver, its own task stack) has
 * to come out of this pool, so when the REPL fails to start these two numbers say
 * whether the pool was short or just fragmented. */
static void boot_ram_report(const char *phase)
{
    ESP_LOGI(TAG, "boot ram after %s: stack %u B free, internal %u B free, largest %u B",
             phase, (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
static void start_os(void)
{
    /* V4.4 P0-D: boot phase timing. Everything below runs inside the LVGL lock
     * and before the first frame, so the sum of these stamps is the real
     * "power-on to first frame" budget the user waits through. Printed as one
     * line at the end so a serial capture can read it without scrolling.
     * esp_timer is microseconds since boot and is already running here. */
    const int64_t t_start = esp_timer_get_time();
    int64_t t_mark = t_start;

    s_prefs_io = *lw_store_nvs_io(&s_prefs_key);
    s_rpg_io = *lw_store_nvs_io(&s_rpg_key);

#ifdef CONFIG_LW_ENABLE_AUDIO
    const bool want_audio = true;
#else
    const bool want_audio = false;
#endif
    lw_hw_caps_t caps = lw_hw_begin(want_audio);
    s_hw_caps = caps;
    const int64_t t_hw = esp_timer_get_time();
    boot_ram_report("hw");

    lw_system_backend_t backend = {0};
    backend.simulated = false;
    backend.network_available = caps.wifi;
    backend.brightness = caps.brightness ? hw_brightness : NULL;
    backend.volume = caps.audio ? hw_volume : NULL;
    backend.load = prefs_load;
    backend.store = prefs_store;
    lw_system_init(&s_system, &backend);

    start_connectivity((uint32_t)(esp_timer_get_time() / 1000), caps.audio);

    if (!caps.brightness) {
        ESP_LOGW(TAG, "brightness: %s", "backend reports Unsupported");
    }
    if (!caps.audio) {
        ESP_LOGW(TAG, "audio: %s", "ES8311 not opened; volume is a stored preference only");
    }
    ESP_LOGI(TAG, "network: %s", lw_hw_wifi_reason());
    ESP_LOGI(TAG, "time: %s", lw_hw_time_reason());

    /* The effective clock starts on the labelled demo time and is upgraded only
     * when the service reports a real sync. */
    lw_time_boot_init();
    /* Hand the one clock to the UI before the first frame: pages read it, they
     * never decide the time themselves. */
    lw_ui_clock_attach(lw_time_boot_day());

    /* The OS keeps a pointer to this descriptor for the lifetime of the
     * session, so it must not live on the stack. */
    static lw_rpg_view_host_t rpg_host = {NULL, rpg_load, rpg_store, rpg_reward};
    lw_os_rpg_storage(&rpg_host);

    lw_diag_log_boot();

    /* Safe mode exists to break a crash loop, so it has to actually change the
     * boot: the card is the only external input to the art path, and a
     * half-written or power-hungry card is a plausible crash cause. Booting
     * without it still renders the embedded characters, so a degraded desktop
     * is visible evidence that safe mode is active rather than a silent one. */
    if (s_recovery.safe_mode) {
        lw_resource_force_no_medium(true);
        ESP_LOGW(TAG, "recovery: safe mode, booting without the card");
    }

    /* Real character art has two sources, and the boot path uses both: the TF
     * card carries the full V4 animation set, the app image always carries a
     * small LWFA pack. lw_art_boot() prefers the card and fills any clip it
     * could not supply from the embedded pack, so a missing card costs the
     * extra clips and never the characters. */
    const lw_actor_art_t *art = lw_art_boot(characters_lwfa_start,
                                            (size_t)(characters_lwfa_end - characters_lwfa_start));
    ESP_LOGI(TAG, "art: %s", lw_art_report());
    const int64_t t_art = esp_timer_get_time();
    /* Main-task stack headroom after each phase. uxTaskGetStackHighWaterMark()
     * reports the smallest gap the task has ever had, so a phase that drops it
     * is the phase whose call depth owns the limit. The device panicked with a
     * stack protection fault inside the boot build, and this is the cheapest
     * way to see which of these five steps actually eats the stack. */
    boot_ram_report("art");
    if (!art) {
        ESP_LOGW(TAG, "art: %s", "no character source loaded, using the labelled fallback");
    }

    /* Read the user's content file from the same verified root the art came
     * from. This has to happen after lw_art_boot() (which resolves the root)
     * but before lw_os_start() (which builds the pages that borrow it). */
    (void)lw_content_boot();
    const int64_t t_content = esp_timer_get_time();
    boot_ram_report("content");

    publish_capabilities();
    if (!lw_os_start(&s_system, art)) {
        ESP_LOGE(TAG, "lw_os_start failed; falling back to the legacy shell");
        /* The legacy shell does not render actors, so the registry and the card
         * read-ahead worker must not stay alive behind it. */
        lw_art_release();
        lw_ui_start();
    } else {
        const int64_t t_ui = esp_timer_get_time();
        boot_ram_report("ui");
        /* Later capability changes are applied from the LVGL task only. */
        lv_timer_create(caps_timer_cb, 1000, NULL);
        /* The desktop exists now, so the anniversary is live and the memories
         * page has been built with the built-in table; installing the source
         * here is the one moment it is still closed. */
        lw_content_boot_apply();
        const int64_t t_apply = esp_timer_get_time();
        boot_ram_report("apply");
        /* Boot phase budget, one line, milliseconds. "hw" is codec/LEDC/SD
         * bring-up, "art" is the card mount plus six pack headers plus the
         * read-ahead cache open, "content" is the user content file, "ui" is
         * the whole first screen build (pages, sprites, fonts), "apply" is the
         * memories/anniversary install. Anything above ~100 ms is worth its own
         * investigation in BOOT_ANIMATION.md. */
        ESP_LOGI(TAG, "boot phases: hw=%lld art=%lld content=%lld ui=%lld apply=%lld total=%lld ms",
                 (t_hw - t_mark) / 1000, (t_art - t_hw) / 1000,
                 (t_content - t_art) / 1000, (t_ui - t_content) / 1000,
                 (t_apply - t_ui) / 1000, (t_apply - t_start) / 1000);
        t_mark = t_apply;
    }

    /* The diagnostics need the live objects, and they must be registered after
     * the art exists: the resource/frame providers read the registry that
     * lw_art_boot() just built. */
    lw_diag_board_bind(&s_system, &s_services);
    char recovery_line[96];
    lw_recovery_describe(&s_recovery_record, &s_recovery, recovery_line, sizeof(recovery_line));
    lw_diag_board_recovery(s_recovery.safe_mode, recovery_line);
    lw_diag_board_init();
}

#endif /* CONFIG_LW_BOOT_MODE_OS_V2 */

void lw_boot_start(void)
{
    /* First thing in the boot path: the reset reason is still the one this boot
     * started with, and classifying it before any subsystem can panic is what
     * makes a crash loop countable. */
    recovery_begin();

    lw_nvs_state_t nvs = lw_store_nvs_begin();
    if (nvs != LW_NVS_READY) {
        ESP_LOGW(TAG, "NVS unavailable (%s); existing saves are preserved and settings stay in RAM",
                 lw_store_nvs_reason() ? lw_store_nvs_reason() : "unknown");
    }

    lw_platform_set(&s_platform);

    /* One-second RTC stamp, so the next boot can report the uptime of the run
     * that died. Created after NVS so the timer task exists. */
    const esp_timer_create_args_t tick_args = {.callback = &recovery_tick_cb, .name = "lw_recovery"};
    esp_timer_handle_t tick = NULL;
    if (esp_timer_create(&tick_args, &tick) == ESP_OK) {
        if (esp_timer_start_periodic(tick, 1000 * 1000) != ESP_OK) {
            esp_timer_delete(tick);
        }
    }

#if CONFIG_LW_BOOT_MODE_OS_V2
    start_os();
#else
    ESP_LOGI(TAG, "boot: legacy shell");
    lw_ui_start();
#endif
}
