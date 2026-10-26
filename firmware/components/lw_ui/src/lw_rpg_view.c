/* Layered exploration room over the frozen DSH RPG core. */
#include "lw_rpg_view.h"
#include "lw_rpg_demo.h"
#include "lw_os_design.h"
#include "lw_theme.h"
#include "lw_polish.h"
#include "lw_os.h"
#include "lw_skin.h"
#include "lw_visual_assets.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
struct lw_rpg_view {
    lw_rpg_state_t state; lw_rpg_view_host_t host; const lw_actor_art_t *residents;
    lv_obj_t *root,*map,*actor[2],*dialog,*status,*progress,*action,*action_label,*marker,*spots[3],*badges[3];
    lv_timer_t *timer; uint32_t last,reward_seen; unsigned theme;
    bool suspended,dirty; int pending,last_progress; uint32_t marker_ms;
    int actor_x[2],actor_y[2];
    lw_sprite_t *players[2]; const lw_clip_t *clips[2];
    lw_rpg_hotspot_t hotspots[3];
    lw_rpg_world_t world; lw_rpg_room_t room; uint8_t floor[128];
    uint32_t animation_ms,reaction_ms; int queued_spot; bool art;
};
enum { ACT_INTERACT=1,ACT_NEXT,ACT_CHOICE0,ACT_CHOICE1,ACT_JOURNAL,ACT_CLOSE };
void lw_rpg_view_tile_point(int x,int y,int *px,int *py) { *px=64+x*56+28; *py=310+y*22; }
static void dialog_render(lw_rpg_view_t *v);
static const lw_os_palette_t *colors(lw_rpg_view_t *v) { return lw_os_palette(v->theme); }
static void action(lv_event_t *e)
{ lw_rpg_view_t *v=lv_event_get_user_data(e); v->pending=(int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)); }
static lv_obj_t *button(lw_rpg_view_t *v,lv_obj_t *p,const char *text,int x,int y,int w,int id)
{
    lv_obj_t *o=lw_os_box(p,x,y,w,48,0xf2dfc4,8,224); lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
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
static const lw_clip_t *resident_clip(lw_rpg_view_t *v,unsigned i,const char *name)
{
    if (!v->residents || i>=2 || v->residents[i].placeholder) return NULL;
    const lw_actor_art_t *art=&v->residents[i];
    for (unsigned n=0;n<art->count;n++) if (!strcmp(art->clips[n].name,name)) return &art->clips[n].clip;
    if (strcmp(name,"idle")) return resident_clip(v,i,"idle");
    return NULL;
}
static void refresh(lw_rpg_view_t *v)
{
    for (unsigned i=0;i<2;i++) {
        lw_rpg_tile_t t=v->state.actors[i].pos; int x,y; lw_rpg_view_tile_point(t.x,t.y,&x,&y);
        if (i) {
            int px,py;lw_rpg_view_tile_point(v->state.actors[0].pos.x,v->state.actors[0].pos.y,&px,&py);
            if (abs(x-px)<118 && abs(y-py)<44) {
                int right=px+128,left=px-128;
                if (lw_rpg_walkable(&v->room,(right-64)/56,(y-299)/22)) x=right;
                else if (lw_rpg_walkable(&v->room,(left-64)/56,(y-299)/22)) x=left;
            }
        }
        if (v->players[i]) {
            const char *name="idle";
            if (v->reaction_ms) name="wave";
            else if (v->state.actors[i].moving) { static const char *const walks[]={"walk_down","walk_left","walk_right","walk_up"}; name=walks[v->state.actors[i].dir]; }
            const lw_clip_t *clip=resident_clip(v,i,name);
            if (clip && clip!=v->clips[i]) { lw_sprite_play(v->players[i],clip); v->clips[i]=clip; }
            int oldx=lv_obj_get_x(v->actor[i]),oldy=lv_obj_get_y(v->actor[i]); lw_sprite_set_anchor(v->players[i],x,y);
            int nx=lv_obj_get_x(v->actor[i]),ny=lv_obj_get_y(v->actor[i]);
            if (v->actor_x[i]!=x || v->actor_y[i]!=y) { lv_obj_set_pos(v->actor[i],oldx,oldy); lw_os_motion_step(v->actor[i],nx,ny,140); }
            if (lw_os_motion_get()!=LW_MOTION_OFF) lw_sprite_seek(v->players[i],v->reaction_ms ? 1600-v->reaction_ms : v->animation_ms);
        } else lv_obj_set_pos(v->actor[i],x-18,y-48);
        if (v->actor_x[i]!=x || v->actor_y[i]!=y) { v->actor_x[i]=x; v->actor_y[i]=y; v->dirty=true; }
    }
    /* Residents stay on the surveyed clear floor; no fake furniture cutouts. */
    if (v->actor_y[0]>v->actor_y[1]) lv_obj_move_to_index(v->actor[0],-1);
    else lv_obj_move_to_index(v->actor[1],-1);
    int progress=lw_rpg_quest_progress(&v->state);
    if (progress!=v->last_progress) {
        lv_label_set_text_fmt(v->progress,"整理回忆盒\n已找到 %d / %d",progress,lw_rpg_quest_total(&v->state));
        for (unsigned i=0;i<3;i++) { bool collected=(v->state.collected_mask&(1u<<i))!=0; lv_obj_set_style_bg_color(v->badges[i],lv_color_hex(collected ? colors(v)->accent : colors(v)->soft),0); if (collected && v->last_progress>=0) lw_os_motion(v->badges[i],LW_MOVE_REWARD); }
        if (v->last_progress>=0) lv_label_set_text(v->status,"收好了，又留住了一点温柔。 · 示例剧情");
        if (v->last_progress>=0) v->reaction_ms=1600;
        v->last_progress=progress;
    }
    lv_label_set_text(v->action_label,lw_rpg_hotspot_near(&v->state)>=0 ? "查看身边物品" : "先走到物品旁边");
}
static void walk_to_spot(lw_rpg_view_t *v,unsigned i)
{
    if (i>=v->world.hotspot_count || v->state.dialogue_active || v->dialog) return;
    if (v->state.collected_mask&(1u<<i)) {lv_label_set_text(v->status,"这片回忆已经收好了。");v->queued_spot=-1;return;}
    lw_rpg_tile_t t=v->world.hotspots[i].tile; int best=999,bx=-1,by=-1;
    for (int y=t.y-1;y<=t.y+1;y++) for (int x=t.x-1;x<=t.x+1;x++) if (lw_rpg_walkable(&v->room,x,y) && ((x-t.x)*(x-t.x)+(y-t.y)*(y-t.y)<=1)) {
        lw_rpg_state_t probe=v->state; probe.actors[0].pos=(lw_rpg_tile_t){(int16_t)x,(int16_t)y}; if (lw_rpg_hotspot_near(&probe)!=(int)i) continue;
        int cost=abs(x-v->state.actors[0].pos.x)+abs(y-v->state.actors[0].pos.y); if (cost<best) {best=cost;bx=x;by=y;}
    }
    if (bx>=0 && lw_rpg_tap_tile(&v->state,bx,by)) { v->queued_spot=(int)i; v->dirty=true; lv_label_set_text_fmt(v->status,"走近%s，再轻轻打开。",v->world.hotspots[i].label); }
}
static void spot_tap(lv_event_t *e)
{ lw_rpg_view_t *v=lv_event_get_user_data(e); if (!v->suspended) walk_to_spot(v,(unsigned)(uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e))); }
static void map_click(lv_event_t *e)
{
    lw_rpg_view_t *v=lv_event_get_user_data(e); if (v->suspended || v->state.dialogue_active || v->dialog) return;
    lv_indev_t *input=lv_indev_active(); if (!input) return; lv_point_t p; lv_indev_get_point(input,&p); lv_area_t a; lv_obj_get_coords(v->map,&a);
    if (p.x<a.x1 || p.y<a.y1 || p.x>a.x2 || p.y>a.y2) return;
    int x=(p.x-a.x1-64)/56,y=(p.y-a.y1-299)/22; if (p.x<a.x1+64 || p.x>=a.x1+960 || p.y<a.y1+299 || y>=8) return;
    int px,py; lw_rpg_view_tile_point(x,y,&px,&py); lv_obj_set_pos(v->marker,px-16,py-7); lv_obj_remove_flag(v->marker,LV_OBJ_FLAG_HIDDEN); v->marker_ms=700;
    int h=lw_rpg_hotspot_at(&v->state,x,y); if (h>=0) { walk_to_spot(v,(unsigned)h); return; } v->queued_spot=-1;
    if (lw_rpg_tap_tile(&v->state,x,y)) { v->dirty=true; lv_label_set_text(v->status,"慢慢走过去…"); }
    else lv_label_set_text(v->status,"这里暂时不能走，试试木地板。");
}
static lv_obj_t *panel(lw_rpg_view_t *v,const char *title,unsigned art)
{
    lw_os_scene_focus(true);
    if (v->dialog) lv_obj_delete(v->dialog);
    v->dialog=lw_os_box(v->root,0,52,1024,494,0x493e43,0,70); lv_obj_add_flag(v->dialog,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *p=lw_os_box(v->dialog,212,60,600,352,colors(v)->surface,20,0);
    if (!lw_polish_art(p,art,28,0)) lv_obj_set_style_bg_opa(p,255,0);
    lw_os_label(p,title,&lw_font_22,art==1 ? 98 : 206,art==1 ? 48 : 90,380,colors(v)->text); lw_os_motion(p,art==1 ? LW_MOVE_PAPER : LW_MOVE_DIALOG); return p;
}
static void dialog_render(lw_rpg_view_t *v)
{
    if (v->dialog) { lv_obj_delete(v->dialog); v->dialog=NULL; }
    if (!v->state.dialogue_active) return;
    const lw_rpg_line_t *line=lw_rpg_dialogue_line(&v->state);
    lv_obj_t *p=panel(v,line ? (!strcmp(line->speaker,"qianqian") ? "千千" : line->speaker) : "选择",0);
    lw_os_label(p,line ? line->text : "",&lw_font_22,170,140,350,colors(v)->text);
    if (lw_rpg_dialogue_has_choices(&v->state)) {
        const lw_rpg_dialogue_t *d=lw_rpg_dialogue_current(&v->state);
        for (unsigned i=0;i<d->choice_count && i<2;i++) button(v,p,d->choices[i].text,70+(int)i*244,238,216,ACT_CHOICE0+(int)i);
    } else button(v,p,"下一句",332,238,198,ACT_NEXT);
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
            lv_obj_t *p=panel(v,"回忆盒整理好了",0); lw_os_icon(p,OS_I_HEART,82,136,48,colors(v)->accent);
            lw_os_label(p,v->state.world->quest->reward_label,&lw_font_22,148,142,376,colors(v)->text);
            button(v,p,"收好奖励",332,238,198,ACT_CLOSE); lw_os_motion(p,LW_MOVE_REWARD);
        }
    } else if (cmd==ACT_JOURNAL && !v->state.dialogue_active) {
        lv_obj_t *p=panel(v,"任务本",1);
        lw_os_label(p,"示例故事",&lw_font_18,330,48,180,colors(v)->muted);
        for (unsigned i=0;i<3;i++) {
            bool done=(v->state.collected_mask&(1u<<i))!=0; int x=i<2 ? 92 : 330,y=i<2 ? 100+(int)i*88 : 100;
            lw_os_icon(p,done ? OS_I_CHECK : i==0 ? OS_I_ALBUM : i==1 ? OS_I_MAIL : OS_I_LEAF,x,y,30,colors(v)->accent);
            lw_os_label(p,v->world.hotspots[i].label,&lw_font_18,x+38,y,150,colors(v)->text);
            lw_os_label(p,done ? "已经收好" : "等你发现",&lw_font_18,x+38,y+30,150,colors(v)->muted);
        }
        button(v,p,"回到房间",330,250,180,ACT_CLOSE);
    } else if (cmd==ACT_CLOSE && v->dialog && !v->state.dialogue_active) { lv_obj_delete(v->dialog); v->dialog=NULL; }
}
static void tick(lv_timer_t *timer)
{
    lw_rpg_view_t *v=lv_timer_get_user_data(timer); uint32_t now=lv_tick_get(),dt=now-v->last; v->last=now; if (dt>200) dt=200;
    if (v->suspended) return;
    if (v->marker_ms) { v->marker_ms=v->marker_ms>dt ? v->marker_ms-dt : 0; if (!v->marker_ms) lv_obj_add_flag(v->marker,LV_OBJ_FLAG_HIDDEN); }
    int cmd=v->pending; v->pending=0; if (cmd) process(v,cmd);
    if (!v->state.dialogue_active && !v->dialog) {
        lw_rpg_tick(&v->state,dt);
        if (v->queued_spot>=0 && !v->state.actors[0].moving && lw_rpg_hotspot_near(&v->state)==v->queued_spot) { v->queued_spot=-1; if (lw_rpg_interact(&v->state)) dialog_render(v); }
    }
    if (lw_os_motion_get()!=LW_MOTION_OFF) v->animation_ms+=dt;
    if (v->reaction_ms) v->reaction_ms=v->reaction_ms>dt ? v->reaction_ms-dt : 0;
    refresh(v);
}
static void destroyed(lv_event_t *e)
{ lw_rpg_view_t *v=lv_event_get_user_data(e); if (v->timer) lv_timer_delete(v->timer); save(v); lv_free(v); }
lw_rpg_view_t *lw_rpg_view_create(lv_obj_t *parent,const lw_rpg_view_host_t *host,unsigned theme,const lw_actor_art_t art[2])
{
    lw_rpg_view_t *v=lv_malloc_zeroed(sizeof(*v)); if (!v) return NULL;
    v->residents=art; v->theme=theme; v->queued_spot=-1; v->last_progress=-1; v->actor_x[0]=v->actor_x[1]=v->actor_y[0]=v->actor_y[1]=-1; if (host) v->host=*host;
    if (!lw_rpg_init(&v->state,lw_rpg_demo_world())) { lv_free(v); return NULL; }
    uint8_t bytes[LW_RPG_SAVE_BYTES];
    if (v->host.load && v->host.load(v->host.user,bytes)) {
        /* Both old and new layouts use the same save format. Validate CRC/version
         * through the frozen decoder, then relocate only positions on furniture. */
        const lw_rpg_world_t *base=v->state.world; lw_rpg_world_t loaded=*base;
        lw_rpg_room_t roomy=*base->room; uint8_t all_floor[128]; memset(all_floor,1,sizeof(all_floor));
        roomy.floor=all_floor; loaded.room=&roomy; v->state.world=&loaded;
        (void)lw_rpg_save_decode(&v->state,bytes,sizeof(bytes)); v->state.world=base;
    }
    /* UI-owned room variant; quest IDs/dialogue/save bytes and frozen core stay unchanged. */
    v->world=*v->state.world; v->room=*v->world.room; memcpy(v->floor,v->room.floor,128); v->room.floor=v->floor; v->world.room=&v->room;
    memcpy(v->hotspots,v->world.hotspots,sizeof(v->hotspots)); v->world.hotspots=v->hotspots;
    /* Footprints measured from Meowa's complete workshop, not from the old demo tiles. */
    for (int y=0;y<8;y++) for (int x=0;x<16;x++)
        v->floor[y*16+x]=(x>=4 && !(x<=6 && y<=3) && !(x>=12 && y<=6));
    v->hotspots[0].tile=(lw_rpg_tile_t){5,3}; /* photo stool: approach from below */
    v->hotspots[1].tile=(lw_rpg_tile_t){12,4}; /* desk: approach from left */
    v->hotspots[2].tile=(lw_rpg_tile_t){4,3}; /* large plant: approach from below */
    for (unsigned i=0;i<3;i++) { lw_rpg_tile_t t=v->hotspots[i].tile; v->floor[t.y*16+t.x]=0; }
    v->state.world=&v->world;
    for (unsigned i=0;i<2;i++) if (!lw_rpg_walkable(&v->room,v->state.actors[i].pos.x,v->state.actors[i].pos.y)) {
        lw_rpg_tile_t p=v->state.actors[i].pos; bool found=false;
        for (int radius=1;radius<24 && !found;radius++) for (int y=0;y<8 && !found;y++) for (int x=0;x<16;x++) if (abs(x-p.x)+abs(y-p.y)==radius && lw_rpg_walkable(&v->room,x,y)) { v->state.actors[i].pos=(lw_rpg_tile_t){(int16_t)x,(int16_t)y}; v->dirty=true; found=true; break; }
    }
    v->root=lw_os_box(parent,0,0,1024,494,colors(v)->bg,0,255); lv_obj_add_event_cb(v->root,destroyed,LV_EVENT_DELETE,v);
    v->map=lw_os_box(v->root,0,0,1024,494,colors(v)->wood,0,255); lv_obj_add_flag(v->map,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(v->map,map_click,LV_EVENT_CLICKED,v);
    lv_obj_t *background=lw_visual_rpg_image(v->map);
    if (!background) lw_os_label(v->map,"房间资源未安装 · 散步与任务仍可用",&lw_font_18,32,150,700,colors(v)->text);
    static const int hits[3][4]={{314,280,96,80},{825,240,80,52},{214,185,102,162}};
    for (unsigned i=0;i<3;i++) {
        v->spots[i]=lw_os_box(v->map,hits[i][0],hits[i][1],hits[i][2],hits[i][3],colors(v)->accent,12,0);
        lv_obj_set_style_bg_opa(v->spots[i],45,LV_STATE_PRESSED); lw_os_motion_touch(v->spots[i]);
        lv_obj_add_flag(v->spots[i],LV_OBJ_FLAG_CLICKABLE); lv_obj_set_user_data(v->spots[i],(void *)(uintptr_t)i); lv_obj_add_event_cb(v->spots[i],spot_tap,LV_EVENT_CLICKED,v);
    }
    v->marker=lw_os_box(v->map,0,0,34,14,0,7,0); lv_obj_set_style_border_color(v->marker,lv_color_hex(colors(v)->accent),0); lv_obj_set_style_border_width(v->marker,2,0); lv_obj_add_flag(v->marker,LV_OBJ_FLAG_HIDDEN);
    v->art=resident_clip(v,0,"idle") && resident_clip(v,1,"idle");
    for (unsigned i=0;i<2;i++) {
        const lw_clip_t *clip=resident_clip(v,i,"idle");
        if (clip) { v->players[i]=lw_sprite_create(v->map,0,0,288); if (v->players[i]) { lw_sprite_set_external_clock(v->players[i],true); lw_sprite_set_pixel_mode(v->players[i]); lw_sprite_play(v->players[i],clip); v->clips[i]=clip; v->actor[i]=lw_sprite_object(v->players[i]); } }
        if (!v->actor[i]) v->actor[i]=lw_os_label(v->map,i ? "千千\n资源待载" : "男方\n资源待载",&lw_font_18,0,0,80,colors(v)->text);
    }
    lv_obj_t *side=lw_os_box(v->root,744,65,254,112,0xf5e4c8,8,210);if(lw_skin_panel(side,SK_CC_PANEL,254,112,12))lv_obj_set_style_bg_opa(side,0,0);
    v->progress=lw_os_label(side,"",&lw_font_18,16,12,126,colors(v)->text);
    for (unsigned i=0;i<3;i++) { v->badges[i]=lw_os_box(side,16+(int)i*38,74,28,28,colors(v)->soft,8,255); lw_os_icon(v->badges[i],i==0 ? OS_I_ALBUM : i==1 ? OS_I_MAIL : OS_I_LEAF,4,4,20,colors(v)->deep); }
    lv_obj_t *journal=button(v,side,"任务本",140,14,100,ACT_JOURNAL);lw_skin_button(journal,SK_ACTION,SK_SELECTED,SK_PRESSED);
    v->action=button(v,v->root,"查看身边物品",760,432,238,ACT_INTERACT);
    v->action_label=lv_obj_get_child(v->action,0);lw_skin_button(v->action,SK_ACTION,SK_SELECTED,SK_PRESSED);
    lv_obj_t *note=lw_os_box(v->root,26,444,708,38,0xf5e4c8,8,0);
    v->status=lw_os_label(note,"示例故事 · 照片盒 / 信封 / 绿植",&lw_font_18,12,8,684,0x75595c);
    v->last=lv_tick_get(); v->timer=lv_timer_create(tick,20,v);
    if (!v->timer) { lv_obj_delete(v->root); return NULL; }
    refresh(v); return v;
}
void lw_rpg_view_suspend(lw_rpg_view_t *v,bool suspended) { if (v) { v->suspended=suspended; v->last=lv_tick_get(); } }
const lw_rpg_state_t *lw_rpg_view_state(const lw_rpg_view_t *v) { return v ? &v->state : NULL; }
bool lw_rpg_view_has_art(const lw_rpg_view_t *v) { return v && v->art; }
lv_obj_t *lw_rpg_view_actor(const lw_rpg_view_t *v,unsigned i) { return v && i<2 ? v->actor[i] : NULL; }
