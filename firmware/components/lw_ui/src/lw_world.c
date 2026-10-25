/* SPDX-License-Identifier: MIT */
#include "lw_world.h"
#include "lw_theme.h"
#include "lw_widgets.h"

struct lw_world {
    lw_scene_t *scene;
    lw_sprite_t *sprite;
    lv_obj_t *days, *status;
    lw_clip_t idle;
    uint16_t sequence[32], durations[32];
    bool has_art;
};
static const lw_hotspot_t s_hotspots[]={
    {72,272,164,128,LW_HIT_REST}, {574,350,112,100,LW_HIT_FEED},
    {584,224,104,104,LW_HIT_PLAY}, {302,218,224,240,LW_HIT_PET},
    {744,112,240,156,LW_HIT_ANNIVERSARY}, {776,304,192,144,LW_HIT_LETTER}
};
static void deleted(lv_event_t *event) { lv_free(lv_event_get_user_data(event)); }
lw_world_t *lw_world_create(lv_obj_t *content, lw_world_kind_t kind, lw_scene_action_fn action, void *context)
{
    if (kind!=LW_WORLD_HOME && kind!=LW_WORLD_PET) return NULL;
    lw_world_t *w=lv_malloc_zeroed(sizeof(*w)); if (!w) return NULL;
    w->scene=lw_scene_create(content,64,440,action,context);
    if (!w->scene) { lv_free(w); return NULL; }
    bool background_loaded=lw_scene_image(w->scene,LW_LAYER_BACKGROUND,"room.home",0,0);
    const lw_asset_spec_t *room=lw_assets_find("room.home");
    w->has_art=background_loaded && room && room->width==1024 && room->height==600;
    /* Optional decor failure never hides the navigation or the character. */
    if (lw_assets_find("ui.anniversary")) (void)lw_scene_image(w->scene,LW_LAYER_MIDDLE,"ui.anniversary",744,112);
    if (lw_assets_find("ui.envelope.closed")) (void)lw_scene_image(w->scene,LW_LAYER_MIDDLE,"ui.envelope.closed",776,304);
    const lw_asset_spec_t *idle=lw_assets_find("pet.idle");
    if (!idle || idle->width!=256 || idle->height!=256 || idle->anchor_x!=128 || idle->anchor_y!=236) w->has_art=false;
    w->sprite=lw_scene_sprite(w->scene,kind==LW_WORLD_HOME ? 414 : 430,kind==LW_WORLD_HOME ? 446 : 448,
                              kind==LW_WORLD_HOME ? 224 : 256);
    if (idle && w->sprite) {
        for (uint16_t i=0;i<idle->frames;i++) { w->sequence[i]=i; w->durations[i]=125; }
        w->idle=(lw_clip_t){"pet.idle",w->sequence,w->durations,idle->frames,true};
        if (!lw_sprite_play(w->sprite,&w->idle)) w->has_art=false;
    } else w->has_art=false;
    for (unsigned i=0;i<sizeof(s_hotspots)/sizeof(s_hotspots[0]);i++) (void)lw_scene_hotspot(w->scene,&s_hotspots[i]);
    lv_obj_t *overlay=lw_scene_layer(w->scene,LW_LAYER_OVERLAY);
    w->days=lw_text(overlay,"",&lw_font_22,lw_palette()->text,768,84);
    w->status=lw_text(overlay,"",&lw_font_18,lw_palette()->dim,48,400);
    if (!w->has_art) {
        lw_text(overlay,"Technical preview - artwork pending",&lv_font_montserrat_14,0x796779,48,24);
    }
    lv_obj_add_event_cb(lw_scene_root(w->scene),deleted,LV_EVENT_DELETE,w);
    return w;
}
lw_scene_t *lw_world_scene(lw_world_t *w) { return w ? w->scene : NULL; }
bool lw_world_has_art(const lw_world_t *w) { return w && w->has_art; }
void lw_world_refresh(lw_world_t *w, const lw_pet_t *pet, uint32_t now, lw_date_t anniversary, lw_date_t today)
{
    if (!w || !pet) return;
    int days=lw_days_between(anniversary,today)+1; if (days<1) days=1;
    lv_label_set_text_fmt(w->days,"在一起 %d 天",days);
    lv_label_set_text_fmt(w->status,"%s   |   饱腹 %u   精力 %u   好感 %u",lw_pet_mood_name(lw_pet_mood(pet,now)),
                          (unsigned)pet->fullness,(unsigned)pet->energy,(unsigned)pet->affection);
}
bool lw_world_motion(lw_world_t *w, const lw_clip_t *clip) { return w && w->sprite && lw_sprite_play(w->sprite,clip); }
void lw_world_destroy(lw_world_t *w) { if (w) lw_scene_destroy(w->scene); }
