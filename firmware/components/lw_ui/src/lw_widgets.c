/* SPDX-License-Identifier: MIT */
#include "lw_widgets.h"

#include <string.h>

#include "lw_theme.h"
#include "lw_os_design.h"

lv_obj_t *lw_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lw_style_card(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *lw_soft_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lw_style_soft_card(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *lw_text(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lw_style_label(label, font, color);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    return label;
}

lv_obj_t *lw_text_wrap(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y,
                       int width)
{
    lv_obj_t *label = lw_text(parent, text, font, color, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(label, 6, 0);
    return label;
}

lv_obj_t *lw_button(lv_obj_t *parent, const char *text, int w, int h, uint32_t bg, uint32_t fg,
                    const lv_font_t *font)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lw_os_motion_touch(btn);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 12, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(lw_palette()->shadow), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_offset_y(btn, 4, 0);
    lv_obj_set_style_translate_y(btn, 2, LV_STATE_PRESSED);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(btn);
    lw_style_label(label, font != NULL ? font : &lw_font_22, fg);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *lw_pill(lv_obj_t *parent, const char *text, uint32_t bg, uint32_t fg)
{
    lv_obj_t *label = lv_label_create(parent);
    lw_style_label(label, &lw_font_18, fg);
    lv_label_set_text(label, text);
    lv_obj_set_style_bg_color(label, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(label, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(label, 12, 0);
    lv_obj_set_style_pad_ver(label, 5, 0);
    return label;
}

lv_obj_t *lw_icon_button(lv_obj_t *parent, const char *symbol, int size, uint32_t bg, uint32_t fg)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 10, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(lw_palette()->shadow), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(fg), 0);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    return btn;
}

/* ----------------------------------------------------------------- toggle */

static void toggle_clicked(lv_event_t *event)
{
    lv_obj_t *toggle = lv_event_get_target(event);
    int on = lw_toggle_get(toggle) ? 0 : 1;
    lw_toggle_set(toggle, on);
}

lv_obj_t *lw_toggle(lv_obj_t *parent, int on, int x, int y)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, 58, 32);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(lw_palette()->accent), 0);
    lv_obj_set_style_bg_opa(track, on ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_add_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *knob = lv_obj_create(track);
    lv_obj_remove_style_all(knob);
    lv_obj_set_size(knob, 26, 26);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(knob, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(knob, 6, 0);
    lv_obj_set_style_shadow_color(knob, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(knob, LV_OPA_20, 0);
    lv_obj_set_pos(knob, on ? 29 : 3, 3);
    lv_obj_remove_flag(knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(track, LV_OBJ_FLAG_USER_1);
    lv_obj_set_user_data(track, (void *)(on ? 1 : 0));
    lv_obj_add_event_cb(track, toggle_clicked, LV_EVENT_CLICKED, NULL);
    return track;
}

void lw_toggle_set(lv_obj_t *toggle, int on)
{
    if (toggle == NULL) {
        return;
    }
    lv_obj_set_user_data(toggle, (void *)(on ? 1 : 0));
    lv_obj_set_style_bg_opa(toggle, on ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_t *knob = lv_obj_get_child(toggle, 0);
    if (knob != NULL) {
        lv_obj_set_pos(knob, on ? 29 : 3, 3);
    }
}

int lw_toggle_get(lv_obj_t *toggle)
{
    if (toggle == NULL) {
        return 0;
    }
    return lv_obj_get_user_data(toggle) != NULL;
}

/* -------------------------------------------------------------------- bar */

lv_obj_t *lw_bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, w, h);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(lw_palette()->line), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fill = lv_obj_create(track);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, w, h);
    lv_obj_set_style_radius(fill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    return track;
}

void lw_bar_set(lv_obj_t *bar, int percent)
{
    if (bar == NULL) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    lv_obj_t *fill = lv_obj_get_child(bar, 0);
    if (fill == NULL) {
        return;
    }
    lv_coord_t width = lv_obj_get_width(bar);
    lv_obj_set_width(fill, (width * percent) / 100);
}

lv_obj_t *lw_icon_badge(lv_obj_t *parent, int x, int y, int size, uint32_t bg, int radius)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_pos(badge, x, y);
    lv_obj_set_size(badge, size, size);
    lv_obj_set_style_radius(badge, radius, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    return badge;
}
