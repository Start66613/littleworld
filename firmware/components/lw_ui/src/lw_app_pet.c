/* Little World - pet page: the character plus direct interactions.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "lw_app_state.h"
#include "lw_pet_view.h"
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"
#include "lw_os_design.h"
#include "lw_os.h"
#include "lw_polish.h"
#include "lw_visual_assets.h"
#include "lw_ambience.h"
#include "lw_skin.h"
#include "lw/lw_pet.h"

static lw_pet_view_t *s_view;
static lv_timer_t *s_timer;
static lv_obj_t *s_mood_label;
static lv_obj_t *s_fullness_bar;
static lv_obj_t *s_energy_bar;
static lv_obj_t *s_affection_bar;
static lv_obj_t *s_feedback;
static lv_obj_t *s_sleep_button_label;
static lv_obj_t *s_sleep_button;
static uint32_t s_feedback_until;
static lw_sprite_t *couple; static const lw_clip_t *couple_clip;
static lw_sprite_t *dog; static const lw_clip_t *dog_clip;
static bool polished; static uint32_t room_clock,room_last,reaction_until;
static bool joint_bed;static uint32_t empty_until;

/* The couple's wave clip is 16 frames at 100 ms = 1600 ms. A 1600 ms reaction
 * window ended exactly on the last frame, so on the board the greeting was over
 * before it registered. 2600 ms plays the whole clip and holds the final pose. */
#define LW_PET_WAVE_MS 2600u

static void refresh(void)
{
    lw_state_t *state = lw_app_state();
    uint32_t now = lw_now();
    lw_pet_t *pet = &state->pet;
    if (polished) {
        uint32_t tick=lv_tick_get(),dt=tick-room_last; room_last=tick;
        if (lw_os_motion_get()!=LW_MOTION_OFF && lw_os_overlay()==OS_OVER_NONE) room_clock+=dt;
        if (couple) {
            const char *name=pet->sleeping ? "v42.couple.rest" : room_clock<reaction_until ? "v42.couple.wave" : "v42.couple.seated";
            const lw_clip_t *clip=lw_polish_clip(name); if (!clip) clip=lw_polish_clip("v42.couple.seated");
            if (clip && clip!=couple_clip) {lw_sprite_play(couple,clip);couple_clip=clip;}
            if (lw_os_overlay()==OS_OVER_NONE) lw_sprite_seek(couple,lw_os_motion_get()==LW_MOTION_OFF ? 0 : room_clock<reaction_until ? LW_PET_WAVE_MS-(reaction_until-room_clock) : room_clock);
        }
        if (dog) {const char *id=joint_bed ? (pet->sleeping ? "v42.dogbed.sleep" : room_clock<empty_until ? "v42.dogbed.empty" : "v42.dogbed.look") : pet->sleeping ? "v42.dog.sleep" : "v42.dog.idle";const lw_clip_t *clip=lw_polish_clip(id);if (clip && clip!=dog_clip) {lw_sprite_play(dog,clip);dog_clip=clip;}if (lw_os_overlay()==OS_OVER_NONE) lw_sprite_seek(dog,lw_os_motion_get()==LW_MOTION_OFF ? 0 : room_clock);}
    }
    if (s_view != NULL) {
        lw_pet_view_refresh(s_view, pet, now);
    }
    if (s_mood_label != NULL) {
        lv_label_set_text(s_mood_label, lw_pet_mood_name(lw_pet_mood(pet, now)));
    }
    if (s_fullness_bar != NULL) {
        lw_bar_set(s_fullness_bar, pet->fullness);
    }
    if (s_energy_bar != NULL) {
        lw_bar_set(s_energy_bar, pet->energy);
    }
    if (s_affection_bar != NULL) {
        lw_bar_set(s_affection_bar, pet->affection);
    }
    if (s_feedback != NULL && s_feedback_until != 0 && now > s_feedback_until) {
        lv_label_set_text(s_feedback, "");
        s_feedback_until = 0;
    }
    if (s_sleep_button_label != NULL) {
        lv_label_set_text(s_sleep_button_label, pet->sleeping ? "叫醒" : "休息");
        if(polished)lw_skin_select(s_sleep_button,pet->sleeping);
    }
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh();
}

static void do_action(lw_pet_action_t action, const char *message)
{
    lw_state_t *state = lw_app_state();
    uint32_t now = lw_now();
    if (lw_pet_interact(&state->pet, action, now)) {
        lw_app_state_save();
    }
    if (s_view != NULL) {
        lw_pet_view_react(s_view, action);
    }
    if (s_feedback != NULL) {
        lv_label_set_text(s_feedback, message);
    }
    s_feedback_until = now + 4;
    if(polished)lw_os_scene_focus(true);
    if(polished && action==LW_PET_ACTION_PLAY && !state->pet.sleeping)empty_until=room_clock+2000;
    if (polished && !state->pet.sleeping && action!=LW_PET_ACTION_SLEEP && action!=LW_PET_ACTION_WAKE) reaction_until=room_clock+LW_PET_WAVE_MS;
    refresh();
}

static void tap_pet(lv_event_t *event)
{
    (void)event;
    do_action(LW_PET_ACTION_TAP, "它轻轻地动了一下。");
}
static void greet(lv_event_t *event)
{
    (void)event;
    do_action(LW_PET_ACTION_TAP, "嗨，今天也很高兴见到你。");
}

static void feed(lv_event_t *event)
{
    (void)event;
    do_action(LW_PET_ACTION_FEED, "吃得很香，饱腹感回来了。");
}

static void play(lv_event_t *event)
{
    (void)event;
    lw_state_t *state = lw_app_state();
    if (state->pet.energy < 15) {
        do_action(LW_PET_ACTION_PLAY, "它有点累了，先让它休息一下吧。");
        return;
    }
    do_action(LW_PET_ACTION_PLAY, "一起玩了一会儿，心情变好了。");
}

static void pet(lv_event_t *event)
{
    (void)event;
    do_action(LW_PET_ACTION_PET, "被摸头了，眼睛都弯起来了。");
}

static void sleep_toggle(lv_event_t *event)
{
    (void)event;
    lw_state_t *state = lw_app_state();
    if (state->pet.sleeping) {
        do_action(LW_PET_ACTION_WAKE, "它醒了，伸了个懒腰。");
    } else {
        do_action(LW_PET_ACTION_SLEEP, "睡着了，呼吸慢慢变轻。");
    }
}

static lv_obj_t *stat_row(lv_obj_t *parent, const char *name, int y, uint32_t color, lv_obj_t **bar_out)
{
    const lw_palette_t *palette = lw_palette();
    lw_text(parent, name, &lw_font_18, palette->dim, 28, y - 8);
    lv_obj_t *bar = lw_bar(parent, 110, y, 270, 12, color);
    *bar_out = bar;
    return bar;
}
static void create_room(lv_obj_t *content)
{
    const lw_palette_t *c=lw_palette(); polished=true; room_clock=0; reaction_until=0;empty_until=0; room_last=lv_tick_get();
    lv_obj_t *stage=lw_os_box(content,0,0,1024,548,c->card_soft,0,255);
    lv_obj_t *bg=lw_visual_daily_image(stage); if (bg) lv_obj_set_y(bg,-52);
    else lw_os_label(stage,"房间资源未安装 · 日常互动仍可用",&lw_font_18,32,100,650,c->dim);
    lw_ambience_layer(stage,1,-52);
    /* State is conveyed by the residents, not a permanent title/mood dashboard. */
    const lw_clip_t *clip=lw_polish_clip("v42.couple.seated");
    if (clip) { couple=lw_sprite_create(stage,516,316,282);if (couple) {lw_sprite_set_external_clock(couple,true);lw_sprite_set_pixel_mode(couple);lw_sprite_play(couple,clip);couple_clip=clip;} }
    if (!couple) lw_os_label(stage,"互动角色资源待载入",&lw_font_18,360,150,500,c->dim);
    joint_bed=lw_polish_clip("v42.dogbed.look")!=NULL;
    if(!joint_bed)lw_os_box(stage,889,373,56,10,0x73534b,LV_RADIUS_CIRCLE,45);
    const lw_clip_t *pup=lw_polish_clip(joint_bed ? "v42.dogbed.look" : "v42.dog.idle"); if (pup) {dog=lw_sprite_create(stage,joint_bed ? 906 : 916,joint_bed ? 412 : 376,256);if (dog) {lw_sprite_set_external_clock(dog,true);lw_sprite_set_pixel_mode(dog);lw_sprite_play(dog,pup);dog_clip=pup;if(!joint_bed){lv_obj_set_style_image_recolor(lw_sprite_object(dog),lv_color_hex(0xd8b99c),0);lv_obj_set_style_image_recolor_opa(lw_sprite_object(dog),65,0);}else if(lw_ambience_phase()>=LW_NIGHT){lv_obj_set_style_image_recolor(lw_sprite_object(dog),lv_color_hex(0x937a8c),0);lv_obj_set_style_image_recolor_opa(lw_sprite_object(dog),35,0);}}}
    if(couple && lw_ambience_phase()>=LW_NIGHT){lv_obj_set_style_image_recolor(lw_sprite_object(couple),lv_color_hex(0x797092),0);lv_obj_set_style_image_recolor_opa(lw_sprite_object(couple),35,0);}
    if (!dog) s_view=lw_pet_view_create(stage,888,330,30);
    lv_obj_t *hello=lw_button(stage,"打招呼",88,44,0xf2dfc4,0x775956,&lw_font_18);
    lv_obj_set_pos(hello,310,190);lw_skin_button(hello,SK_ACTION,SK_SELECTED,SK_PRESSED); lv_obj_add_event_cb(hello,greet,LV_EVENT_CLICKED,NULL);
    lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(stage,tap_pet,LV_EVENT_CLICKED,NULL);
    lv_obj_t *stats=lw_os_box(stage,868,14,130,120,0xf5e4c8,6,210);if(lw_skin_fit(stats,SK_NOTE,0,0,130,120))lv_obj_set_style_bg_opa(stats,0,0);
    const char *names[]={"饱腹","精力","好感"}; uint32_t colors[]={c->gold,c->mint,c->accent}; lv_obj_t **bars[]={&s_fullness_bar,&s_energy_bar,&s_affection_bar};
    for (unsigned i=0;i<3;i++) {lw_os_icon(stats,i==0 ? OS_I_LEAF : i==1 ? OS_I_SUN : OS_I_HEART,12,39+(int)i*29,14,0xa47770);lw_text(stats,names[i],&lw_font_18,0x806953,36,35+(int)i*29); *bars[i]=lw_bar(stats,78,46+(int)i*29,40,4,colors[i]); }
    lv_obj_t *note=lw_os_box(stage,198,450,630,34,0xf5e4c8,6,0);
    s_feedback=lw_text_wrap(note,"",&lw_font_18,0x75595c,14,4,602);
    const char *labels[]={"喂食","玩耍","抚摸","休息"}; lv_event_cb_t callbacks[]={feed,play,pet,sleep_toggle};
    for (unsigned i=0;i<4;i++) { lv_obj_t *b=lw_button(stage,labels[i],112,48,0xf2dfc4,0x775956,&lw_font_22);lv_obj_set_pos(b,248+(int)i*128,378);lv_obj_t *label=lv_obj_get_child(b,0);lw_skin_button(b,SK_ACTION,SK_SELECTED,SK_PRESSED);lv_obj_add_event_cb(b,callbacks[i],LV_EVENT_CLICKED,NULL);if(i==3){s_sleep_button=b;s_sleep_button_label=label;} }
    refresh(); s_timer=lv_timer_create(timer_cb,40,NULL);
}

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
    if (lw_shell_host_active()) { create_room(content); return; }
    const lw_palette_t *palette = lw_palette();

    lv_obj_t *stage = lw_card(content, 32, 14, 520, 412);
    lv_obj_set_style_bg_color(stage, lv_color_hex(palette->card_soft), 0);
    s_view = lw_pet_view_create(stage, 128, 76, 120);
    if (s_view != NULL) {
        lv_obj_add_flag(lv_obj_get_child(stage, 0), LV_OBJ_FLAG_CLICKABLE);
    }

    /* make the whole stage tappable for the immediate "tap" feedback */
    lv_obj_add_flag(stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stage, tap_pet, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hint = lw_text(content, "点一点它试试", &lw_font_18, palette->dim, 32 + 20, 14 + 372);

    lv_obj_t *panel = lw_card(content, 572, 14, 420, 412);
    lw_text(panel, "小世界居民", &lw_font_28, palette->text, 28, 24);

    s_mood_label = lw_pill(panel, "安静", palette->card_soft, palette->accent_deep);
    lv_obj_set_pos(s_mood_label, 28, 68);

    stat_row(panel, "饱腹", 120, palette->gold, &s_fullness_bar);
    stat_row(panel, "精力", 168, palette->mint, &s_energy_bar);
    stat_row(panel, "好感", 216, palette->accent, &s_affection_bar);

    s_feedback = lw_text(panel, "", &lw_font_18, palette->accent_deep, 28, 262);

    lv_obj_t *feed_btn = lw_button(panel, "喂食", 150, 52, palette->gold, 0xFFFFFF, &lw_font_22);
    lv_obj_set_pos(feed_btn, 28, 300);
    lv_obj_add_event_cb(feed_btn, feed, LV_EVENT_CLICKED, NULL);

    lv_obj_t *play_btn = lw_button(panel, "玩耍", 150, 52, palette->mint, 0xFFFFFF, &lw_font_22);
    lv_obj_set_pos(play_btn, 194, 300);
    lv_obj_add_event_cb(play_btn, play, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pet_btn = lw_button(panel, "抚摸", 150, 52, palette->accent, 0xFFFFFF, &lw_font_22);
    lv_obj_set_pos(pet_btn, 28, 360);
    lv_obj_add_event_cb(pet_btn, pet, LV_EVENT_CLICKED, NULL);

    s_sleep_button = lw_button(panel, "休息", 150, 52, palette->lav, 0xFFFFFF, &lw_font_22);
    lv_obj_set_pos(s_sleep_button, 194, 360);
    lv_obj_add_event_cb(s_sleep_button, sleep_toggle, LV_EVENT_CLICKED, NULL);
    s_sleep_button_label = lv_obj_get_child(s_sleep_button, 0);

    (void)hint;
    refresh();
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
}

static void destroy(void)
{
    polished=false; couple=NULL; couple_clip=NULL;dog=NULL;dog_clip=NULL;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_view != NULL) {
        lw_pet_view_free(s_view);
        s_view = NULL;
    }
    s_mood_label = NULL;
    s_fullness_bar = NULL;
    s_energy_bar = NULL;
    s_affection_bar = NULL;
    s_feedback = NULL;
    s_sleep_button_label = NULL;
    s_sleep_button = NULL;
}

const lw_app_t lw_app_pet_def = {
    .id = "pet",
    .name = "宠物",
    .create = create,
    .destroy = destroy,
};
