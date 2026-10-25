/* Boot and runtime diagnostics for the ESP32-P4 build.
 * Reads real allocator and reset-reason data; never prints in the render loop.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t reset_reason;
    uint32_t boot_ms;
    size_t heap_free;
    size_t heap_min_free;
    size_t heap_largest;
    size_t psram_free;
    size_t psram_largest;
    bool nvs_ready;
    const char *nvs_reason;
    bool brightness_ready;
    bool audio_ready;
    bool wifi_supported;
    bool time_source;
} lw_diag_t;

lw_diag_t lw_diag_capture(void);
void lw_diag_log_boot(void);
/* Rate limited to one line per 30 s and only when free heap moved by >= 8 KiB.
 * Safe to call from the UI timer at low frequency; do not call per frame. */
void lw_diag_tick(uint32_t now_ms);
/* One labelled measurement, e.g. after entering the RPG page. */
void lw_diag_mark(const char *label);

#ifdef __cplusplus
}
#endif
