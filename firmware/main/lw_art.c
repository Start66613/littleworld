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

/* The packs now differ in size (characters 256x256, dogbed 320x320, UI sheet
 * 544x352, skin atlas 512x544), so the lease budget can no longer be a multiple
 * of one frame size. The desktop holds two character frames, the pet page holds
 * the atlas plus a couple frame plus a dog frame, and the lock screen holds two
 * more; lw_assets refuses any budget above 2 MiB, which is the ceiling used
 * here. Leaving it at 4x196608 = 786,432 B would make the 835,584 B atlas
 * unacquirable ("budget" in the failure counter, no visual error). */
#define LW_ART_LEASE_BUDGET (2u * 1024u * 1024u)

/* Read-ahead cache for the card. Six frames of the largest geometry in use
 * (320x320x3 = 307,200 B) plus one skin atlas would be far too much to keep
 * resident, and the cache is not a lease: it is capped at 1.5 MiB so a couple
 * frame, a dog frame and the 835,584 B atlas can co-exist without thrashing.
 * Only opened when a card verified. */
#define LW_ART_CACHE_BUDGET (1536u * 1024u)

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
    /* Re-entrant by design: a second call must not leave the first call's
     * registry published, because lw_pack_registry_register() replaces the
     * table and the first call's specs would then be the ones pointing at a
     * closed cache. Detach (and close the old cache) before rebuilding. */
    if (s_ready) lw_art_release();
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
        /* The auxiliary slot holds the room scenes (daily-room / rpg-room),
         * which are 1024x600 LWSR images that live in the same verified root.
         * Without this call they are simply never found and the pet page keeps
         * the flat colour background even though the card has them. One slot
         * only, so the two rooms are mutually exclusive by design. */
        (void)lw_visual_aux_configure(root);
        /* The cache owns its own read-ahead worker, so opening it is what makes
         * the card path non-blocking. A NULL cache still works but reads every
         * frame synchronously inside the LVGL timer, which is the exact stall
         * this exists to avoid; the log says so when it happens. */
        s_cache = lw_file_cache_open(root, LW_ART_CACHE_BUDGET);
        lw_pack_t info;
        /* Every pack is added on its own and judged on its own return code. The
         * V4.3 set is six files: two characters, the daily/dogbed clips, the
         * component sheet and the skin atlas. Losing one must cost exactly that
         * one - the consumers all check lw_assets_find() and fall back - so a
         * single shared "did it work" flag would hide which file actually
         * failed. qianqian arrives in TWO files on purpose: base carries the 13
         * original clips, wave only appends the greeting, and both land in her
         * one actor slot.
         *
         * The id prefixes below are the contract the visual code already looks
         * up, so they are NOT derived from the file names:
         *   male.lwp          -> "male.*"
         *   qianqian-base.lwp -> "qianqian.*"   (13 clips)
         *   qianqian-wave.lwp -> "qianqian.wave"
         *   daily.lwp         -> "v42.*"        (lw_polish.c)
         *   ui.lwp            -> "v42ui.sheet"  (lw_polish_art)
         *   skin.lwp          -> "v43skin.atlas"(lw_skin.c)
         *
         * skin.lwp goes through the cache like the rest: one 835,584 B frame
         * against a 1.5 MiB card cache is affordable, and a NULL cache would
         * make every component read the atlas synchronously inside the LVGL
         * timer. The atlas is still leased once and shared, which is what keeps
         * the residency bounded. */
        static const struct { const char *file, *actor; bool required; } packs[] = {
            {"male.lwp", "male", true},
            {"qianqian-base.lwp", "qianqian", false},
            {"qianqian-wave.lwp", "qianqian", false},
            {"daily.lwp", "v42", false},
            {"ui.lwp", "v42ui", false},
            {"skin.lwp", "v43skin", false},
        };
        unsigned loaded = 0;
        for (unsigned i = 0; i < sizeof(packs) / sizeof(packs[0]); i++) {
            lw_pack_status_t status = lw_pack_registry_add(root, packs[i].file, packs[i].actor,
                                                           s_cache, &info);
            if (status == LW_PACK_OK) {
                loaded++;
                continue;
            }
            /* A missing optional file is normal (an older card holds only the
             * two character packs) and is not worth a log line. A missing
             * REQUIRED file, or any present-but-broken pack, must be named:
             * that is the difference between "no art on the card" and "the card
             * is there but this build cannot read it". */
            if (status == LW_PACK_ERR_OPEN && !packs[i].required) continue;
            char tail[96];
            (void)snprintf(tail, sizeof(tail), "%s%s:%s",
                           s_report[0] ? "; " : "card: ", packs[i].file,
                           lw_pack_status_text(status));
            strncat(s_report, tail, sizeof(s_report) - strlen(s_report) - 1);
        }
        /* Both characters count as covered only when the card actually supplied
         * a usable clip table for them; the embedded fill-in below re-checks
         * per clip name, so a half-loaded actor still gets its idle frame. */
        card = loaded > 0;
        if (card && !s_report[0]) {
            (void)snprintf(s_report, sizeof(s_report), "card: %u/%u packs", loaded,
                           (unsigned)(sizeof(packs) / sizeof(packs[0])));
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
        (void)lw_pack_registry_reset();
        return NULL;
    }

    /* ---- 3. publish the two actor tables -------------------------------- */
    for (unsigned i = 0; i < 2; i++) {
        unsigned count = 0;
        const lw_named_clip_t *named = lw_pack_registry_clips(s_actor_names[i], &count);
        s_art[i] = (lw_actor_art_t){s_actor_names[i], named, count, named == NULL || count == 0};
    }
    if (!s_art[0].count && !s_art[1].count) {
        (void)lw_pack_registry_reset();
        return NULL;
    }
    s_ready = true;
    (void)snprintf(s_report + strlen(s_report), sizeof(s_report) - strlen(s_report),
                   " [%s%s]", card ? "card" : "no card", embedded_used ? " + embedded" : "");
    return s_art;
}

const char *lw_art_report(void) { return s_report[0] ? s_report : "no art source"; }

lw_file_cache_t *lw_art_cache(void) { return s_cache; }

void lw_art_release(void)
{
    /* Order matters. Detach the registry FIRST: every spec in it carries a
     * pointer to s_cache, so closing the cache while the registry is still
     * published would leave live specs reading through freed memory the next
     * time a sprite asks for a frame. If the detach is refused (a lease is
     * still live, i.e. a sprite outlived this call) the cache is kept open on
     * purpose: a leak is recoverable, a use-after-free is not. */
    if (!lw_pack_registry_reset()) {
        s_ready = false;
        return;
    }
    if (s_cache) {
        lw_file_cache_close(s_cache);
        s_cache = NULL;
    }
    /* Scene images are freed only once no widget still references them, which
     * is what makes this safe to call on the failed-boot path. */
    (void)lw_visual_assets_shutdown();
    s_ready = false;
}
