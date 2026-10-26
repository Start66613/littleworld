#include "lw_polish.h"
#include "lw_os_design.h"
#include "lw_theme.h"
#include <string.h>
static const char *const ids[]={"v42.male.idle","v42.male.blink","v42.male.wave","v42.q.idle","v42.q.wave","v42.q.sleep","v42.q.walk_down","v42.q.walk_left","v42.q.walk_right","v42.q.walk_up","v42.male.seated","v42.q.seated","v42.male.seated_wave","v42.q.seated_wave","v42.male.drowsy","v42.dog.idle","v42.dog.sleep"};
static uint16_t sequence[32],time_idle[32],time_wave[32]; static lw_clip_t clips[17];
const lw_clip_t *lw_polish_clip(const char *name)
{
    for (unsigned i=0;i<17;i++) if (!strcmp(name,ids[i])) {
        const lw_asset_spec_t *spec=lw_assets_find(name); if (!spec) return NULL;
        for (unsigned n=0;n<32;n++) { sequence[n]=(uint16_t)n; time_idle[n]=125; time_wave[n]=100; }
        bool wave=strstr(name,"wave")!=NULL; bool fast=wave || strstr(name,"dog")!=NULL;
        clips[i]=(lw_clip_t){ids[i],sequence,fast ? time_wave : time_idle,spec->frames,!wave}; return &clips[i];
    }
    return NULL;
}
static void release(lv_event_t *e) { lv_image_set_src(lv_event_get_target_obj(e),NULL); lw_asset_release(lv_event_get_user_data(e)); }
lv_obj_t *lw_polish_prop(lv_obj_t *parent,unsigned frame,int x,int y)
{
    const lw_asset_spec_t *spec=lw_assets_find("v42.props"); if (!spec || frame>=spec->frames) return NULL;
    lw_asset_lease_t *lease=lw_asset_acquire(spec->id,(uint16_t)frame); if (!lease) return NULL;
    lv_obj_t *o=lv_image_create(parent); lv_image_set_src(o,lw_asset_image(lease)); lv_image_set_antialias(o,false);
    lv_obj_set_pos(o,x-spec->anchor_x,y-spec->anchor_y); lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(o,release,LV_EVENT_DELETE,lease); return o;
}
static void ruled(lv_event_t *e)
{
    lv_obj_t *o=lv_event_get_target_obj(e);lv_area_t a;lv_obj_get_coords(o,&a);lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(lw_palette()->line);d.bg_opa=75;
    for (int y=64;y<lv_obj_get_height(o)-16;y+=32) {lv_area_t line={a.x1+24,a.y1+y,a.x2-24,a.y1+y};lv_draw_rect(lv_event_get_layer(e),&d,&line);}
}
void lw_polish_paper(lv_obj_t *o,bool lines)
{
    lv_obj_set_style_radius(o,10,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,lv_color_hex(lw_palette()->line),0);lv_obj_set_style_shadow_width(o,14,0);lv_obj_set_style_shadow_opa(o,16,0);
    if (lines) lv_obj_add_event_cb(o,ruled,LV_EVENT_DRAW_MAIN,NULL);
}
void lw_polish_game_badge(lv_obj_t *p,unsigned game,int x,int y)
{
    const lw_palette_t *c=lw_palette();lv_obj_t *o=lw_os_box(p,x,y,252,76,c->card_soft,12,255);
    if (game==0) for (unsigned i=0;i<4;i++) {lv_obj_t *t=lw_os_box(o,12+(int)(i%2)*40,4+(int)(i/2)*34,36,30,i==3 ? c->accent : c->gold,6,255);lw_os_label(t,i==3 ? "8" : i ? "4" : "2",&lw_font_22,8,1,30,c->text);}
    else if (game==1) {for (unsigned i=0;i<6;i++) lw_os_box(o,16+(int)i*22,48-(i>2 ? 22 : 0),20,20,c->mint,5,255);lw_os_box(o,192,20,16,16,c->accent,8,255);}
    else {for (int r=0;r<3;r++) for (int k=0;k<7;k++) lw_os_box(o,14+k*32,6+r*13,28,10,r==1 ? c->gold : c->accent,3,255);lw_os_box(o,94,66,64,6,c->lav,3,255);lw_os_box(o,166,50,10,10,c->text,5,255);}
}
