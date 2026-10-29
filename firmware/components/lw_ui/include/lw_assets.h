/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

/* All calls belong to the LVGL thread. Registry/spec/context lifetime must
 * exceed all leases. Providers fill exactly bytes of RGB565 or RGB565+A8:
 * little-endian RGB plane first, then one tightly packed alpha plane. */
typedef bool (*lw_asset_read_fn)(void *context, uint16_t frame, uint8_t *pixels, size_t bytes);
typedef struct {
    const char *id;
    uint16_t width, height, frames;
    int16_t anchor_x, anchor_y;
    lv_color_format_t format;
    /* V4.5: content box of this clip, in source pixels, unioned over all of its
     * frames. width/height stay the full source canvas so providers and the
     * packer keep their existing geometry; only the draw path uses the box.
     *
     * A zero-size box means "no metadata, treat the whole canvas as content",
     * which keeps every legacy pack and the embedded fallback valid.
     *
     * Why: LVGL clips to obj->coords, so an image object sized to the content
     * box blends the content instead of the mostly-transparent canvas. Host
     * measurement (reports/perf-v4-5/render_bench.txt) says the box alone is
     * worth 1.35-2.21x and, combined with baking the scale into the asset,
     * 5.6-6.0x, with the rendered pixels unchanged. */
    uint16_t bbox_x, bbox_y, bbox_w, bbox_h;
    /* V4.5: nonzero when the provider already stores this clip at its final
     * on-screen size, i.e. the 1.125x correction was baked into the asset. The
     * sprite then draws it with LV_SCALE_NONE, which is what avoids LVGL's
     * per-pixel transform_and_recolor() path. */
    uint16_t prescaled;
    lw_asset_read_fn read;
    void *context;
    /* V4.5: optional zero-copy provider. When set, a frame that is already in the
     * file cache is handed over as a pointer instead of being memcpy'd into the
     * lease buffer; the sprite then draws straight out of the cache. Measured on
     * the RPG page: copying one 97 KB frame out of PSRAM cost 6-20 ms, and walking
     * needs a new frame every 50 ms per actor, which was the whole hitch. The
     * pointer must stay valid until unborrow() is called. NULL = copy as before. */
    const uint8_t *(*borrow)(void *context, uint16_t frame, size_t bytes);
    void (*unborrow)(void *context, const uint8_t *pixels);
    /* V4.5: queue one frame for background read-ahead. Optional; NULL means the
     * provider has nothing to do (embedded or host packs). A looping sprite asks
     * for the rest of its clip here so the card is never on the draw path. */
    void (*prefetch)(void *context, uint16_t frame);
} lw_asset_spec_t;
typedef struct lw_asset_lease lw_asset_lease_t;
typedef struct {
    size_t live_bytes, peak_bytes, budget_bytes;
    uint32_t live_leases, loaded_frames, failures;
} lw_asset_stats_t;

/* Atomic validation; refuses replacing a registry with outstanding leases. */
bool lw_assets_register(const lw_asset_spec_t *specs, size_t count, size_t budget_bytes);
const lw_asset_spec_t *lw_assets_find(const char *id);
size_t lw_asset_frame_bytes(const lw_asset_spec_t *spec);

/* Draw geometry for one spec at one requested scale.
 *
 * This is the single place that decides how an asset maps onto an lv_image, so
 * lw_sprite.c cannot drift between "first present" and "move the anchor".
 *
 * The object is sized to the content box because LVGL clips to obj->coords;
 * offset_x/y cancels both LV_ALIGN_CENTER's centring term and the in-canvas
 * content origin, so the pixels land exactly where the full-canvas draw put
 * them. For a prescaled asset the scale collapses to LV_SCALE_NONE and the
 * offset is zero, which is what keeps LVGL out of transform_and_recolor(). */
typedef struct {
    uint16_t scale;        /* LV_SCALE_NONE when spec->prescaled */
    uint16_t obj_w, obj_h; /* content box at that scale, >= 1 */
    int32_t offset_x, offset_y;
    int32_t anchor_dx, anchor_dy; /* subtract from the anchor to get obj pos */
} lw_asset_draw_t;
void lw_asset_draw_geometry(const lw_asset_spec_t *spec, uint16_t scale, lw_asset_draw_t *out);
lw_asset_lease_t *lw_asset_acquire(const char *id, uint16_t frame);
/* Re-read one more frame of the same asset into an existing lease. The pixel
 * buffer is reused, so an animation no longer allocates and frees a frame
 * buffer (and drops the LVGL image cache entry) on every single frame. The
 * caller owns the detach/invalidate step; lw_sprite does it right after. */
bool lw_asset_refill(lw_asset_lease_t *lease, uint16_t frame);
const lv_image_dsc_t *lw_asset_image(const lw_asset_lease_t *lease);
/* Pixels of the frame currently presented: the borrowed cache buffer when the
 * provider could borrow, otherwise the lease's own copy. */
const uint8_t *lw_asset_pixels(const lw_asset_lease_t *lease);
void lw_asset_release(lw_asset_lease_t *lease);
lw_asset_stats_t lw_assets_stats(void);
/* Refill cost split, for the diag console: calls, how many of them were served by
 * the zero-copy borrow, and the total milliseconds spent inside refill. */
/* Refill counts since the last reset. Deliberately no timing here: this file is
 * also linked into the host resource test, which has no LVGL clock. The cost of
 * a refill is visible through lw_rpg_view's seek_ms split instead. */
void lw_assets_refill_perf(uint32_t *calls, uint32_t *borrowed);
void lw_assets_refill_perf_reset(void);

/* Optional standard-C/VFS raw-frame provider. Caller mounts storage and maps
 * platform paths; no directory, device, partition or filesystem is created. */
typedef struct { const char *path; uint32_t offset, frame_stride; uint16_t frames; } lw_asset_raw_file_t;
bool lw_asset_read_raw_file(void *context, uint16_t frame, uint8_t *pixels, size_t bytes);
