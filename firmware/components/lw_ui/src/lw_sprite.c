/* SPDX-License-Identifier: MIT */
#include "lw_sprite.h"
#include <string.h>

struct lw_sprite {
    lv_obj_t *image;
    lv_timer_t *timer;
    lw_asset_lease_t *lease;
    const lw_clip_t *clip;
    const char *shown_id;
    uint32_t epoch, elapsed, total;
    int anchor_x, anchor_y;
    /* V4.5: how far the object origin sits from the anchor, which depends on the
     * asset's content box and is therefore no longer "anchor * scale / 256". */
    int anchor_dx, anchor_dy;
    uint16_t scale, frame;
    bool enabled, paused, finished, external;
    struct lw_sprite *prev, *next;
};

/* Live sprites, in creation order. Two sprites exist on the desktop, so a list
 * is cheaper than any registry, and it lets the diagnostics answer "is anything
 * actually animating" without the UI handing out pointers to its own state. */
static lw_sprite_t *s_head;

static void link_sprite(lw_sprite_t *s)
{
    s->prev = NULL;
    s->next = s_head;
    if (s_head) {
        s_head->prev = s;
    }
    s_head = s;
}

static void unlink_sprite(lw_sprite_t *s)
{
    if (s->prev) {
        s->prev->next = s->next;
    } else if (s_head == s) {
        s_head = s->next;
    }
    if (s->next) {
        s->next->prev = s->prev;
    }
    s->prev = s->next = NULL;
}

void lw_sprite_stats(unsigned *live, unsigned *running)
{
    unsigned count = 0;
    unsigned moving = 0;
    for (const lw_sprite_t *s = s_head; s != NULL; s = s->next) {
        count++;
        /* The same condition tick() uses to advance a frame, so this is what the
         * panel is really doing rather than what was requested. */
        if (s->clip && s->enabled && !s->paused && !s->finished) {
            moving++;
        }
    }
    if (live) {
        *live = count;
    }
    if (running) {
        *running = moving;
    }
}

static uint32_t clip_length(const lw_clip_t *clip)
{
    const lw_asset_spec_t *spec=clip ? lw_assets_find(clip->asset_id) : NULL;
    if (!spec || !clip->sequence || !clip->duration_ms || !clip->count || clip->count>32) return 0;
    uint32_t total=0;
    for (uint16_t i=0;i<clip->count;i++) {
        if (clip->sequence[i]>=spec->frames || clip->duration_ms[i]<20 || clip->duration_ms[i]>2000) return 0;
        total+=clip->duration_ms[i];
    }
    return total;
}

/* Re-apply the asset's draw geometry: object box, offset and position.
 *
 * V4.5: this used to hardcode "object = whole canvas, scale = s->scale,
 * position = anchor - spec->anchor*scale/256". LVGL clips to obj->coords, so
 * that made every frame blend the full 256x256 / 320x320 canvas - about 67% of
 * it fully transparent - and, because the scale was 288 or 282 rather than
 * LV_SCALE_NONE, it also forced the per-pixel transform_and_recolor() path.
 * Both are decided once in lw_asset_draw_geometry() now. */
/* A pack without bbox metadata or prescaling must render exactly as V4.4 did,
 * so that path keeps the original call sequence: no explicit size, no offset.
 * Only the two new asset shapes touch the object box. */
static bool has_geometry(const lw_asset_spec_t *spec)
{
    return spec->prescaled || (spec->bbox_w && spec->bbox_h);
}

static void apply_geometry(lw_sprite_t *s, const lw_asset_spec_t *spec)
{
    lw_asset_draw_t g;
    lw_asset_draw_geometry(spec, s->scale, &g);
    if (has_geometry(spec)) {
        lv_obj_set_size(s->image, g.obj_w, g.obj_h);
        lv_image_set_pivot(s->image, 0, 0);
        lv_image_set_scale(s->image, g.scale);
        lv_image_set_offset_x(s->image, g.offset_x);
        lv_image_set_offset_y(s->image, g.offset_y);
    }
    else {
        lv_image_set_pivot(s->image, 0, 0);
        lv_image_set_scale(s->image, g.scale);
    }
    s->anchor_dx = g.anchor_dx;
    s->anchor_dy = g.anchor_dy;
}

static bool present(lw_sprite_t *s, const lw_clip_t *clip, uint16_t frame)
{
    if (s->lease && s->frame==frame && !strcmp(s->shown_id,clip->asset_id)) return true;
    /* Same asset, next frame: refill the lease's existing pixel buffer and just
     * invalidate the widget. Allocating and freeing a frame buffer (plus
     * lv_image_cache_drop) on every animation frame was the main cause of the
     * stutter - a 196 KB buffer churned 8-16 times per second per actor. */
    if (s->lease && !strcmp(s->shown_id,clip->asset_id) && lw_asset_refill(s->lease,frame)) {
        s->frame=frame;
        lv_obj_invalidate(s->image);
        return true;
    }
    lw_asset_lease_t *next=lw_asset_acquire(clip->asset_id,frame);
    if (!next) return false; /* Keep the previous frame alive, including its decoder source. */
    const lw_asset_spec_t *spec=lw_assets_find(clip->asset_id);
    if (!spec) { lw_asset_release(next); return false; }
    lv_image_set_src(s->image,lw_asset_image(next));
    apply_geometry(s,spec);
    lv_obj_set_pos(s->image, s->anchor_x - s->anchor_dx, s->anchor_y - s->anchor_dy);
    lv_obj_remove_flag(s->image,LV_OBJ_FLAG_HIDDEN);
    lw_asset_release(s->lease); s->lease=next; s->shown_id=spec->id; s->frame=frame;
    return true;
}

bool lw_sprite_seek(lw_sprite_t *s, uint32_t elapsed)
{
    if (!s || !s->clip) return false;
    uint32_t time=elapsed;
    bool finished=!s->clip->loop && time>=s->total;
    if (s->clip->loop) time%=s->total;
    else if (finished) time=s->total-1;
    uint16_t index=0;
    while (index+1<s->clip->count && time>=s->clip->duration_ms[index]) {
        time-=s->clip->duration_ms[index++];
    }
    if (!present(s,s->clip,s->clip->sequence[index])) return false;
    s->elapsed=elapsed; s->epoch=lv_tick_get()-elapsed; s->finished=finished;
    if (finished) lv_timer_pause(s->timer);
    return true;
}

static void tick(lv_timer_t *timer)
{
    lw_sprite_t *s=lv_timer_get_user_data(timer);
    /* No clip yet (created but never played) means nothing to advance; seeking
     * with a NULL clip would be a no-op anyway, but this keeps the running
     * condition in lw_sprite_stats() and here identical. */
    if (!s || !s->clip) return;
    if (s->enabled && !s->paused) (void)lw_sprite_seek(s,lv_tick_get()-s->epoch);
}

static void deleted(lv_event_t *event)
{
    lw_sprite_t *s=lv_event_get_user_data(event);
    lv_timer_delete(s->timer);
    lv_image_set_src(s->image,NULL);
    lw_asset_release(s->lease);
    unlink_sprite(s);
    lv_free(s);
}

lw_sprite_t *lw_sprite_create(lv_obj_t *parent, int x, int y, uint16_t scale)
{
    if (!parent || !scale || scale>320) return NULL; /* User-approved V4.3 proportion correction, bounded to 1.25x. */
    lw_sprite_t *s=lv_malloc_zeroed(sizeof(*s));
    if (!s) return NULL;
    s->image=lv_image_create(parent);
    s->anchor_x=x; s->anchor_y=y; s->scale=scale; s->frame=UINT16_MAX; s->enabled=true; s->paused=true;
    lv_obj_remove_flag(s->image,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s->image,LV_OBJ_FLAG_HIDDEN);
    s->timer=lv_timer_create(tick,20,s);
    if (!s->timer) { lv_obj_delete(s->image); lv_free(s); return NULL; }
    lv_timer_pause(s->timer);
#if CONFIG_LW_DIAG_STATIC_UI
    /* Diagnostic build C: lw_sprite_play() re-pauses the timer, so the panel
     * keeps showing frame 0 and never sees a new image buffer. Comparing this
     * build with the normal one separates "the animation load hurts the display"
     * from "the display path flickers on its own". */
#endif
    lv_obj_add_event_cb(s->image,deleted,LV_EVENT_DELETE,s);
    link_sprite(s);
    return s;
}
lv_obj_t *lw_sprite_object(lw_sprite_t *s) { return s ? s->image : NULL; }
bool lw_sprite_play(lw_sprite_t *s, const lw_clip_t *clip)
{
    uint32_t total=clip_length(clip);
    if (!s || !total || !present(s,clip,clip->sequence[0])) return false;
    s->clip=clip; s->total=total; s->elapsed=0; s->epoch=lv_tick_get(); s->finished=false; s->paused=false;
    /* Queue the rest of the clip for background read-ahead. Walking needs a new
     * frame every 50 ms, and a demand read of one frame from the card measured
     * 60-75 ms on the board, so waiting for it on the draw path can never keep
     * up; asking for the whole clip up front is what turns "every frame misses"
     * into "the first frame misses". */
    if (clip->count > 1) {
        const lw_asset_spec_t *spec=lw_assets_find(clip->asset_id);
        if (spec && spec->prefetch) {
            for (unsigned i=1;i<clip->count;i++) spec->prefetch(spec->context,clip->sequence[i]);
        }
    }
    if (s->enabled && !s->external) lv_timer_resume(s->timer); else lv_timer_pause(s->timer);
#if CONFIG_LW_DIAG_STATIC_UI
    lv_timer_pause(s->timer);   /* diagnostic C: keep the first frame */
#endif
    return true;
}
void lw_sprite_pause(lw_sprite_t *s)
{
    if (!s) return;
    if (!s->paused && s->enabled && s->clip && !s->finished) s->elapsed=lv_tick_get()-s->epoch;
    s->paused=true; lv_timer_pause(s->timer);
}
void lw_sprite_resume(lw_sprite_t *s)
{
    if (!s || !s->clip || s->finished) return;
    s->paused=false; s->epoch=lv_tick_get()-s->elapsed;
    if (s->enabled && !s->external) lv_timer_resume(s->timer);
}
void lw_sprite_set_enabled(lw_sprite_t *s, bool enabled)
{
    if (!s || s->enabled==enabled) return;
    s->enabled=enabled;
    if (!enabled) { if (s->clip) (void)lw_sprite_seek(s,0); lv_timer_pause(s->timer); }
    else { s->epoch=lv_tick_get()-s->elapsed; if (!s->paused && !s->finished && s->clip && !s->external) lv_timer_resume(s->timer); }
}
bool lw_sprite_set_pixel_mode(lw_sprite_t *s)
{ if (!s) return false; lv_image_set_antialias(s->image,false); return true; }
void lw_sprite_set_anchor(lw_sprite_t *s,int x,int y)
{
    if (!s) return;
    s->anchor_x=x; s->anchor_y=y;
    /* The object origin is no longer "anchor - spec->anchor*scale/256": with a
     * content box the image sits at the box origin, so the distance to the
     * anchor is what lw_asset_draw_geometry() computed. */
    lv_obj_set_pos(s->image,x-s->anchor_dx,y-s->anchor_dy);
}
void lw_sprite_set_external_clock(lw_sprite_t *s,bool external)
{
    if (!s) return;
    s->external=external; s->epoch=lv_tick_get()-s->elapsed;
    if (external || !s->enabled || s->paused || s->finished || !s->clip) lv_timer_pause(s->timer);
    else lv_timer_resume(s->timer);
}
uint16_t lw_sprite_frame(const lw_sprite_t *s) { return s ? s->frame : UINT16_MAX; }
bool lw_sprite_finished(const lw_sprite_t *s) { return s && s->finished; }
void lw_sprite_destroy(lw_sprite_t *s) { if (s) lv_obj_delete(s->image); }
