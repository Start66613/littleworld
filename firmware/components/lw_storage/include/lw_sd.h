#pragma once
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "esp_err.h"
/* Explicit future hardware call, never invoked from current app_main. */
esp_err_t lw_sd_mount_existing(void);
esp_err_t lw_sd_unmount_existing(void);
const char *lw_sd_asset_root(void);
#endif
