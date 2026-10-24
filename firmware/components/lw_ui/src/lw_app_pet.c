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

static void refresh(void)
{
    lw_state_t *state = lw_app_state();
    uint32_t now = lw_now();
    lw_pet_t *pet = &state->pet;
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
    refresh();
}

static void tap_pet(lv_event_t *event)
{
    (void)event;
    do_action(LW_PET_ACTION_TAP, "它轻轻地动了一下。");
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

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
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
