/* Loop an embedded music bed through the ES8311 speaker. SPDX-License-Identifier: MIT */
#ifndef LW_BGM_H
#define LW_BGM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The speaker is the only output path on the 7B board, and until now nothing
 * ever wrote to it: lw_hw.c opened the codec so the volume slider could reach
 * the hardware, but silence was the only thing the user ever heard. These three
 * calls are deliberately boring - start, stop, ask - because the interesting
 * part is the WAV parsing and the I2S pacing, not a mixer.
 *
 * All of them are safe to call when the codec failed to open: they return false
 * and the UI keeps its volume preference without pretending to play. */

bool lw_bgm_start(void);
void lw_bgm_stop(void);
bool lw_bgm_running(void);

/* Percent, 0..100. Reports false when the codec is unavailable. */
bool lw_bgm_volume(unsigned percent);

/* Bytes of PCM the embedded track contributes, for the console status line. */
unsigned lw_bgm_bytes(void);
unsigned lw_bgm_rate(void);

#endif /* LW_BGM_H */
