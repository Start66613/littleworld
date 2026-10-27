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
