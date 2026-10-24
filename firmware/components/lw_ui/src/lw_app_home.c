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

static void refresh(void)
{
    lw_state_t *state = lw_app_state();
    uint32_t now = lw_now();
    lw_pet_t *pet = &state->pet;

    if (s_view != NULL) {
        lw_pet_view_refresh(s_view, pet, now);
    }
    if (s_mood_label != NULL) {
        char mood[32];
        snprintf(mood, sizeof(mood), "%s", lw_pet_mood_name(lw_pet_mood(pet, now)));
        lv_label_set_text(s_mood_label, mood);
    }
    if (s_days_label != NULL) {
        lw_date_t today = lw_today();
        int days = lw_days_between(state->anniversary, today) + 1;
        if (days < 1) {
            days = 1;
        }
        lv_label_set_text_fmt(s_days_label, "%d", days);
        if (s_days_unit != NULL) {
            lv_obj_align_to(s_days_unit, s_days_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -10);
        }
    }
    if (s_next_label != NULL) {
        lw_date_t today = lw_today();
        int days = lw_days_until_anniversary(state->anniversary, today);
        if (days == 0) {
            lv_label_set_text(s_next_label, "今天就是纪念日");
        } else {
            lv_label_set_text_fmt(s_next_label, "距离下一个纪念日还有 %d 天", days);
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
}

const lw_app_t lw_app_home_def = {
    .id = "home",
    .name = "首页",
    .create = create,
    .destroy = destroy,
};
