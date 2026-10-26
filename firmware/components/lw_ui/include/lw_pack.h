/* SPDX-License-Identifier: MIT */
/* Hardware-side LWPX pack loader.
 *
 * The LWPX container is NOT the embedded LWFA pack and is not treated as one:
 * it has its own header, 108-byte clip records and a 256x256 RGB565+A8 frame
 * layout. What it shares with the old pack is the contract the UI already
 * depends on: lw_assets registers a pointer-only spec table, every frame is
 * fetched through a lease (lw_asset_acquire -> spec->read -> lw_asset_release)
 * and lw_sprite owns the visible image. So this file only supplies specs that
 * read frames out of a file; it never keeps a whole pack in RAM.
 *
 * Frame reads go through lw_file_cache when one is supplied, which is what
 * gives the board its single-worker read-ahead and its PSRAM frame buffers.
 * Without a cache the reads fall back to lw_asset_read_raw_file (plain stdio),
 * which is what the Windows side already does.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lw_assets.h"
#include "lw_duo_view.h" /* lw_named_clip_t */
#include "lw_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LW_PACK_MAX_CLIPS 6u
#define LW_PACK_MAX_FRAMES 32u
#define LW_PACK_NAME_BYTES 32u
#define LW_PACK_RECORD_BYTES 108u
#define LW_PACK_HEADER_BYTES 16u
#define LW_PACK_SIDE 256u
#define LW_PACK_FRAME_BYTES (LW_PACK_SIDE * LW_PACK_SIDE * 3u) /* RGB565 + A8 */
#define LW_PACK_PATH_BYTES 768u
#define LW_PACK_ID_BYTES 48u
#define LW_PACK_FILE_BYTES 128u

/* Registration budget handed to lw_assets: two live frames per actor. */
#define LW_PACK_LEASE_BUDGET (2u * LW_PACK_FRAME_BYTES)

typedef enum {
    LW_PACK_OK = 0,
    LW_PACK_ERR_ARGUMENT,
    LW_PACK_ERR_OPEN,
    LW_PACK_ERR_MAGIC,
    LW_PACK_ERR_GEOMETRY,
    LW_PACK_ERR_CLIP_COUNT,
    LW_PACK_ERR_RECORD,
    LW_PACK_ERR_CLIP_NAME,
    LW_PACK_ERR_FRAMES,
    LW_PACK_ERR_ANCHOR,
    LW_PACK_ERR_DURATION,
    LW_PACK_ERR_OFFSET,
    LW_PACK_ERR_TRUNCATED,
    LW_PACK_ERR_DUPLICATE,
    LW_PACK_ERR_LOOP,
    LW_PACK_ERR_ACTORS,
    LW_PACK_ERR_REGISTER,
} lw_pack_status_t;

typedef struct {
    /* Identity of the pack, for logs and the acceptance report. */
    unsigned clips;             /* clips added by THIS file */
    unsigned frames;            /* frames added by THIS file */
    unsigned bytes;             /* size of the pack file on disk */
    /* The actor's clip table in pack order, including clips appended by an
     * earlier pack of the same actor. The caller owns this view. */
    const lw_named_clip_t *named;
    unsigned named_count;       /* length of named after the append */
} lw_pack_t;

const char *lw_pack_status_text(lw_pack_status_t status);

/* Parse <root>/<file> and register its clips with lw_assets.
 *
 * actor_name prefixes every internal asset id ("male.idle", "qianqian.wave")
 * so two packs can be registered at once without colliding. The clip NAMES
 * are left exactly as the pack spells them (idle / blink / wave), because the
 * UI looks clips up by name and renaming them here would silently break the
 * blink and wave paths.
 *
 * Registration REPLACES the whole registry: lw_assets stores the pointer to the
 * spec array and never copies it, so every spec from every pack has to be alive
 * in one table. Use lw_pack_registry_* below with more than one pack.
 */
lw_pack_status_t lw_pack_open(const char *root, const char *file, const char *actor_name,
                              lw_file_cache_t *cache, lw_pack_t *out);

/* ---- multi-pack registry -------------------------------------------------
 * lw_assets_register() keeps the caller's pointers, so the spec table, the ids
 * and the source records must all be file-scope and stay alive for as long as
 * they are registered. This registry is that storage. */
void lw_pack_registry_reset(void);

/* Add one pack. Fails (and leaves the registry usable) if the pack is
 * malformed, has a clip name that collides with an existing clip for the same
 * actor, or would overflow the table. */
lw_pack_status_t lw_pack_registry_add(const char *root, const char *file, const char *actor_name,
                                      lw_file_cache_t *cache, lw_pack_t *out);

/* Publish everything added so far to lw_assets. Call once, after all packs. */
bool lw_pack_registry_register(size_t budget);

/* The clip list for one actor (by the name passed to lw_pack_registry_add). */
const lw_named_clip_t *lw_pack_registry_clips(const char *actor_name, unsigned *count);

/* Detach from lw_assets; the caller must have destroyed every sprite first. */
void lw_pack_registry_release(void);

#ifdef __cplusplus
}
#endif
