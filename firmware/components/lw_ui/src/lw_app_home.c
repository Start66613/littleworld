/* Little World - home screen: greeting, the pet, the anniversary card.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lw_app_state.h"
#include "lw_apps.h"
#include "lw_pet_view.h"
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"
#include "lw/lw_memories.h"
#include "lw/lw_time.h"

static lw_pet_view_t *s_view;
static lv_timer_t *s_timer;
static lv_obj_t *s_mood_label;
static lv_obj_t *s_days_label;
static lv_obj_t *s_days_unit;
static lv_obj_t *s_next_label;
static lv_obj_t *s_tip_label;

/* One short tip per day, rotated by calendar date in refresh() below.
 * Kept short on purpose: the label sits in the pet card's right column
 * (200 px wide) under the existing hint and above the button. */
static const char *const s_daily_tips[] = {
    "先伸个懒腰再开始",
    "好好吃饭，慢慢走路",
    "晒会儿太阳补补钙",
    "抬头看看窗外吧",
    "陪小家伙玩一会儿",
    "晚饭后一起散散步",
    "早点休息，做个好梦",
    "普通日子也值得收好",
};

/* Implemented by the memories page so the home buttons can land on a tab. */
void lw_memories_open_tab(int tab);

static void open_pet(lv_event_t *event)
{
    (void)event;
    lw_shell_open("pet");
}

static void open_memories(lv_event_t *event)
{
    (void)event;
    lw_memories_open_tab(0);
    lw_shell_open("memories");
}

static void open_surprise(lv_event_t *event)
{
    (void)event;
    lw_memories_open_tab(2);
    lw_shell_open("memories");
}

static const char *greeting_for_hour(int hour)
{
    if (hour < 6) {
        return "夜深了";
    }
    if (hour < 11) {
        return "早上好";
    }
    if (hour < 14) {
        return "中午好";
    }
    if (hour < 18) {
        return "下午好";
    }
    return "晚上好";
}

/* Last values refresh() actually wrote. The 1 s timer re-runs refresh(), but
 * the day counts change once a day and the mood only with the pet's state,
 * while every lv_label_set_text() re-allocates, re-lays out and invalidates.
 * Guard on the inputs - the pattern lw_app_pet.c already uses - and reset in
 * destroy(), because create() builds fresh labels. */
static int s_mood_shown = -1;
static int s_days_shown = -1;
static int s_next_shown = -1;
static int s_tip_shown = -1;

static void refresh(void)
{
    lw_state_t *state = lw_app_state();
    uint32_t now = lw_now();
    lw_pet_t *pet = &state->pet;

    if (s_view != NULL) {
        lw_pet_view_refresh(s_view, pet, now);
    }
    if (s_mood_label != NULL) {
        lw_pet_mood_t mood = lw_pet_mood(pet, now);
        if ((int)mood != s_mood_shown) {
            s_mood_shown = (int)mood;
            lv_label_set_text(s_mood_label, lw_pet_mood_name(mood));
        }
    }
    if (s_days_label != NULL) {
        lw_date_t today = lw_today();
        int days = lw_days_between(state->anniversary, today) + 1;
        if (days < 1) {
            days = 1;
        }
        if (days != s_days_shown) {
            s_days_shown = days;
            lv_label_set_text_fmt(s_days_label, "%d", days);
            if (s_days_unit != NULL) {
                lv_obj_align_to(s_days_unit, s_days_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
            }
        }
    }
    if (s_next_label != NULL) {
        lw_date_t today = lw_today();
        int days = lw_days_until_anniversary(state->anniversary, today);
        if (days != s_next_shown) {
            s_next_shown = days;
            if (days == 0) {
                lv_label_set_text(s_next_label, "今天就是纪念日");
            } else {
                lv_label_set_text_fmt(s_next_label, "距离下一个纪念日还有 %d 天", days);
            }
        }
    }
    if (s_tip_label != NULL) {
        /* Date modulo picks the day's tip; no new timer - this rides the
         * existing 1 s refresh, and the guard means the label is rewritten
         * only when the date (and therefore the tip) actually rolls over. */
        lw_date_t today = lw_today();
        int count = (int)(sizeof(s_daily_tips) / sizeof(s_daily_tips[0]));
        int index = lw_days_from_civil(today.year, today.month, today.day) % count;
        if (index < 0) {
            index += count;
        }
        if (index != s_tip_shown) {
            s_tip_shown = index;
            lv_label_set_text_fmt(s_tip_label, "今日小提示：%s", s_daily_tips[index]);
        }
    }
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh();
}

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();
    uint32_t now = lw_now();

    /* left: the pet lives on the home screen */
    lv_obj_t *pet_card = lw_card(content, 32, 14, 520, 412);
    lv_obj_set_style_bg_color(pet_card, lv_color_hex(0xFFFFFF), 0);

    s_view = lw_pet_view_create(pet_card, 34, 62, 100);

    lv_obj_t *greeting = lw_text(pet_card, "", &lw_font_28, palette->text, 288, 46);
    lv_label_set_text(greeting, greeting_for_hour((int)((now % 86400) / 3600)));

    lv_obj_t *welcome = lw_text(pet_card, "它今天也在这里等你。", &lw_font_18, palette->dim, 288, 86);

    lv_obj_t *mood_caption = lw_text(pet_card, "现在的心情", &lw_font_18, palette->dim, 288, 130);
    (void)mood_caption;
    s_mood_label = lw_pill(pet_card, "", palette->card_soft, palette->accent_deep);
    lv_obj_set_pos(s_mood_label, 288, 158);
    lv_obj_set_style_pad_hor(s_mood_label, 16, 0);
    lv_obj_set_style_pad_ver(s_mood_label, 7, 0);

    lv_obj_t *tip = lw_text_wrap(pet_card, "点一点它，它会回应你。", &lw_font_18, palette->dim, 288, 210, 200);
    (void)tip;

    s_tip_label = lw_text_wrap(pet_card, "", &lw_font_18, palette->dim, 288, 244, 200);

    lv_obj_t *pet_btn = lw_button(pet_card, "去陪陪它", 190, 54, palette->accent, 0xFFFFFF, &lw_font_22);
    lv_obj_set_pos(pet_btn, 288, 300);
    lv_obj_add_event_cb(pet_btn, open_pet, LV_EVENT_CLICKED, NULL);

    /* right top: anniversary */
    lv_obj_t *anniv = lw_card(content, 572, 14, 420, 196);
    lw_text(anniv, "在一起", &lw_font_18, palette->dim, 28, 24);
    s_days_label = lw_text(anniv, "--", &lw_font_44, palette->accent_deep, 28, 52);
    s_days_unit = lw_text(anniv, "天", &lw_font_28, palette->accent_deep, 0, 0);
    lv_obj_align_to(s_days_unit, s_days_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
    s_next_label = lw_text(anniv, "", &lw_font_18, palette->dim, 28, 120);

    lv_obj_t *mem_btn = lw_button(anniv, "回忆世界", 150, 46, palette->lav, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(mem_btn, 244, 128);
    lv_obj_add_event_cb(mem_btn, open_memories, LV_EVENT_CLICKED, NULL);

    /* right bottom: surprise */
    lv_obj_t *surprise = lw_card(content, 572, 226, 420, 200);
    lv_obj_set_style_bg_color(surprise, lv_color_hex(palette->card_soft), 0);

    lv_obj_t *gift = lw_icon_badge(surprise, 28, 30, 64, palette->accent, 20);
    lv_obj_t *ribbon_v = lv_obj_create(gift);
    lv_obj_remove_style_all(ribbon_v);
    lv_obj_set_size(ribbon_v, 6, 40);
    lv_obj_set_pos(ribbon_v, 29, 12);
    lv_obj_set_style_bg_color(ribbon_v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ribbon_v, LV_OPA_80, 0);
    lv_obj_remove_flag(ribbon_v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ribbon_h = lv_obj_create(gift);
    lv_obj_remove_style_all(ribbon_h);
    lv_obj_set_size(ribbon_h, 40, 6);
    lv_obj_set_pos(ribbon_h, 12, 29);
    lv_obj_set_style_bg_color(ribbon_h, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ribbon_h, LV_OPA_80, 0);
    lv_obj_remove_flag(ribbon_h, LV_OBJ_FLAG_CLICKABLE);

    lw_text(surprise, "今天的惊喜", &lw_font_28, palette->text, 112, 30);
    lw_text(surprise, state->surprise_unlocked ? "已经打开过了，可以再看一次。" : "盒子还没有打开。",
            &lw_font_18, palette->dim, 112, 74);

    lv_obj_t *gift_btn = lw_button(surprise, "打开看看", 150, 46, palette->accent_deep, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(gift_btn, 112, 122);
    lv_obj_add_event_cb(gift_btn, open_surprise, LV_EVENT_CLICKED, NULL);

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
    s_days_label = NULL;
    s_next_label = NULL;
    s_tip_label = NULL;
    s_mood_shown = -1;
    s_days_shown = -1;
    s_next_shown = -1;
    s_tip_shown = -1;
}

const lw_app_t lw_app_home_def = {
    .id = "home",
    .name = "首页",
    .create = create,
    .destroy = destroy,
};
