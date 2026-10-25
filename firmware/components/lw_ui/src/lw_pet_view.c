/* SPDX-License-Identifier: MIT */
#include "lw_pet_view.h"

#include <stdlib.h>

#include "lw_app_state.h"
#include "lw_theme.h"
#include "lw_os_design.h"

struct lw_pet_view {
    lv_obj_t *root;
    lv_obj_t *ear_left;
    lv_obj_t *ear_right;
    lv_obj_t *body;
    lv_obj_t *eye_left;
    lv_obj_t *eye_right;
    lv_obj_t *blush_left;
    lv_obj_t *blush_right;
    lv_obj_t *mouth;
    lv_obj_t *zzz;
    lv_timer_t *blink_timer;
    lv_timer_t *blink_restore_timer;
    lv_anim_t breath;
    lw_pet_mood_t mood;
    int scale;
};

static int s(int value, int scale)
{
    return value * scale / 100;
}

static lv_obj_t *part(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

/* --------------------------------------------------------------- animation */

static void breath_cb(void *var, int32_t value)
{
    lv_obj_t *body = (lv_obj_t *)var;
    lw_pet_view_t *view = (lw_pet_view_t *)lv_obj_get_user_data(body);
    if (view == NULL) {
        return;
    }
    lv_obj_set_height(body, value);
    lv_obj_set_y(body, s(40, view->scale) - (value - s(170, view->scale)) / 2);
}

static void wiggle_cb(void *var, int32_t value)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, value, 0);
}

/* ---------------------------------------------------------------- blinking */

static void blink_restore_cb(lv_timer_t *timer)
{
    lw_pet_view_t *view = (lw_pet_view_t *)lv_timer_get_user_data(timer);
    if (view == NULL) {
        return;
    }
    lv_obj_set_height(view->eye_left, s(18, view->scale));
    lv_obj_set_height(view->eye_right, s(18, view->scale));
}

static void blink_cb(lv_timer_t *timer)
{
    lw_pet_view_t *view = (lw_pet_view_t *)lv_timer_get_user_data(timer);
    if (view == NULL || view->mood == LW_PET_MOOD_SLEEPING || lw_os_motion_get()!=LW_MOTION_FULL) {
        return;
    }
    lv_obj_set_height(view->eye_left, s(5, view->scale));
    lv_obj_set_height(view->eye_right, s(5, view->scale));
    if (view->blink_restore_timer != NULL) {
        lv_timer_reset(view->blink_restore_timer);
        lv_timer_resume(view->blink_restore_timer);
    }
}

/* -------------------------------------------------------------- public API */

lw_pet_view_t *lw_pet_view_create(lv_obj_t *parent, int x, int y, int scale)
{
    if (scale < 40) {
        scale = 100;
    }
    lw_pet_view_t *view = (lw_pet_view_t *)calloc(1, sizeof(*view));
    if (view == NULL) {
        return NULL;
    }
    view->scale = scale;
    view->mood = LW_PET_MOOD_CALM;

    view->root = lv_obj_create(parent);
    lv_obj_remove_style_all(view->root);
    lv_obj_set_pos(view->root, x, y);
    lv_obj_set_size(view->root, s(220, scale), s(230, scale));
    lv_obj_remove_flag(view->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(view->root, LV_OBJ_FLAG_CLICKABLE);

    uint32_t body_color = 0xFFF3E9;
    uint32_t ear_color = 0xFFE3EC;
    uint32_t line_color = 0x4A3B45;

    view->ear_left = part(view->root, s(30, scale), s(22, scale), s(44, scale), s(44, scale), s(22, scale), ear_color,
                          LV_OPA_COVER);
    view->ear_right = part(view->root, s(146, scale), s(22, scale), s(44, scale), s(44, scale), s(22, scale), ear_color,
                           LV_OPA_COVER);
    view->body = part(view->root, s(15, scale), s(40, scale), s(190, scale), s(170, scale), s(85, scale), body_color,
                      LV_OPA_COVER);
    lv_obj_set_user_data(view->body, view);

    view->eye_left = part(view->body, s(53, scale), s(63, scale), s(14, scale), s(18, scale), s(7, scale), line_color,
                          LV_OPA_COVER);
    view->eye_right = part(view->body, s(123, scale), s(63, scale), s(14, scale), s(18, scale), s(7, scale), line_color,
                           LV_OPA_COVER);
    view->blush_left = part(view->body, s(28, scale), s(96, scale), s(30, scale), s(14, scale), s(7, scale), 0xFF9DB6,
                            LV_OPA_50);
    view->blush_right = part(view->body, s(132, scale), s(96, scale), s(30, scale), s(14, scale), s(7, scale), 0xFF9DB6,
                             LV_OPA_50);
    view->mouth = part(view->body, s(85, scale), s(100, scale), s(20, scale), s(6, scale), s(3, scale), line_color,
                       LV_OPA_COVER);

    view->zzz = lv_label_create(view->root);
    lv_obj_set_style_text_font(view->zzz, &lw_font_22, 0);
    lv_obj_set_style_text_color(view->zzz, lv_color_hex(lw_palette()->lav_deep), 0);
    lv_label_set_text(view->zzz, "z z");
    lv_obj_set_pos(view->zzz, s(178, scale), s(10, scale));
    lv_obj_add_flag(view->zzz, LV_OBJ_FLAG_HIDDEN);

    /* breathing */
    lv_anim_init(&view->breath);
    lv_anim_set_var(&view->breath, view->body);
    lv_anim_set_values(&view->breath, s(170, scale), s(176, scale));
    lv_anim_set_duration(&view->breath, 1400);
    lv_anim_set_playback_duration(&view->breath, 1400);
    lv_anim_set_repeat_count(&view->breath, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&view->breath, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&view->breath, breath_cb);
    if (lw_os_motion_get()==LW_MOTION_FULL) lv_anim_start(&view->breath);

    view->blink_timer = lv_timer_create(blink_cb, 3600, view);
    view->blink_restore_timer = lv_timer_create(blink_restore_cb, 130, view);
    lv_timer_pause(view->blink_restore_timer);
    return view;
}

void lw_pet_view_free(lw_pet_view_t *view)
{
    if (view == NULL) {
        return;
    }
    if (view->blink_timer != NULL) {
        lv_timer_delete(view->blink_timer);
    }
    if (view->blink_restore_timer != NULL) {
        lv_timer_delete(view->blink_restore_timer);
    }
    lv_anim_delete(view->body, breath_cb);
    free(view);
}

void lw_pet_view_refresh(lw_pet_view_t *view, const lw_pet_t *pet, uint32_t now)
{
    if (view == NULL || pet == NULL) {
        return;
    }
    lw_pet_mood_t mood = lw_pet_mood(pet, now);
    if (lw_os_motion_get()!=LW_MOTION_FULL) { lv_anim_delete(view->body,breath_cb); lv_obj_set_height(view->body,s(170,view->scale)); }
    if (mood == view->mood) {
        return;
    }
    view->mood = mood;

    /* reset to the calm pose, then specialise */
    lv_obj_set_height(view->eye_left, s(18, view->scale));
    lv_obj_set_height(view->eye_right, s(18, view->scale));
    lv_obj_set_width(view->mouth, s(20, view->scale));
    lv_obj_set_style_bg_opa(view->blush_left, LV_OPA_50, 0);
    lv_obj_set_style_bg_opa(view->blush_right, LV_OPA_50, 0);
    lv_obj_set_style_translate_x(view->root, 0, 0);
    lv_obj_add_flag(view->zzz, LV_OBJ_FLAG_HIDDEN);
    lv_anim_set_duration(&view->breath, 1400);
    lv_anim_set_playback_duration(&view->breath, 1400);

    switch (mood) {
    case LW_PET_MOOD_HAPPY:
        lv_obj_set_height(view->eye_left, s(8, view->scale));
        lv_obj_set_height(view->eye_right, s(8, view->scale));
        lv_obj_set_style_bg_opa(view->blush_left, LV_OPA_80, 0);
        lv_obj_set_style_bg_opa(view->blush_right, LV_OPA_80, 0);
        lv_obj_set_width(view->mouth, s(28, view->scale));
        break;
    case LW_PET_MOOD_SLEEPY:
        lv_obj_set_height(view->eye_left, s(10, view->scale));
        lv_obj_set_height(view->eye_right, s(10, view->scale));
        break;
    case LW_PET_MOOD_SLEEPING:
        lv_obj_set_height(view->eye_left, s(4, view->scale));
        lv_obj_set_height(view->eye_right, s(4, view->scale));
        lv_obj_set_width(view->mouth, s(14, view->scale));
        lv_obj_remove_flag(view->zzz, LV_OBJ_FLAG_HIDDEN);
        lv_anim_set_duration(&view->breath, 3200);
        lv_anim_set_playback_duration(&view->breath, 3200);
        break;
    case LW_PET_MOOD_CURIOUS:
        lv_obj_set_height(view->eye_left, s(22, view->scale));
        lv_obj_set_height(view->eye_right, s(22, view->scale));
        lv_obj_set_style_translate_x(view->root, s(4, view->scale), 0);
        break;
    case LW_PET_MOOD_PLAYING:
        lv_obj_set_height(view->eye_left, s(8, view->scale));
        lv_obj_set_height(view->eye_right, s(8, view->scale));
        lv_anim_set_duration(&view->breath, 700);
        lv_anim_set_playback_duration(&view->breath, 700);
        break;
    case LW_PET_MOOD_CALM:
    default:
        break;
    }

    /* restart the breathing animation so a duration change takes effect */
    lv_anim_delete(view->body, breath_cb);
    lv_anim_set_values(&view->breath, s(170, view->scale), s(176, view->scale));
    lv_anim_set_var(&view->breath, view->body);
    lv_anim_set_exec_cb(&view->breath, breath_cb);
    if (lw_os_motion_get()==LW_MOTION_FULL) lv_anim_start(&view->breath);
}

void lw_pet_view_react(lw_pet_view_t *view, lw_pet_action_t action)
{
    if (view == NULL || lw_os_motion_get()!=LW_MOTION_FULL) {
        return;
    }
    lv_anim_t bounce;
    lv_anim_init(&bounce);
    lv_anim_set_var(&bounce, view->root);
    lv_anim_set_values(&bounce, 0, -s(10, view->scale));
    lv_anim_set_duration(&bounce, 160);
    lv_anim_set_playback_duration(&bounce, 220);
    lv_anim_set_path_cb(&bounce, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&bounce, wiggle_cb);
    lv_anim_start(&bounce);

    if (action == LW_PET_ACTION_PLAY) {
        lv_anim_t wiggle;
        lv_anim_init(&wiggle);
        lv_anim_set_var(&wiggle, view->root);
        lv_anim_set_values(&wiggle, -s(6, view->scale), s(6, view->scale));
        lv_anim_set_duration(&wiggle, 180);
        lv_anim_set_playback_duration(&wiggle, 180);
        lv_anim_set_repeat_count(&wiggle, 4);
        lv_anim_set_exec_cb(&wiggle, wiggle_cb);
        lv_anim_start(&wiggle);
    }
}
