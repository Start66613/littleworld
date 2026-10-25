/* SPDX-License-Identifier: MIT */
#include "lw_theme.h"

#include "lw_app_state.h"

static int s_variant;
static int s_ready;

static const lw_palette_t s_variants[3] = {
    /* cream */
    {
        0xFFFAF4, 0xF4E9FB, 0xFFFFFF, 0xFFF5F8, 0x4A3B45, 0x8E7C88, 0xF0E3EC,
        0xF58FAE, 0xE06A90, 0xB79CED, 0x8A6BD1, 0x7FD3B4, 0xF6C37C, 0xE7D0DF, 0xFFFFFF,
    },
    /* rose */
    {
        0xFFF7F6, 0xFBE7F0, 0xFFFFFF, 0xFFF2F5, 0x4C3A43, 0x937C86, 0xF6DEE6,
        0xF27FA6, 0xD95E86, 0xC09AE8, 0x916FD6, 0x86D6B8, 0xF7C478, 0xEFD3DD, 0xFFFFFF,
    },
    /* lavender */
    {
        0xFBF8FF, 0xF0E7FC, 0xFFFFFF, 0xF7F2FF, 0x453B4E, 0x8A7E96, 0xE8DFF5,
        0xC79AF0, 0xA173DD, 0xB79CED, 0x8A6BD1, 0x8AD9C2, 0xF3C98A, 0xE2D5EF, 0xFFFFFF,
    },
};

static lv_style_t s_card;
static lv_style_t s_card_pressed;
static lv_style_t s_soft;

static void anim_translate_cb(void *var, int32_t value)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, value, 0);
}

static void anim_opa_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}

const lw_palette_t *lw_palette(void)
{
    return &s_variants[s_variant];
}

int lw_theme_variant(void)
{
    return s_variant;
}

void lw_theme_init(void)
{
    if (s_ready) {
        return;
    }
    lv_style_init(&s_card);
    lv_style_set_radius(&s_card, 26);
    lv_style_set_bg_color(&s_card, lv_color_hex(s_variants[0].card));
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_border_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, 0);
    lv_style_set_shadow_width(&s_card, 22);
    lv_style_set_shadow_color(&s_card, lv_color_hex(s_variants[0].shadow));
    lv_style_set_shadow_opa(&s_card, LV_OPA_30);
    lv_style_set_shadow_offset_y(&s_card, 8);
    lv_style_set_text_font(&s_card, &lw_font_22);
    lv_style_set_text_color(&s_card, lv_color_hex(s_variants[0].text));

    lv_style_init(&s_card_pressed);
    lv_style_set_shadow_width(&s_card_pressed, 6);
    lv_style_set_shadow_offset_y(&s_card_pressed, 2);
    lv_style_set_translate_y(&s_card_pressed, 2);

    lv_style_init(&s_soft);
    lv_style_set_radius(&s_soft, 20);
    lv_style_set_bg_color(&s_soft, lv_color_hex(s_variants[0].card_soft));
    lv_style_set_bg_opa(&s_soft, LV_OPA_COVER);
    lv_style_set_border_width(&s_soft, 0);
    lv_style_set_pad_all(&s_soft, 0);

    s_ready = 1;
}

void lw_theme_set_variant(int variant)
{
    if (variant < 0 || variant > 2) {
        variant = 0;
    }
    s_variant = variant;
    if (s_ready) {
        lv_style_set_bg_color(&s_card, lv_color_hex(s_variants[s_variant].card));
        lv_style_set_shadow_color(&s_card, lv_color_hex(s_variants[s_variant].shadow));
        lv_style_set_text_color(&s_card, lv_color_hex(s_variants[s_variant].text));
        lv_style_set_bg_color(&s_soft, lv_color_hex(s_variants[s_variant].card_soft));
    }
}

void lw_theme_paint_background(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(lw_palette()->bg_top), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(lw_palette()->bg_bottom), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(obj, &lw_font_22, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(lw_palette()->text), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void lw_style_card(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_card, 0);
    lv_obj_add_style(obj, &s_card_pressed, LV_STATE_PRESSED);
}

void lw_style_soft_card(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_soft, 0);
}

void lw_style_label(lv_obj_t *obj, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

/* Fade + rise transition used when a page is created. */
void lw_theme_enter_anim(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    /* The settings page can switch page animations off. */
    if (!lw_app_state()->settings.animations) {
        lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_translate_y(obj, 0, 0);
        return;
    }
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, obj);
    lv_anim_set_values(&fade, 0, 255);
    lv_anim_set_duration(&fade, 180);
    lv_anim_set_exec_cb(&fade, anim_opa_cb);
    lv_anim_start(&fade);

    lv_obj_set_style_translate_y(obj, 14, 0);
    lv_anim_t rise;
    lv_anim_init(&rise);
    lv_anim_set_var(&rise, obj);
    lv_anim_set_values(&rise, 14, 0);
    lv_anim_set_duration(&rise, 220);
    lv_anim_set_path_cb(&rise, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&rise, anim_translate_cb);
    lv_anim_start(&rise);
}
