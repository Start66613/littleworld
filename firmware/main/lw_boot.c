/* Startup orchestration for the ESP32-P4 build.
 *
 * This file owns the decision between the integrated OS V2 desktop and the
 * legacy application shell, the platform storage hooks shared by both, and the
 * real persistence adapters. UI internals stay in lw_ui.
 *
 * SPDX-License-Identifier: MIT */
#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lw/lw_platform.h"
#include "lw_hw.h"
#include "lw_store_nvs.h"
#include "lw_ui.h"

#if CONFIG_LW_BOOT_MODE_OS_V2
#include "lw_diag.h"
#include "lw_os.h"
#include "lw_store.h"
#endif

static const char *TAG = "littleworld";

/* ------------------------------------------------------------ platform --- */

static bool platform_get_time(struct tm *out)
{
#if CONFIG_LW_BOOT_MODE_OS_V2
    /* No RTC and no SNTP session: say so instead of inventing a date. */
    return lw_hw_time_get(out);
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

static void start_connectivity(uint32_t now_ms)
{
    lw_services_init(&s_services);
    lw_services_start(&s_services, now_ms);

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

static void start_os(void)
{
    s_prefs_io = *lw_store_nvs_io(&s_prefs_key);
    s_rpg_io = *lw_store_nvs_io(&s_rpg_key);

    lw_hw_caps_t caps = lw_hw_begin(CONFIG_LW_ENABLE_AUDIO);

    lw_system_backend_t backend = {0};
    backend.simulated = false;
    backend.network_available = caps.wifi;
    backend.brightness = caps.brightness ? hw_brightness : NULL;
    backend.volume = caps.audio ? hw_volume : NULL;
    backend.load = prefs_load;
    backend.store = prefs_store;
    lw_system_init(&s_system, &backend);

    start_connectivity((uint32_t)(esp_timer_get_time() / 1000));

    if (!caps.brightness) {
        ESP_LOGW(TAG, "brightness: %s", "backend reports Unsupported");
    }
    if (!caps.audio) {
        ESP_LOGW(TAG, "audio: %s", "ES8311 not opened; volume is a stored preference only");
    }
    ESP_LOGI(TAG, "network: %s", lw_hw_wifi_reason());
    ESP_LOGI(TAG, "time: %s", lw_hw_time_reason());

    /* The OS keeps a pointer to this descriptor for the lifetime of the
     * session, so it must not live on the stack. */
    static lw_rpg_view_host_t rpg_host = {NULL, rpg_load, rpg_store, rpg_reward};
    lw_os_rpg_storage(&rpg_host);

    lw_diag_log_boot();

    /* The TF card is not part of this round, so no character pack is available.
     * Passing NULL selects the labelled no-art fallback instead of failing. */
    if (!lw_os_start(&s_system, NULL)) {
        ESP_LOGE(TAG, "lw_os_start failed; falling back to the legacy shell");
        lw_ui_start();
    }
}

#endif /* CONFIG_LW_BOOT_MODE_OS_V2 */

void lw_boot_start(void)
{
    lw_nvs_state_t nvs = lw_store_nvs_begin();
    if (nvs != LW_NVS_READY) {
        ESP_LOGW(TAG, "NVS unavailable (%s); existing saves are preserved and settings stay in RAM",
                 lw_store_nvs_reason() ? lw_store_nvs_reason() : "unknown");
    }

    lw_platform_set(&s_platform);

#if CONFIG_LW_BOOT_MODE_OS_V2
    start_os();
#else
    ESP_LOGI(TAG, "boot: legacy shell");
    lw_ui_start();
#endif
}
