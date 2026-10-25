/* Diagnostics backed by real allocator statistics. ESP-IDF only.
 * SPDX-License-Identifier: MIT */
#include "lw_diag.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lw_hw.h"
#include "lw_store_nvs.h"

static const char *TAG = "lw_diag";

static uint32_t s_last_log_ms;
static size_t s_last_heap = SIZE_MAX;

static size_t largest_internal(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static size_t psram_free(void)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    return info.total_free_bytes;
}

static size_t psram_largest(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

lw_diag_t lw_diag_capture(void)
{
    lw_diag_t d = {0};
    d.reset_reason = (uint32_t)esp_reset_reason();
    d.boot_ms = (uint32_t)(esp_timer_get_time() / 1000);
    d.heap_free = esp_get_free_heap_size();
    d.heap_min_free = esp_get_minimum_free_heap_size();
    d.heap_largest = largest_internal();
    d.psram_free = psram_free();
    d.psram_largest = psram_largest();
    d.nvs_ready = lw_store_nvs_state() == LW_NVS_READY;
    d.nvs_reason = lw_store_nvs_reason();
    return d;
}

void lw_diag_log_boot(void)
{
    lw_diag_t d = lw_diag_capture();
    ESP_LOGI(TAG, "reset_reason=%u boot_ms=%u", (unsigned)d.reset_reason, (unsigned)d.boot_ms);
    ESP_LOGI(TAG, "heap free=%u min=%u largest=%u", (unsigned)d.heap_free, (unsigned)d.heap_min_free,
             (unsigned)d.heap_largest);
    ESP_LOGI(TAG, "psram free=%u largest=%u", (unsigned)d.psram_free, (unsigned)d.psram_largest);
    ESP_LOGI(TAG, "nvs=%s%s%s", d.nvs_ready ? "ready" : "unavailable",
             d.nvs_ready || d.nvs_reason == NULL ? "" : " reason=", d.nvs_reason ? d.nvs_reason : "");
}

void lw_diag_mark(const char *label)
{
    lw_diag_t d = lw_diag_capture();
    ESP_LOGI(TAG, "mark=%s heap=%u largest=%u psram=%u", label ? label : "?", (unsigned)d.heap_free,
             (unsigned)d.heap_largest, (unsigned)d.psram_free);
}

void lw_diag_tick(uint32_t now_ms)
{
    if (s_last_heap != SIZE_MAX && now_ms - s_last_log_ms < 30000u) {
        return;
    }
    size_t heap = esp_get_free_heap_size();
    size_t delta = s_last_heap == SIZE_MAX ? 0 : (heap > s_last_heap ? heap - s_last_heap : s_last_heap - heap);
    if (s_last_heap != SIZE_MAX && delta < 8192u) {
        s_last_log_ms = now_ms;
        return;
    }
    s_last_heap = heap;
    s_last_log_ms = now_ms;
    ESP_LOGI(TAG, "heap=%u largest=%u psram=%u", (unsigned)heap, (unsigned)largest_internal(),
             (unsigned)psram_free());
}

#endif /* ESP_PLATFORM */
