/* SPDX-License-Identifier: MIT */
/* Hardware-side LWPX pack loader.
 *
 * The LWPX container is NOT the embedded LWFA pack and is not treated as one:
 * it has its own header and 108-byte clip records. Frame geometry comes from
 * the header (u16 width, u16 height at offsets 8 and 10), so one loader serves
 * the 256x256 character packs, the 320x320 daily/dogbed pack, the 544x352 UI
 * sheet and the 512x544 skin atlas without a transcode step. What it shares with the old pack is the contract the UI already
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

/* Clips per actor slot. 8 covered the character packs (male 7, qianqian-wave 1)
 * but not the frozen V4.3 base pack: qianqian-base.lwp carries 13 clips (idle,
 * sleep, sleep_enter, sleep_idle, sleep_turn and eight walk directions) and was
 * refused with bad-clip-count, so she silently fell back to the single embedded
 * idle frame. 32 matches the per-clip frame bound and is the measured maximum
 * Codex validated against the real files (31 ids across 6 packs). */
#define LW_PACK_MAX_CLIPS 32u
#define LW_PACK_MAX_FRAMES 32u
#define LW_PACK_NAME_BYTES 32u
#define LW_PACK_RECORD_BYTES 108u
#define LW_PACK_HEADER_BYTES 16u
#define LW_PACK_PATH_BYTES 768u
#define LW_PACK_ID_BYTES 48u
#define LW_PACK_FILE_BYTES 128u

/* Frame geometry is per FILE and comes from the header (u16 width at offset 8,
 * u16 height at offset 10, frame = width*height*3 for RGB565+A8). It used to be
 * the compile-time constant LW_PACK_SIDE 256, which silently rejected every
 * pack that is not 256x256 - the 320x320 daily/dogbed pack, the 544x352 UI
 * sheet and the 512x544 skin atlas all failed with bad-geometry on hardware
 * while the Windows build, which reads the header, showed them fine.
 *
 * LW_PACK_MAX_WIDTH/HEIGHT/FRAME_BYTES mirror lw_assets' own limits exactly
 * (lw_asset_frame_bytes rejects width>1024 || height>600, MAX_FRAME_BYTES is
 * 1,250,000). A pack that parses here must not be rejected later by the
 * registry, so the two caps are kept in lockstep rather than guessed. */
#define LW_PACK_MAX_WIDTH 1024u
#define LW_PACK_MAX_HEIGHT 600u
#define LW_PACK_MAX_FRAME_BYTES 1250000u

/* Registration budget handed to lw_assets. It is the ceiling lw_assets accepts
 * (it refuses anything above 2 MiB), because the packs now differ in size: the
 * skin atlas alone is 512*544*3 = 835,584 B and the pet page holds it together
 * with a 320x320 couple frame and a 320x320 dog frame = 1,449,984 B live. */
#define LW_PACK_LEASE_BUDGET (2u * 1024u * 1024u)

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
 * they are registered. This registry is that storage.
 *
 * Up to LW_PACK_MAX_FILES (4) files may be added. Every parsed file keeps its
 * own resolved path record for the lifetime of the registry, so appending a
 * second pack for one actor can never repoint the first pack's frame reads at
 * the wrong file. */
bool lw_pack_registry_reset(void);

/* Add one pack. Fails (and leaves the registry usable) if the pack is
 * malformed, has a clip name that collides with an existing clip for the same
 * actor, or would overflow the table. */
lw_pack_status_t lw_pack_registry_add(const char *root, const char *file, const char *actor_name,
                                      lw_file_cache_t *cache, lw_pack_t *out);

/* Publish everything added so far to lw_assets. Call once, after all packs. */
bool lw_pack_registry_register(size_t budget);

/* Add one clip from an EMBEDDED LWFA v1 blob (firmware/main/art/characters.lwfa).
 *
 * LWFA is a different container from LWPX: 32-byte records, per-clip geometry
 * (male 108x212, qianqian 104x202) and a payload that is already in the
 * flash-mapped image, so frames are memcpy'd instead of read from a file. It
 * lands in the same spec table on purpose: lw_assets_register() replaces the
 * whole registry, so a second table would silently unregister the first one.
 *
 * clip_name is what the UI will look up ("idle"); the blob's own record name is
 * the character name and is used only to find the record. */
lw_pack_status_t lw_pack_registry_add_embedded(const uint8_t *data, size_t size,
                                               const char *actor_name, const char *clip_name,
                                               lw_pack_t *out);

/* The clip list for one actor (by the name passed to lw_pack_registry_add). */
const lw_named_clip_t *lw_pack_registry_clips(const char *actor_name, unsigned *count);

/* Detach from lw_assets but keep the parsed tables in place, so a later
 * lw_pack_registry_register() can re-publish the same specs (theme rebuild,
 * a UI that tears its sprites down and puts them back). Refused while a lease
 * is live, for the same reason as the reset above. */
void lw_pack_registry_release(void);

/* Detach from lw_assets and clear the parsed tables; the caller must have
 * destroyed every sprite first.
 *
 * Returns false when lw_assets still holds a live lease: the registry is left
 * intact rather than being cleared under a spec table lw_assets is reading. A
 * caller that ignores this and proceeds anyway gets a dangling registry, which
 * is exactly how V3.1 panicked on hardware. */
bool lw_pack_registry_reset(void);

#ifdef __cplusplus
}
#endif
