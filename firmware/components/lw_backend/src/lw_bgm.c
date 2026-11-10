/* Embedded music bed played through the ES8311 speaker. SPDX-License-Identifier: MIT */
#include "lw_bgm.h"

#ifdef ESP_PLATFORM
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "lw_bgm";

/* Embedded by EMBED_FILES in this component's CMakeLists. The name is the file
 * name with every non-alphanumeric byte folded to '_'. */
extern const uint8_t cozy_wav_start[] asm("_binary_cozy_wav_start");
extern const uint8_t cozy_wav_end[] asm("_binary_cozy_wav_end");

#define BGM_CHUNK 1024u

static esp_codec_dev_handle_t s_dev;
static TaskHandle_t s_task;
static volatile bool s_stopping;
static const uint8_t *s_pcm;
static unsigned s_pcm_bytes;
static unsigned s_rate;

/* RIFF parsing is done once, by hand, because the track is fixed: pulling in a
 * WAV reader for one file would be more code than the reader itself. */
static bool parse_wav(const uint8_t *data, unsigned len, const uint8_t **pcm, unsigned *bytes, unsigned *rate)
{
    if (len < 44u || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }
    unsigned pos = 12u;
    unsigned channels = 1u;
    unsigned bits = 16u;
    unsigned sample_rate = 22050u;
    while (pos + 8u <= len) {
        const uint8_t *id = data + pos;
        unsigned size = (unsigned)data[pos + 4] | ((unsigned)data[pos + 5] << 8) |
                        ((unsigned)data[pos + 6] << 16) | ((unsigned)data[pos + 7] << 24);
        const unsigned body = pos + 8u;
        if (body + size > len) {
            size = len - body;
        }
        if (memcmp(id, "fmt ", 4) == 0 && size >= 16u) {
            channels = (unsigned)data[body + 2] | ((unsigned)data[body + 3] << 8);
            sample_rate = (unsigned)data[body + 4] | ((unsigned)data[body + 5] << 8) |
                          ((unsigned)data[body + 6] << 16) | ((unsigned)data[body + 7] << 24);
            bits = (unsigned)data[body + 14] | ((unsigned)data[body + 15] << 8);
        } else if (memcmp(id, "data", 4) == 0) {
            /* The board has one speaker; a stereo file would play at double
             * speed, so refuse rather than pretend. */
            if (channels != 1u || bits != 16u) {
                ESP_LOGW(TAG, "expected mono 16-bit, got %u ch / %u bits", channels, bits);
                return false;
            }
            *pcm = data + body;
            *bytes = size;
            *rate = sample_rate;
            return true;
        }
        pos = body + size + (size & 1u);
    }
    return false;
}

static void bgm_task(void *arg)
{
    (void)arg;
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = s_rate,
        .channel = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_OK) {
        ESP_LOGW(TAG, "speaker open failed; no sound path");
        s_dev = NULL;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "playing %u B at %u Hz", s_pcm_bytes, s_rate);
    while (!s_stopping) {
        unsigned sent = 0;
        while (sent < s_pcm_bytes && !s_stopping) {
            unsigned n = s_pcm_bytes - sent;
            if (n > BGM_CHUNK) {
                n = BGM_CHUNK;
            }
            if (esp_codec_dev_write(s_dev, (void *)(s_pcm + sent), (int)n) != ESP_OK) {
                ESP_LOGW(TAG, "write failed after %u B", sent);
                s_stopping = true;
                break;
            }
            sent += n;
        }
    }
    esp_codec_dev_close(s_dev);
    s_task = NULL;
    vTaskDelete(NULL);
}

bool lw_bgm_start(void)
{
    if (s_task != NULL) {
        return true;
    }
    if (s_dev == NULL) {
        s_dev = bsp_audio_codec_speaker_init();
        if (s_dev == NULL) {
            ESP_LOGW(TAG, "ES8311 speaker path unavailable");
            return false;
        }
    }
    if (s_pcm == NULL) {
        const unsigned len = (unsigned)(cozy_wav_end - cozy_wav_start);
        if (!parse_wav(cozy_wav_start, len, &s_pcm, &s_pcm_bytes, &s_rate)) {
            ESP_LOGW(TAG, "embedded track is not a mono 16-bit WAV (%u B)", len);
            return false;
        }
    }
    s_stopping = false;
    /* 4 KB is enough for one chunk plus the codec driver's own stack use, and
     * internal RAM after the UI is only a few kilobytes: a generous stack here
     * would push the boot path back over the edge it was just rescued from. */
    if (xTaskCreate(bgm_task, "lw_bgm", 4096, NULL, 4, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGW(TAG, "no memory for the music task");
        return false;
    }
    return true;
}

void lw_bgm_stop(void)
{
    if (s_task == NULL) {
        return;
    }
    s_stopping = true;
    /* The task closes the codec and clears the handle itself. Waiting here
     * would deadlock a caller that runs on the LVGL task while the writer is
     * blocked in esp_codec_dev_write(). */
}

bool lw_bgm_running(void)
{
    return s_task != NULL;
}

bool lw_bgm_volume(unsigned percent)
{
    if (s_dev == NULL) {
        return false;
    }
    unsigned value = percent > 100u ? 100u : percent;
    return esp_codec_dev_set_out_vol(s_dev, (int)value) == ESP_OK;
}

unsigned lw_bgm_bytes(void)
{
    return s_pcm_bytes;
}

unsigned lw_bgm_rate(void)
{
    return s_rate;
}

#endif /* ESP_PLATFORM */
