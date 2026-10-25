/* Little World - settings and diagnostics.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "lw_app_state.h"
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"
#include "lw/lw_pet.h"

static lv_obj_t *s_sound_toggle;
static lv_obj_t *s_anim_toggle;
static lv_obj_t *s_diag_label;
static lv_timer_t *s_timer;

static int count_objects(lv_obj_t *obj)
{
    int count = 1;
    uint32_t children = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < children; i++) {
        count += count_objects(lv_obj_get_child(obj, i));
    }
    return count;
}

static void sound_changed(lv_event_t *event)
{
    (void)event;
    lw_state_t *state = lw_app_state();
    state->settings.sound = (uint8_t)lw_toggle_get(s_sound_toggle);
    lw_app_state_save();
}

static void anim_changed(lv_event_t *event)
{
    (void)event;
    lw_state_t *state = lw_app_state();
    state->settings.animations = (uint8_t)lw_toggle_get(s_anim_toggle);
    lw_app_state_save();
}

static void sound_row_clicked(lv_event_t *event)
{
    (void)event;
    lw_toggle_set(s_sound_toggle, !lw_toggle_get(s_sound_toggle));
    sound_changed(NULL);
}

static void anim_row_clicked(lv_event_t *event)
{
    (void)event;
    lw_toggle_set(s_anim_toggle, !lw_toggle_get(s_anim_toggle));
    anim_changed(NULL);
}

static void theme_clicked(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    lw_state_t *state = lw_app_state();
    state->settings.theme = (uint8_t)index;
    lw_app_state_save();
    lw_theme_set_variant(index);
    lw_shell_refresh_theme();
}

static void reset_clicked(lv_event_t *event)
{
    (void)event;
    lw_app_state_reset();
    lw_theme_set_variant(0);
    lw_shell_refresh_theme();
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_diag_label == NULL) {
        return;
    }
    lw_state_t *state = lw_app_state();
    lv_label_set_text_fmt(s_diag_label,
                          "活动对象：%d\n宠物互动：%u 次\n时钟：%s\n版本：%s",
                          count_objects(lv_screen_active()), (unsigned)state->pet.interactions,
                          lw_time_is_demo() ? "DEMO（未接实时时钟）" : "系统时间", LW_VERSION);
}

static lv_obj_t *row_label(lv_obj_t *parent, const char *text, int y)
{
    return lw_text(parent, text, &lw_font_22, lw_palette()->text, 28, y);
}

static void create(lv_obj_t *content, const struct lw_app *self)
{
    (void)self;
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();

    lv_obj_t *left = lw_card(content, 32, 14, 470, 412);
    row_label(left, "外观与反馈", 26);

    lw_text(left, "音效", &lw_font_18, palette->dim, 28, 84);
    s_sound_toggle = lw_toggle(left, state->settings.sound, 380, 76);
    lv_obj_add_event_cb(s_sound_toggle, sound_changed, LV_EVENT_CLICKED, NULL);

    lw_text(left, "页面动画", &lw_font_18, palette->dim, 28, 136);
    s_anim_toggle = lw_toggle(left, state->settings.animations, 380, 128);
    lv_obj_add_event_cb(s_anim_toggle, anim_changed, LV_EVENT_CLICKED, NULL);

    /* the whole row is a hit area, so tapping the label also flips the switch */
    lv_obj_t *sound_row = lv_obj_create(left);
    lv_obj_remove_style_all(sound_row);
    lv_obj_set_pos(sound_row, 24, 70);
    lv_obj_set_size(sound_row, 420, 52);
    lv_obj_add_flag(sound_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sound_row, sound_row_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *anim_row = lv_obj_create(left);
    lv_obj_remove_style_all(anim_row);
    lv_obj_set_pos(anim_row, 24, 122);
    lv_obj_set_size(anim_row, 420, 52);
    lv_obj_add_flag(anim_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(anim_row, anim_row_clicked, LV_EVENT_CLICKED, NULL);

    lw_text(left, "主题", &lw_font_18, palette->dim, 28, 192);
    static const uint32_t swatch[3] = {0xF58FAE, 0xE06A90, 0xB79CED};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lw_button(left, "", 52, 52, swatch[i], 0xFFFFFF, &lw_font_18);
        lv_obj_set_pos(btn, 120 + i * 72, 180);
        lv_obj_add_event_cb(btn, theme_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (state->settings.theme == i) {
            lv_obj_set_style_border_width(btn, 3, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(palette->text), 0);
        }
    }
    lw_text(left, "奶油 / 玫瑰 / 薰衣草", &lw_font_18, palette->dim, 120, 240);

    lw_text(left, "音效开关只保存偏好；本版本还没有接入音频输出。", &lw_font_18, palette->dim, 28, 286);
    lw_text(left, "DEMO 版本号 " LW_VERSION, &lw_font_18, palette->dim, 28, 316);

    lv_obj_t *right = lw_card(content, 522, 14, 470, 412);
    row_label(right, "关于 Little World", 26);
    lw_text_wrap(right,
                 "一个会慢慢长大的小世界：桌面、宠物、回忆都在这里。\n"
                 "当前是可以在 Windows 上体验的演示版本。",
                 &lw_font_18, palette->dim, 28, 70, 400);

    s_diag_label = lw_text(right, "", &lw_font_18, palette->text, 28, 150);
    lv_obj_set_style_text_line_space(s_diag_label, 8, 0);

    lv_obj_t *reset_target = lw_button(right, "清空存档并重新开始", 280, 50, palette->accent, 0xFFFFFF,
                                       &lw_font_18);
    lv_obj_set_pos(reset_target, 28, 330);
    lv_obj_add_event_cb(reset_target, reset_clicked, LV_EVENT_CLICKED, NULL);

    timer_cb(NULL);
    s_timer = lv_timer_create(timer_cb, 2000, NULL);
}

static void destroy(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_sound_toggle = NULL;
    s_anim_toggle = NULL;
    s_diag_label = NULL;
}

const lw_app_t lw_app_settings_def = {
    .id = "settings",
    .name = "设置",
    .create = create,
    .destroy = destroy,
};
