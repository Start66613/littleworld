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

/* ---------------------------------------------------- backlight variants --
 *
 * The panel flicker report has to be narrowed down on the board, and the only
 * honest way is to compare builds that differ in exactly one mechanism. These
 * macros are driven by tools/build_v3_2_matrix.ps1 (sdkconfig.extra.diag-*):
 *
 *   CONFIG_LW_BACKLIGHT_FIXED   (diag-b) the duty cycle is written once at boot and
 *                        never again; the brightness slider becomes a stored
 *                        preference that cannot reach the LEDC channel. If the
 *                        flicker disappears, the runtime duty writes are the
 *                        cause, not the panel or the DSI stream.
 *   CONFIG_LW_BACKLIGHT_MINIMAL (diag-c) additionally pins the level low, so the
 *                        LCD's own boost/charge-pump load is at its smallest.
 *
 * Both variants still report the brightness capability as unavailable: the UI
 * must not offer a slider that the firmware will ignore.
 */
#if CONFIG_LW_BACKLIGHT_FIXED || CONFIG_LW_BACKLIGHT_MINIMAL
#define LW_BACKLIGHT_RUNTIME 0
#else
#define LW_BACKLIGHT_RUNTIME 1
#endif

#if CONFIG_LW_BACKLIGHT_MINIMAL
#define LW_BACKLIGHT_BOOT_LEVEL 30
#else
#define LW_BACKLIGHT_BOOT_LEVEL 100
#endif
lw_hw_caps_t lw_hw_begin(bool enable_audio)
{
    lw_hw_caps_t caps = {0};

    /* The BSP already owns the LEDC channel that drives the panel backlight and
     * bsp_display_backlight_on() left it at full scale. The level is not lowered
     * at boot: a user who never touches the slider keeps the brightest panel. A
     * failed write inside lw_hw_brightness_set() restores the last good level, so
     * a bad request can never leave the screen dark. */
#if LW_BACKLIGHT_RUNTIME
    s_brightness = true;
    s_brightness_last = 100;
    caps.brightness = true;
    ESP_LOGI(TAG, "brightness: %s", "PWM backlight control available");
#else
    if (bsp_display_brightness_set(LW_BACKLIGHT_BOOT_LEVEL) != ESP_OK) {
        ESP_LOGW(TAG, "brightness: boot-level write failed");
    }
    s_brightness = false;
    s_brightness_last = LW_BACKLIGHT_BOOT_LEVEL;
    caps.brightness = false;
    ESP_LOGI(TAG, "brightness: fixed at %d%% (diagnostic build, runtime writes disabled)",
             LW_BACKLIGHT_BOOT_LEVEL);
#endif

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
     * esp_hosted. The hosted stack is only compiled into the experimental
     * configuration, and even there it must be proven on the board, so the
     * backend never claims a radio: lw_boot.c takes the Wi-Fi and SNTP
     * capability from the connectivity service, which only reports READY after
     * esp_wifi_init() actually succeeded. */
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
