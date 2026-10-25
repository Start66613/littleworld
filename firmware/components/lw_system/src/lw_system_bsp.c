/* Optional adapter factory. Construction performs no hardware action. SPDX-License-Identifier: MIT */
#ifdef ESP_PLATFORM
#include "lw_system.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
static bool brightness(void *context,unsigned value)
{ (void)context; return bsp_display_brightness_set((int)value)==ESP_OK; }
static bool volume(void *context,unsigned value,bool muted)
{ return context && esp_codec_dev_set_out_vol((esp_codec_dev_handle_t)context,muted ? 0 : (int)value)==ESP_OK; }
lw_system_backend_t lw_system_bsp_backend(void *speaker_codec)
{
    lw_system_backend_t backend={0}; backend.context=speaker_codec;
    backend.brightness=brightness; backend.volume=volume;
    /* Network/credential storage require the reviewed esp_wifi_remote/esp_hosted integration in P3.
       Unsupported capabilities remain disabled, never reported as connected. */
    return backend;
}
#endif
