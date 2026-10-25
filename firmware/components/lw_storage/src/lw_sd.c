#ifdef ESP_PLATFORM
#include "lw_sd.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
#error "Little World refuses a build that could auto-format an SD card"
#endif
static bool owned;
esp_err_t lw_sd_mount_existing(void)
{
    if (owned || bsp_sdcard) return ESP_ERR_INVALID_STATE;
    esp_err_t result=bsp_sdcard_mount(); owned=result==ESP_OK; return result;
}
esp_err_t lw_sd_unmount_existing(void)
{
    if (!owned) return ESP_ERR_INVALID_STATE;
    esp_err_t result=bsp_sdcard_unmount(); if (result==ESP_OK) owned=false; return result;
}
const char *lw_sd_asset_root(void) { return BSP_SD_MOUNT_POINT "/littleworld"; }
#endif
