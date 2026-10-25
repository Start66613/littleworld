/* Little World OS: real LVGL controls over a shared service model. SPDX-License-Identifier: MIT */
#include "lw_os.h"
#include "lw_os_design.h"
#include "lw_theme.h"
#include "lw_apps.h"
#include "lw_shell.h"
#include "lw_app_state.h"
#include "lw_ui_content.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(lw_font_88);
typedef struct {
    lw_system_t *system; const lw_actor_art_t *art;
    lv_obj_t *root,*previous,*content,*mount,*over,*closing,*foreground,*toast,*clock,*net_icon,*volume_icon,*mute_badge;
    lv_obj_t *base_controls[OC_COUNT],*over_controls[OC_COUNT];
    lw_duo_t duo; lw_sprite_t *sprites[2]; const lw_clip_t *clips[2]; lv_obj_t *shadows[2];
    const lw_app_t *legacy;
    lw_rpg_view_t *rpg;
    lw_os_page_t page; lw_os_overlay_t overlay;
    lv_timer_t *timer; uint32_t last_tick,last_revision,toast_ms,last_clock; lw_wifi_status_t last_wifi;
    unsigned preview_theme,preview_wallpaper,selected_ap,section;
    int pending,navigation; uint32_t navigation_ms; bool building_overlay; uint8_t last_legacy_volume; unsigned front_actor;
    int lock_press_y; bool lock_dragging,lock_ready;
    unsigned painted_wallpaper; bool painted;
    lw_os_page_t theme_return;
    bool caps_refresh_on_unlock;
} os_t;
static os_t *s;
static lw_os_capabilities_t capabilities; static bool explicit_capabilities;
static void render(void);
static void refresh(void); static void show_overlay(lw_os_overlay_t);
static void derive_capabilities(const lw_system_t *system)
{ lw_service_state_t ready=system->backend.simulated ? LW_SERVICE_SIMULATED : LW_SERVICE_READY; capabilities=(lw_os_capabilities_t){system->backend.brightness ? ready : LW_SERVICE_UNAVAILABLE,system->backend.volume ? ready : LW_SERVICE_UNAVAILABLE,system->backend.network_available ? ready : LW_SERVICE_UNAVAILABLE,LW_SERVICE_UNAVAILABLE,LW_SERVICE_UNAVAILABLE}; }
void lw_os_capabilities(const lw_os_capabilities_t *value)
{
    explicit_capabilities=value!=NULL; if (value) capabilities=*value; else if (s) derive_capabilities(s->system);
    if (s) {
        if (s->system->locked) { s->caps_refresh_on_unlock=true; refresh(); }
        else { lw_os_overlay_t prior=s->overlay; render(); if (prior!=OS_OVER_NONE) show_overlay(prior); }
    }
}
static const char *service_text(lw_service_state_t state)
{ return state==LW_SERVICE_READY ? "设备服务已就绪" : state==LW_SERVICE_SIMULATED ? "模拟调节 · 无硬件输出" : "设备服务尚未接入"; }
static lw_rpg_view_host_t rpg_storage;
void lw_os_rpg_storage(const lw_rpg_view_host_t *host) { if (!s) { memset(&rpg_storage,0,sizeof(rpg_storage)); if (host) rpg_storage=*host; } }
const lw_rpg_state_t *lw_os_rpg_state(void) { return s ? lw_rpg_view_state(s->rpg) : NULL; }
static const char *const section_names[]={"网络","显示与亮度","声音","个性化","日期与时间","存储","关于设备"};
static const lw_os_palette_t *palette(void) { return lw_os_palette(s->page==OS_THEMES ? s->preview_theme : s->system->prefs.theme); }
static void render(void); static void show_overlay(lw_os_overlay_t); static void refresh(void);
static void route_app(const char *id)
{
    static const char *const names[]={"home","pet","album","memories","music","arcade","settings"};
    static const int routes[]={OC_HOME,OC_PET,OC_ALBUM,OC_MEMORIES,OC_MUSIC,OC_GAMES,OC_SETTINGS};
    if (!s || !id) return;
    for (unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++) if (!strcmp(id,names[i])) { s->pending=routes[i]; return; }
}
static void route_refresh(void) { if (s) s->pending=OC_REFRESH; }
static lv_obj_t *route_content(void) { return s ? s->mount : NULL; }
static const lw_app_t *route_current(void) { return s ? s->legacy : NULL; }
static const lw_shell_host_t shell_host={route_app,route_refresh,route_content,route_current};
static void event_action(lv_event_t *event)
{ if (s) s->pending=(int)(intptr_t)lv_event_get_user_data(event); }
static void register_control(lv_obj_t *o,lw_os_control_t id)
{ (s->building_overlay ? s->over_controls : s->base_controls)[id]=o; }
static bool available(lw_os_control_t id)
{
    if (id==OC_BRIGHTNESS) return capabilities.brightness!=LW_SERVICE_UNAVAILABLE;
    if (id==OC_VOLUME || id==OC_MUTE) return capabilities.volume!=LW_SERVICE_UNAVAILABLE;
    if (id==OC_WIFI || id==OC_WIFI_TOGGLE || id==OC_SCAN || id==OC_CONNECT) return capabilities.wifi!=LW_SERVICE_UNAVAILABLE;
    return true;
}
static lv_obj_t *button(lv_obj_t *parent,const char *name,int x,int y,int w,int h,lw_os_control_t id,bool primary)
{
    const lw_os_palette_t *c=palette(); lv_obj_t *o=lw_os_box(parent,x,y,w,h,primary ? c->accent : c->soft,16,255);
    lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_bg_opa(o,200,LV_STATE_PRESSED);
    lv_obj_t *text=lw_os_label(o,name,&lw_font_18,0,0,w,primary ? 0xffffff : c->text); lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(text);
    lw_os_motion_touch(o); lv_obj_set_style_border_width(o,1,LV_STATE_PRESSED); lv_obj_set_style_border_color(o,lv_color_hex(c->accent),LV_STATE_PRESSED);
    if (!available(id)) { lv_obj_add_state(o,LV_STATE_DISABLED); lv_obj_set_style_opa(o,110,LV_STATE_DISABLED); }
    lv_obj_add_event_cb(o,event_action,LV_EVENT_CLICKED,(void *)(intptr_t)id); register_control(o,id); return o;
}
static lv_obj_t *icon_button(lv_obj_t *parent,lw_os_icon_t icon,int x,int y,int size,lw_os_control_t id)
{
    lv_obj_t *o=button(parent,"",x,y,size,size,id,false); lw_os_icon(o,icon,(size-28)/2,(size-28)/2,28,palette()->text); return o;
}
static lv_obj_t *surface(lv_obj_t *parent,int x,int y,int w,int h,int radius)
{
    lv_obj_t *o=lw_os_box(parent,x,y,w,h,palette()->surface,radius,246);
    lv_obj_set_style_border_color(o,lv_color_hex(palette()->line),0); lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_shadow_color(o,lv_color_hex(0x413846),0); lv_obj_set_style_shadow_width(o,28,0); lv_obj_set_style_shadow_opa(o,18,0); lv_obj_set_style_shadow_offset_y(o,8,0); return o;
}
static const char *wifi_text(void)
{
    switch (s->system->wifi) {
    case LW_WIFI_OFF:return "无线网络已关闭";
    case LW_WIFI_SCANNING:return "正在寻找附近网络…";
    case LW_WIFI_CONNECTING:return "正在连接…";
    case LW_WIFI_CONNECTED:return s->system->ssid;
    case LW_WIFI_FAILED:return "连接失败，请检查密码";
    case LW_WIFI_UNSUPPORTED:return "无线驱动尚未接入";
    default:return "选择一个网络";
    }
}
static void clear_overlay(void)
{
    if (s->closing) { lv_obj_delete(s->closing); s->closing=NULL; }
    if (s->over) lv_obj_delete(s->over);
    s->over=NULL; s->overlay=OS_OVER_NONE;
    lw_rpg_view_suspend(s->rpg,false);
    memset(s->over_controls,0,sizeof(s->over_controls)); s->building_overlay=false;
}
static void fade(void *object,int32_t value) { lv_obj_set_style_opa(object,(lv_opa_t)value,0); }
static void faded(lv_anim_t *anim) { if (s && s->closing==anim->var) s->closing=NULL; lv_obj_delete(anim->var); }
static void dismiss_overlay(void)
{
    if (!s->over) return;
    if (lw_os_motion_get()!=LW_MOTION_FULL) { clear_overlay(); return; }
    if (s->closing) lv_obj_delete(s->closing);
    s->closing=s->over;
    lv_anim_t anim; lv_anim_init(&anim); lv_anim_set_var(&anim,s->over); lv_anim_set_exec_cb(&anim,fade);
    lv_anim_set_values(&anim,255,0); lv_anim_set_duration(&anim,180); lv_anim_set_completed_cb(&anim,faded); lv_anim_start(&anim);
    s->over=NULL; s->overlay=OS_OVER_NONE; memset(s->over_controls,0,sizeof(s->over_controls));
    lw_rpg_view_suspend(s->rpg,false);
}
static void slider_event(lv_event_t *event)
{
    lv_obj_t *o=lv_event_get_target_obj(event); int kind=(int)(intptr_t)lv_event_get_user_data(event);
    bool ok=kind==OC_BRIGHTNESS ? lw_system_brightness(s->system,lv_slider_get_value(o)) : lw_system_volume(s->system,lv_slider_get_value(o));
    if (!ok) lw_os_notify("设备服务暂不可用");
    if (lv_event_get_code(event)==LV_EVENT_RELEASED) { if (!lw_system_save(s->system)) lw_os_notify("设置未保存，请重试"); }
    refresh();
}
static lv_obj_t *slider(lv_obj_t *parent,int x,int y,int w,int h,lw_os_control_t id)
{
    lv_obj_t *o=lv_slider_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h); lv_slider_set_range(o,id==OC_BRIGHTNESS ? 10 : 0,100);
    lv_obj_set_style_bg_color(o,lv_color_hex(palette()->line),LV_PART_MAIN); lv_obj_set_style_bg_opa(o,255,LV_PART_MAIN);
    lv_obj_set_style_bg_color(o,lv_color_hex(palette()->accent),LV_PART_INDICATOR); lv_obj_set_style_bg_color(o,lv_color_hex(palette()->surface),LV_PART_KNOB);
    lv_obj_set_style_pad_all(o,7,LV_PART_KNOB); lv_obj_set_style_radius(o,16,LV_PART_MAIN); lv_obj_set_style_radius(o,16,LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(o,16); lv_obj_add_event_cb(o,slider_event,LV_EVENT_VALUE_CHANGED,(void *)(intptr_t)id); lv_obj_add_event_cb(o,slider_event,LV_EVENT_RELEASED,(void *)(intptr_t)id);
    lv_obj_set_style_border_width(o,2,LV_PART_KNOB); lv_obj_set_style_border_color(o,lv_color_hex(palette()->accent),LV_PART_KNOB);
    if (!available(id)) { lv_obj_add_state(o,LV_STATE_DISABLED); lv_obj_set_style_opa(o,95,0); }
    register_control(o,id); return o;
}
static void gesture(lv_event_t *event)
{
    if (!s) return;
    if (lv_event_get_code(event)==LV_EVENT_LONG_PRESSED && s->page==OS_DESKTOP && s->overlay==OS_OVER_NONE) s->pending=OC_EDITOR;
    else if (lv_event_get_code(event)==LV_EVENT_GESTURE) {
        lv_indev_t *indev=lv_indev_active(); lv_point_t point; if (!indev) return; lv_indev_get_point(indev,&point);
        if (point.x>740 && lv_indev_get_gesture_dir(indev)==LV_DIR_BOTTOM) s->pending=OC_CONTROL;
    }
}
static void build_status(void)
{
    const lw_os_palette_t *c=palette();
    lw_os_label(s->root,"LITTLE WORLD",&lw_font_18,32,14,192,c->text);
    s->clock=lw_os_label(s->root,"",&lw_font_18,258,14,330,c->muted);
    lv_obj_t *entry=button(s->root,"",752,2,242,44,OC_CONTROL,false); lv_obj_set_style_bg_opa(entry,0,0);
    s->net_icon=lw_os_icon(entry,OS_I_WIFI,7,7,28,c->text); s->volume_icon=lw_os_icon(entry,OS_I_VOLUME,48,7,28,c->text);
    s->mute_badge=lw_os_label(entry,"静",&lw_font_18,52,10,30,c->muted);
    lw_os_label(entry,s->system->backend.simulated ? "Windows 模拟" : "Little World",&lw_font_18,88,10,148,c->muted);
    lv_obj_add_flag(entry,LV_OBJ_FLAG_GESTURE_BUBBLE); lv_obj_add_event_cb(entry,gesture,LV_EVENT_GESTURE,NULL);
}
static void build_dock(void)
{
    const lw_os_palette_t *c=palette(); lv_obj_t *dock=surface(s->root,176,500,672,88,30);
    static const lw_os_icon_t icons[]={OS_I_HOME,OS_I_ALBUM,OS_I_HEART,OS_I_MUSIC,OS_I_GAME,OS_I_SETTINGS};
    static const char *const names[]={"小世界","相册","回忆","音乐","游戏","设置"};
    static const lw_os_control_t ids[]={OC_WORLD,OC_ALBUM,OC_MEMORIES,OC_MUSIC,OC_GAMES,OC_SETTINGS};
    static const uint32_t fills[]={0xe3c6c4,0xe8d7bd,0xe8c8d1,0xd7cde5,0xcddcc9,0xd3dbe0};
    for (int i=0;i<6;i++) {
        lv_obj_t *item=button(dock,"",14+i*108,3,104,80,ids[i],false); lv_obj_set_style_bg_opa(item,0,0);
        lv_obj_t *tile=lw_os_box(item,30,4,44,44,fills[i],14,255); lw_os_icon(tile,icons[i],5,5,34,0x65505e);
        lw_os_label(item,names[i],&lw_font_18,0,52,104,c->text); lv_obj_set_style_text_align(lv_obj_get_child(item,-1),LV_TEXT_ALIGN_CENTER,0);
        if (((s->page==OS_DESKTOP || s->page==OS_WORLD || s->page==OS_PET || s->page==OS_RPG) && i==0)||(s->page==OS_SETTINGS && i==5)||(s->page>=OS_ALBUM && s->page<=OS_GAMES && (int)s->page-OS_ALBUM+1==i)) { lw_os_box(item,44,75,16,3,c->accent,2,255); lv_obj_set_style_bg_color(tile,lv_color_hex(c->line),0); }
    }
}
static const lw_clip_t *art_clip(unsigned actor,const char *name)
{ for (unsigned j=0;j<s->art[actor].count;j++) if (!strcmp(s->art[actor].clips[j].name,name)) return &s->art[actor].clips[j].clip; return NULL; }
static void select_actor(lv_event_t *event)
{
    unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(event); lw_duo_touch(&s->duo,i);
    lw_os_notify(i ? "千千：今天也想和你待在一起。" : "男方当前是测试角色，等待美术确认。");
}
static void actors(void)
{
    if (!s->art) return;
    for (unsigned i=0;i<2;i++) {
        s->shadows[i]=lw_os_box(s->content,0,0,76,12,0x67505a,LV_RADIUS_CIRCLE,32);
        s->sprites[i]=lw_sprite_create(s->content,0,0,256);
        if (!s->sprites[i]) continue;
        lw_sprite_set_pixel_mode(s->sprites[i]); lw_sprite_set_external_clock(s->sprites[i],true);
        s->clips[i]=art_clip(i,"idle"); if (s->clips[i]) lw_sprite_play(s->sprites[i],s->clips[i]);
        lv_obj_t *hit=lw_os_box(s->content,(int)s->duo.actors[i].x-55,(int)s->duo.actors[i].y-210,110,214,0,0,0);
        lv_obj_add_flag(hit,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(hit,select_actor,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        /* The sprite's own position is updated below; the hit box follows it in tick. */
        lv_obj_set_user_data(lw_sprite_object(s->sprites[i]),hit);
    }
}
static void update_actors(uint32_t dt)
{
    if (!s->art || !s->sprites[0] || !s->sprites[1]) return;
    if (!s->system->locked) lw_duo_tick(&s->duo,dt);
    static const char *const dirs[]={"down","down_left","left","up_left","up","up_right","right","down_right"};
    for (unsigned i=0;i<2;i++) {
        lw_actor_t *a=&s->duo.actors[i]; char clipname[40];
        if (a->motion==LW_ACTOR_WALK) snprintf(clipname,sizeof(clipname),"walk_%s",dirs[a->facing]);
        else snprintf(clipname,sizeof(clipname),"%s",a->motion==LW_ACTOR_SLEEP ? "sleep_idle" : "idle");
        const lw_clip_t *clip=art_clip(i,clipname); if (!clip) clip=art_clip(i,"idle");
        if (clip && clip!=s->clips[i] && lw_sprite_play(s->sprites[i],clip)) s->clips[i]=clip;
        if (clip) lw_sprite_seek(s->sprites[i],s->duo.phase==LW_DUO_ACT ? s->duo.interaction_ms : a->animation_ms);
        int x=(int)lroundf(a->x),y=(int)lroundf(a->y); lw_sprite_set_anchor(s->sprites[i],x,y);
        bool lying=a->motion==LW_ACTOR_SLEEP; int shadow_width=lying ? 180 : 76;
        lv_obj_set_pos(s->shadows[i],x-shadow_width/2,y-5); lv_obj_set_width(s->shadows[i],shadow_width);
        lv_obj_t *hit=lv_obj_get_user_data(lw_sprite_object(s->sprites[i]));
        if (hit) { lv_obj_set_pos(hit,x-(lying ? 112 : 55),y-(lying ? 100 : 210)); lv_obj_set_size(hit,lying ? 224 : 110,lying ? 100 : 214); }
    }
    unsigned near=s->duo.actors[0].y>s->duo.actors[1].y ? 0 : 1;
    if (s->front_actor!=near) { lv_obj_move_to_index(lw_sprite_object(s->sprites[1-near]),-1); lv_obj_move_to_index(lw_sprite_object(s->sprites[near]),-1); s->front_actor=near; }
    if (s->foreground) lv_obj_move_foreground(s->foreground);
}
static void build_desktop(bool world)
{
    const lw_os_palette_t *c=palette();
    lw_os_label(s->content,world ? "两个人的日常。" : "我们的小世界",&lw_font_28,48,world ? 76 : 86,280,c->text);
    lw_os_label(s->content,world ? "像素生活 · 原生 1×" : "把平凡，慢慢过成喜欢的日子。",&lw_font_18,48,world ? 122 : 137,272,c->muted);
    int days=lw_days_between(lw_app_state()->anniversary,lw_today())+1; char number[32]; snprintf(number,sizeof(number),"%d",days<1 ? 1 : days);
    if (!world && s->system->prefs.widgets) {
        lv_obj_t *anniversary=button(s->content,"",44,216,264,118,OC_ANNIVERSARY,false); lv_obj_set_style_bg_opa(anniversary,125,0);
        lw_os_label(anniversary,"在一起的",&lw_font_18,18,12,220,c->muted);
        lw_os_label(anniversary,number,&lw_font_44,18,42,180,c->deep); lw_os_label(anniversary,"天",&lw_font_22,204,65,40,c->muted);
        lw_os_label(s->content,"点开纪念卡片，慢慢收藏。",&lw_font_18,48,340,280,c->muted);
        lv_obj_t *letter=button(s->content,"",44,382,264,72,OC_ENVELOPE,false); lv_obj_set_style_bg_opa(letter,160,0); lv_obj_set_style_radius(letter,22,0);
        lw_os_icon(letter,OS_I_MAIL,16,16,40,c->accent); lw_os_label(letter,"今日的小信封",&lw_font_18,70,14,180,c->text); lw_os_label(letter,"留一点惊喜给今天",&lw_font_18,70,42,180,c->muted);
    }
    if (world) {
        button(s->content,"见面打招呼",48,200,232,50,OC_GREET,true); button(s->content,"一起读封信",48,266,232,50,OC_LETTER,false); button(s->content,"一起休息",48,332,232,50,OC_REST,false);
        button(s->content,"照顾宠物",48,398,110,50,OC_PET,false); button(s->content,"小冒险",172,398,108,50,OC_RPG,true);
    }
    lw_os_label(s->content,s->art ? "男方形象待定 · 当前为 TEST A" : "角色资源未安装 · 场景与应用可用",&lw_font_18,565,466,420,c->muted);
    /* Furniture targets are created first so character hit areas stay above them. */
    lv_obj_t *furniture=button(s->content,"",518,106,224,184,OC_WINDOW,false); lv_obj_set_style_bg_opa(furniture,0,0); lv_obj_set_style_bg_opa(furniture,25,LV_STATE_PRESSED);
    furniture=button(s->content,"",378,276,174,96,OC_SOFA,false); lv_obj_set_style_bg_opa(furniture,0,0); lv_obj_set_style_bg_opa(furniture,25,LV_STATE_PRESSED);
    furniture=button(s->content,"",824,112,76,76,OC_ROOM_PHOTO,false); lv_obj_set_style_bg_opa(furniture,0,0); lv_obj_set_style_bg_opa(furniture,25,LV_STATE_PRESSED);
    actors();
    s->foreground=lw_os_foreground(s->content,s->system->prefs.wallpaper);
    lv_obj_add_flag(s->content,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(s->content,gesture,LV_EVENT_LONG_PRESSED,NULL);
}
static void build_settings(void)
{
    const lw_os_palette_t *c=palette(); lw_os_label(s->content,"设置",&lw_font_28,48,70,250,c->text);
    lw_os_label(s->content,"让小世界更像你。",&lw_font_18,48,112,250,c->muted);
    static const lw_os_control_t controls[]={OC_NETWORK,OC_DISPLAY,OC_SOUND,OC_PERSONAL,OC_TIME,OC_STORAGE,OC_ABOUT};
    static const lw_os_icon_t icons[]={OS_I_WIFI,OS_I_SUN,OS_I_VOLUME,OS_I_LEAF,OS_I_MOON,OS_I_ALBUM,OS_I_HEART};
    for (unsigned i=0;i<7;i++) {
        lv_obj_t *row=button(s->content,"",36,153+(int)i*46,260,44,controls[i],false); lv_obj_set_style_bg_opa(row,i==s->section ? 255 : 0,0);
        lw_os_icon(row,icons[i],14,7,28,i==s->section ? c->deep : c->muted); lw_os_label(row,section_names[i],&lw_font_18,56,9,184,c->text);
    }
    lv_obj_t *panel=surface(s->content,328,80,648,392,26);
    lw_os_label(panel,s->section<7 ? section_names[s->section] : s->section==8 ? "动态与反馈" : "开发者选项",&lw_font_22,28,24,520,c->text);
    if (s->section>=7) icon_button(panel,OS_I_BACK,560,16,48,OC_SETTINGS_BACK);
    if (s->section==0) {
        lw_os_label(panel,"连接，也保留离线的自在。",&lw_font_18,28,70,570,c->muted);
        lw_os_label(panel,"Wi-Fi",&lw_font_22,28,128,300,c->text);
        register_control(lw_os_label(panel,wifi_text(),&lw_font_18,28,167,570,c->muted),OC_WIFI_STATUS);
        button(panel,"选择网络",28,232,260,52,OC_WIFI,true); button(panel,s->system->wifi_enabled ? "关闭 Wi-Fi" : "开启 Wi-Fi",310,232,298,52,OC_WIFI_TOGGLE,false);
        lw_os_label(panel,service_text(capabilities.wifi),&lw_font_18,28,324,570,c->muted);
    } else if (s->section==1 || s->section==2) {
        bool display=s->section==1; lw_os_label(panel,display ? "屏幕亮度" : "系统与音乐音量",&lw_font_18,28,96,450,c->muted);
        slider(panel,40,166,450,14,display ? OC_BRIGHTNESS : OC_VOLUME);
        register_control(lw_os_label(panel,"",&lw_font_28,515,143,98,c->text),display ? OC_BRIGHT_VALUE : OC_VOLUME_VALUE);
        button(panel,display ? "壁纸与主题" : s->system->prefs.muted ? "取消静音" : "静音",28,234,286,54,display ? OC_THEMES : OC_MUTE,true);
        button(panel,display ? "查看锁屏" : "勿扰模式",334,234,274,54,display ? OC_LOCK : OC_DND,false);
        lw_os_label(panel,display ? service_text(capabilities.brightness) : service_text(capabilities.volume),&lw_font_18,28,328,580,c->muted);
    } else if (s->section==3) {
        lw_os_label(panel,"喜欢的颜色，让每天都不一样。",&lw_font_18,28,86,570,c->muted);
        button(panel,"壁纸与主题",28,146,580,58,OC_THEMES,true); button(panel,s->system->prefs.widgets ? "桌面小组件：已显示" : "桌面小组件：已隐藏",28,222,580,58,OC_WIDGETS,false);
        button(panel,"动态与反馈",28,298,580,52,OC_MOTION,false);
    } else if (s->section==4) {
        lw_os_label(panel,"北京时间 · UTC+8",&lw_font_28,28,102,580,c->text);
        lw_os_label(panel,lw_time_is_demo() ? "当前为演示日期，尚未取得可信设备时钟。" : "本机时间 · 网络校时尚未接入",&lw_font_18,28,164,580,c->muted);
        lw_os_label(panel,"纪念日计算继续使用原有日期服务。\n离线时不会假装已经完成网络同步。",&lw_font_18,28,236,580,c->muted);
    } else if (s->section==5) {
        lw_os_icon(panel,OS_I_ALBUM,28,100,58,c->accent);
        lw_os_label(panel,"存储卡尚未挂载",&lw_font_22,108,111,480,c->text);
        lw_os_label(panel,"本轮不扫描私人照片目录。\n自定义壁纸导入将在 SD / 文件系统接入后启用。\n内置场景由 LVGL 绘制，无高清图片整包解码。",&lw_font_18,28,208,580,c->muted);
    } else if (s->section==8) {
        lw_os_label(panel,"让动作慢一点，也可以安静一点。",&lw_font_18,28,82,580,c->muted);
        static const char *const names[]={"完整动效","减少动态","关闭动效"};
        for (unsigned i=0;i<3;i++) {
            lv_obj_t *o=button(panel,names[i],28,132+(int)i*62,580,50,(lw_os_control_t)(OC_MOTION_FULL+i),lw_os_motion_get()==(lw_motion_mode_t)i);
            if (lw_os_motion_get()==(lw_motion_mode_t)i) lw_os_icon(o,OS_I_CHECK,528,10,28,0xffffff);
        }
        lw_os_label(panel,"关闭沿用原有设置存档；减少动态当前仅本次会话。",&lw_font_18,28,337,580,c->muted);
    } else {
        lw_os_label(panel,s->section==6 ? "Little World OS 2.0" : "开发验证",&lw_font_28,28,100,580,c->text);
        lw_os_label(panel,s->section==6 ? "7 英寸 · 1024 × 600\nESP32-P4 / LVGL 9.5\nWindows 原型 · 硬件功能未在本次验证" : "所有网络与音量为 Windows Mock。\n真实硬件能力由服务适配层独立报告。\n无设备连接、无刷写、无付费生成。",&lw_font_18,28,165,580,c->muted);
        if (s->section==6) button(panel,"开发者选项",28,300,280,50,OC_DEVELOPER,false);
    }
}
static void build_themes(void)
{
    const lw_os_palette_t *c=palette(); lw_os_label(s->content,"让小世界，换一种心情。",&lw_font_28,48,72,800,c->text);
    lw_os_label(s->content,"场景、Dock 和所有系统浮层，会一起改变。",&lw_font_18,48,116,860,c->muted);
    static const char *const desc[]={"樱花奶油 · 柔和日光","月光微紫 · 温暖星夜","鼠尾草绿 · 像素日常"};
    for (unsigned i=0;i<3;i++) {
        const lw_os_palette_t *t=lw_os_palette(i); lv_obj_t *tile=button(s->content,"",48+(int)i*312,169,296,248,(lw_os_control_t)(OC_THEME0+i),false);
        lv_obj_set_style_bg_color(tile,lv_color_hex(t->surface),0); lv_obj_set_style_clip_corner(tile,true,0);
        lw_os_wallpaper(tile,i,0,0,296,166); lw_os_label(tile,t->name,&lw_font_22,18,178,260,t->text); lw_os_label(tile,desc[i],&lw_font_18,18,212,260,t->muted);
        lv_obj_set_style_border_width(tile,i==s->preview_theme ? 3 : 1,0); lv_obj_set_style_border_color(tile,lv_color_hex(i==s->preview_theme ? c->accent : c->line),0);
        if (i==s->preview_theme) { lv_obj_t *check=lw_os_box(tile,246,12,34,34,c->accent,17,255); lw_os_icon(check,OS_I_CHECK,4,4,26,0xffffff); }
    }
    lw_os_label(s->content,"即时预览 · 确认后保存",&lw_font_18,48,444,430,c->muted);
    button(s->content,"取消",618,433,140,50,OC_CANCEL,false); button(s->content,"使用这个主题",776,433,200,50,OC_APPLY,true);
}
static void build_legacy(void)
{
    static const char *const ids[]={"album","memories","music","arcade"};
    const lw_app_t *app=lw_app_find(s->page==OS_PET ? "pet" : ids[s->page-OS_ALBUM]);
    s->mount=lw_os_box(s->content,0,52,1024,440,palette()->bg,0,255);
    if (app) { s->legacy=app; app->create(s->mount,app); }
}
static void render(void)
{
    if (s->legacy && s->legacy->destroy) s->legacy->destroy();
    s->legacy=NULL; s->mount=NULL; s->rpg=NULL; s->foreground=NULL; s->closing=NULL;
    s->sprites[0]=s->sprites[1]=NULL; s->clips[0]=s->clips[1]=NULL; s->toast=NULL; s->front_actor=2;
    lv_obj_clean(s->root); memset(s->base_controls,0,sizeof(s->base_controls)); memset(s->over_controls,0,sizeof(s->over_controls)); s->over=NULL; s->overlay=OS_OVER_NONE;
    unsigned wallpaper=s->page==OS_THEMES ? s->preview_wallpaper : s->system->prefs.wallpaper;
    const lw_os_palette_t *c=palette();
    lw_palette_t app_palette={c->bg,c->lower,c->surface,c->soft,c->text,c->muted,c->line,c->accent,c->deep,c->accent,c->deep,0x8fab9a,0xcfa873,c->line,0xffffff};
    lw_theme_session_palette(&app_palette);
    lv_obj_t *wall=lw_os_wallpaper(s->root,wallpaper,0,0,1024,600);
    if (s->painted && s->painted_wallpaper!=wallpaper) lw_os_wallpaper_change(wall,s->painted_wallpaper);
    s->painted=true; s->painted_wallpaper=wallpaper;
    s->content=lw_os_box(s->root,0,0,1024,494,0,0,0);
    if (s->page==OS_DESKTOP || s->page==OS_WORLD) build_desktop(s->page==OS_WORLD);
    else {
        lw_os_box(s->content,0,48,1024,446,palette()->bg,0,252);
        if (s->page==OS_SETTINGS) build_settings(); else if (s->page==OS_THEMES) build_themes();
        else if (s->page==OS_RPG) { s->rpg=lw_rpg_view_create(s->content,&rpg_storage,s->system->prefs.theme); if (!s->rpg) lw_os_notify("冒险房间暂不可用"); }
        else build_legacy();
    }
    build_status(); build_dock(); if (s->page!=OS_DESKTOP) icon_button(s->root,OS_I_BACK,700,2,44,OC_HOME);
    refresh(); update_actors(0); lw_os_motion(s->content,s->page==OS_THEMES ? LW_MOVE_THEME : LW_MOVE_PAGE);
}
static void build_control(void)
{
    const lw_os_palette_t *c=palette(); lv_obj_t *p=surface(s->over,484,56,500,486,32);
    lw_os_label(p,"控制中心",&lw_font_28,24,14,360,c->text); icon_button(p,OS_I_CLOSE,430,14,46,OC_CLOSE);
    lw_os_label(p,s->system->backend.simulated ? "Windows 模拟 · 状态与设置同步" : "设备服务 · 按可用能力显示",&lw_font_18,24,53,450,c->muted);
    lv_obj_t *wifi=button(p,"",24,78,286,92,OC_WIFI,false); lw_os_icon(wifi,OS_I_WIFI,17,23,44,c->accent);
    lw_os_label(wifi,"Wi-Fi",&lw_font_22,78,14,190,c->text); register_control(lw_os_label(wifi,wifi_text(),&lw_font_18,78,50,194,c->muted),OC_WIFI_STATUS);
    lv_obj_t *moon=icon_button(p,OS_I_MOON,326,78,66,OC_DND); lv_obj_set_size(moon,66,92); lw_os_label(moon,s->system->prefs.dnd ? "已开启" : "勿扰",&lw_font_18,7,55,60,c->muted);
    if (s->system->prefs.dnd) lv_obj_set_style_bg_color(moon,lv_color_hex(c->line),0);
    lv_obj_t *lock=icon_button(p,OS_I_LOCK,408,78,66,OC_LOCK); lv_obj_set_size(lock,66,92); lw_os_label(lock,"锁屏",&lw_font_18,13,55,54,c->muted);
    for (unsigned i=0;i<2;i++) {
        lv_obj_t *tile=lw_os_box(p,24+(int)i*124,188,108,196,c->soft,24,255);
        lw_os_icon(tile,i ? OS_I_VOLUME : OS_I_SUN,36,13,36,c->deep);
        slider(tile,42,65,24,86,i ? OC_VOLUME : OC_BRIGHTNESS);
        register_control(lw_os_label(tile,"",&lw_font_18,0,169,108,c->text),i ? OC_VOLUME_VALUE : OC_BRIGHT_VALUE);
        lv_obj_set_style_text_align(lv_obj_get_child(tile,-1),LV_TEXT_ALIGN_CENTER,0);
    }
    lv_obj_t *music=button(p,"",272,188,202,112,OC_MUSIC,false);
    lw_os_icon(music,OS_I_MUSIC,16,18,32,c->deep); lw_os_label(music,"音乐盒",&lw_font_18,58,22,126,c->text);
    lw_os_label(music,capabilities.audio==LW_SERVICE_READY ? "打开播放页面" : "声音尚未接入",&lw_font_18,16,72,184,c->muted);
    button(p,s->system->prefs.muted ? "取消静音" : "静音",272,316,94,68,OC_MUTE,false);
    button(p,"换个主题",380,316,94,68,OC_THEMES,false);
    button(p,"打开设置",24,404,286,58,OC_SETTINGS,true); button(p,"回桌面",326,404,148,58,OC_HOME,false);
    lw_os_motion(p,LW_MOVE_CONTROL);
}
static void build_wifi(void)
{
    const lw_os_palette_t *c=palette(); lv_obj_t *p=surface(s->over,268,62,488,472,30);
    icon_button(p,OS_I_BACK,16,16,44,OC_BACK); lw_os_label(p,"Wi-Fi",&lw_font_28,76,23,300,c->text); icon_button(p,OS_I_CLOSE,426,16,44,OC_CLOSE);
    register_control(lw_os_label(p,wifi_text(),&lw_font_18,28,84,430,c->muted),OC_WIFI_STATUS);
    button(p,s->system->wifi_enabled ? "已开启" : "已关闭",330,77,130,44,OC_WIFI_TOGGLE,s->system->wifi_enabled);
    lw_os_label(p,s->system->backend.simulated ? "模拟网络 · 不会连接你的真实路由器" : "附近的网络",&lw_font_18,28,137,430,c->muted);
    for (unsigned i=0;s->system->wifi_enabled && i<s->system->network_count && i<3;i++) {
        lv_obj_t *row=button(p,"",20,178+(int)i*67,448,58,(lw_os_control_t)(OC_AP0+i),false); lv_obj_set_style_bg_opa(row,0,0);
        lw_os_icon(row,OS_I_WIFI,12,14,28,c->accent); lw_os_label(row,s->system->networks[i].ssid,&lw_font_22,56,7,284,c->text);
        char signal[40]; snprintf(signal,sizeof(signal),"%d dBm%s",s->system->networks[i].rssi,s->system->networks[i].secured ? "  ·  加密" : "  ·  开放");
        lw_os_label(row,signal,&lw_font_18,56,34,290,c->muted);
        if (s->system->wifi==LW_WIFI_CONNECTED && !strcmp(s->system->ssid,s->system->networks[i].ssid)) lw_os_icon(row,OS_I_CHECK,393,14,30,c->deep);
        lw_os_box(row,56,57,376,1,c->line,0,255);
    }
    if (s->system->wifi==LW_WIFI_CONNECTED) { button(p,"断开",20,398,133,48,OC_DISCONNECT,false); button(p,"忘记网络",169,398,149,48,OC_FORGET,false); button(p,"刷新",334,398,134,48,OC_SCAN,false); }
    else button(p,s->system->wifi==LW_WIFI_SCANNING ? "正在扫描…" : "重新扫描",20,398,448,48,OC_SCAN,false);
}
static void keyboard_event(lv_event_t *event)
{ s->pending=lv_event_get_code(event)==LV_EVENT_READY ? OC_CONNECT : OC_BACK; }
static void build_password(void)
{
    const lw_os_palette_t *c=palette(); lv_obj_t *p=surface(s->over,202,26,620,548,30);
    icon_button(p,OS_I_BACK,16,14,44,OC_BACK); lw_os_label(p,s->system->networks[s->selected_ap].ssid,&lw_font_28,78,23,450,c->text);
    lw_os_label(p,"输入网络密码",&lw_font_18,28,82,550,c->muted);
    lv_obj_t *input=lv_textarea_create(p); lv_obj_set_pos(input,28,120); lv_obj_set_size(input,564,56); lv_textarea_set_one_line(input,true); lv_textarea_set_password_mode(input,true); lv_textarea_set_max_length(input,63);
    lv_textarea_set_placeholder_text(input,"至少 8 位密码"); lv_obj_set_style_text_font(input,&lw_font_22,0); lv_obj_set_style_radius(input,16,0); lv_obj_set_style_bg_color(input,lv_color_hex(c->surface),0); lv_obj_set_style_text_color(input,lv_color_hex(c->text),0); register_control(input,OC_PASSWORD);
    register_control(lw_os_label(p,s->system->wifi==LW_WIFI_FAILED ? "连接失败，请重新输入密码。" : "本轮为模拟连接，密码不会写入存档或日志。",&lw_font_18,28,189,558,c->muted),OC_WIFI_STATUS);
    button(p,"连接",400,230,192,50,OC_CONNECT,true); button(p,"返回网络列表",28,230,242,50,OC_BACK,false);
    lv_obj_t *keyboard=lv_keyboard_create(p); lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT); lv_obj_set_pos(keyboard,16,300); lv_obj_set_size(keyboard,588,232); lv_keyboard_set_textarea(keyboard,input);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(c->soft),0); lv_obj_set_style_text_font(keyboard,&lv_font_montserrat_20,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard,lv_color_hex(c->surface),LV_PART_ITEMS); lv_obj_set_style_text_color(keyboard,lv_color_hex(c->text),LV_PART_ITEMS); lv_obj_set_style_radius(keyboard,8,LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_READY,NULL); lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_CANCEL,NULL); register_control(keyboard,OC_KEYBOARD);
}
static void build_lock(void)
{
    const lw_os_palette_t *c=palette(); lw_os_wallpaper(s->over,s->system->prefs.wallpaper,0,0,1024,600);
    lw_os_box(s->over,0,0,1024,600,s->system->screen_off ? 0x151822 : c->bg,0,s->system->screen_off ? 255 : 145);
    if (s->system->screen_off) { lw_os_icon(s->over,OS_I_MOON,482,198,60,0xd5c6dc); lw_os_label(s->over,"屏幕已熄灭 · Windows 模拟",&lw_font_22,310,292,600,0xe9dfea); button(s->over,"轻触唤醒",360,418,304,60,OC_UNLOCK,true); return; }
    uint32_t now=lw_now(); char clock[16]; snprintf(clock,sizeof(clock),"%02u:%02u",(unsigned)(now%86400/3600),(unsigned)(now%3600/60));
    /* Reserve the left column for text; the room remains a quiet right vignette. */
    lw_os_box(s->over,24,96,424,340,c->bg,28,245);
    lv_obj_t *t=lw_os_label(s->over,clock,&lw_font_88,48,146,376,c->text); register_control(t,OC_LOCK_CLOCK);
    lw_date_t d=lw_today(); char date[80]; snprintf(date,sizeof(date),"%d 年 %d 月 %d 日%s",d.year,d.month,d.day,lw_time_is_demo() ? " · 演示日期" : "");
    t=lw_os_label(s->over,date,&lw_font_18,50,260,374,c->muted); register_control(t,OC_LOCK_DATE);
    int days=lw_days_between(lw_app_state()->anniversary,d)+1; char love[96]; snprintf(love,sizeof(love),"在一起的第 %d 天，晚安也想对你说。",days<1 ? 1 : days);
    t=lw_os_label(s->over,love,&lw_font_18,50,330,364,c->text); register_control(t,OC_LOCK_LOVE);
    lw_os_icon(s->over,OS_I_LOCK,54,103,28,c->muted);
    lw_os_label(s->over,"向上轻扫，或点下方回家",&lw_font_18,50,401,374,c->muted);
    button(s->over,"轻触，回到小世界",48,472,376,60,OC_UNLOCK,true);
}
static void lock_touch(lv_event_t *e)
{
    if (!s || s->overlay!=OS_OVER_LOCK) return;
    lv_indev_t *indev=lv_indev_active(); if (!indev) return; lv_point_t p; lv_indev_get_point(indev,&p);
    lv_event_code_t code=lv_event_get_code(e); lv_obj_t *unlock=s->over_controls[OC_UNLOCK];
    if (code==LV_EVENT_PRESSED) { s->lock_press_y=p.y; s->lock_dragging=true; }
    else if (code==LV_EVENT_PRESSING && s->lock_dragging) {
        int distance=s->lock_press_y-p.y; s->lock_ready=distance>=72;
        if (unlock) {
            lv_label_set_text(lv_obj_get_child(unlock,0),s->lock_ready ? "松开，回到小世界" : "轻触，回到小世界");
            if (lw_os_motion_get()==LW_MOTION_FULL) lv_obj_set_style_translate_y(unlock,distance>0 ? -(distance>96 ? 96 : distance)/6 : 0,0);
        }
    } else if (code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
        if (s->lock_ready && code==LV_EVENT_RELEASED) s->pending=OC_UNLOCK;
        else if (unlock) { lv_label_set_text(lv_obj_get_child(unlock,0),"轻触，回到小世界"); lv_obj_set_style_translate_y(unlock,0,0); }
        s->lock_dragging=s->lock_ready=false;
    }
}
static void show_overlay(lw_os_overlay_t kind)
{
    if (kind==OS_OVER_LOCK && s->overlay==kind && s->over) { refresh(); return; }
    clear_overlay(); if (kind==OS_OVER_NONE) return;
    s->overlay=kind; s->building_overlay=true;
    lw_rpg_view_suspend(s->rpg,true);
    s->over=lw_os_box(s->root,0,0,1024,600,0x282335,0,105); lv_obj_add_flag(s->over,LV_OBJ_FLAG_CLICKABLE);
    if (kind==OS_OVER_LOCK) { lv_obj_add_event_cb(s->over,lock_touch,LV_EVENT_PRESSED,NULL); lv_obj_add_event_cb(s->over,lock_touch,LV_EVENT_PRESSING,NULL); lv_obj_add_event_cb(s->over,lock_touch,LV_EVENT_RELEASED,NULL); lv_obj_add_event_cb(s->over,lock_touch,LV_EVENT_PRESS_LOST,NULL); }
    if (kind==OS_OVER_CONTROL) build_control(); else if (kind==OS_OVER_WIFI) build_wifi(); else if (kind==OS_OVER_PASSWORD) build_password(); else if (kind==OS_OVER_LOCK) build_lock();
    else {
        lv_obj_t *p=surface(s->over,270,117,484,354,32); icon_button(p,OS_I_CLOSE,420,16,44,OC_CLOSE);
        if (kind==OS_OVER_EDIT) {
            lw_os_label(p,"布置你的小世界",&lw_font_28,28,29,370,palette()->text);
            button(p,"壁纸与主题",28,103,428,58,OC_THEMES,true); button(p,s->system->prefs.widgets ? "隐藏桌面小组件" : "显示桌面小组件",28,181,428,58,OC_WIDGETS,false); button(p,"走进双人房间",28,259,428,58,OC_WORLD,false);
        } else {
            lw_os_icon(p,OS_I_MAIL,28,28,48,palette()->accent); lw_os_label(p,"今天也有一封小信",&lw_font_28,28,105,428,palette()->text);
            lw_os_label(p,"愿普通的一天，也有被惦记的温柔。\n一起吃饭，一起散步，一起慢慢生活。\n下一封惊喜，留给未来的我们。",&lw_font_18,28,167,428,palette()->muted); button(p,"收好这份温柔",28,280,428,50,OC_CLOSE,true);
        }
        lw_os_motion(p,LW_MOVE_DIALOG);
    }
    s->building_overlay=false; refresh();
}
void lw_os_notify(const char *message)
{
    if (!s || s->system->prefs.dnd) return;
    if (s->toast) lv_obj_delete(s->toast);
    s->toast=surface(s->root,270,428,484,60,22); lw_os_label(s->toast,message,&lw_font_18,22,18,444,palette()->text); s->toast_ms=3000; lw_os_motion(s->toast,LW_MOVE_TOAST);
}
static void refresh(void)
{
    if (s->net_icon) lv_obj_set_style_opa(s->net_icon,s->system->wifi==LW_WIFI_CONNECTED ? 255 : s->system->wifi==LW_WIFI_OFF ? 50 : 125,0);
    if (s->volume_icon && s->mute_badge) {
        if (s->system->prefs.muted) { lv_obj_add_flag(s->volume_icon,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(s->mute_badge,LV_OBJ_FLAG_HIDDEN); }
        else { lv_obj_remove_flag(s->volume_icon,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s->mute_badge,LV_OBJ_FLAG_HIDDEN); }
    }
    for (unsigned layer=0;layer<2;layer++) {
        lv_obj_t **controls=layer ? s->over_controls : s->base_controls;
        if (controls[OC_BRIGHTNESS]) lv_slider_set_value(controls[OC_BRIGHTNESS],s->system->prefs.brightness,LV_ANIM_OFF);
        if (controls[OC_VOLUME]) lv_slider_set_value(controls[OC_VOLUME],s->system->prefs.volume,LV_ANIM_OFF);
        if (controls[OC_BRIGHT_VALUE]) { if (available(OC_BRIGHTNESS)) lv_label_set_text_fmt(controls[OC_BRIGHT_VALUE],"%u%%",s->system->prefs.brightness); else lv_label_set_text(controls[OC_BRIGHT_VALUE],"不可用"); }
        if (controls[OC_VOLUME_VALUE]) { if (available(OC_VOLUME)) lv_label_set_text_fmt(controls[OC_VOLUME_VALUE],"%u%%",s->system->prefs.volume); else lv_label_set_text(controls[OC_VOLUME_VALUE],"不可用"); }
        if (controls[OC_WIFI_STATUS] && s->overlay!=OS_OVER_PASSWORD) lv_label_set_text(controls[OC_WIFI_STATUS],wifi_text());
    }
    lw_date_t d=lw_today(); uint32_t now=lw_now(); if (s->clock) lv_label_set_text_fmt(s->clock,"%02u:%02u   %d 月 %d 日%s",(unsigned)(now%86400/3600),(unsigned)(now%3600/60),d.month,d.day,lw_time_is_demo() ? " · 演示" : "");
    if (s->over_controls[OC_LOCK_CLOCK]) lv_label_set_text_fmt(s->over_controls[OC_LOCK_CLOCK],"%02u:%02u",(unsigned)(now%86400/3600),(unsigned)(now%3600/60));
    if (s->over_controls[OC_LOCK_DATE]) lv_label_set_text_fmt(s->over_controls[OC_LOCK_DATE],"%d 年 %d 月 %d 日%s",d.year,d.month,d.day,lw_time_is_demo() ? " · 演示日期" : "");
    if (s->over_controls[OC_LOCK_LOVE]) { int days=lw_days_between(lw_app_state()->anniversary,d)+1; lv_label_set_text_fmt(s->over_controls[OC_LOCK_LOVE],"在一起的第 %d 天，晚安也想对你说。",days<1 ? 1 : days); }
}
void lw_os_open(lw_os_page_t page)
{
    if (!s || page>OS_RPG || page<OS_DESKTOP || s->system->locked) return;
    s->navigation=-1; s->navigation_ms=0;
    if (s->page==OS_THEMES && page!=OS_THEMES) { s->preview_theme=s->system->prefs.theme; s->preview_wallpaper=s->system->prefs.wallpaper; }
    if (page==OS_THEMES && s->page!=OS_THEMES) s->theme_return=s->page;
    s->page=page; if (page==OS_THEMES) { s->preview_theme=s->system->prefs.theme; s->preview_wallpaper=s->system->prefs.wallpaper; }
    s->duo.autonomous=page==OS_WORLD;
    render();
}
void lw_os_show(lw_os_overlay_t overlay) { if (s) show_overlay(overlay); }
static void navigate(lw_os_page_t page)
{
    if (lw_os_motion_get()==LW_MOTION_FULL && s->page!=page) { clear_overlay(); s->navigation=page; s->navigation_ms=70; lw_os_motion_exit(s->content); }
    else lw_os_open(page);
}
static void act(int id)
{
    if (id<0) return;
    switch (id) {
    case OC_HOME:navigate(OS_DESKTOP);break;
    case OC_WORLD:navigate(OS_WORLD);break;
    case OC_ALBUM:navigate(OS_ALBUM);break;
    case OC_MEMORIES:navigate(OS_MEMORIES);break;
    case OC_ANNIVERSARY:lw_memories_open_tab(0); navigate(OS_MEMORIES);break;
    case OC_MUSIC:navigate(OS_MUSIC);break;
    case OC_GAMES:navigate(OS_GAMES);break;
    case OC_PET:navigate(OS_PET);break;
    case OC_RPG:navigate(OS_RPG);break;
    case OC_SOFA:navigate(OS_WORLD);break;
    case OC_ROOM_PHOTO:navigate(OS_ALBUM);break;
    case OC_REFRESH:render();break;
    case OC_SETTINGS:navigate(OS_SETTINGS);break;
    case OC_CONTROL:show_overlay(OS_OVER_CONTROL);break;
    case OC_CLOSE:dismiss_overlay();break;
    case OC_BACK:show_overlay(s->overlay==OS_OVER_PASSWORD ? OS_OVER_WIFI : OS_OVER_CONTROL);break;
    case OC_WIFI:if (!s->system->network_count) lw_system_wifi_scan(s->system); show_overlay(OS_OVER_WIFI);break;
    case OC_WIFI_TOGGLE:lw_system_wifi_enable(s->system,!s->system->wifi_enabled); if (s->system->wifi_enabled) lw_system_wifi_scan(s->system); if (s->overlay!=OS_OVER_NONE) show_overlay(s->overlay); else render();break;
    case OC_SCAN:lw_system_wifi_scan(s->system); show_overlay(OS_OVER_WIFI);break;
    case OC_AP0:case OC_AP1:case OC_AP2:
        s->selected_ap=(unsigned)(id-OC_AP0); if (s->selected_ap>=s->system->network_count) break;
        if (s->system->wifi==LW_WIFI_CONNECTED && !strcmp(s->system->ssid,s->system->networks[s->selected_ap].ssid)) { lw_os_notify("已连接，可在下方断开或忘记网络。"); break; }
        if (!s->system->networks[s->selected_ap].secured) lw_system_wifi_connect(s->system,s->selected_ap,""); else show_overlay(OS_OVER_PASSWORD); break;
    case OC_CONNECT:
        if (s->over_controls[OC_PASSWORD]) {
            const char *password=lv_textarea_get_text(s->over_controls[OC_PASSWORD]); bool ok=lw_system_wifi_connect(s->system,s->selected_ap,password);
            lv_textarea_set_text(s->over_controls[OC_PASSWORD],"");
            lv_label_set_text(s->over_controls[OC_WIFI_STATUS],ok ? "正在连接…" : "密码至少 8 位，最多 63 位。");
        } break;
    case OC_DISCONNECT:case OC_FORGET:lw_system_wifi_disconnect(s->system,id==OC_FORGET); show_overlay(OS_OVER_WIFI);break;
    case OC_MUTE:lw_system_mute(s->system,!s->system->prefs.muted); lw_system_save(s->system); if (s->overlay==OS_OVER_CONTROL) show_overlay(OS_OVER_CONTROL); else render();break;
    case OC_DND:lw_system_dnd(s->system,!s->system->prefs.dnd); lw_system_save(s->system); if (s->overlay==OS_OVER_CONTROL) show_overlay(OS_OVER_CONTROL); else render();break;
    case OC_WINDOW:case OC_THEMES:case OC_WALLPAPER:lw_os_open(OS_THEMES);break;
    case OC_THEME0:case OC_THEME1:case OC_THEME2:s->preview_theme=(unsigned)(id-OC_THEME0); s->preview_wallpaper=s->preview_theme; render();break;
    case OC_APPLY:lw_system_theme(s->system,s->preview_theme,s->preview_wallpaper); lw_system_save(s->system); lw_os_open(s->theme_return);break;
    case OC_CANCEL:lw_os_open(s->theme_return);break;
    case OC_LOCK:case OC_SCREEN_OFF:s->system->locked=true; s->system->screen_off=id==OC_SCREEN_OFF; show_overlay(OS_OVER_LOCK);break;
    case OC_UNLOCK:s->system->locked=false; s->system->screen_off=false; dismiss_overlay(); if (s->caps_refresh_on_unlock) { s->caps_refresh_on_unlock=false; render(); } break;
    case OC_EDITOR:show_overlay(OS_OVER_EDIT);break;
    case OC_ENVELOPE:show_overlay(OS_OVER_LETTER);break;
    case OC_WIDGETS:lw_system_widgets(s->system,!s->system->prefs.widgets); lw_system_save(s->system); render();break;
    case OC_PLAY:s->system->playing=!s->system->playing; show_overlay(OS_OVER_CONTROL);break;
    case OC_NETWORK:case OC_DISPLAY:case OC_SOUND:case OC_PERSONAL:case OC_TIME:case OC_STORAGE:case OC_ABOUT:s->section=(unsigned)(id-OC_NETWORK); render();break;
    case OC_DEVELOPER:s->section=7; render();break;
    case OC_MOTION:s->section=8; render();break;
    case OC_SETTINGS_BACK:s->section=s->section==8 ? 3 : 6; render();break;
    case OC_MOTION_FULL:case OC_MOTION_REDUCED:case OC_MOTION_OFF:
        lw_os_motion_mode((lw_motion_mode_t)(id-OC_MOTION_FULL)); lw_app_state()->settings.animations=id!=OC_MOTION_OFF; lw_app_state_save();
        if (s->closing) { lv_obj_delete(s->closing); s->closing=NULL; }
        lw_os_motion_settle(s->root); render();break;
    case OC_GREET:case OC_LETTER:case OC_REST:
        if (!lw_duo_request(&s->duo,id==OC_GREET ? LW_DUO_GREET : id==OC_LETTER ? LW_DUO_LETTER : LW_DUO_REST)) lw_os_notify("有人正在休息，先留一点安静。");
        else lw_os_notify("互动协调已开始，正式动作素材尚待验收。");
        break;
    }
}
static void tick(lv_timer_t *timer)
{
    (void)timer; uint32_t now=lv_tick_get(),dt=now-s->last_tick; s->last_tick=now; if (dt>200) dt=200;
    if (s->navigation>=0) { if (s->navigation_ms>dt) s->navigation_ms-=dt; else { int page=s->navigation; lw_os_open((lw_os_page_t)page); } }
    int action=s->pending; s->pending=-1; act(action);
    lw_system_tick(s->system,dt); update_actors(dt);
    s->last_clock+=dt; if (s->last_clock>=1000) { s->last_clock=0; refresh(); }
    if (s->last_wifi!=s->system->wifi) {
        lw_wifi_status_t prior=s->last_wifi; s->last_wifi=s->system->wifi;
        if (s->overlay==OS_OVER_WIFI && prior==LW_WIFI_SCANNING) show_overlay(OS_OVER_WIFI);
        else if (s->overlay==OS_OVER_PASSWORD && s->system->wifi==LW_WIFI_CONNECTED) { show_overlay(OS_OVER_WIFI); lw_os_notify("已连接 · 模拟网络"); }
        else if (s->overlay==OS_OVER_PASSWORD && s->system->wifi==LW_WIFI_FAILED) lv_label_set_text(s->over_controls[OC_WIFI_STATUS],"连接失败，请重新输入密码。");
    }
    if (s->last_revision!=s->system->revision) {
        s->last_revision=s->system->revision; lw_app_state()->music_volume=s->system->prefs.volume; s->last_legacy_volume=s->system->prefs.volume; refresh();
    } else if (s->legacy==&lw_app_music_def && lw_app_state()->music_volume!=s->last_legacy_volume) {
        lw_system_volume(s->system,lw_app_state()->music_volume); lw_system_save(s->system); s->last_legacy_volume=s->system->prefs.volume;
    }
    if (s->toast) { s->toast_ms=s->toast_ms>dt ? s->toast_ms-dt : 0; if (!s->toast_ms) { lv_obj_delete(s->toast); s->toast=NULL; } }
}
bool lw_os_start(lw_system_t *system,const lw_actor_art_t art[2])
{
    if (s || !system) return false;
    s=lv_malloc_zeroed(sizeof(*s)); if (!s) return false;
    s->system=system; s->art=art; s->pending=s->navigation=-1; s->previous=lv_screen_active(); s->last_tick=lv_tick_get(); s->last_wifi=system->wifi;
    if (!explicit_capabilities) derive_capabilities(system);
    lw_shell_bind_host(&shell_host);
    lw_duo_init(&s->duo,0x10252026); s->duo.autonomous=false;
    for (unsigned i=0;i<2;i++) { s->duo.actors[i].x=592.0f+i*224.0f; s->duo.actors[i].y=446.0f; s->duo.actors[i].target_x=s->duo.actors[i].x; s->duo.actors[i].target_y=446.0f; }
    (void)lw_duo_set_bounds(&s->duo,510,862,400,446);
    s->root=lv_obj_create(NULL); lv_obj_remove_style_all(s->root); lv_obj_set_size(s->root,1024,600); lv_obj_set_style_bg_opa(s->root,255,0); lv_obj_remove_flag(s->root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s->root,gesture,LV_EVENT_GESTURE,NULL); lv_screen_load(s->root); render(); s->timer=lv_timer_create(tick,20,s);
    if (!s->timer) { lw_os_stop(); return false; } return true;
}
void lw_os_stop(void)
{
    if (!s) return;
    if (s->timer) lv_timer_delete(s->timer);
    if (s->legacy && s->legacy->destroy) s->legacy->destroy();
    lw_system_save(s->system); lv_screen_load(s->previous); lv_obj_delete(s->root); lv_free(s); s=NULL; lw_theme_session_palette(NULL); lw_shell_bind_host(NULL);
}
lv_obj_t *lw_os_control(lw_os_control_t id) { return !s || id>=OC_COUNT ? NULL : s->over_controls[id] ? s->over_controls[id] : s->base_controls[id]; }
lv_obj_t *lw_os_root(void) { return s ? s->root : NULL; }
lw_os_page_t lw_os_page(void) { return s ? s->page : OS_DESKTOP; }
lw_os_overlay_t lw_os_overlay(void) { return s ? s->overlay : OS_OVER_NONE; }
