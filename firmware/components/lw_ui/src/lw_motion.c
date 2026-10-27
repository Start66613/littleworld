/* Central LVGL motion vocabulary; object-scoped, no permanent timers. */
#include "lw_os_design.h"
#include "lw_app_state.h"
static lw_motion_mode_t motion_mode;
static void move(void *o,int32_t y) { lv_obj_set_style_translate_y(o,y,0); }
static void move_x(void *o,int32_t x) { lv_obj_set_style_translate_x(o,x,0); }
static void appear(void *o,int32_t opacity) { lv_obj_set_style_opa(o,(lv_opa_t)opacity,0); }
static void scale(void *o,int32_t value) { lv_obj_set_style_transform_scale_x(o,value,0); lv_obj_set_style_transform_scale_y(o,value,0); }
static void progress(void *o,int32_t value) { lv_obj_set_style_outline_width(o,2,0); lv_obj_set_style_outline_opa(o,(lv_opa_t)value,0); }
static void flap_height(void *o,int32_t value) { lv_obj_set_height(o,value); }
static void dim(void *o,int32_t value) { lv_obj_set_style_bg_opa(o,(lv_opa_t)value,0); }
static void animate(lv_obj_t *o,lv_anim_exec_xcb_t exec,int from,int to,unsigned duration,bool spring)
{
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,o); lv_anim_set_exec_cb(&a,exec); lv_anim_set_values(&a,from,to);
    lv_anim_set_duration(&a,duration); lv_anim_set_path_cb(&a,spring ? lv_anim_path_overshoot : lv_anim_path_ease_out); lv_anim_start(&a);
}
lw_motion_mode_t lw_os_motion_get(void) { return !lw_app_state()->settings.animations ? LW_MOTION_OFF : motion_mode; }
void lw_os_motion_mode(lw_motion_mode_t mode) { motion_mode=mode; }
void lw_os_motion_cancel(lv_obj_t *o)
{ lv_anim_delete(o,move); lv_anim_delete(o,move_x); lv_anim_delete(o,appear); lv_anim_delete(o,scale); lv_anim_delete(o,progress); move(o,0); move_x(o,0); appear(o,255); scale(o,256); progress(o,0); }
void lw_os_motion_slide(lv_obj_t *o,int from,int to,unsigned ms,unsigned delay,bool spring)
{
    lv_anim_delete(o,move);
    if (lw_os_motion_get()!=LW_MOTION_FULL) { move(o,to); return; }
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,o); lv_anim_set_exec_cb(&a,move); lv_anim_set_values(&a,from,to); lv_anim_set_duration(&a,ms); lv_anim_set_delay(&a,delay); lv_anim_set_path_cb(&a,spring ? lv_anim_path_overshoot : lv_anim_path_ease_out); lv_anim_start(&a);
}
void lw_os_motion_stagger(lv_obj_t *o,unsigned index)
{ if (lw_os_motion_get()==LW_MOTION_FULL) lw_os_motion_slide(o,12,0,240,index*24,false); }
void lw_os_motion_vector(lv_obj_t *o,int x,int y,unsigned ms,unsigned delay)
{
    if (lw_os_motion_get()!=LW_MOTION_FULL) { move(o,0);move_x(o,0);return; }
    lw_os_motion_slide(o,y,0,ms,delay,false);
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,o);lv_anim_set_exec_cb(&a,move_x);lv_anim_set_values(&a,x,0);lv_anim_set_duration(&a,ms);lv_anim_set_delay(&a,delay);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);lv_anim_start(&a);
}
void lw_os_motion_dim(lv_obj_t *o,int from,int to,unsigned ms)
{ lv_anim_delete(o,dim); if (lw_os_motion_get()!=LW_MOTION_FULL) { dim(o,to);return; } animate(o,dim,from,to,ms,false); }
void lw_os_motion(lv_obj_t *o,lw_motion_kind_t kind)
{
    lw_motion_mode_t mode=lw_os_motion_get(); if (kind==LW_MOVE_FLAP && mode!=LW_MOTION_FULL) { flap_height(o,0); return; } if (mode==LW_MOTION_OFF) return;
    if (mode==LW_MOTION_REDUCED) { animate(o,appear,220,255,90,false); return; }
    switch (kind) {
    case LW_MOVE_PAGE:animate(o,move_x,36,0,260,false);animate(o,appear,195,255,180,false);break;
    case LW_MOVE_CONTROL:animate(o,move_x,42,0,290,true);break;
    case LW_MOVE_DIALOG:animate(o,move,20,0,240,true);animate(o,scale,244,256,240,true);animate(o,appear,170,255,160,false);break;
    case LW_MOVE_TOAST:animate(o,move,-10,0,170,false);animate(o,appear,0,255,170,false);break;
    case LW_MOVE_REWARD:animate(o,move,28,0,420,true);break;
    case LW_MOVE_THEME:animate(o,appear,210,255,260,false);break;
    case LW_MOVE_RETURN:animate(o,move_x,-36,0,240,false);break;
    case LW_MOVE_BOOT:lw_os_motion_slide(o,32,0,680,160,false);animate(o,appear,0,255,560,false);break;
    case LW_MOVE_ACTOR:lw_os_motion_slide(o,16,0,460,220,false);animate(o,appear,0,255,540,false);break;
    case LW_MOVE_PAPER:lw_os_motion_slide(o,82,0,480,120,true);animate(o,appear,140,255,340,false);break;
    case LW_MOVE_PHOTO:lv_obj_update_layout(o);lv_obj_set_style_transform_pivot_x(o,lv_obj_get_width(o)/2,0);lv_obj_set_style_transform_pivot_y(o,lv_obj_get_height(o)/2,0);animate(o,scale,104,256,300,false);animate(o,move,24,0,300,false);animate(o,appear,140,255,200,false);break;
    case LW_MOVE_CARD:animate(o,move,28,0,320,false);animate(o,scale,236,256,320,false);break;
    case LW_MOVE_LOCK:animate(o,move,14,0,360,false);animate(o,appear,150,255,320,false);break;
    case LW_MOVE_FLAP:animate(o,flap_height,80,0,280,false);break;
    case LW_MOVE_DISMISS:animate(o,move,0,18,180,false);animate(o,scale,256,246,180,false);break;
    }
}
void lw_os_motion_exit(lv_obj_t *o) { if (lw_os_motion_get()==LW_MOTION_FULL) animate(o,move_x,0,-14,70,false); }
void lw_os_motion_step(lv_obj_t *o,int x,int y,unsigned duration)
{
    lv_obj_update_layout(o); int old_x=lv_obj_get_x(o),old_y=lv_obj_get_y(o);
    lv_obj_set_pos(o,x,y);
    if (lw_os_motion_get()==LW_MOTION_FULL) { animate(o,move_x,old_x-x,0,duration,false); animate(o,move,old_y-y,0,duration,false); }
    else { lv_anim_delete(o,move); lv_anim_delete(o,move_x); move(o,0); move_x(o,0); }
}
static void touched(lv_event_t *e)
{
    lv_obj_t *o=lv_event_get_target_obj(e);
    if (lv_obj_has_state(o,LV_STATE_DISABLED) || lw_os_motion_get()!=LW_MOTION_FULL) return;
    lv_anim_delete(o,move);
    if (lv_event_get_code(e)==LV_EVENT_PRESSED) { move(o,2); animate(o,progress,0,110,600,false); }
    else { animate(o,move,2,0,140,true); lv_anim_delete(o,progress); progress(o,0); }
}
void lw_os_motion_touch(lv_obj_t *o)
{ lv_obj_add_event_cb(o,touched,LV_EVENT_PRESSED,NULL); lv_obj_add_event_cb(o,touched,LV_EVENT_RELEASED,NULL); lv_obj_add_event_cb(o,touched,LV_EVENT_PRESS_LOST,NULL); }
void lw_os_animate(lv_obj_t *o,int offset,uint32_t duration)
{
    if (lw_os_motion_get()==LW_MOTION_OFF) return;
    if (lw_os_motion_get()==LW_MOTION_FULL) animate(o,move,offset,0,duration,false);
    else animate(o,appear,220,255,90,false);
}
