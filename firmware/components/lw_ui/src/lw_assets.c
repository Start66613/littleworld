/* SPDX-License-Identifier: MIT */
#include "lw_assets.h"
#include "lw_fault.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define MAX_ASSETS 64u
#define MAX_FRAME_BYTES 1250000u
struct lw_asset_lease { lv_image_dsc_t image; uint8_t *pixels; const lw_asset_spec_t *spec; };
static const lw_asset_spec_t *s_specs;
static size_t s_count;
static lw_asset_stats_t s_stats;

size_t lw_asset_frame_bytes(const lw_asset_spec_t *s)
{
    if (!s || !s->width || !s->height || s->width > 1024 || s->height > 600) return 0;
    size_t pixels = (size_t)s->width * s->height;
    if (s->format == LV_COLOR_FORMAT_RGB565) return pixels * 2;
    if (s->format == LV_COLOR_FORMAT_RGB565A8) return pixels * 3;
    return 0;
}

bool lw_assets_register(const lw_asset_spec_t *specs, size_t count, size_t budget)
{
    if (s_stats.live_leases || count > MAX_ASSETS || (count && !specs) || !budget || budget > 2u*1024u*1024u) return false;
    for (size_t i=0; i<count; i++) {
        size_t bytes = lw_asset_frame_bytes(&specs[i]);
        if (!specs[i].id || !specs[i].id[0] || !specs[i].read || !bytes || bytes>MAX_FRAME_BYTES ||
            !specs[i].frames || specs[i].frames>32 || specs[i].anchor_x<0 || specs[i].anchor_y<0 ||
            specs[i].anchor_x>specs[i].width || specs[i].anchor_y>specs[i].height) return false;
        for (size_t j=0; j<i; j++) if (!strcmp(specs[i].id,specs[j].id)) return false;
    }
    s_specs=specs; s_count=count;
    memset(&s_stats,0,sizeof(s_stats)); s_stats.budget_bytes=budget;
    return true;
}

const lw_asset_spec_t *lw_assets_find(const char *id)
{
    if (id) for (size_t i=0;i<s_count;i++) if (!strcmp(id,s_specs[i].id)) return &s_specs[i];
    return NULL;
}

/* Content box in source pixels; the whole canvas when a pack predates bbox
 * metadata or the box would be degenerate.
 *
 * A prescaled asset is stored already cropped to its content and already at the
 * final size, so its whole frame is the content and its origin is 0:0. The
 * packer rewrites anchor_x/anchor_y into those cropped coordinates. */
static void spec_bbox(const lw_asset_spec_t *s, uint16_t *x, uint16_t *y,
                      uint16_t *w, uint16_t *h)
{
    if (s->prescaled) { *x = 0; *y = 0; *w = s->width; *h = s->height; return; }
    if (s->bbox_w && s->bbox_h && s->bbox_x < s->width && s->bbox_y < s->height) {
        *x = s->bbox_x;
        *y = s->bbox_y;
        *w = (uint16_t)((s->bbox_x + s->bbox_w > s->width) ? s->width - s->bbox_x : s->bbox_w);
        *h = (uint16_t)((s->bbox_y + s->bbox_h > s->height) ? s->height - s->bbox_y : s->bbox_h);
        return;
    }
    *x = 0; *y = 0; *w = s->width; *h = s->height;
}

/* 8.8 fixed point with LVGL's half-pixel bias. lv_draw_sw_transform.c does
 * "xs_ups = xs1_ups + 0x80", i.e. it rounds rather than truncates; using plain
 * truncation here shifts the content by one pixel whenever the fraction is
 * >= 0.5, which is exactly how couple.seated (57*282/256 = 62.79) failed to
 * match while male.idle (75*288/256 = 84.375) happened to match. */
static int32_t scale_px(int32_t v, uint16_t scale)
{
    return (v * (int32_t)scale + 0x80) >> 8;
}

/* Placement uses the truncating form on purpose: V4.4 positioned every sprite
 * at "anchor - anchor_x * scale / 256" in plain C integer arithmetic, and the
 * anchor is our own choice, not something LVGL computes. Keeping that form is
 * what makes the no-metadata path bit-identical to the V4.4 screenshots. */
static int32_t place_px(int32_t v, uint16_t scale)
{
    return (v * (int32_t)scale) / 256;
}

void lw_asset_draw_geometry(const lw_asset_spec_t *spec, uint16_t scale, lw_asset_draw_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!spec || !spec->width || !spec->height) return;

    uint16_t bx, by, bw, bh;
    spec_bbox(spec, &bx, &by, &bw, &bh);

    /* A prescaled asset already holds its final pixels, so it must be drawn at
     * 1:1 - any scale would re-enter the transform path this exists to avoid. */
    uint16_t use = spec->prescaled ? (uint16_t)LV_SCALE_NONE : scale;
    if (!use) use = (uint16_t)LV_SCALE_NONE;

    int32_t ow = scale_px((int32_t)bw, use);
    int32_t oh = scale_px((int32_t)bh, use);
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;

    out->scale = use;
    out->obj_w = (uint16_t)ow;
    out->obj_h = (uint16_t)oh;

    /* A pack with no geometry metadata must draw exactly like V4.4 did, or every
     * page shifts by a pixel and the screenshots stop matching. V4.4 left the
     * object at the decoded image size and relied on LVGL clipping to obj->coords:
     * a 288/256 sprite therefore had its rightmost and bottom 32 destination
     * columns clipped away. Giving that case a 288-wide object would *reveal*
     * pixels HEAD never drew (qianqian's 246-px content scales to 277 > 256), so
     * keep the canvas-sized object and a zero offset here. */
    if (!spec->prescaled && (!spec->bbox_w || !spec->bbox_h)) {
        out->scale = use;
        out->obj_w = spec->width;
        out->obj_h = spec->height;
        out->offset_x = 0;
        out->offset_y = 0;
        out->anchor_dx = place_px(spec->anchor_x, use);
        out->anchor_dy = place_px(spec->anchor_y, use);
        return;
    }

    /* Where the content lands relative to the object, in destination pixels.
     * For the full-canvas case that is bbox*scale/256; for a prescaled asset
     * the image *is* the content, so it is zero. */
    int32_t content_dx = spec->prescaled ? 0 : scale_px((int32_t)bx, use);
    int32_t content_dy = spec->prescaled ? 0 : scale_px((int32_t)by, use);

    /* LV_ALIGN_CENTER puts image_area at obj.x1 + (obj_w/2 - img_w/2) and then
     * adds the offset; the content sits content_dx further right. Subtracting
     * both makes image_area land exactly where the full-canvas draw put it.
     * Note LVGL halves the two dimensions separately, so (obj_w/2 - img_w/2) is
     * not (obj_w - img_w)/2. */
    int32_t img_w = spec->width, img_h = spec->height;
    out->offset_x = -content_dx - (ow / 2 - img_w / 2);
    out->offset_y = -content_dy - (oh / 2 - img_h / 2);

    /* The object origin sits at the content origin, so the distance from the
     * anchor to the object is the scaled anchor minus that same content origin.
     * Both terms are rounded the way LVGL rounds, not rounded after subtracting. */
    out->anchor_dx = scale_px(spec->anchor_x, use) - content_dx;
    out->anchor_dy = scale_px(spec->anchor_y, use) - content_dy;
}

lw_asset_lease_t *lw_asset_acquire(const char *id, uint16_t frame)
{
    const lw_asset_spec_t *spec=lw_assets_find(id);
    size_t bytes=lw_asset_frame_bytes(spec);
    if (!spec || frame>=spec->frames || bytes>s_stats.budget_bytes-s_stats.live_bytes) { s_stats.failures++; return NULL; }
    /* Injected lease refusal. Counted like any other acquire failure, and the
     * registry must still be intact afterwards - that is the property under
     * test, not the NULL itself. */
    if (lw_fault_should_fail(LW_FAULT_ASSET_ACQUIRE)) { s_stats.failures++; return NULL; }
    lw_asset_lease_t *lease=lv_malloc_zeroed(sizeof(*lease));
    if (!lease) { s_stats.failures++; return NULL; }
    lease->pixels=lv_malloc(bytes);
    if (!lease->pixels) { lv_free(lease); s_stats.failures++; return NULL; }
    if (!spec->read(spec->context,frame,lease->pixels,bytes)) {
        lv_free(lease->pixels); lv_free(lease); s_stats.failures++; return NULL;
    }
    lease->image.header.magic=LV_IMAGE_HEADER_MAGIC;
    lease->image.header.cf=spec->format;
    lease->image.header.w=spec->width; lease->image.header.h=spec->height;
    lease->image.header.stride=spec->width*2;
    lease->image.data_size=(uint32_t)bytes; lease->image.data=lease->pixels;
    lease->spec=spec;
    s_stats.live_bytes+=bytes; s_stats.live_leases++; s_stats.loaded_frames++;
    if (s_stats.live_bytes>s_stats.peak_bytes) s_stats.peak_bytes=s_stats.live_bytes;
    return lease;
}

bool lw_asset_refill(lw_asset_lease_t *lease, uint16_t frame)
{
    if (!lease || !lease->spec || frame>=lease->spec->frames) { s_stats.failures++; return false; }
    if (lw_fault_should_fail(LW_FAULT_ASSET_ACQUIRE)) { s_stats.failures++; return false; }
    size_t bytes=lease->image.data_size;
    if (!lease->spec->read(lease->spec->context,frame,lease->pixels,bytes)) { s_stats.failures++; return false; }
    s_stats.loaded_frames++;
    return true;
}

const lv_image_dsc_t *lw_asset_image(const lw_asset_lease_t *lease) { return lease ? &lease->image : NULL; }
void lw_asset_release(lw_asset_lease_t *lease)
{
    if (!lease) return;
    /* Source must already be detached from every image widget. */
    lv_image_cache_drop(&lease->image);
    s_stats.live_bytes-=lease->image.data_size; s_stats.live_leases--;
    lv_free(lease->pixels); lv_free(lease);
}
lw_asset_stats_t lw_assets_stats(void) { return s_stats; }

bool lw_asset_read_raw_file(void *context, uint16_t frame, uint8_t *pixels, size_t bytes)
{
    const lw_asset_raw_file_t *file=context;
    if (!file || !file->path || !pixels || !bytes || frame>=file->frames || bytes>file->frame_stride) return false;
    uint64_t offset=(uint64_t)file->offset+(uint64_t)frame*file->frame_stride;
    if (offset>LONG_MAX) return false;
    FILE *stream=fopen(file->path,"rb");
    if (!stream) return false;
    bool ok=fseek(stream,(long)offset,SEEK_SET)==0 && fread(pixels,1,bytes,stream)==bytes;
    fclose(stream);
    return ok;
}
