/* Technical RPG slice; test tokens are intentionally not final character art. */
#include "lw_rpg_view.h"
#include "lw_rpg_demo.h"
#include "lw_os_design.h"
#include "lw_theme.h"
#include <stdint.h>
#include <string.h>
struct lw_rpg_view {
    lw_rpg_state_t state; lw_rpg_view_host_t host;
    lv_obj_t *root,*map,*actor[2],*dialog,*status,*progress,*action,*marker,*spots[3],*badges[3];
    lv_timer_t *timer; uint32_t last,reward_seen; unsigned theme;
    bool suspended,dirty; int pending,last_progress; uint32_t marker_ms;
    int actor_x[2],actor_y[2];
};
enum { ACT_INTERACT=1,ACT_NEXT,ACT_CHOICE0,ACT_CHOICE1,ACT_JOURNAL,ACT_CLOSE };
static void dialog_render(lw_rpg_view_t *v);
static const lw_os_palette_t *colors(lw_rpg_view_t *v) { return lw_os_palette(v->theme); }
static void action(lv_event_t *e)
{ lw_rpg_view_t *v=lv_event_get_user_data(e); v->pending=(int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)); }
static lv_obj_t *button(lw_rpg_view_t *v,lv_obj_t *p,const char *text,int x,int y,int w,int id)
{
    lv_obj_t *o=lw_os_box(p,x,y,w,48,colors(v)->soft,14,255); lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lw_os_motion_touch(o);
    lv_obj_set_user_data(o,(void *)(intptr_t)id); lv_obj_add_event_cb(o,action,LV_EVENT_CLICKED,v);
    lv_obj_t *label=lw_os_label(o,text,&lw_font_18,0,0,w,colors(v)->text); lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(label); return o;
}
static void save(lw_rpg_view_t *v)
{
    if (!v->dirty || !v->host.save) return;
    uint8_t bytes[LW_RPG_SAVE_BYTES];
    if (lw_rpg_save_encode(&v->state,bytes,sizeof(bytes))==sizeof(bytes) && v->host.save(v->host.user,bytes)) v->dirty=false;
    else lv_label_set_text(v->status,"保存失败，退出前将重试。");
}
static void refresh(lw_rpg_view_t *v)
{
    bool overlap=v->state.actors[0].pos.x==v->state.actors[1].pos.x && v->state.actors[0].pos.y==v->state.actors[1].pos.y;
    for (unsigned i=0;i<2;i++) {
        lw_rpg_tile_t t=v->state.actors[i].pos; int x=t.x*44+(overlap ? (i ? 18 : 0) : 5),y=t.y*44+5;
        lv_obj_set_width(v->actor[i],overlap ? 26 : 34);
        if (v->actor_x[i]!=x || v->actor_y[i]!=y) { lw_os_motion_step(v->actor[i],x,y,120); v->actor_x[i]=x; v->actor_y[i]=y; v->dirty=true; }
    }
    unsigned near=v->state.actors[0].pos.y>v->state.actors[1].pos.y ? 0 : 1;
    lv_obj_move_to_index(v->actor[1-near],-1); lv_obj_move_to_index(v->actor[near],-1);
    int progress=lw_rpg_quest_progress(&v->state);
    if (progress!=v->last_progress) {
        lv_label_set_text_fmt(v->progress,"整理回忆盒\n已找到 %d / %d",progress,lw_rpg_quest_total(&v->state));
        for (unsigned i=0;i<3;i++) { bool collected=(v->state.collected_mask&(1u<<i))!=0; lv_obj_set_style_opa(v->spots[i],collected ? 95 : 255,0); lv_obj_set_style_bg_color(v->badges[i],lv_color_hex(collected ? colors(v)->accent : colors(v)->soft),0); if (collected && v->last_progress>=0) lw_os_motion(v->badges[i],LW_MOVE_REWARD); }
        if (v->last_progress>=0) lv_label_set_text(v->status,"收好了，又留住了一点温柔。 · 示例剧情");
        v->last_progress=progress;
    }
    lv_label_set_text(lv_obj_get_child(v->action,0),lw_rpg_hotspot_near(&v->state)>=0 ? "查看身边物品" : "先走到物品旁边");
}
static void map_click(lv_event_t *e)
{
    lw_rpg_view_t *v=lv_event_get_user_data(e); if (v->suspended || v->state.dialogue_active || v->dialog) return;
    lv_indev_t *input=lv_indev_active(); if (!input) return; lv_point_t p; lv_indev_get_point(input,&p); lv_area_t a; lv_obj_get_coords(v->map,&a);
    if (p.x<a.x1 || p.y<a.y1 || p.x>a.x2 || p.y>a.y2) return;
    int x=(p.x-a.x1)/44,y=(p.y-a.y1)/44;
    lv_obj_set_pos(v->marker,x*44+3,y*44+3); lv_obj_remove_flag(v->marker,LV_OBJ_FLAG_HIDDEN); v->marker_ms=700;
    if (lw_rpg_tap_tile(&v->state,x,y)) { v->dirty=true; lv_label_set_text(v->status,"慢慢走过去…"); }
    else lv_label_set_text(v->status,"这里暂时不能走，试试木地板。");
}
static lv_obj_t *panel(lw_rpg_view_t *v,const char *title)
{
    if (v->dialog) lv_obj_delete(v->dialog);
    v->dialog=lw_os_box(v->root,0,52,1024,442,0x282335,0,110); lv_obj_add_flag(v->dialog,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *p=lw_os_box(v->dialog,112,132,800,280,colors(v)->surface,26,255);
    lw_os_box(p,24,23,4,24,colors(v)->accent,2,255); lw_os_label(p,title,&lw_font_22,42,20,720,colors(v)->text); lw_os_motion(p,LW_MOVE_DIALOG); return p;
}
static void dialog_render(lw_rpg_view_t *v)
{
    if (v->dialog) { lv_obj_delete(v->dialog); v->dialog=NULL; }
    if (!v->state.dialogue_active) return;
    const lw_rpg_line_t *line=lw_rpg_dialogue_line(&v->state);
    lv_obj_t *p=panel(v,line ? line->speaker : "选择");
    lw_os_label(p,line ? line->text : "",&lw_font_22,24,76,752,colors(v)->text);
    if (lw_rpg_dialogue_has_choices(&v->state)) {
        const lw_rpg_dialogue_t *d=lw_rpg_dialogue_current(&v->state);
        for (unsigned i=0;i<d->choice_count && i<2;i++) button(v,p,d->choices[i].text,24+(int)i*388,208,364,ACT_CHOICE0+(int)i);
    } else button(v,p,"下一句",528,208,248,ACT_NEXT);
}
static void process(lw_rpg_view_t *v,int cmd)
{
    if (cmd==ACT_INTERACT && !v->dialog) {
        if (lw_rpg_interact(&v->state)) dialog_render(v);
        else lv_label_set_text(v->status,"附近没有新的物品，继续散步吧。");
    } else if (cmd==ACT_NEXT || cmd==ACT_CHOICE0 || cmd==ACT_CHOICE1) {
        if (cmd==ACT_NEXT) lw_rpg_dialogue_advance(&v->state); else lw_rpg_dialogue_choose(&v->state,cmd-ACT_CHOICE0);
        v->dirty=true; dialog_render(v); save(v);
        if (v->state.reward_events!=v->reward_seen) {
            v->reward_seen=v->state.reward_events;
            if (v->host.reward) v->host.reward(v->host.user,v->state.unlocked_reward);
            lv_obj_t *p=panel(v,"回忆盒整理好了"); lw_os_icon(p,OS_I_HEART,28,82,64,colors(v)->accent);
            lw_os_label(p,v->state.world->quest->reward_label,&lw_font_28,112,96,640,colors(v)->text);
            button(v,p,"收好奖励",528,208,248,ACT_CLOSE); lw_os_motion(p,LW_MOVE_REWARD);
        }
    } else if (cmd==ACT_JOURNAL && !v->state.dialogue_active) {
        lv_obj_t *p=panel(v,"任务本 · 示例故事");
        lw_os_label(p,v->state.world->quest->hint,&lw_font_22,24,76,752,colors(v)->text);
        lv_obj_t *n=lw_os_label(p,"",&lw_font_18,24,144,752,colors(v)->muted); lv_label_set_text_fmt(n,"照片盒 · 信封桌 · 小盆栽   %d / 3",lw_rpg_quest_progress(&v->state));
        button(v,p,"回到房间",528,208,248,ACT_CLOSE);
    } else if (cmd==ACT_CLOSE && v->dialog && !v->state.dialogue_active) { lv_obj_delete(v->dialog); v->dialog=NULL; }
}
static void tick(lv_timer_t *timer)
{
    lw_rpg_view_t *v=lv_timer_get_user_data(timer); uint32_t now=lv_tick_get(),dt=now-v->last; v->last=now; if (dt>200) dt=200;
    if (v->suspended) return;
    if (v->marker_ms) { v->marker_ms=v->marker_ms>dt ? v->marker_ms-dt : 0; if (!v->marker_ms) lv_obj_add_flag(v->marker,LV_OBJ_FLAG_HIDDEN); }
    int cmd=v->pending; v->pending=0; if (cmd) process(v,cmd);
    if (!v->state.dialogue_active && !v->dialog) lw_rpg_tick(&v->state,dt);
    refresh(v);
}
static void destroyed(lv_event_t *e)
{ lw_rpg_view_t *v=lv_event_get_user_data(e); if (v->timer) lv_timer_delete(v->timer); save(v); lv_free(v); }
static void floor_rect(lv_layer_t *layer,int x,int y,int w,int h,uint32_t color,int radius)
{ lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_color=lv_color_hex(color); d.radius=radius; lv_area_t area={x,y,x+w-1,y+h-1}; lv_draw_rect(layer,&d,&area); }
static void room_draw(lv_event_t *e)
{
    lw_rpg_view_t *v=lv_event_get_user_data(e); lv_area_t a; lv_obj_get_coords(v->map,&a); lv_layer_t *layer=lv_event_get_layer(e); const lw_os_palette_t *c=colors(v);
    floor_rect(layer,a.x1,a.y1,704,352,c->wood,10);
    for (int y=0;y<8;y++) { floor_rect(layer,a.x1,a.y1+y*44,704,1,c->line,0); for (int x=(y%2)*44;x<704;x+=88) floor_rect(layer,a.x1+x,a.y1+y*44,1,43,c->line,0); }
    /* Same blocked cells as the immutable demo map: small sofa and bookcase. */
    floor_rect(layer,a.x1+88,a.y1+88,88,44,c->deep,8); floor_rect(layer,a.x1+94,a.y1+98,35,24,c->soft,6); floor_rect(layer,a.x1+135,a.y1+98,35,24,c->soft,6);
    floor_rect(layer,a.x1+396,a.y1+88,132,88,c->deep,8); floor_rect(layer,a.x1+402,a.y1+97,120,28,c->soft,3); floor_rect(layer,a.x1+402,a.y1+133,120,32,c->soft,3);
    for (int i=0;i<7;i++) floor_rect(layer,a.x1+410+i*15,a.y1+103,9,19,i%2 ? c->accent : c->wood,1);
    floor_rect(layer,a.x1+220,a.y1+132,44,44,c->surface,8); floor_rect(layer,a.x1+396,a.y1+176,44,44,c->surface,8);
}
lw_rpg_view_t *lw_rpg_view_create(lv_obj_t *parent,const lw_rpg_view_host_t *host,unsigned theme)
{
    lw_rpg_view_t *v=lv_malloc_zeroed(sizeof(*v)); if (!v) return NULL;
    v->theme=theme; v->last_progress=-1; v->actor_x[0]=v->actor_x[1]=v->actor_y[0]=v->actor_y[1]=-1; if (host) v->host=*host;
    if (!lw_rpg_init(&v->state,lw_rpg_demo_world())) { lv_free(v); return NULL; }
    uint8_t bytes[LW_RPG_SAVE_BYTES]; if (v->host.load && v->host.load(v->host.user,bytes)) (void)lw_rpg_save_decode(&v->state,bytes,sizeof(bytes));
    v->root=lw_os_box(parent,0,0,1024,494,colors(v)->bg,0,255); lv_obj_add_event_cb(v->root,destroyed,LV_EVENT_DELETE,v);
    lw_os_label(v->root,"回忆小冒险 · 示例房间 / TEST A+B",&lw_font_22,32,63,920,colors(v)->text);
    v->map=lw_os_box(v->root,32,104,704,352,colors(v)->wood,8,255); lv_obj_add_flag(v->map,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(v->map,map_click,LV_EVENT_CLICKED,v);
    lv_obj_add_event_cb(v->map,room_draw,LV_EVENT_DRAW_MAIN,v);
    for (unsigned i=0;i<v->state.world->hotspot_count;i++) { const lw_rpg_hotspot_t *h=&v->state.world->hotspots[i]; v->spots[i]=lw_os_icon(v->map,i==0 ? OS_I_ALBUM : i==1 ? OS_I_MAIL : OS_I_LEAF,h->tile.x*44+5,h->tile.y*44+5,34,colors(v)->deep); }
    v->marker=lw_os_box(v->map,0,0,38,38,0,19,0); lv_obj_set_style_border_color(v->marker,lv_color_hex(colors(v)->accent),0); lv_obj_set_style_border_width(v->marker,3,0); lv_obj_add_flag(v->marker,LV_OBJ_FLAG_HIDDEN);
    for (unsigned i=0;i<2;i++) { v->actor[i]=lw_os_box(v->map,0,0,34,34,i ? 0x917b9f : 0x7e9c97,12,255); lv_obj_t *t=lw_os_label(v->actor[i],i ? "B" : "A",&lv_font_montserrat_20,0,0,0,0xffffff); lv_obj_center(t); }
    lv_obj_t *side=lw_os_box(v->root,760,104,232,352,colors(v)->surface,22,255);
    v->progress=lw_os_label(side,"",&lw_font_22,20,18,196,colors(v)->text);
    for (unsigned i=0;i<3;i++) { v->badges[i]=lw_os_box(side,20+(int)i*64,100,44,44,colors(v)->soft,14,255); lw_os_icon(v->badges[i],i==0 ? OS_I_ALBUM : i==1 ? OS_I_MAIL : OS_I_LEAF,7,7,30,colors(v)->deep); }
    button(v,side,"打开任务本",16,164,200,ACT_JOURNAL); v->action=button(v,side,"查看身边物品",16,224,200,ACT_INTERACT);
    lw_os_label(side,"点地板走动\n测试角色 · 素材待定",&lw_font_18,20,286,196,colors(v)->muted);
    v->status=lw_os_label(v->root,"示例剧情，不是真实私人回忆。",&lw_font_18,32,466,940,colors(v)->muted);
    v->last=lv_tick_get(); v->timer=lv_timer_create(tick,20,v);
    if (!v->timer) { lv_obj_delete(v->root); return NULL; }
    refresh(v); return v;
}
void lw_rpg_view_suspend(lw_rpg_view_t *v,bool suspended) { if (v) { v->suspended=suspended; v->last=lv_tick_get(); } }
const lw_rpg_state_t *lw_rpg_view_state(const lw_rpg_view_t *v) { return v ? &v->state : NULL; }
