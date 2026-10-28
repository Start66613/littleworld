/* Little World - ESP32-P4 entry point.
 *
 * Board bring-up only: the BSP owns the MIPI-DSI panel, GT911 touch and the
 * LVGL adapter task. Startup policy and persistence live in lw_boot.c so the
 * same UI code also runs in the Windows simulator.
 *
 * SPDX-License-Identifier: MIT
 */
#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "lvgl.h"

void lw_boot_start(void);
#include "lw_capture.h"

static const char *TAG = "littleworld";

/* V4.4 display benchmark: the tear-avoid mode is a build-time choice so two
 * images differ in exactly one display-path parameter (see Kconfig.projbuild).
 * The shipping image is TRIPLE_PARTIAL, the adapter's MIPI-DSI default. */
#if CONFIG_LW_TEAR_AVOID_DOUBLE_PARTIAL
#define LW_TEAR_AVOID_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_PARTIAL
#elif CONFIG_LW_TEAR_AVOID_TRIPLE_FULL
#define LW_TEAR_AVOID_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL
#else
#define LW_TEAR_AVOID_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "boot: reset reason %d, free heap %u B", (int)esp_reset_reason(),
             (unsigned)esp_get_free_heap_size());

    /* 1024x600 panel, rotated 180 degrees with mirrored GT911 axes; both
     * mirrors stay off because the rotation already flips the touch mapping. */
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_180,
        .tear_avoid_mode = LW_TEAR_AVOID_MODE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    lv_display_t *display = bsp_display_start_with_config(&cfg);
    if (display == NULL) {
        ESP_LOGE(TAG, "bsp_display_start_with_config failed");
        return;
    }
    bsp_display_backlight_on();

#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS
    /* Frame counter for acceptance measurements (< 2 microseconds per frame,
     * per the adapter's Kconfig). The "fps" console command reads it. */
    if (esp_lv_adapter_fps_stats_enable(display, true) != ESP_OK) {
        ESP_LOGW(TAG, "FPS statistics unavailable");
    }
#endif

    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "cannot take the LVGL lock");
        return;
    }

    lw_boot_start();

    bsp_display_unlock();

    bsp_display_lock(0);
    lw_capture_attach_input();
    bsp_display_unlock();

    /* Serial REPL on UART0: shot / tap / status (acceptance evidence). */
    lw_capture_console_start();

    ESP_LOGI(TAG, "boot: Little World ready, free heap %u B", (unsigned)esp_get_free_heap_size());
}
