/* SPDX-License-Identifier: MIT */
#include "lw_ui_asset.h"

#include <string.h>

static const char MAGIC[8] = {'L', 'W', 'U', 'I', '0', '0', '0', '1'};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const char *lw_ui_asset_status_text(lw_ui_asset_status_t status)
{
    switch (status) {
    case LW_UI_ASSET_OK:
        return "ok";
    case LW_UI_ASSET_TOO_SMALL:
        return "file shorter than the 32-byte header";
    case LW_UI_ASSET_BAD_MAGIC:
        return "not a LWUI0001 asset";
    case LW_UI_ASSET_BAD_KIND:
        return "unknown asset kind";
    case LW_UI_ASSET_BAD_FORMAT:
        return "unknown pixel format";
    case LW_UI_ASSET_BAD_SIZE:
        return "zero or oversized bitmap";
    case LW_UI_ASSET_BAD_STATES:
        return "state count is not 1 and not 3";
    case LW_UI_ASSET_BAD_INSETS:
        return "border insets leave no centre";
    case LW_UI_ASSET_BAD_PAYLOAD:
        return "payload size does not match the geometry";
    case LW_UI_ASSET_BAD_RESERVED:
        return "reserved field is not zero";
    default:
        return "unknown status";
    }
}

size_t lw_ui_asset_state_bytes(const lw_ui_asset_t *asset)
{
    if (asset == NULL || asset->width == 0 || asset->height == 0) {
        return 0;
    }
    size_t pixels = (size_t)asset->width * asset->height;
    if (asset->format == LW_UI_ASSET_RGB565) {
        return pixels * 2u;
    }
    if (asset->format == LW_UI_ASSET_RGB565A8) {
        return pixels * 3u;
    }
    return 0;
}

lw_ui_asset_status_t lw_ui_asset_parse(const uint8_t *bytes, size_t length, lw_ui_asset_t *out)
{
    if (out == NULL) {
        return LW_UI_ASSET_TOO_SMALL;
    }
    memset(out, 0, sizeof(*out));
    if (bytes == NULL || length < LW_UI_ASSET_HEADER_BYTES) {
        return LW_UI_ASSET_TOO_SMALL;
    }
    if (memcmp(bytes, MAGIC, sizeof(MAGIC)) != 0) {
        return LW_UI_ASSET_BAD_MAGIC;
    }
    uint16_t width = rd16(bytes + 8);
    uint16_t height = rd16(bytes + 10);
    uint8_t kind = bytes[12];
    uint8_t format = bytes[13];
    uint16_t states = rd16(bytes + 14);
    uint16_t reserved = rd16(bytes + 26);
    uint32_t data_bytes = rd32(bytes + 28);
    if (kind > (uint8_t)LW_UI_ASSET_BUTTON) {
        return LW_UI_ASSET_BAD_KIND;
    }
    if (format > (uint8_t)LW_UI_ASSET_RGB565A8) {
        return LW_UI_ASSET_BAD_FORMAT;
    }
    if (reserved != 0) {
        return LW_UI_ASSET_BAD_RESERVED;
    }
    if (width == 0 || height == 0 || width > LW_UI_ASSET_MAX_SIDE || height > LW_UI_ASSET_MAX_SIDE) {
        return LW_UI_ASSET_BAD_SIZE;
    }
    if (states == 0 || states > LW_UI_ASSET_MAX_STATES) {
        return LW_UI_ASSET_BAD_STATES;
    }
    if (kind == LW_UI_ASSET_BUTTON) {
        if (states != (uint16_t)LW_UI9_STATE_COUNT) {
            return LW_UI_ASSET_BAD_STATES;
        }
    } else if (states != 1u) {
        /* A panel or an icon with three bands is ambiguous: refuse it rather
         * than guess which band is meant. */
        return LW_UI_ASSET_BAD_STATES;
    }
    lw_ui_asset_t asset;
    memset(&asset, 0, sizeof(asset));
    asset.kind = (lw_ui_asset_kind_t)kind;
    asset.format = (lw_ui_asset_format_t)format;
    asset.width = width;
    asset.height = height;
    asset.states = states;
    asset.insets.left = rd16(bytes + 16);
    asset.insets.top = rd16(bytes + 18);
    asset.insets.right = rd16(bytes + 20);
    asset.insets.bottom = rd16(bytes + 22);
    asset.pixel_art = (rd16(bytes + 24) & 1u) != 0;
    asset.data_bytes = data_bytes;

    if (kind != LW_UI_ASSET_ICON && !lw_ui9_insets_valid(width, height, &asset.insets)) {
        return LW_UI_ASSET_BAD_INSETS;
    }
    size_t per_state = lw_ui_asset_state_bytes(&asset);
    if (per_state == 0) {
        return LW_UI_ASSET_BAD_SIZE;
    }
    size_t expected = per_state * (size_t)states;
    if (expected > length - LW_UI_ASSET_HEADER_BYTES || data_bytes != (uint32_t)expected) {
        return LW_UI_ASSET_BAD_PAYLOAD;
    }
    asset.payload = bytes + LW_UI_ASSET_HEADER_BYTES;
    *out = asset;
    return LW_UI_ASSET_OK;
}

bool lw_ui_asset_fits(const lw_ui_asset_t *asset, uint16_t target_w, uint16_t target_h)
{
    if (asset == NULL || target_w == 0 || target_h == 0) {
        return false;
    }
    if (asset->kind == LW_UI_ASSET_ICON) {
        return true;
    }
    return lw_ui9_fits(asset->width, asset->height, &asset->insets, target_w, target_h);
}

void lw_ui_asset_minimum(const lw_ui_asset_t *asset, uint16_t *min_w, uint16_t *min_h)
{
    if (min_w != NULL) {
        *min_w = 0;
    }
    if (min_h != NULL) {
        *min_h = 0;
    }
    if (asset == NULL || asset->kind == LW_UI_ASSET_ICON) {
        return;
    }
    lw_ui9_minimum(asset->width, asset->height, &asset->insets, min_w, min_h);
}

bool lw_ui_asset_state_rect(const lw_ui_asset_t *asset, lw_ui9_button_state_t state,
                            lw_ui9_rect_t *out)
{
    if (asset == NULL || out == NULL) {
        return false;
    }
    if (asset->kind != LW_UI_ASSET_BUTTON) {
        if (state != LW_UI9_STATE_NORMAL) {
            return false;
        }
        out->x = 0;
        out->y = 0;
        out->w = asset->width;
        out->h = asset->height;
        return true;
    }
    uint16_t strip_h = (uint16_t)(asset->height * asset->states);
    return lw_ui9_state_rect(asset->width, strip_h, state, out);
}

size_t lw_ui_asset_piece_bytes(const lw_ui_asset_t *asset, uint16_t w, uint16_t h)
{
    if (asset == NULL || w == 0 || h == 0) {
        return 0;
    }
    size_t pixels = (size_t)w * h;
    return asset->format == LW_UI_ASSET_RGB565A8 ? pixels * 3u : pixels * 2u;
}

size_t lw_ui_asset_copy_piece(const lw_ui_asset_t *asset, unsigned state,
                              const lw_ui9_rect_t *src, uint8_t *dst, size_t cap)
{
    if (asset == NULL || src == NULL || dst == NULL || src->w == 0 || src->h == 0) {
        return 0;
    }
    if (state >= asset->states) {
        return 0;
    }
    size_t image_w = asset->width;
    size_t image_h = (size_t)asset->height * asset->states;
    if ((size_t)src->x + src->w > image_w) {
        return 0;
    }
    if ((size_t)src->y + src->h > image_h) {
        return 0;
    }
    size_t need = lw_ui_asset_piece_bytes(asset, src->w, src->h);
    if (need == 0 || need > cap) {
        return 0;
    }
    bool alpha = asset->format == LW_UI_ASSET_RGB565A8;
    size_t colour_plane = image_w * image_h * 2u;
    size_t colour_out = (size_t)src->w * src->h * 2u;
    for (unsigned row = 0; row < src->h; row++) {
        size_t src_row = (size_t)src->y + row;
        size_t src_col = (size_t)src->x;
        memcpy(dst + (size_t)row * src->w * 2u, asset->payload + (src_row * image_w + src_col) * 2u,
               (size_t)src->w * 2u);
        if (alpha) {
            memcpy(dst + colour_out + (size_t)row * src->w,
                   asset->payload + colour_plane + src_row * image_w + src_col, src->w);
        }
    }
    return need;
}
