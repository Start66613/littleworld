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
    uint16_t scale, frame;
    bool enabled, paused, finished, external;
};

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

static bool present(lw_sprite_t *s, const lw_clip_t *clip, uint16_t frame)
{
    if (s->lease && s->frame==frame && !strcmp(s->shown_id,clip->asset_id)) return true;
    lw_asset_lease_t *next=lw_asset_acquire(clip->asset_id,frame);
    if (!next) return false; /* Keep the previous frame alive, including its decoder source. */
    const lw_asset_spec_t *spec=lw_assets_find(clip->asset_id);
    lv_image_set_src(s->image,lw_asset_image(next));
    lv_image_set_pivot(s->image,0,0);
    lv_image_set_scale(s->image,s->scale);
    lv_obj_set_pos(s->image,s->anchor_x-spec->anchor_x*s->scale/256,
                   s->anchor_y-spec->anchor_y*s->scale/256);
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
    if (s->enabled && !s->paused) (void)lw_sprite_seek(s,lv_tick_get()-s->epoch);
}

static void deleted(lv_event_t *event)
{
    lw_sprite_t *s=lv_event_get_user_data(event);
    lv_timer_delete(s->timer);
    lv_image_set_src(s->image,NULL);
    lw_asset_release(s->lease);
    lv_free(s);
}

lw_sprite_t *lw_sprite_create(lv_obj_t *parent, int x, int y, uint16_t scale)
{
    if (!parent || !scale || scale>256) return NULL; /* Export a larger asset rather than upscale. */
    lw_sprite_t *s=lv_malloc_zeroed(sizeof(*s));
    if (!s) return NULL;
    s->image=lv_image_create(parent);
    s->anchor_x=x; s->anchor_y=y; s->scale=scale; s->frame=UINT16_MAX; s->enabled=true; s->paused=true;
    lv_obj_remove_flag(s->image,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s->image,LV_OBJ_FLAG_HIDDEN);
    s->timer=lv_timer_create(tick,20,s);
    if (!s->timer) { lv_obj_delete(s->image); lv_free(s); return NULL; }
    lv_timer_pause(s->timer);
    lv_obj_add_event_cb(s->image,deleted,LV_EVENT_DELETE,s);
    return s;
}
lv_obj_t *lw_sprite_object(lw_sprite_t *s) { return s ? s->image : NULL; }
bool lw_sprite_play(lw_sprite_t *s, const lw_clip_t *clip)
{
    uint32_t total=clip_length(clip);
    if (!s || !total || !present(s,clip,clip->sequence[0])) return false;
    s->clip=clip; s->total=total; s->elapsed=0; s->epoch=lv_tick_get(); s->finished=false; s->paused=false;
    if (s->enabled && !s->external) lv_timer_resume(s->timer); else lv_timer_pause(s->timer);
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
{ if (!s || s->scale!=256) return false; lv_image_set_antialias(s->image,false); return true; }
void lw_sprite_set_anchor(lw_sprite_t *s,int x,int y)
{
    if (!s) return;
    s->anchor_x=x; s->anchor_y=y;
    const lw_asset_spec_t *spec=lw_assets_find(s->shown_id);
    if (spec) lv_obj_set_pos(s->image,x-spec->anchor_x*s->scale/256,y-spec->anchor_y*s->scale/256);
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
