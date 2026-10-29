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
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

void lw_boot_start(void);
#include "lw_capture.h"

static const char *TAG = "littleworld";

/* The console REPL is brought up on its own task rather than on the main task.
 *
 * Measured on the board with the SD card present: main has 28,968 B of its
 * 32,768 B stack free when the first screen finishes, yet the REPL setup that
 * follows - esp_console_new_repl_uart() plus the banner printf - runs the main
 * stack into its very last bytes and the core takes a store fault 24 B below
 * the stack bound (CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK reports the store to
 * 0x4ff825dc with SP at 0x4ff825d0). Without a card the same code needs about
 * 200 B, so the trigger is the slower first screen, not the console itself.
 * Until that is understood properly the REPL gets a stack of its own: whatever
 * it needs, it cannot take the main task's last bytes with it. */
static void console_task(void *arg)
{
    (void)arg;
    if (!lw_capture_console_start()) {
        ESP_LOGW(TAG, "console REPL unavailable");
    }
    vTaskDelete(NULL);
}

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

    /* Serial REPL on UART0: shot / tap / status (acceptance evidence).
     *
     * It has to come up before the first screen is built, not after. Measured on
     * the board: internal RAM - the only pool a UART driver and a task stack can
     * come from - falls from 97,743 B free to 6,179 B across lw_boot_start(),
     * almost all of it inside the first-screen build. Starting the REPL afterwards
     * needs a 16 KB stack plus driver buffers out of 6 KB; the allocation fails,
     * and the failure tears down state the rest of the boot still owns. On the
     * board that surfaced as abort() in newlib's lock init and as a "stack
     * protection fault" blamed on the main task, whose stack had 29 KB free.
     * Bringing the instrument up first keeps ~97 KB available for it. */
    ESP_LOGI(TAG, "internal heap before console: free %u B, largest block %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (xTaskCreate(console_task, "lw_console", 12288, NULL, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no stack for the console task; the REPL stays off");
    }

    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "cannot take the LVGL lock");
        return;
    }

    lw_boot_start();

    bsp_display_unlock();

    /* The tap injector creates an indev, which writes into the LVGL object tree,
     * so it has to hold the adapter lock. This used to try with a zero timeout
     * and ignore the result: whenever the LVGL task happened to hold the mutex -
     * which is exactly what a slower first screen makes likely - the attach ran
     * unlocked and the following unlock released a recursive mutex this task had
     * never taken. On the board that surfaced as "Failed to acquire LVGL lock"
     * followed ~400 ms later by a stack-protection panic whose saved frame
     * pointer sat 24 B below the stack bound while 29 KB of that stack was still
     * unused, i.e. the mutex control block was corrupted, not the stack
     * exhausted. Block instead, and skip the injector rather than corrupt state. */
    if (bsp_display_lock(-1)) {
        lw_capture_attach_input();
        bsp_display_unlock();
    } else {
        ESP_LOGW(TAG, "cannot take the LVGL lock; tap injection stays disabled");
    }

    ESP_LOGI(TAG, "boot: Little World ready, free heap %u B", (unsigned)esp_get_free_heap_size());
}
