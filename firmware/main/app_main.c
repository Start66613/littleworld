/* Little World - ESP32-P4 entry point.
 *
 * Board bring-up only: the BSP owns the MIPI-DSI panel, GT911 touch and the
 * LVGL adapter task. Everything else lives in the lw_* components so the same
 * code also runs in the Windows simulator.
 *
 * SPDX-License-Identifier: MIT
 */
#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lw/lw_platform.h"
#include "lw_ui.h"

static const char *TAG = "littleworld";

/* ------------------------------------------------------------ platform --- */

static bool platform_get_time(struct tm *out)
{
    /* No RTC on this board yet: the UI falls back to its demo clock and says so. */
    (void)out;
    return false;
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

/* ---------------------------------------------------------------- boot --- */

void app_main(void)
{
    ESP_LOGI(TAG, "boot: reset reason %d, free heap %u B", (int)esp_reset_reason(),
             (unsigned)esp_get_free_heap_size());

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s); settings will not persist", esp_err_to_name(nvs));
    }

    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_180,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };

    lv_display_t *display = bsp_display_start_with_config(&cfg);
    if (display == NULL) {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        return;
    }
    bsp_display_backlight_on();

    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "cannot take the LVGL lock");
        return;
    }

    lw_platform_set(&s_platform);
    lw_ui_start();

    bsp_display_unlock();

    ESP_LOGI(TAG, "boot: Little World ready, free heap %u B", (unsigned)esp_get_free_heap_size());
}
