/* BSP-backed hardware adapters. ESP-IDF only. SPDX-License-Identifier: MIT */
#include "lw_hw.h"

#ifdef ESP_PLATFORM
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

static const char *TAG = "lw_hw";

static bool s_brightness;
static bool s_audio;
static unsigned s_brightness_last = 72;
static esp_codec_dev_handle_t s_speaker;

lw_hw_caps_t lw_hw_begin(bool enable_audio)
{
    lw_hw_caps_t caps = {0};

    /* The BSP already owns the LEDC channel that drives the panel backlight and
     * bsp_display_backlight_on() left it at full scale. The level is not lowered
     * at boot: a user who never touches the slider keeps the brightest panel. A
     * failed write inside lw_hw_brightness_set() restores the last good level, so
     * a bad request can never leave the screen dark. */
    s_brightness = true;
    s_brightness_last = 100;
    caps.brightness = true;
    ESP_LOGI(TAG, "brightness: %s", "PWM backlight control available");

    if (enable_audio) {
        s_speaker = bsp_audio_codec_speaker_init();
        if (s_speaker != NULL && esp_codec_dev_set_out_vol(s_speaker, 40) == ESP_OK) {
            s_audio = true;
        } else {
            s_speaker = NULL;
            ESP_LOGW(TAG, "ES8311 speaker path unavailable; volume UI stays a preference only");
        }
    }
    caps.audio = s_audio;

    /* ESP32-P4 has no radio of its own: Wi-Fi arrives through the ESP32-C6 via
     * esp_hosted. That integration is not part of this build, so the backend
     * reports Unsupported instead of pretending to scan. */
    caps.wifi = false;
    caps.time_source = false;
    return caps;
}

bool lw_hw_brightness_set(unsigned percent)
{
    if (!s_brightness) {
        return false;
    }
    unsigned value = percent < 10 ? 10 : (percent > 100 ? 100 : percent);
    if (bsp_display_brightness_set((int)value) != ESP_OK) {
        /* Restore the last known good level so a failed write cannot leave the
         * panel dark. The service layer keeps its previous value as well. */
        (void)bsp_display_brightness_set((int)s_brightness_last);
        ESP_LOGW(TAG, "brightness write failed; restored %u%%", s_brightness_last);
        return false;
    }
    s_brightness_last = value;
    return true;
}

unsigned lw_hw_brightness_last(void)
{
    return s_brightness_last;
}

bool lw_hw_volume_set(unsigned percent, bool muted)
{
    if (s_speaker == NULL) {
        return false;
    }
    unsigned value = percent > 100 ? 100 : percent;
    return esp_codec_dev_set_out_vol(s_speaker, muted ? 0 : (int)value) == ESP_OK;
}

bool lw_hw_time_get(struct tm *out)
{
    /* No RTC battery and no SNTP session yet: the caller must fall back to the
     * clearly labelled demo clock instead of inventing a date. */
    (void)out;
    return false;
}

const char *lw_hw_wifi_reason(void)
{
    return "ESP32-C6 link is not integrated in this build; Wi-Fi reported Unsupported";
}

const char *lw_hw_time_reason(void)
{
    return "no RTC and no SNTP session; demo clock shown";
}

#endif /* ESP_PLATFORM */
