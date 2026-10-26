/* SPDX-License-Identifier: MIT */
/* Boot-time resource bring-up: find the V4 runtime assets, or say why not.
 *
 * The hard rule this file implements is that a missing card is NOT a failure
 * to start. Every path either returns a verified root or leaves a status the UI
 * turns into the labelled fallback; nothing here can panic or block the boot
 * screen. */
#include "lw_resource_mount.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lw_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "lw_sd.h"
static const char *TAG = "lw_res";
#endif

/* The embedded fallback is a small clip set inside the app image. It means the
 * device is never blank; it does not mean the V4 art loaded. */
#ifndef LW_RESOURCE_EMBEDDED_ROOT
#define LW_RESOURCE_EMBEDDED_ROOT "/littleworld/embedded"
#endif

static bool s_medium_mounted;

bool lw_resource_mount(void)
{
    /* Card first: the V4 runtime payload is ~12 MB and cannot live in a 6 MiB
     * app partition, so the card is the only place the full set fits. */
#ifdef ESP_PLATFORM
    esp_err_t err = lw_sd_mount_existing();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "storage: %s%s", "no card mounted (", esp_err_to_name(err));
        s_medium_mounted = false;
        lw_resource_set_root(NULL);
        lw_resource_flag(LW_RESOURCE_MOUNT_FAILED);
        return false;
    }
    s_medium_mounted = true;
    lw_resource_set_root(lw_sd_asset_root());
#else
    /* Host builds point at a directory through the environment so the same code
     * path can be exercised on Windows. */
    const char *root = getenv("LW_RESOURCE_ROOT");
    if (!root || !root[0]) {
        s_medium_mounted = false;
        lw_resource_set_root(NULL);
        lw_resource_flag(LW_RESOURCE_MOUNT_FAILED);
        return false;
    }
    s_medium_mounted = true;
    lw_resource_set_root(root);
#endif

    if (!lw_resource_verify()) {
        /* The card is present but its payload is missing, corrupt or from a
         * newer pack: report the exact reason, keep the app usable. */
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "resources: %s", lw_resource_status_text(lw_resource_report()->status));
#endif
        return false;
    }
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "resources: verified, %u theme(s) available", lw_resource_report()->themes);
#endif
    return true;
}

bool lw_resource_medium_mounted(void) { return s_medium_mounted; }

const char *lw_resource_embedded_root(void) { return LW_RESOURCE_EMBEDDED_ROOT; }

#ifdef __cplusplus
}
#endif
