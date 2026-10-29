/* Pixel-exactness check for the baked sprite packs (V4.5 P0).
 * SPDX-License-Identifier: MIT
 *
 * lwp_prescale.py rewrites a character pack so lv_image never enters
 * transform_and_recolor(): the 1.125x proportion correction is baked into the
 * pixels and the geometry trailer tells lw_asset_draw_geometry() to use
 * LV_SCALE_NONE. That is a 4-6x cheaper draw in host/render_bench.c, but only
 * worth shipping if the picture is unchanged.
 *
 * This renders both forms through the same LVGL software renderer on the same
 * canvas and diffs the framebuffer:
 *
 *   before: the shipped 256x256 frame, scale 288, pivot 0, offset 0
 *   after:  the baked frame, scale 256, offset 0, placed by anchor - anchor_dx
 *
 * A nonzero diff means the bake does not reproduce LVGL's fixed-point mapping
 * and must not ship. The two documented sources of a small residual are the
 * alpha channel (blended, not compared) and antialias, which lw_sprite turns
 * off for every sprite and which this tool also turns off.
 *
 *   prescale_verify <before.raw> <before_w> <before_h> <before_scale>
 *                   <after.raw> <after_w> <after_h>
 *                   <anchor_x> <anchor_y> <after_anchor_x> <after_anchor_y>
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "lvgl.h"

#define CANVAS_W 1024
#define CANVAS_H 600
#define PLACE_X 360
#define PLACE_Y 200

static uint16_t s_fb[CANVAS_W * CANVAS_H];
static uint16_t s_ref[CANVAS_W * CANVAS_H];
static uint8_t *s_before_px;
static uint8_t *s_after_px;
static lv_image_dsc_t s_before_dsc;
static lv_image_dsc_t s_after_dsc;
static lv_obj_t *s_sprite;

static uint32_t tick_cb(void) { return (uint32_t)GetTickCount(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const uint16_t *src = (const uint16_t *)px_map;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint16_t *row = s_fb + (size_t)y * CANVAS_W;
        for (int32_t x = area->x1; x <= area->x2; x++) row[x] = *src++;
    }
    lv_display_flush_ready(disp);
}

/* Deterministic background: a sprite over a flat colour would hide an alpha
 * mistake, because blending a wrong mask over black still reads black. */
static void paint_background(void)
{
    for (int y = 0; y < CANVAS_H; y++) {
        uint16_t *row = s_fb + (size_t)y * CANVAS_W;
        for (int x = 0; x < CANVAS_W; x++) {
            unsigned r = (unsigned)((x * 31) / (CANVAS_W - 1));
            unsigned g = (unsigned)((y * 63) / (CANVAS_H - 1));
            unsigned b = (unsigned)(((x + y) * 31) / (CANVAS_W + CANVAS_H - 2));
            row[x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

static bool load_raw(const char *path, int w, int h, uint8_t **out, lv_image_dsc_t *dsc)
{
    size_t bytes = (size_t)w * h * 3;
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return false; }
    uint8_t *data = (uint8_t *)malloc(bytes);
    bool ok = data && fread(data, 1, bytes, f) == bytes;
    fclose(f);
    if (!ok) { printf("%s: short read, expected %zu bytes\n", path, bytes); free(data); return false; }
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.w = (uint16_t)w;
    dsc->header.h = (uint16_t)h;
    dsc->header.stride = (uint32_t)w * 2;
    dsc->data_size = (uint32_t)bytes;
    dsc->data = data;
    *out = data;
    return true;
}

static void render(const lv_image_dsc_t *dsc, int obj_w, int obj_h, int scale,
                   int pos_x, int pos_y, int off_x, int off_y)
{
    lv_image_set_src(s_sprite, dsc);
    lv_image_set_antialias(s_sprite, false);
    lv_obj_set_size(s_sprite, obj_w, obj_h);
    lv_obj_set_pos(s_sprite, pos_x, pos_y);
    lv_image_set_pivot(s_sprite, 0, 0);
    lv_image_set_scale(s_sprite, (uint16_t)scale);
    lv_image_set_offset_x(s_sprite, off_x);
    lv_image_set_offset_y(s_sprite, off_y);
    paint_background();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
}

/* LVGL maps with 8.8 fixed point plus a half-pixel bias; this is the same
 * helper lw_assets.c and render_bench.c use. */
static int scale_px(int v, int scale) { return (v * scale + 0x80) >> 8; }

int main(int argc, char **argv)
{
    if (argc != 12 && argc != 16) {
        printf("usage: %s <before.raw> <bw> <bh> <bscale> <after.raw> <aw> <ah> "
               "<anchor_x> <anchor_y> <after_anchor_x> <after_anchor_y> "
               "[<box_x> <box_y> <box_w> <box_h>]\n", argv[0]);
        return 2;
    }
    const char *before_path = argv[1];
    int bw = atoi(argv[2]), bh = atoi(argv[3]), bscale = atoi(argv[4]);
    const char *after_path = argv[5];
    int aw = atoi(argv[6]), ah = atoi(argv[7]);
    int anchor_x = atoi(argv[8]), anchor_y = atoi(argv[9]);
    int after_anchor_x = atoi(argv[10]), after_anchor_y = atoi(argv[11]);
    /* Optional content box inside the baked canvas. The pack format has one
     * canvas per pack, so daily.lwp's clips all occupy 276x252 bytes even though
     * dog.idle only covers 54x47; the trailer records that smaller box and the
     * firmware draws just the box. Mirroring it here is what keeps the verifier
     * measuring what the device actually does. */
    int box_x = 0, box_y = 0, box_w = aw, box_h = ah;
    if (argc == 16) {
        box_x = atoi(argv[12]), box_y = atoi(argv[13]);
        box_w = atoi(argv[14]), box_h = atoi(argv[15]);
    }

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(CANVAS_W, CANVAS_H);
    lv_display_set_flush_cb(disp, flush_cb);
    static uint8_t draw_buf[CANVAS_W * 80 * 2];
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(disp);

    s_sprite = lv_image_create(lv_screen_active());

    if (!load_raw(before_path, bw, bh, &s_before_px, &s_before_dsc)) return 1;
    if (!load_raw(after_path, aw, ah, &s_after_px, &s_after_dsc)) return 1;

    /* before: what ships today - whole canvas, transformed */
    render(&s_before_dsc, bw, bh, bscale, PLACE_X, PLACE_Y, 0, 0);
    memcpy(s_ref, s_fb, sizeof(s_ref));

    /* after: baked pixels drawn 1:1. The object origin is the anchor minus the
     * distance from the object origin to the anchor; for a baked asset that
     * distance is the baked anchor minus the box origin, because the object now
     * covers only the box rather than the whole canvas. The offset then puts the
     * box back where the full-canvas draw had it: LV_ALIGN_CENTER places the
     * image at obj.x1 + (obj_w/2 - img_w/2) and LVGL halves the two dimensions
     * separately, so this is not (obj_w - img_w)/2. */
    render(&s_after_dsc, box_w, box_h, LV_SCALE_NONE,
           PLACE_X + scale_px(anchor_x, bscale) - (after_anchor_x - box_x),
           PLACE_Y + scale_px(anchor_y, bscale) - (after_anchor_y - box_y),
           -box_x - (box_w / 2 - aw / 2),
           -box_y - (box_h / 2 - ah / 2));

    long diff = 0, worst = 0, opaque = 0;
    int min_x = CANVAS_W, max_x = -1, min_y = CANVAS_H, max_y = -1;
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            size_t i = (size_t)y * CANVAS_W + x;
            if (s_ref[i] != s_fb[i]) {
                int d = abs((int)(s_ref[i] & 0x1F) - (int)(s_fb[i] & 0x1F))
                      + abs((int)((s_ref[i] >> 5) & 0x3F) - (int)((s_fb[i] >> 5) & 0x3F))
                      + abs((int)((s_ref[i] >> 11) & 0x1F) - (int)((s_fb[i] >> 11) & 0x1F));
                diff++;
                if (d > worst) worst = d;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
            if (s_ref[i] != 0) opaque++;
        }
    }

    printf("before %s %dx%d scale %d\n", before_path, bw, bh, bscale);
    printf("after  %s %dx%d scale 256 (baked)\n", after_path, aw, ah);
    printf("differing pixels: %ld of %d drawn\n", diff, CANVAS_W * CANVAS_H);
    if (diff) printf("worst channel delta: %ld, bbox (%d,%d)-(%d,%d)\n",
                     worst, min_x, min_y, max_x, max_y);
    printf("verdict: %s\n", diff == 0 ? "IDENTICAL" : "DIFFERS");

    free(s_before_px);
    free(s_after_px);
    return diff == 0 ? 0 : 1;
}
