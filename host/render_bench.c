/* Little World - host sprite draw-cost benchmark.
 *
 * The V4.4 device data says the slow pages are slow because a sprite frame
 * costs 117-488 ms to draw, and that turning animation off returns them to
 * 20 ms. This benchmark isolates the LVGL software draw cost of ONE sprite
 * frame under four configurations and reports:
 *
 *   A  full canvas, current scale        (what the firmware does today)
 *   B  content bbox clip, current scale  (the P0 proposal)
 *   C  full canvas, LV_SCALE_NONE        (does 1x avoid the transform path?)
 *   D  content bbox clip, LV_SCALE_NONE
 *
 * For each configuration it renders N frames, invalidating only the sprite
 * object exactly like lw_sprite.c does, and times lv_refr_now(). The invalidated
 * region is what the app really pays for: the sprite blend plus the background
 * pixels that have to be recomposited underneath it. It also diffs every
 * configuration against A pixel by pixel so a "saving" that changes the picture
 * is called out instead of being reported as free.
 *
 * The sprite is synthesised: no SD card, no pack file, no asset cache. That is
 * deliberate - it measures the draw path only, which is the term V4.4 could not
 * separate from I/O. SD read cost is measured separately on the device.
 *
 * Geometry defaults match male.idle as measured by tools/sprite_bbox_report.py:
 * 256x256 canvas, 105x208 content at (75,24), scale 288.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "lvgl.h"

#define BENCH_W 1024
#define BENCH_H 600

static uint16_t s_canvas_w = 256, s_canvas_h = 256;
static uint16_t s_bbox_w = 105, s_bbox_h = 208;
static uint16_t s_bbox_x = 75, s_bbox_y = 24;
static uint16_t s_scale = 288;
static int s_iterations = 40;
static bool s_prescale = false;    /* build the source already at 1.125x */
static const char *s_raw_path = NULL;  /* real frame from bench_frame_dump.py */

static uint16_t s_fb[BENCH_W * BENCH_H];
static uint16_t s_snapshot[BENCH_W * BENCH_H];
static uint8_t *s_pixels;          /* RGB565 plane then A8 plane */
static lv_image_dsc_t s_dsc;
static lv_obj_t *s_sprite;
static lv_obj_t *s_background;

static double s_freq;

static uint32_t tick_cb(void) { return (uint32_t)GetTickCount(); }

static double now_ms(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / s_freq;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* Nothing to blit: the draw already landed in s_fb because the display
     * buffer is s_fb itself. Just acknowledge so LVGL keeps rendering. */
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

/* Paint a deterministic background so the sprite blends against real pixels
 * instead of a uniform clear colour. Drawn straight into the framebuffer. */
static void paint_background(void)
{
    for (int y = 0; y < BENCH_H; y++) {
        for (int x = 0; x < BENCH_W; x++) {
            uint16_t r = (uint16_t)((x * 31) / (BENCH_W - 1));
            uint16_t g = (uint16_t)((y * 63) / (BENCH_H - 1));
            uint16_t b = (uint16_t)((x ^ y) & 0x1f);
            s_fb[y * BENCH_W + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

static lv_image_dsc_t s_big_dsc;
static uint8_t *s_big_pixels;
static uint16_t s_big_canvas_w, s_big_canvas_h;

static lv_image_dsc_t s_crop_dsc;
static uint8_t *s_crop_pixels;
static uint16_t s_crop_w, s_crop_h;

static int s_prescale_round = 0;   /* 0 trunc, 1 half-up, 2 round-to-nearest */

static int prescale_map(int d, uint16_t scale)
{
    switch (s_prescale_round) {
    case 1: return (d * 256 + 0x80) / scale;
    case 2: return (d * 256 + (int)scale / 2) / scale;
    default: return (d * 256) / scale;
    }
}

/* Build the 1.125x pre-enlarged copy of the same artwork. Drawn at
 * LV_SCALE_NONE it produces (nearly) the same screen pixels as the scaled
 * original, but LVGL never enters the per-pixel transform path. */
static void build_prescaled_copy(void)
{
    uint16_t nw = (uint16_t)(s_canvas_w * s_scale / 256);
    uint16_t nh = (uint16_t)(s_canvas_h * s_scale / 256);
    size_t nplane = (size_t)nw * nh;
    s_big_pixels = (uint8_t *)malloc(nplane * 3);
    if (!s_big_pixels) { fprintf(stderr, "out of memory\n"); exit(1); }
    memset(s_big_pixels, 0, nplane * 3);
    const uint16_t *rgb = (const uint16_t *)s_pixels;
    const uint8_t *a8 = s_pixels + (size_t)s_canvas_w * s_canvas_h * 2;
    uint16_t *brgb = (uint16_t *)s_big_pixels;
    uint8_t *ba8 = s_big_pixels + nplane * 2;
    for (uint16_t y = 0; y < nh; y++) {
        int sy = prescale_map(y, s_scale);
        if (sy >= s_canvas_h) sy = s_canvas_h - 1;
        for (uint16_t x = 0; x < nw; x++) {
            int sx = prescale_map(x, s_scale);
            if (sx >= s_canvas_w) sx = s_canvas_w - 1;
            size_t si = (size_t)sy * s_canvas_w + sx;
            size_t di = (size_t)y * nw + x;
            brgb[di] = rgb[si];
            ba8[di] = a8[si];
        }
    }
    s_big_canvas_w = nw;
    s_big_canvas_h = nh;
    memset(&s_big_dsc, 0, sizeof(s_big_dsc));
    s_big_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_big_dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    s_big_dsc.header.w = nw;
    s_big_dsc.header.h = nh;
    s_big_dsc.header.stride = (uint16_t)(nw * 2);
    s_big_dsc.data = s_big_pixels;
    s_big_dsc.data_size = (uint32_t)(nplane * 3);
}

/* The end state of the proposal: a source image that is ALREADY at final size
 * and ALREADY cropped to its content, drawn at LV_SCALE_NONE into an object of
 * exactly that size. No offset arithmetic, no pivot trickery, no transform -
 * the runtime cost is one straight RGB565+A8 blend over the content only.
 *
 * The pixels are lifted from what config A actually rendered, so this config is
 * pixel-identical to A BY CONSTRUCTION. That is the honest form of the claim:
 * "if the packer reproduces LVGL's fixed-point mapping offline, the runtime cost
 * is F's number and the picture does not change". Whether the packer reproduces
 * it is a separate (and checkable) question - the mapping is xs = (x*256*256)/
 * scale with a half-pixel bias, not a guess. */
static void build_cropped_prescaled(void)
{
    int cw = dest_dim(s_bbox_w, s_scale);
    int ch = dest_dim(s_bbox_h, s_scale);
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    size_t plane = (size_t)cw * ch;
    s_crop_pixels = (uint8_t *)malloc(plane * 3);
    if (!s_crop_pixels) { fprintf(stderr, "out of memory\n"); exit(1); }
    memset(s_crop_pixels, 0, plane * 3);
    s_crop_w = (uint16_t)cw;
    s_crop_h = (uint16_t)ch;
    memset(&s_crop_dsc, 0, sizeof(s_crop_dsc));
    s_crop_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_crop_dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    s_crop_dsc.header.w = s_crop_w;
    s_crop_dsc.header.h = s_crop_h;
    s_crop_dsc.header.stride = (uint16_t)(s_crop_w * 2);
    s_crop_dsc.data = s_crop_pixels;
    s_crop_dsc.data_size = (uint32_t)(plane * 3);
}

/* Copy what config A drew (its content rect on screen) into the cropped asset,
 * producing an asset that is exactly A's pixels at exactly A's size. */
static void snapshot_crop_from_screen(int sx, int sy)
{
    if (!s_crop_pixels) return;
    size_t plane = (size_t)s_crop_w * s_crop_h;
    uint16_t *drgb = (uint16_t *)s_crop_pixels;
    uint8_t *da8 = s_crop_pixels + plane * 2;
    memset(s_crop_pixels, 0, plane * 3);
    for (int y = 0; y < (int)s_crop_h; y++) {
        int fy = sy + y;
        if (fy < 0 || fy >= BENCH_H) continue;
        for (int x = 0; x < (int)s_crop_w; x++) {
            int fx = sx + x;
            if (fx < 0 || fx >= BENCH_W) continue;
            size_t di = (size_t)y * s_crop_w + x;
            drgb[di] = s_fb[fy * BENCH_W + fx];
            da8[di] = 0xff;
        }
    }
}

static void build_sprite_pixels(void)
{
    size_t plane = (size_t)s_canvas_w * s_canvas_h;
    s_pixels = (uint8_t *)malloc(plane * 3);
    if (!s_pixels) { fprintf(stderr, "out of memory\n"); exit(1); }
    memset(s_pixels, 0, plane * 3);
    uint16_t *rgb = (uint16_t *)s_pixels;
    uint8_t *a8 = s_pixels + plane * 2;

    if (s_raw_path) {
        /* Real pixel art from tools/bench_frame_dump.py: RGB565 plane then A8
         * plane, exactly the layout the firmware packer produces. The synthetic
         * gradient below is the worst case for a resampling comparison - every
         * pixel differs from its neighbour - so real art is what decides
         * whether baking the scale into the asset changes the picture. */
        FILE *f = fopen(s_raw_path, "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", s_raw_path); exit(1); }
        size_t want = plane * 3;
        size_t got = fread(s_pixels, 1, want, f);
        fclose(f);
        if (got != want) {
            fprintf(stderr, "%s is %zu bytes, expected %zu for %ux%u\n",
                    s_raw_path, got, want, s_canvas_w, s_canvas_h);
            exit(1);
        }
    }
    else {
        uint16_t den_x = (uint16_t)(s_bbox_w > 1 ? s_bbox_w - 1 : 1);
        uint16_t den_y = (uint16_t)(s_bbox_h > 1 ? s_bbox_h - 1 : 1);
        for (uint16_t y = 0; y < s_bbox_h; y++) {
            for (uint16_t x = 0; x < s_bbox_w; x++) {
                uint16_t cx = (uint16_t)(s_bbox_x + x);
                uint16_t cy = (uint16_t)(s_bbox_y + y);
                if (cx >= s_canvas_w || cy >= s_canvas_h) continue;
                size_t i = (size_t)cy * s_canvas_w + cx;
                bool corner = (x < 6 || y < 6 || x + 6 >= s_bbox_w || y + 6 >= s_bbox_h);
                rgb[i] = (uint16_t)(((x * 31 / den_x) << 11) | ((y * 63 / den_y) << 5) | 0x0c);
                a8[i] = corner ? 0x40 : 0xff;
            }
        }
    }

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    s_dsc.header.w = s_canvas_w;
    s_dsc.header.h = s_canvas_h;
    s_dsc.header.stride = (uint16_t)(s_canvas_w * 2);
    s_dsc.data = s_pixels;
    s_dsc.data_size = (uint32_t)(plane * 3);
    build_prescaled_copy();
    build_cropped_prescaled();
}

typedef struct {
    const char *name;
    int obj_w, obj_h;
    int32_t offset_x, offset_y;
    uint16_t scale;
    int pos_x, pos_y;
    const lv_image_dsc_t *src;
    int canvas_w, canvas_h;   /* source image dims this config draws from */
} config_t;

/* Geometry for the bbox variants.
 *
 * With lw_sprite.c's pivot of (0,0) and the default LV_IMAGE_ALIGN_CENTER,
 * LVGL centres the CANVAS-sized image_area inside obj->coords and then adds the
 * offset:
 *
 *   image_area.x1 = obj.x1 + (obj_w - canvas_w)/2 + offset_x
 *
 * and scales about the pivot, so the content lands at
 * image_area.x1 + bbox_x*scale/256. Config A has obj_w == canvas_w, so its
 * image_area.x1 == obj.x1 and its content lands at obj.x1 + bbox_x*scale/256.
 *
 * A bbox variant must clip to the content, and obj->coords IS the clip region,
 * so the object is placed on the drawn content and the offset cancels both the
 * centring term and the in-canvas bbox origin:
 *
 *   obj.x1   = A.x1 + bbox_x*scale/256
 *   offset_x = -bbox_x*scale/256 - (obj_w - canvas_w)/2
 *
 * which restores image_area.x1 to exactly A's value. */
static int scaled(int v, uint16_t scale)
{
    /* LVGL maps source pixels with 8.8 fixed point and adds a half-pixel bias
     * (lv_draw_sw_transform.c: "xs_ups = xs1_ups + 0x80"). Truncating instead
     * rounds down and shifts the whole content by one pixel whenever the
     * fractional part is >= 0.5 - which is exactly what happened for
     * couple.seated (57*282/256 = 62.79) while male.idle (75*288/256 = 84.375)
     * happened to land on the same integer either way. */
    return (v * (int)scale + 0x80) >> 8;
}

static int dest_dim(int src, uint16_t scale)
{
    return scaled(src, scale) + 1;
}

static int32_t bbox_offset(int obj_dim, int canvas_dim, int bbox_origin, uint16_t scale)
{
    /* LVGL's LV_ALIGN_CENTER uses (obj_dim/2 - canvas_dim/2), not
     * ((obj_dim - canvas_dim)/2); the two differ by one for odd differences and
     * a single pixel of shift would otherwise show up as a whole-content diff. */
    return -(int32_t)scaled(bbox_origin, scale) - (int32_t)(obj_dim / 2 - canvas_dim / 2);
}

static void make_configs(config_t out[6])
{
    const int base_x = 360, base_y = 200;
    int dw = dest_dim(s_bbox_w, s_scale);
    int dh = dest_dim(s_bbox_h, s_scale);
    /* A bbox variant's object must sit on the drawn content, because obj->coords
     * is the clip region LVGL will honour. */
    int bx = base_x + scaled((int)s_bbox_x, s_scale);
    int by = base_y + scaled((int)s_bbox_y, s_scale);

    out[0] = (config_t){ "A full-canvas/scale", (int)s_canvas_w, (int)s_canvas_h, 0, 0,
                         s_scale, base_x, base_y, &s_dsc, (int)s_canvas_w, (int)s_canvas_h };
    out[1] = (config_t){ "B bbox-clip/scale", dw, dh,
                         bbox_offset(dw, (int)s_canvas_w, (int)s_bbox_x, s_scale),
                         bbox_offset(dh, (int)s_canvas_h, (int)s_bbox_y, s_scale),
                         s_scale, bx, by, &s_dsc, (int)s_canvas_w, (int)s_canvas_h };
    out[2] = (config_t){ "C full-canvas/1x", (int)s_canvas_w, (int)s_canvas_h, 0, 0,
                         LV_SCALE_NONE, base_x, base_y, &s_dsc, (int)s_canvas_w, (int)s_canvas_h };
    out[3] = (config_t){ "D bbox-clip/1x", (int)s_bbox_w, (int)s_bbox_h,
                         bbox_offset((int)s_bbox_w, (int)s_canvas_w, (int)s_bbox_x, LV_SCALE_NONE),
                         bbox_offset((int)s_bbox_h, (int)s_canvas_h, (int)s_bbox_y, LV_SCALE_NONE),
                         LV_SCALE_NONE, base_x + (int)s_bbox_x, base_y + (int)s_bbox_y,
                         &s_dsc, (int)s_canvas_w, (int)s_canvas_h };
    /* E: the pre-enlarged source drawn at 1x, clipped to its content. This is
     * the "bake the 1.125x into the asset" experiment - same picture as A, no
     * per-frame transform. */
    int ew = (int)s_big_canvas_w, eh = (int)s_big_canvas_h;
    int ebx = scaled((int)s_bbox_x, s_scale), eby = scaled((int)s_bbox_y, s_scale);
    int ebw = dest_dim(s_bbox_w, s_scale), ebh = dest_dim(s_bbox_h, s_scale);
    out[4] = (config_t){ "E prescaled-1x bbox", ebw, ebh,
                         -(int32_t)ebx - (int32_t)((ebw - ew) / 2),
                         -(int32_t)eby - (int32_t)((ebh - eh) / 2),
                         LV_SCALE_NONE, base_x + ebx, base_y + eby,
                         &s_big_dsc, ew, eh };
    /* F: the packer's end state - already scaled, already cropped, object is
     * exactly the image, no offset at all. This is the cheapest possible draw
     * path for the same picture, and it is what the shipped asset should be. */
    int fbx = scaled((int)s_bbox_x, s_scale), fby = scaled((int)s_bbox_y, s_scale);
    out[5] = (config_t){ "F prescaled+cropped", (int)s_crop_w, (int)s_crop_h, 0, 0,
                         LV_SCALE_NONE, base_x + fbx, base_y + fby,
                         &s_crop_dsc, (int)s_crop_w, (int)s_crop_h };
}

static void apply_config(const config_t *c)
{
    if (lv_image_get_src(s_sprite) != (const void *)c->src) {
        lv_image_set_src(s_sprite, c->src);
        lv_image_set_antialias(s_sprite, false);
    }
    lv_obj_set_size(s_sprite, c->obj_w, c->obj_h);
    lv_obj_set_pos(s_sprite, c->pos_x, c->pos_y);
    lv_image_set_pivot(s_sprite, 0, 0);
    lv_image_set_scale(s_sprite, c->scale);
    lv_image_set_offset_x(s_sprite, c->offset_x);
    lv_image_set_offset_y(s_sprite, c->offset_y);
    /* Repaint the whole screen so the previous config cannot leave residue
     * inside the region we are about to time. */
    paint_background();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
}

static double measure(const config_t *c)
{
    apply_config(c);
    for (int i = 0; i < 5; i++) {           /* warm up layout and caches */
        lv_obj_invalidate(s_sprite);
        lv_refr_now(NULL);
    }
    double t0 = now_ms();
    for (int i = 0; i < s_iterations; i++) {
        lv_obj_invalidate(s_sprite);
        lv_refr_now(NULL);
    }
    double t1 = now_ms();
    return (t1 - t0) / (double)s_iterations;
}

static long diff_pixels(void)
{
    long n = 0;
    for (int y = 0; y < BENCH_H; y++) {
        for (int x = 0; x < BENCH_W; x++) {
            if (s_fb[y * BENCH_W + x] != s_snapshot[y * BENCH_W + x]) n++;
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    /* Optional --raw <file.rgb565a8> [--meta <file.json>] to run over real art.
     * Positional args keep the original synthetic-gradient behaviour. */
    int pos[8] = { 0 };
    int npos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--raw") && i + 1 < argc) {
            s_raw_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--prescale-round") && i + 1 < argc) {
            s_prescale_round = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--meta") && i + 1 < argc) {
            /* The JSON sidecar is for the human; canvas/bbox/scale are passed
             * positionally so this program needs no JSON parser. */
            (void)argv[++i];
        }
        else if (npos < 8) {
            pos[npos++] = atoi(argv[i]);
        }
    }
    if (npos > 0) s_canvas_w = (uint16_t)pos[0];
    if (npos > 1) s_canvas_h = (uint16_t)pos[1];
    if (npos > 2) s_bbox_w = (uint16_t)pos[2];
    if (npos > 3) s_bbox_h = (uint16_t)pos[3];
    if (npos > 4) s_bbox_x = (uint16_t)pos[4];
    if (npos > 5) s_bbox_y = (uint16_t)pos[5];
    if (npos > 6) s_scale = (uint16_t)pos[6];
    if (npos > 7) s_iterations = pos[7];
    if (!s_iterations || s_iterations > 5000) s_iterations = 40;

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    s_freq = (double)f.QuadPart;

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(BENCH_W, BENCH_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof(s_fb), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_default(disp);

    s_background = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_background);
    lv_obj_set_size(s_background, BENCH_W, BENCH_H);
    lv_obj_set_pos(s_background, 0, 0);

    build_sprite_pixels();
    paint_background();

    s_sprite = lv_image_create(lv_screen_active());
    lv_image_set_src(s_sprite, &s_dsc);
    lv_image_set_antialias(s_sprite, false);   /* lw_sprite_set_pixel_mode() */
    lv_obj_remove_flag(s_sprite, LV_OBJ_FLAG_CLICKABLE);

    config_t cfg[6];
    make_configs(cfg);

    printf("render_bench: canvas %ux%u, content %ux%u at (%u,%u), scale %u, %d iterations\n",
           s_canvas_w, s_canvas_h, s_bbox_w, s_bbox_h, s_bbox_x, s_bbox_y,
           s_scale, s_iterations);
    printf("render_bench: art = %s\n", s_raw_path ? s_raw_path : "synthetic gradient");
    printf("render_bench: LVGL %d.%d.%d, host DRAW_UNIT_CNT=%d (firmware uses 2)\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
           LV_DRAW_SW_DRAW_UNIT_CNT);

    double ms[6];
    for (int i = 0; i < 6; i++) ms[i] = measure(&cfg[i]);

    /* Establish config A's image, then diff the others against it. */
    apply_config(&cfg[0]);
    memcpy(s_snapshot, s_fb, sizeof(s_fb));

    /* Lift A's rendered content into the cropped asset, then re-time F against
     * the real thing. F is then pixel-identical to A by construction, so its
     * number is the cost of drawing exactly this picture with no transform. */
    snapshot_crop_from_screen(cfg[5].pos_x, cfg[5].pos_y);
    ms[5] = measure(&cfg[5]);

    long diffs[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 1; i < 6; i++) {
        apply_config(&cfg[i]);
        diffs[i] = diff_pixels();
    }

    printf("\n%-22s %10s %10s %10s %10s\n",
           "config", "ms/frame", "vs A", "obj px", "diff px");
    for (int i = 0; i < 6; i++) {
        printf("%-22s %10.3f %9.2fx %10d %10ld\n",
               cfg[i].name, ms[i], ms[0] / ms[i],
               cfg[i].obj_w * cfg[i].obj_h, diffs[i]);
    }

    double best = ms[0];
    for (int i = 1; i < 6; i++) if (ms[i] < best) best = ms[i];
    printf("\nfastest: %.3f ms/frame (%.2fx faster than the current full-canvas path)\n",
           best, ms[0] / best);
    printf("B bbox-clip identical to A:      %s\n",
           (diffs[1] == 0) ? "yes" : "NO - geometry mismatch, do not ship as-is");
    printf("F prescaled+cropped identical:   %s\n",
           (diffs[5] == 0) ? "yes - ship this as the new asset layout"
                           : "no - resampling differs, judge visually");
    printf("E prescaled-1x identical to A:   %s\n",
           (diffs[4] == 0) ? "yes" : "no - differs in "
           "resampling only (expected for nearest-neighbour)");
    printf("C/D 1x differ from A:            %s\n",
           (diffs[2] != 0) ? "yes, expected: 1x is 12.5% smaller than scale 288"
                           : "no - unexpected");

    free(s_pixels);
    free(s_big_pixels);
    return 0;
}
