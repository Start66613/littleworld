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
    lw_asset_read_fn read;
    void *context;
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
lw_asset_lease_t *lw_asset_acquire(const char *id, uint16_t frame);
const lv_image_dsc_t *lw_asset_image(const lw_asset_lease_t *lease);
void lw_asset_release(lw_asset_lease_t *lease);
lw_asset_stats_t lw_assets_stats(void);

/* Optional standard-C/VFS raw-frame provider. Caller mounts storage and maps
 * platform paths; no directory, device, partition or filesystem is created. */
typedef struct { const char *path; uint32_t offset, frame_stride; uint16_t frames; } lw_asset_raw_file_t;
bool lw_asset_read_raw_file(void *context, uint16_t frame, uint8_t *pixels, size_t bytes);
