/* Board capabilities that the OS backend may report to the UI.
 * Every flag is set only after the corresponding BSP call actually succeeded.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool brightness;   /* backlight can be dimmed through the BSP */
    bool audio;        /* ES8311 speaker path opened and accepts volume */
    bool wifi;         /* C6 link is not integrated in this round */
    bool time_source;  /* wall clock is valid (RTC or SNTP) */
} lw_hw_caps_t;

/* Probes brightness, optional audio, and reports fixed-unsupported radios.
 * Never powers a codec that was not detected by the BSP. */
lw_hw_caps_t lw_hw_begin(bool enable_audio);

bool lw_hw_brightness_set(unsigned percent); /* clamped to 10..100 */
unsigned lw_hw_brightness_last(void);
bool lw_hw_volume_set(unsigned percent, bool muted);
bool lw_hw_time_get(struct tm *out);

/* Reason strings for reports and logs; never NULL. */
const char *lw_hw_wifi_reason(void);
const char *lw_hw_time_reason(void);

#ifdef __cplusplus
}
#endif
