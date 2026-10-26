/* Little World - UI asset container (".lwu"), pure C and host tested.
 *
 * The problem this solves: a nine-slice panel needs its border widths, its
 * button states and its pixel format to travel WITH the bitmap. Splitting those
 * into a JSON manifest means the device has to parse JSON, and any mismatch
 * between manifest and file becomes a runtime surprise. One header, one file.
 *
 * Layout (little endian, 32 bytes):
 *
 *   0   char     magic[8]   "LWUI0001"
 *   8   uint16   width      source pixels per state band
 *   10  uint16   height     source pixels per state band
 *   12  uint8    kind       0 icon, 1 panel, 2 button
 *   13  uint8    format     0 RGB565, 1 RGB565A8
 *   14  uint16   states     1, or exactly 3 for a button strip
 *   16  uint16   inset_left
 *   18  uint16   inset_top
 *   20  uint16   inset_right
 *   22  uint16   inset_bottom
 *   24  uint16   flags      bit 0: pixel art (nearest neighbour required)
 *   26  uint16   reserved   must be 0
 *   28  uint32   data_bytes payload size, checked against the header
 *
 * The payload is states * width * height * bytes-per-pixel, RGB565 plane first
 * then one tightly packed alpha plane, which is exactly the layout
 * lw_asset_spec_t already consumes.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lw_ui9.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LW_UI_ASSET_HEADER_BYTES 32u
#define LW_UI_ASSET_MAX_STATES 3u
#define LW_UI_ASSET_MAX_SIDE 1024u

typedef enum {
    LW_UI_ASSET_ICON = 0,
    LW_UI_ASSET_PANEL = 1,
    LW_UI_ASSET_BUTTON = 2
} lw_ui_asset_kind_t;

typedef enum {
    LW_UI_ASSET_RGB565 = 0,
    LW_UI_ASSET_RGB565A8 = 1
} lw_ui_asset_format_t;

typedef enum {
    LW_UI_ASSET_OK = 0,
    LW_UI_ASSET_TOO_SMALL,     /* fewer bytes than a header */
    LW_UI_ASSET_BAD_MAGIC,
    LW_UI_ASSET_BAD_KIND,
    LW_UI_ASSET_BAD_FORMAT,
    LW_UI_ASSET_BAD_SIZE,      /* zero, over LW_UI_ASSET_MAX_SIDE, or 9-slice impossible */
    LW_UI_ASSET_BAD_STATES,    /* not 1, and not 3 for a button */
    LW_UI_ASSET_BAD_INSETS,    /* panel/button whose borders leave no centre */
    LW_UI_ASSET_BAD_PAYLOAD,   /* data_bytes does not match the geometry */
    LW_UI_ASSET_BAD_RESERVED
} lw_ui_asset_status_t;

typedef struct {
    lw_ui_asset_kind_t kind;
    lw_ui_asset_format_t format;
    uint16_t width, height; /* one state band */
    uint16_t states;
    lw_ui9_insets_t insets;
    bool pixel_art;
    uint32_t data_bytes;
    const uint8_t *payload; /* points into the caller's buffer */
} lw_ui_asset_t;

const char *lw_ui_asset_status_text(lw_ui_asset_status_t status);

/* Bytes one state band occupies, 0 when the geometry is unusable. */
size_t lw_ui_asset_state_bytes(const lw_ui_asset_t *asset);

/* Parse and fully validate @p bytes. On success every field of @p out is filled
 * and out->payload points into @p bytes, which must outlive the asset. */
lw_ui_asset_status_t lw_ui_asset_parse(const uint8_t *bytes, size_t length, lw_ui_asset_t *out);

/* True when the asset can be sliced for @p target_w x @p target_h. An icon is
 * always stretchable; a panel or button must satisfy the nine-slice minimum. */
bool lw_ui_asset_fits(const lw_ui_asset_t *asset, uint16_t target_w, uint16_t target_h);

/* Smallest target that keeps every corner intact (0 for an icon). */
void lw_ui_asset_minimum(const lw_ui_asset_t *asset, uint16_t *min_w, uint16_t *min_h);

/* Source rectangle of one button state inside the strip. Icons and panels only
 * have state 0. */
bool lw_ui_asset_state_rect(const lw_ui_asset_t *asset, lw_ui9_button_state_t state,
                            lw_ui9_rect_t *out);

/* Bytes one w x h piece occupies in the drawable layout: w*h*2 for RGB565,
 * w*h*3 for RGB565A8 (colour plane then alpha plane). */
size_t lw_ui_asset_piece_bytes(const lw_ui_asset_t *asset, uint16_t w, uint16_t h);

/* Copy one rectangle of one state out of the container into a tightly packed
 * descriptor payload.
 *
 * Why this exists: an lv_image_dsc_t must describe a contiguous buffer, and in
 * RGB565A8 the alpha plane for the WHOLE image sits after the colour plane for
 * the whole image. A nine-slice piece is therefore not addressable by offsetting
 * a pointer - it has to be repacked. Doing that here (pure C, no LVGL) is what
 * makes the copy verifiable on the build machine instead of only on the panel.
 *
 * @p state selects the button band (ignored for panels/icons).
 * Returns the number of bytes written, 0 when the request is out of bounds or
 * @p cap is too small. Nothing is written on failure. */
size_t lw_ui_asset_copy_piece(const lw_ui_asset_t *asset, unsigned state,
                              const lw_ui9_rect_t *src, uint8_t *dst, size_t cap);

#ifdef __cplusplus
}
#endif
