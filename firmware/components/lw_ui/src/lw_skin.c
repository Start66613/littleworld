#include "lw_skin.h"
#include "lw_assets.h"
#include "lw_os_design.h"
#include "lw_ambience.h"
typedef struct {int x,y,w,h;} rect_t;
static const rect_t rects[SK_COUNT]={
 {0,0,512,44},{0,75,316,45},{316,75,196,28},
 {0,120,112,44},{112,120,112,44},{224,120,112,44},
 {336,120,64,44},{400,120,56,44},{456,120,20,58},
 {0,178,148,136},{0,314,480,220},
 {152,178,128,128},{284,178,96,48},{380,178,96,48},{284,230,96,48},{492,178,16,128},{380,230,32,40}};
static lw_asset_lease_t *atlas;static unsigned refs;
typedef struct {lv_obj_t *image,*button;rect_t source;int width,height,offx,offy;lw_skin_id_t base,selected,pressed;} slice_t;
static void pressed(lv_event_t *e);
static void show(slice_t *s,rect_t r)
{
    s->source=r;unsigned sx=(unsigned)s->width*256/(unsigned)r.w,sy=(unsigned)s->height*256/(unsigned)r.h;
    lv_image_set_scale_x(s->image,sx);lv_image_set_scale_y(s->image,sy);lv_image_set_pivot(s->image,0,0);lv_obj_set_pos(s->image,-r.x*(int)sx/256,-r.y*(int)sy/256);
}
static void deleted(lv_event_t *e)
{
    slice_t *s=lv_event_get_user_data(e);if(s->button)lv_obj_remove_event_cb_with_user_data(s->button,pressed,s);lv_image_set_src(s->image,NULL);lv_free(s);
    if(refs && !--refs){lw_asset_release(atlas);atlas=NULL;}
}
static lv_obj_t *piece(lv_obj_t *p,rect_t r,int x,int y,int w,int h)
{
    if(w<1 || h<1 || r.w<1 || r.h<1){return NULL;}
    if(!atlas && !lw_assets_find("v43skin.atlas"))return NULL;
    if(!atlas)atlas=lw_asset_acquire("v43skin.atlas",0);
    if(!atlas)return NULL;
    slice_t *s=lv_malloc_zeroed(sizeof(*s));if(!s){if(!refs){lw_asset_release(atlas);atlas=NULL;}return NULL;}
    lv_obj_t *o=lw_os_box(p,x,y,w,h,0,0,0);s->image=lv_image_create(o);s->width=w;s->height=h;
    lv_image_set_src(s->image,lw_asset_image(atlas));lv_image_set_antialias(s->image,false);lv_obj_remove_flag(s->image,LV_OBJ_FLAG_CLICKABLE);show(s,r);
    if(lw_ambience_phase()>=LW_NIGHT){lv_obj_set_style_image_recolor(s->image,lv_color_hex(0xb79496),0);lv_obj_set_style_image_recolor_opa(s->image,26,0);}
    else if(lw_ambience_phase()==LW_DUSK){lv_obj_set_style_image_recolor(s->image,lv_color_hex(0xd8a17d),0);lv_obj_set_style_image_recolor_opa(s->image,12,0);}
    refs++;lv_obj_set_user_data(o,s);lv_obj_add_event_cb(o,deleted,LV_EVENT_DELETE,s);return o;
}
lv_obj_t *lw_skin_place(lv_obj_t *p,lw_skin_id_t id,int x,int y)
{if(id>=SK_COUNT){return NULL;}rect_t r=rects[id];return piece(p,r,x,y,r.w,r.h);}
lv_obj_t *lw_skin_fit(lv_obj_t *p,lw_skin_id_t id,int x,int y,int w,int h)
{return id<SK_COUNT ? piece(p,rects[id],x,y,w,h) : NULL;}
lv_obj_t *lw_skin_panel(lv_obj_t *p,lw_skin_id_t id,int w,int h,int b)
{
    if(id>=SK_COUNT || b<1 || w<=2*b || h<=2*b || !lw_assets_find("v43skin.atlas"))return NULL;
    rect_t r=rects[id];if(r.w<=2*b || r.h<=2*b){return NULL;}lv_obj_t *o=lw_os_box(p,0,0,w,h,0,0,0);
    int sw[3]={b,r.w-2*b,b},sh[3]={b,r.h-2*b,b},dw[3]={b,w-2*b,b},dh[3]={b,h-2*b,b};
    int sy=r.y,dy=0;for(unsigned j=0;j<3;j++){int sx=r.x,dx=0;for(unsigned i=0;i<3;i++){if(!piece(o,(rect_t){sx,sy,sw[i],sh[j]},dx,dy,dw[i],dh[j])){lv_obj_delete(o);return NULL;}sx+=sw[i];dx+=dw[i];}sy+=sh[j];dy+=dh[j];}
    return o;
}
static void pressed(lv_event_t *e)
{
    lv_obj_t *button=lv_event_get_target_obj(e);slice_t *s=lv_event_get_user_data(e);
    lw_skin_id_t id=lv_event_get_code(e)==LV_EVENT_PRESSED ? s->pressed : lv_obj_has_state(button,LV_STATE_CHECKED) ? s->selected : s->base;
    rect_t r=rects[id];r.x+=s->offx;r.y+=s->offy;r.w=s->source.w;r.h=s->source.h;show(s,r);
}
bool lw_skin_button(lv_obj_t *o,lw_skin_id_t normal,lw_skin_id_t selected,lw_skin_id_t down)
{
    if(normal>=SK_COUNT || selected>=SK_COUNT || down>=SK_COUNT)return false;
    rect_t r=rects[normal];if(rects[selected].w!=r.w || rects[selected].h!=r.h || rects[down].w!=r.w || rects[down].h!=r.h){return false;}
    lv_obj_update_layout(o);int w=lv_obj_get_width(o),h=lv_obj_get_height(o);
    lv_obj_t *art=lw_skin_panel(o,normal,w,h,10);if(!art){return false;}
    for(unsigned i=0;i<lv_obj_get_child_count(art);i++){
        slice_t *s=lv_obj_get_user_data(lv_obj_get_child(art,(int)i));s->button=o;s->base=normal;s->selected=selected;s->pressed=down;s->offx=s->source.x-r.x;s->offy=s->source.y-r.y;
        lv_obj_add_event_cb(o,pressed,LV_EVENT_PRESSED,s);lv_obj_add_event_cb(o,pressed,LV_EVENT_RELEASED,s);lv_obj_add_event_cb(o,pressed,LV_EVENT_PRESS_LOST,s);
    }
    lv_obj_move_to_index(art,0);lv_obj_set_style_bg_opa(o,0,0);lv_obj_set_style_bg_opa(o,0,LV_STATE_PRESSED);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_shadow_width(o,0,0);
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t *label=lv_obj_get_child(o,(int)i);if(lv_obj_check_type(label,&lv_label_class))lv_obj_set_style_text_color(label,lv_color_hex(0x75595c),0);}
    return true;
}
void lw_skin_select(lv_obj_t *o,bool selected){if(!o || lv_obj_has_state(o,LV_STATE_CHECKED)==selected){return;}if(selected){lv_obj_add_state(o,LV_STATE_CHECKED);}else{lv_obj_remove_state(o,LV_STATE_CHECKED);}lv_obj_send_event(o,LV_EVENT_RELEASED,NULL);}
unsigned lw_skin_references(void){return refs;}
static void knob_follow(lv_event_t *e)
{
    lv_obj_t *slider=lv_event_get_target_obj(e),*knob=lv_event_get_user_data(e);int low=lv_slider_get_min_value(slider),high=lv_slider_get_max_value(slider),v=lv_slider_get_value(slider),h=lv_obj_get_height(slider);
    lv_obj_set_pos(knob,(lv_obj_get_width(slider)-32)/2,h-(v-low)*h/(high-low ? high-low : 1)-16);
}
void lw_skin_slider(lv_obj_t *slider)
{
    lv_obj_t *p=lv_obj_get_parent(slider);lv_obj_update_layout(slider);int x=lv_obj_get_x(slider),y=lv_obj_get_y(slider),h=lv_obj_get_height(slider);
    lv_obj_t *track=lw_skin_fit(p,SK_SLIDER,x+2,y,16,h);if(!track){return;}lv_obj_move_to_index(track,0);
    lv_obj_set_style_bg_opa(slider,0,LV_PART_MAIN);lv_obj_set_style_pad_hor(slider,6,LV_PART_MAIN);lv_obj_set_style_bg_opa(slider,170,LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider,0,LV_PART_KNOB);lv_obj_set_style_border_width(slider,0,LV_PART_KNOB);lv_obj_set_style_shadow_width(slider,0,LV_PART_KNOB);lv_obj_add_flag(slider,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_t *knob=lw_skin_fit(slider,SK_KNOB,-6,h/2-16,32,32);if(!knob){return;}lv_obj_add_event_cb(slider,knob_follow,LV_EVENT_VALUE_CHANGED,knob);lv_obj_add_event_cb(slider,knob_follow,LV_EVENT_REFRESH,knob);lv_obj_send_event(slider,LV_EVENT_REFRESH,NULL);
}
