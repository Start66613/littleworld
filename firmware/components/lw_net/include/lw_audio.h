/* Little World - audio pipeline core (pure C, host tested).
 *
 * The board has an ES8311 codec on I2S. The IDLE/READY/PLAYING state machine,
 * the bounded PCM ring and the "no audio" fallback are the parts that must be
 * right on the device, so they live here and are tested on the host. The ESP
 * adapter only moves bytes between this ring and the I2S driver.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LW_AUDIO_RING_BYTES 4096u

typedef enum {
    LW_AUDIO_UNAVAILABLE = 0, /* no codec / adapter: the UI shows "no audio" */
    LW_AUDIO_READY,
    LW_AUDIO_PLAYING,
    LW_AUDIO_PAUSED,
    LW_AUDIO_DONE,
    LW_AUDIO_FAILED
} lw_audio_state_t;

typedef enum {
    LW_AUDIO_ERR_NONE = 0,
    LW_AUDIO_ERR_NOT_SUPPORTED,
    LW_AUDIO_ERR_NOT_PLAYING,
    LW_AUDIO_ERR_FULL,
    LW_AUDIO_ERR_IO
} lw_audio_error_t;

typedef struct {
    lw_audio_state_t state;
    lw_audio_error_t error;
    bool sink_ok;
    bool muted;
    unsigned volume; /* 0..100 */
    unsigned used;   /* bytes currently buffered */
    uint32_t written_bytes;
    uint32_t played_bytes;
    uint32_t dropped_bytes; /* muted writes and overflow */
    uint32_t underruns;
    uint8_t ring[LW_AUDIO_RING_BYTES]; /* written_bytes / played_bytes index it */
} lw_audio_t;

void lw_audio_init(lw_audio_t *audio, bool sink_ok);
void lw_audio_set_sink(lw_audio_t *audio, bool sink_ok);
bool lw_audio_sink_ok(const lw_audio_t *audio);
lw_audio_state_t lw_audio_state(const lw_audio_t *audio);
lw_audio_error_t lw_audio_error(const lw_audio_t *audio);
const char *lw_audio_state_name(lw_audio_state_t state);
const char *lw_audio_error_name(lw_audio_error_t error);

bool lw_audio_start(lw_audio_t *audio);
bool lw_audio_pause(lw_audio_t *audio);
bool lw_audio_resume(lw_audio_t *audio);
bool lw_audio_stop(lw_audio_t *audio);
void lw_audio_set_volume(lw_audio_t *audio, unsigned volume);
void lw_audio_set_muted(lw_audio_t *audio, bool muted);
unsigned lw_audio_volume(const lw_audio_t *audio);
bool lw_audio_muted(const lw_audio_t *audio);

/** Queue PCM. Returns the number of bytes accepted (0 when unavailable, muted
 *  or the buffer is full). Dropped bytes are counted. */
unsigned lw_audio_write(lw_audio_t *audio, const uint8_t *pcm, unsigned len);
/** Copy up to @p max bytes for the sink. Returns the byte count copied. */
unsigned lw_audio_read(lw_audio_t *audio, uint8_t *dst, unsigned max);
/** Free space for producers. */
unsigned lw_audio_free(const lw_audio_t *audio);

/** Adapter notifications. */
void lw_audio_on_underrun(lw_audio_t *audio);
void lw_audio_on_io_error(lw_audio_t *audio);
/** Called when the adapter has drained the ring after a stop request. */
void lw_audio_on_drained(lw_audio_t *audio);

#ifdef __cplusplus
}
#endif
