/* SPDX-License-Identifier: MIT */
#include "lw_scene.h"

#define MAX_SCENE_IMAGES 8
#define MAX_HOTSPOTS 12
typedef struct { lw_scene_t *scene; lw_scene_action_t action; } hit_context_t;
struct lw_scene {
    lv_obj_t *root, *layers[LW_LAYER_COUNT];
    lv_obj_t *images[MAX_SCENE_IMAGES];
    lw_asset_lease_t *leases[MAX_SCENE_IMAGES];
    hit_context_t hits[MAX_HOTSPOTS];
    unsigned image_count, hit_count;
    int screen_y, height;
    lw_scene_action_fn action;
    void *context;
};
static lv_obj_t *container(lv_obj_t *parent, int height)
{
    lv_obj_t *o=lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_size(o,1024,height);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static void deleted(lv_event_t *event)
{
    lw_scene_t *s=lv_event_get_user_data(event);
    for (unsigned i=0;i<s->image_count;i++) { lv_image_set_src(s->images[i],NULL); lw_asset_release(s->leases[i]); }
    lv_free(s);
}
static void hit(lv_event_t *event)
{
    hit_context_t *h=lv_event_get_user_data(event);
    if (h->scene->action) h->scene->action(h->action,h->scene->context);
}
lw_scene_t *lw_scene_create(lv_obj_t *parent, int y, int height, lw_scene_action_fn action, void *context)
{
    if (!parent || y<0 || height<=0 || y+height>600) return NULL;
    lw_scene_t *s=lv_malloc_zeroed(sizeof(*s)); if (!s) return NULL;
    s->screen_y=y; s->height=height; s->action=action; s->context=context;
    s->root=container(parent,height);
    for (unsigned i=0;i<LW_LAYER_COUNT;i++) s->layers[i]=container(s->root,height);
    lv_obj_add_event_cb(s->root,deleted,LV_EVENT_DELETE,s);
    return s;
}
lv_obj_t *lw_scene_root(lw_scene_t *s) { return s ? s->root : NULL; }
lv_obj_t *lw_scene_layer(lw_scene_t *s, lw_scene_layer_t layer)
{ return s && (unsigned)layer<LW_LAYER_COUNT ? s->layers[layer] : NULL; }
bool lw_scene_image(lw_scene_t *s, lw_scene_layer_t layer, const char *id, int x, int y)
{
    lv_obj_t *parent=lw_scene_layer(s,layer);
    if (!parent || s->image_count>=MAX_SCENE_IMAGES) return false;
    lw_asset_lease_t *lease=lw_asset_acquire(id,0);
    if (!lease) return false;
    lv_obj_t *image=lv_image_create(parent);
    lv_image_set_src(image,lw_asset_image(lease)); lv_obj_set_pos(image,x,y-s->screen_y);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);
    s->images[s->image_count]=image; s->leases[s->image_count++]=lease;
    return true;
}
bool lw_scene_hotspot(lw_scene_t *s, const lw_hotspot_t *r)
{
    if (!s || !r || s->hit_count>=MAX_HOTSPOTS || r->x<0 || r->y<s->screen_y || r->w<48 || r->h<48 ||
        r->w>1024 || r->h>s->height || r->x>1024-r->w || r->y>s->screen_y+s->height-r->h ||
        (unsigned)r->action>LW_HIT_LETTER) return false;
    hit_context_t *ctx=&s->hits[s->hit_count++]; ctx->scene=s; ctx->action=r->action;
    lv_obj_t *button=lv_obj_create(s->layers[LW_LAYER_HOTSPOTS]); lv_obj_remove_style_all(button);
    lv_obj_set_pos(button,r->x,r->y-s->screen_y); lv_obj_set_size(button,r->w,r->h);
    lv_obj_remove_flag(button,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button,hit,LV_EVENT_CLICKED,ctx);
    return true;
}
lw_sprite_t *lw_scene_sprite(lw_scene_t *s, int x, int y, uint16_t scale)
{ return s ? lw_sprite_create(s->layers[LW_LAYER_CHARACTER],x,y-s->screen_y,scale) : NULL; }
void lw_scene_destroy(lw_scene_t *s) { if (s) lv_obj_delete(s->root); }
