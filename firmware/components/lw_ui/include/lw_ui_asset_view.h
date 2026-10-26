/* Little World - device side of the ".lwu" UI asset container.
 *
 * lw_ui_asset.h (in lw_engine) validates the file and computes nine-slice
 * rectangles; it never touches LVGL. This header is the other half: it turns a
 * validated .lwu into ready-to-draw LVGL image descriptors, and places them.
 *
 * Why a dedicated cache instead of the sprite lease system:
 * lw_asset_acquire() exists to bound the memory of ANIMATED character frames,
 * which change every 100 ms and can pile up. UI chrome is the opposite - a
 * panel or an icon is loaded once at boot and never changes, so paying a lease
 * per widget would only add accounting for something that cannot grow. What UI
 * assets do need is a hard cap on how much art can be resident, and that is the
 * fixed slot count below plus the per-file size check.
 *
 * The source .lwu is NOT kept after loading. Its nine pieces are repacked into
 * their own tightly packed descriptors (same total pixel count, so no extra
 * memory) and the file buffer is freed. Repacking is what makes a nine-slice
 * piece drawable at all: an RGB565A8 image keeps its alpha plane immediately
 * after the colour plane of the WHOLE image, so a sub-rectangle cannot be
 * described by offsetting a pointer.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "lw_ui9.h"
#include "lw_ui_asset.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many .lwu files one boot can keep resident. The UI needs a handful:
 * panels, a button strip, a few icons. */
#define LW_UI_ASSET_SLOTS 16u
#define LW_UI_ASSET_ID_MAX 24u
#define LW_UI_ASSET_FILE_MAX (1024u * 1024u)

/* Reset the cache and free every piece buffer.
 *
 * Refused (returns false) while a widget created by this module is still alive,
 * because those widgets point at the buffers. Call it after the UI tree that
 * used the art has been deleted - which is also the order lw_ui_asset_release()
 * enforces at shutdown. */
bool lw_ui_asset_reset(void);

/* Parse @p bytes (one complete .lwu) and take ownership of the art under @p id.
 *
 * The buffer is consumed: the pieces are copied out and @p bytes is not
 * referenced afterwards, so the caller may free it as soon as this returns
 * true. Returns false - leaving the cache unchanged - when the file is invalid,
 * the id is too long or already taken, or the cache is full. */
bool lw_ui_asset_add(const char *id, const uint8_t *bytes, size_t length);

/* Read <root>/<relative> and register it. The only file the UI asset path
 * opens, and the only place a read failure can surface. */
bool lw_ui_asset_add_file(const char *id, const char *relative);

/* Number of registered assets, and how many bytes their piece buffers hold. */
size_t lw_ui_asset_count(void);
size_t lw_ui_asset_bytes(void);

/* The parsed header of a registered asset, NULL when unknown. */
const lw_ui_asset_t *lw_ui_asset_header(const char *id);

/* Create a nine-slice panel/button at (x, y) with size w x h.
 *
 * Corners keep their source size exactly, edges stretch on one axis, the centre
 * on both - which is the whole point: scaling the whole bitmap (what
 * lv_image_set_scale alone does) makes a pixel-art border thicken and a corner
 * round off. @p state selects a button band; panels and icons ignore it.
 *
 * Returns the container object, or NULL when the asset is unknown, the target is
 * smaller than the borders, or memory ran out. The caller deletes the container;
 * this module notices the deletion and drops its widget count.
 */
lv_obj_t *lw_ui_asset_panel(lv_obj_t *parent, const char *id, int x, int y, int w, int h,
                            lw_ui9_button_state_t state);

/* A plain icon stretched to w x h. Nearest-neighbour when the asset is flagged
 * as pixel art, so a scaled icon keeps hard edges instead of blurring. */
lv_obj_t *lw_ui_asset_icon(lv_obj_t *parent, const char *id, int x, int y, int w, int h);

/* Smallest size this asset can be drawn at without breaking its borders. */
bool lw_ui_asset_minimum_size(const char *id, int *min_w, int *min_h);

#ifdef __cplusplus
}
#endif
