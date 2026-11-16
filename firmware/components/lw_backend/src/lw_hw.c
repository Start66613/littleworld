/* BSP-backed hardware adapters. ESP-IDF only. SPDX-License-Identifier: MIT */
#include "lw_hw.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

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

/* Largest internal DMA block the BSP path may start on. bsp_audio_init() builds
 * a duplex channel with six descriptors of 256 frames each, about 6 KB, and
 * wraps every failure in ESP_ERROR_CHECK - below this the board reboots instead
 * of just muting. lw_speaker_small() needs a fraction of that, so it runs first
 * and this floor only decides whether the BSP fallback is worth trying. */
#define LW_AUDIO_MIN_DMA_BLOCK (8u * 1024u)

/* Descriptors the small path asks for. 96 frames at 22050 Hz is 4.3 ms a
 * buffer, which is short enough to stay inside one PSRAM block but long enough
 * that the player task does not have to wake up every millisecond. */
#define LW_AUDIO_DESC 3
#define LW_AUDIO_FRAMES 96

/* Speaker, transmit only.
 *
 * The same codec and the same pins as bsp_audio_codec_speaker_init(), minus the
 * microphone half of the duplex channel. The board only ever plays, and the
 * captured mic channel was costing an I2S channel's worth of internal DMA
 * memory that the ESP32-C6 link now wants: with hosted running, the duplex
 * allocation failed and bsp_audio_init()'s ESP_ERROR_CHECK turned "no speaker"
 * into a boot loop. Shrinking the depth and dropping the unused direction puts
 * the speaker back inside what is left. */
static esp_codec_dev_handle_t lw_speaker_small(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = LW_AUDIO_DESC;
    chan_cfg.dma_frame_num = LW_AUDIO_FRAMES;

    i2s_chan_handle_t tx = NULL;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker small: i2s_new_channel failed (%s)", esp_err_to_name(err));
        return NULL;
    }

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(22050),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    err = i2s_channel_init_std_mode(tx, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_enable(tx);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker small: channel setup failed (%s)", esp_err_to_name(err));
        i2s_del_channel(tx);
        return NULL;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
        .rx_handle = NULL,
        .tx_handle = tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (data_if == NULL) {
        i2s_del_channel(tx);
        return NULL;
    }

    if (bsp_i2c_init() != ESP_OK) {
        return NULL;
    }
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if == NULL) {
        return NULL;
    }
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (gpio_if == NULL) {
        return NULL;
    }

    const esp_codec_dev_hw_gain_t gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 };
    const es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *dev = es8311_codec_new(&es8311_cfg);
    if (dev == NULL) {
        return NULL;
    }
    const esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = dev,
        .data_if = data_if,
    };
    return esp_codec_dev_new(&dev_cfg);
}

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
        /* Small transmit-only channel first: it needs a few hundred bytes of
         * internal DMA memory instead of the BSP's ~6 KB duplex pair, so it
         * survives next to the ESP32-C6 link. Only if that fails and the pool
         * is roomy do we fall back to the BSP, which aborts the board rather
         * than returning NULL when memory runs out. */
        s_speaker = lw_speaker_small();
        if (s_speaker == NULL) {
            const size_t dma_block = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
            if (dma_block >= LW_AUDIO_MIN_DMA_BLOCK) {
                s_speaker = bsp_audio_codec_speaker_init();
            } else {
                ESP_LOGW(TAG, "speaker skipped: largest internal DMA block is %u B (needs %u)",
                         (unsigned)dma_block, (unsigned)LW_AUDIO_MIN_DMA_BLOCK);
            }
        }
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
#if CONFIG_LW_NET_WIFI_ENABLE
    return "ESP32-C6 hosted stack compiled in; radio state reported by the connectivity service";
#else
    return "ESP32-C6 link is not integrated in this build; Wi-Fi reported Unsupported";
#endif
}

const char *lw_hw_time_reason(void)
{
    return "no RTC and no SNTP session; demo clock shown";
}

#endif /* ESP_PLATFORM */
