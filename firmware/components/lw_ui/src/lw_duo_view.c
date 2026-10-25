/* Native pixel actors in a temporary room. Not final art. SPDX-License-Identifier: MIT */
#include "lw_duo_view.h"
#include "lw_theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { struct lw_duo_view *view; unsigned actor; bool wake; } hit_context_t;
struct lw_duo_view {
    lw_scene_t *scene;
    lw_duo_t *model;
    const lw_actor_art_t *art;
    lw_sprite_t *sprites[2];
    const lw_clip_t *current[2];
    lv_obj_t *names[2],*hits[2],*status,*menu,*selected_name;
    lv_timer_t *timer;
    hit_context_t hit[2],options[2];
    unsigned selected;
    uint32_t last_tick;
    bool animations;
};
static lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y,int width)
{
    lv_obj_t *o=lv_label_create(parent); lv_label_set_text(o,value); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,width);
    lv_obj_set_style_text_font(o,&lv_font_montserrat_14,0); lv_obj_set_style_text_color(o,lv_color_hex(0x574b5c),0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE); return o;
}
static lv_obj_t *rect(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius)
{
    lv_obj_t *o=lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_opa(o,255,0); lv_obj_set_style_bg_color(o,lv_color_hex(color),0); lv_obj_set_style_radius(o,radius,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE); return o;
}
static void temporary_room(lv_obj_t *root)
{
    rect(root,0,0,1024,440,0xfff8ee,0);
    rect(root,0,244,1024,196,0xf0dfca,0);
    rect(root,0,240,1024,4,0xcbb6a6,0);
    for (int y=272;y<440;y+=40) rect(root,0,y,1024,1,0xe1ccb7,0);
    for (int x=96;x<1024;x+=176) { rect(root,x,244,1,28,0xe1ccb7,0); rect(root,x+70,312,1,40,0xe1ccb7,0); }
    rect(root,64,28,212,164,0xd7c3ac,3); rect(root,72,36,196,148,0xdbe8e5,0);
    rect(root,166,36,8,148,0xd7c3ac,0); rect(root,72,106,196,7,0xd7c3ac,0);
    rect(root,780,78,168,112,0xd8c4d8,8); rect(root,792,66,144,78,0xe7d8e7,8);
    rect(root,800,186,8,28,0xa58e7a,0); rect(root,920,186,8,28,0xa58e7a,0);
    text(root,"TECHNICAL ROOM / original qianqian + TEST A placeholder",310,10,650);
}
static const lw_clip_t *find_clip(const lw_actor_art_t *art,const char *name)
{
    for (unsigned i=0;i<art->count;i++) if (!strcmp(art->clips[i].name,name)) return &art->clips[i].clip;
    return NULL;
}
static const char *motion_name(lw_actor_motion_t motion)
{
    static const char *const names[]={"idle","walk","touch","wave","sit","letter","window","celebrate","sleep_idle"};
    return (unsigned)motion<sizeof(names)/sizeof(names[0]) ? names[motion] : "idle";
}
static const lw_clip_t *choose(lw_duo_view_t *v,unsigned i,bool *fallback,bool *still)
{
    static const char *const directions[]={"down","down_left","left","up_left","up","up_right","right","down_right"};
    const lw_actor_t *actor=&v->model->actors[i]; char name[48];
    *fallback=false; *still=false;
    if (actor->motion==LW_ACTOR_WALK || v->model->phase==LW_DUO_ALIGN) {
        snprintf(name,sizeof(name),"walk_%s",directions[actor->facing]);
        *still=actor->motion!=LW_ACTOR_WALK;
    } else snprintf(name,sizeof(name),"%s",motion_name(actor->motion));
    const lw_clip_t *clip=find_clip(&v->art[i],name);
    if (!clip) { clip=find_clip(&v->art[i],"idle"); *fallback=true; }
    return clip;
}
static void refresh(lw_duo_view_t *v)
{
    unsigned fallback_count=0;
    for (unsigned i=0;i<2;i++) {
        const lw_actor_t *actor=&v->model->actors[i]; bool fallback,still;
        const lw_clip_t *clip=choose(v,i,&fallback,&still);
        if (fallback) fallback_count++;
        if (clip && clip!=v->current[i]) {
            if (lw_sprite_play(v->sprites[i],clip)) v->current[i]=clip;
        }
        if (clip && v->current[i]==clip) {
            uint32_t elapsed=v->model->phase==LW_DUO_ACT ? v->model->interaction_ms : actor->animation_ms;
            (void)lw_sprite_seek(v->sprites[i],!v->animations || still ? 0 : elapsed);
        }
        int x=(int)lroundf(actor->x),y=(int)lroundf(actor->y)-64;
        lw_sprite_set_anchor(v->sprites[i],x,y);
        lv_obj_set_pos(v->names[i],x-68,y-(actor->sleeping ? 110 : 236));
        lv_obj_set_pos(v->hits[i],x-(actor->sleeping ? 112 : 56),y-(actor->sleeping ? 100 : 210));
        lv_obj_set_size(v->hits[i],actor->sleeping ? 224 : 112,actor->sleeping ? 100 : 220);
    }
    unsigned near=v->model->actors[0].y>v->model->actors[1].y ? 0 : 1;
    lv_obj_move_to_index(lw_sprite_object(v->sprites[near]),1);
    const char *phase=v->model->phase==LW_DUO_FREE ? "Living independently" : lw_duo_interaction(v->model->interaction)->name;
    lv_label_set_text_fmt(v->status,"%s  |  %s  |  pose fallbacks: %u",phase,
                          v->art[0].placeholder ? "TEST A IS NOT FINAL MALE ART" : "local art preview",fallback_count);
}
void lw_duo_view_update(lw_duo_view_t *v,uint32_t dt)
{ if (v) { if (v->animations) lw_duo_tick(v->model,dt); refresh(v); } }
static void timer(lv_timer_t *timer)
{
    lw_duo_view_t *v=lv_timer_get_user_data(timer); uint32_t now=lv_tick_get();
    lw_duo_view_update(v,now-v->last_tick); v->last_tick=now;
}
static void option(lv_event_t *event)
{
    hit_context_t *h=lv_event_get_user_data(event);
    if (h->wake) lw_duo_sleep(h->view->model,h->view->selected,!h->view->model->actors[h->view->selected].sleeping);
    else lw_duo_touch(h->view->model,h->view->selected);
    lv_obj_add_flag(h->view->menu,LV_OBJ_FLAG_HIDDEN); refresh(h->view);
}
void lw_duo_view_select(lw_duo_view_t *v,unsigned i)
{
    if (!v || i>=2) return;
    v->selected=i; lv_label_set_text(v->selected_name,v->art[i].label); lv_obj_remove_flag(v->menu,LV_OBJ_FLAG_HIDDEN);
}
static void selected(lv_event_t *event)
{ hit_context_t *h=lv_event_get_user_data(event); lw_duo_view_select(h->view,h->actor); }
static void deleted(lv_event_t *event)
{ lw_duo_view_t *v=lv_event_get_user_data(event); lv_timer_delete(v->timer); lv_free(v); }
lw_duo_view_t *lw_duo_view_create(lv_obj_t *parent,lw_duo_t *model,const lw_actor_art_t art[2])
{
    if (!parent || !model || !art || !find_clip(&art[0],"idle") || !find_clip(&art[1],"idle")) return NULL;
    lw_duo_view_t *v=lv_malloc_zeroed(sizeof(*v)); if (!v) return NULL;
    v->model=model; v->art=art; v->animations=true; v->last_tick=lv_tick_get();
    v->scene=lw_scene_create(parent,64,440,NULL,NULL); if (!v->scene) { lv_free(v); return NULL; }
    temporary_room(lw_scene_layer(v->scene,LW_LAYER_BACKGROUND));
    lv_obj_t *overlay=lw_scene_layer(v->scene,LW_LAYER_OVERLAY);
    for (unsigned i=0;i<2;i++) {
        v->sprites[i]=lw_scene_sprite(v->scene,0,0,256);
        if (!v->sprites[i]) { lw_scene_destroy(v->scene); lv_free(v); return NULL; }
        (void)lw_sprite_set_pixel_mode(v->sprites[i]); lw_sprite_set_external_clock(v->sprites[i],true);
        v->names[i]=text(overlay,art[i].label,0,0,136); lv_obj_set_style_text_align(v->names[i],LV_TEXT_ALIGN_CENTER,0);
        v->hits[i]=lv_obj_create(lw_scene_layer(v->scene,LW_LAYER_HOTSPOTS)); lv_obj_remove_style_all(v->hits[i]);
        lv_obj_add_flag(v->hits[i],LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(v->hits[i],LV_OBJ_FLAG_SCROLLABLE);
        v->hit[i]=(hit_context_t){v,i,false}; lv_obj_add_event_cb(v->hits[i],selected,LV_EVENT_CLICKED,&v->hit[i]);
    }
    v->status=text(overlay,"",24,416,980);
    v->menu=rect(overlay,384,30,256,176,0xfffcf8,18);
    lv_obj_add_flag(v->menu,LV_OBJ_FLAG_CLICKABLE);
    v->selected_name=text(v->menu,"",20,16,220);
    for (unsigned i=0;i<2;i++) {
        lv_obj_t *button=rect(v->menu,16,48+(int)i*60,224,48,0xf1e5ef,12);
        lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE); text(button,i ? "Sleep / wake" : "Touch",16,15,190);
        v->options[i]=(hit_context_t){v,0,i==1}; lv_obj_add_event_cb(button,option,LV_EVENT_CLICKED,&v->options[i]);
    }
    lv_obj_add_flag(v->menu,LV_OBJ_FLAG_HIDDEN);
    v->timer=lv_timer_create(timer,20,v);
    if (!v->timer) { lw_scene_destroy(v->scene); lv_free(v); return NULL; }
    lv_obj_add_event_cb(lw_scene_root(v->scene),deleted,LV_EVENT_DELETE,v);
    refresh(v); return v;
}
void lw_duo_view_set_animations(lw_duo_view_t *v,bool enabled)
{ if (v) { v->animations=enabled; v->last_tick=lv_tick_get(); refresh(v); } }
bool lw_duo_view_request(lw_duo_view_t *v,lw_interaction_t kind)
{ return v && lw_duo_request(v->model,kind); }
lv_obj_t *lw_duo_view_root(lw_duo_view_t *v) { return v ? lw_scene_root(v->scene) : NULL; }
void lw_duo_view_destroy(lw_duo_view_t *v) { if (v) lw_scene_destroy(v->scene); }
