/* Little World - audio pipeline core. Pure C, no IDF and no LVGL. SPDX-License-Identifier: MIT */
#include "lw_audio.h"

#include <string.h>

void lw_audio_init(lw_audio_t *audio, bool sink_ok)
{
    memset(audio, 0, sizeof(*audio));
    audio->sink_ok = sink_ok;
    audio->state = sink_ok ? LW_AUDIO_READY : LW_AUDIO_UNAVAILABLE;
    audio->error = sink_ok ? LW_AUDIO_ERR_NONE : LW_AUDIO_ERR_NOT_SUPPORTED;
    audio->volume = 60;
}

void lw_audio_set_sink(lw_audio_t *audio, bool sink_ok)
{
    audio->sink_ok = sink_ok;
    if (!sink_ok) {
        audio->state = LW_AUDIO_UNAVAILABLE;
        audio->error = LW_AUDIO_ERR_NOT_SUPPORTED;
        audio->used = 0;
    } else if (audio->state == LW_AUDIO_UNAVAILABLE) {
        audio->state = LW_AUDIO_READY;
        audio->error = LW_AUDIO_ERR_NONE;
    }
}

bool lw_audio_sink_ok(const lw_audio_t *audio)
{
    return audio->sink_ok;
}

lw_audio_state_t lw_audio_state(const lw_audio_t *audio)
{
    return audio->state;
}

lw_audio_error_t lw_audio_error(const lw_audio_t *audio)
{
    return audio->error;
}

const char *lw_audio_state_name(lw_audio_state_t state)
{
    switch (state) {
    case LW_AUDIO_UNAVAILABLE:
        return "unavailable";
    case LW_AUDIO_READY:
        return "ready";
    case LW_AUDIO_PLAYING:
        return "playing";
    case LW_AUDIO_PAUSED:
        return "paused";
    case LW_AUDIO_DONE:
        return "done";
    case LW_AUDIO_FAILED:
        return "failed";
    }
    return "?";
}

const char *lw_audio_error_name(lw_audio_error_t error)
{
    switch (error) {
    case LW_AUDIO_ERR_NONE:
        return "none";
    case LW_AUDIO_ERR_NOT_SUPPORTED:
        return "not-supported";
    case LW_AUDIO_ERR_NOT_PLAYING:
        return "not-playing";
    case LW_AUDIO_ERR_FULL:
        return "buffer-full";
    case LW_AUDIO_ERR_IO:
        return "io-error";
    }
    return "?";
}

bool lw_audio_start(lw_audio_t *audio)
{
    if (!audio->sink_ok) {
        audio->error = LW_AUDIO_ERR_NOT_SUPPORTED;
        return false;
    }
    audio->used = 0;
    audio->written_bytes = 0;
    audio->played_bytes = 0;
    audio->dropped_bytes = 0;
    audio->underruns = 0;
    audio->state = LW_AUDIO_PLAYING;
    audio->error = LW_AUDIO_ERR_NONE;
    return true;
}

bool lw_audio_pause(lw_audio_t *audio)
{
    if (audio->state != LW_AUDIO_PLAYING) {
        audio->error = LW_AUDIO_ERR_NOT_PLAYING;
        return false;
    }
    audio->state = LW_AUDIO_PAUSED;
    return true;
}

bool lw_audio_resume(lw_audio_t *audio)
{
    if (audio->state != LW_AUDIO_PAUSED) {
        audio->error = LW_AUDIO_ERR_NOT_PLAYING;
        return false;
    }
    audio->state = LW_AUDIO_PLAYING;
    audio->error = LW_AUDIO_ERR_NONE;
    return true;
}

bool lw_audio_stop(lw_audio_t *audio)
{
    if (!audio->sink_ok) {
        audio->error = LW_AUDIO_ERR_NOT_SUPPORTED;
        return false;
    }
    audio->used = 0;
    audio->state = LW_AUDIO_READY;
    audio->error = LW_AUDIO_ERR_NONE;
    return true;
}

void lw_audio_set_volume(lw_audio_t *audio, unsigned volume)
{
    if (volume > 100u) {
        volume = 100u;
    }
    audio->volume = volume;
}

void lw_audio_set_muted(lw_audio_t *audio, bool muted)
{
    audio->muted = muted;
}

unsigned lw_audio_volume(const lw_audio_t *audio)
{
    return audio->volume;
}

bool lw_audio_muted(const lw_audio_t *audio)
{
    return audio->muted;
}

unsigned lw_audio_free(const lw_audio_t *audio)
{
    return LW_AUDIO_RING_BYTES - audio->used;
}

unsigned lw_audio_write(lw_audio_t *audio, const uint8_t *pcm, unsigned len)
{
    if (!audio->sink_ok) {
        audio->error = LW_AUDIO_ERR_NOT_SUPPORTED;
        return 0;
    }
    if (audio->state != LW_AUDIO_PLAYING) {
        audio->error = LW_AUDIO_ERR_NOT_PLAYING;
        return 0;
    }
    if (len == 0 || pcm == NULL) {
        return 0;
    }
    if (audio->muted) {
        /* Muted still consumes the stream so timing stays realistic. */
        audio->dropped_bytes += len;
        return len;
    }
    unsigned free_bytes = LW_AUDIO_RING_BYTES - audio->used;
    unsigned take = len < free_bytes ? len : free_bytes;
    for (unsigned i = 0; i < take; i++) {
        unsigned slot = (audio->written_bytes + i) % LW_AUDIO_RING_BYTES;
        audio->ring[slot] = pcm[i];
    }
    audio->written_bytes += take;
    audio->used += take;
    if (take < len) {
        if (take > 0) {
            /* Partially queued: the rest is lost, the producer retries next time. */
            audio->dropped_bytes += len - take;
        }
        audio->error = LW_AUDIO_ERR_FULL;
    }
    return take;
}

unsigned lw_audio_read(lw_audio_t *audio, uint8_t *dst, unsigned max)
{
    if (audio->state != LW_AUDIO_PLAYING || audio->used == 0) {
        return 0;
    }
    unsigned take = max < audio->used ? max : audio->used;
    for (unsigned i = 0; i < take; i++) {
        unsigned slot = (audio->played_bytes + i) % LW_AUDIO_RING_BYTES;
        if (dst != NULL) {
            dst[i] = audio->ring[slot];
        }
    }
    audio->played_bytes += take;
    audio->used -= take;
    if (audio->used == 0) {
        /* Ring empty: the producer is expected to append more. */
        audio->state = LW_AUDIO_PLAYING;
    }
    return take;
}

void lw_audio_on_underrun(lw_audio_t *audio)
{
    audio->underruns++;
}

void lw_audio_on_io_error(lw_audio_t *audio)
{
    audio->state = LW_AUDIO_FAILED;
    audio->error = LW_AUDIO_ERR_IO;
}

void lw_audio_on_drained(lw_audio_t *audio)
{
    audio->used = 0;
    if (audio->state == LW_AUDIO_PLAYING) {
        audio->state = LW_AUDIO_DONE;
    }
}
