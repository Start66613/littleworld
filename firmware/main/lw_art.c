/* Boot-time character art selection: TF card first, embedded pack always.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_art.h"

#include <stdio.h>
#include <string.h>

#include "lw_pack.h"
#include "lw_resource.h"
#include "lw_resource_mount.h"
#include "lw_storage.h"
#include "lw_visual_assets.h"

/* Two actors, and the desktop and the lock screen each hold one live frame per
 * actor, so four 256x256 RGB565+A8 frames is the honest ceiling. lw_assets
 * refuses a budget above 2 MiB, and 4 x 196608 = 786432 fits comfortably. */
#define LW_ART_LEASE_BUDGET (4u * LW_PACK_FRAME_BYTES)

/* Read-ahead cache for the card. Four frames of 256x256x3 = 786,432 B, which
 * is what lw_file_cache keeps resident; without it every frame change is a
 * blocking card read on the LVGL thread. Only opened when a card verified. */
#define LW_ART_CACHE_BUDGET (4u * LW_PACK_FRAME_BYTES)

static lw_actor_art_t s_art[2];
static lw_file_cache_t *s_cache;
static char s_report[192];
static bool s_ready;

/* Names the OS looks up. Kept here so the two loaders cannot drift apart. */
static const char *const s_actor_names[2] = {"male", "qianqian"};

static bool has_clip(const lw_named_clip_t *named, unsigned count, const char *name)
{
    for (unsigned i = 0; i < count; i++) {
        if (named[i].name && !strcmp(named[i].name, name)) return true;
    }
    return false;
}

const lw_actor_art_t *lw_art_boot(const uint8_t *embedded, size_t embedded_size)
{
    s_ready = false;
    s_report[0] = '\0';

    /* ---- 1. the card, when one is present and its payload verifies ------- */
    bool card = false;
    if (lw_resource_mount()) {
        const char *root = lw_resource_root();
        /* Scenes live beside the packs under the same verified root. Without
         * this call the directory string stays empty, available() answers false
         * and the board silently draws the flat colour background even though
         * the card holds three perfectly good scenes. */
        if (!lw_visual_assets_configure(root)) {
            strncat(s_report, "scenes: configure failed",
                    sizeof(s_report) - strlen(s_report) - 1);
        }
        /* The cache owns its own read-ahead worker, so opening it is what makes
         * the card path non-blocking. A NULL cache still works but reads every
         * frame synchronously inside the LVGL timer, which is the exact stall
         * this exists to avoid; the log says so when it happens. */
        s_cache = lw_file_cache_open(root, LW_ART_CACHE_BUDGET);
        lw_pack_t info;
        /* male.lwp carries idle + blink + wave; qianqian-wave.lwp only appends
         * wave, so her base idle still comes from the embedded pack below. */
        lw_pack_status_t male = lw_pack_registry_add(root, "male.lwp", "male", s_cache, &info);
        if (male != LW_PACK_OK) {
            (void)snprintf(s_report, sizeof(s_report), "card: male.lwp %s", lw_pack_status_text(male));
        } else {
            lw_pack_status_t qian = lw_pack_registry_add(root, "qianqian-wave.lwp", "qianqian",
                                                         s_cache, &info);
            if (qian != LW_PACK_OK) {
                (void)snprintf(s_report, sizeof(s_report), "card: qianqian-wave.lwp %s",
                               lw_pack_status_text(qian));
            } else {
                card = true;
                (void)snprintf(s_report, sizeof(s_report), "card: male.lwp + qianqian-wave.lwp");
            }
        }
        if (!s_cache) {
            strncat(s_report, " (no cache: blocking reads)",
                    sizeof(s_report) - strlen(s_report) - 1);
        }
    } else {
        (void)snprintf(s_report, sizeof(s_report), "card: %s",
                       lw_resource_status_text(lw_resource_report()->status));
    }

    /* ---- 2. the embedded pack fills whatever the card did not provide ---- */
    bool embedded_used = false;
    for (unsigned i = 0; i < 2; i++) {
        unsigned count = 0;
        const lw_named_clip_t *named = lw_pack_registry_clips(s_actor_names[i], &count);
        if (named && has_clip(named, count, "idle")) continue;
        lw_pack_t info;
        lw_pack_status_t status = lw_pack_registry_add_embedded(embedded, embedded_size,
                                                               s_actor_names[i], "idle", &info);
        if (status == LW_PACK_OK) {
            embedded_used = true;
        } else if (!s_report[0] || card) {
            char tail[96];
            (void)snprintf(tail, sizeof(tail), "%sembedded %s: %s",
                           s_report[0] ? "; " : "", s_actor_names[i],
                           lw_pack_status_text(status));
            strncat(s_report, tail, sizeof(s_report) - strlen(s_report) - 1);
        }
    }

    if (!lw_pack_registry_register(LW_ART_LEASE_BUDGET)) {
        strncat(s_report, " [registry rejected]", sizeof(s_report) - strlen(s_report) - 1);
        lw_pack_registry_reset();
        return NULL;
    }

    /* ---- 3. publish the two actor tables -------------------------------- */
    for (unsigned i = 0; i < 2; i++) {
        unsigned count = 0;
        const lw_named_clip_t *named = lw_pack_registry_clips(s_actor_names[i], &count);
        s_art[i] = (lw_actor_art_t){s_actor_names[i], named, count, named == NULL || count == 0};
    }
    if (!s_art[0].count && !s_art[1].count) {
        lw_pack_registry_reset();
        return NULL;
    }
    s_ready = true;
    (void)snprintf(s_report + strlen(s_report), sizeof(s_report) - strlen(s_report),
                   " [%s%s]", card ? "card" : "no card", embedded_used ? " + embedded" : "");
    return s_art;
}

const char *lw_art_report(void) { return s_report[0] ? s_report : "no art source"; }

void lw_art_release(void)
{
    /* Sprites must already be destroyed: lw_pack_registry_reset() refuses to
     * drop a table that still has leases, and the cache must not disappear
     * under a pending frame read. */
    if (s_cache) {
        lw_file_cache_close(s_cache);
        s_cache = NULL;
    }
    lw_pack_registry_reset();
    /* Scene images are freed only once no widget still references them, which
     * is what makes this safe to call on the failed-boot path. */
    (void)lw_visual_assets_shutdown();
    s_ready = false;
}
