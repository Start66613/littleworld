/*
 * On-board acceptance evidence: LVGL snapshots over the UART console.
 *
 *   shot [name]   render the active screen to a BMP, emit it base64-encoded
 *   tap X Y       synthesise one press/release at screen coordinates
 *   status        print heap, display size and the last captured frame
 *
 * Protocol, one record per line:
 *   LCAP BMP <base64 of the 54-byte BMP header>
 *   LCAP DATA <base64 rows...>
 *   LCAP END
 *
 * The image is halved (for example 1024x600 -> 512x300) before encoding:
 * 460 800 bytes of BMP become 614 400 base64 characters, about 7 s at
 * 921600 baud. Halving keeps evidence readable and a capture inside one
 * UART session.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lw_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lw_capture";

#define CAP_SCALE 2          /* downscale factor of the emitted BMP */
#define CAP_B64_PER_LINE 96  /* must be a multiple of 4 */

static lv_display_t *s_snap_display;
static int s_last_w;
static int s_last_h;
static volatile int s_tap_x = -1;
static volatile int s_tap_y = -1;
static volatile int s_tap_step;   /* 0 idle, 1 pressed, 2 released */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* ---------------------------------------------------------------- input --- */

static void sim_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    const int step = s_tap_step;
    if (step == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        return;
    }
    data->point.x = (int32_t)s_tap_x;
    data->point.y = (int32_t)s_tap_y;
    if (step == 1) {
        data->state = LV_INDEV_STATE_PRESSED;
        s_tap_step = 2;
        data->continue_reading = true;   /* deliver the release in this poll */
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        s_tap_step = 0;
        data->continue_reading = false;
    }
}

void lw_capture_attach_input(void)
{
    if (s_snap_display != NULL) {
        return;
    }
    lv_display_t *disp = lv_display_get_default();
    if (disp == NULL) {
        ESP_LOGW(TAG, "no default display yet, taps disabled");
        return;
    }
    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL) {
        ESP_LOGE(TAG, "cannot allocate a synthetic pointer device");
        return;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, sim_read_cb);
    lv_indev_set_display(indev, disp);
    s_snap_display = disp;
    ESP_LOGI(TAG, "synthetic pointer ready (%dx%d)",
             (int)lv_display_get_horizontal_resolution(disp),
             (int)lv_display_get_vertical_resolution(disp));
}

/* --------------------------------------------------------------- base64 --- */

static void emit_base64(const uint8_t *src, size_t len)
{
    char line[CAP_B64_PER_LINE + 4];
    size_t col = 0;
    size_t i = 0;
    while (i + 2 < len) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
        line[col++] = B64[(v >> 18) & 0x3F];
        line[col++] = B64[(v >> 12) & 0x3F];
        line[col++] = B64[(v >> 6) & 0x3F];
        line[col++] = B64[v & 0x3F];
        i += 3;
        if (col >= CAP_B64_PER_LINE) {
            line[col] = '\0';
            fputs(line, stdout);
            fputc('\n', stdout);
            col = 0;
        }
    }
    if (i < len) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < len) {
            v |= (uint32_t)src[i + 1] << 8;
        }
        line[col++] = B64[(v >> 18) & 0x3F];
        line[col++] = B64[(v >> 12) & 0x3F];
        line[col++] = (i + 1 < len) ? B64[(v >> 6) & 0x3F] : '=';
        line[col++] = '=';
        line[col] = '\0';
        fputs(line, stdout);
        fputc('\n', stdout);
    }
    fflush(stdout);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* Emits a 24-bit BMP of the snapshot, rows bottom-up, halved by `scale`. */
static void emit_bmp_rgb565(const uint8_t *data, uint32_t stride, int w, int h, int scale)
{
    const int out_w = w / scale;
    const int out_h = h / scale;
    const int row = (out_w * 3 + 3) & ~3;
    const uint32_t bytes = (uint32_t)row * (uint32_t)out_h;

    uint8_t head[54];
    memset(head, 0, sizeof(head));
    head[0] = 'B';
    head[1] = 'M';
    put_le32(&head[2], 54u + bytes);
    put_le32(&head[10], 54u);
    put_le32(&head[14], 40u);
    put_le32(&head[18], (uint32_t)out_w);
    put_le32(&head[22], (uint32_t)out_h);
    put_le16(&head[26], 1);
    put_le16(&head[28], 24);
    put_le32(&head[34], bytes);

    fputs("LCAP BMP ", stdout);
    emit_base64(head, sizeof(head));

    uint8_t *line = NULL;
    line = (uint8_t *)calloc(1, (size_t)row);   /* zero padding keeps -Wmaybe-uninitialized happy */
    if (line == NULL) {
        ESP_LOGE(TAG, "no memory for a BMP row");
        fputs("LCAP FAIL rows\n", stdout);
        fflush(stdout);
        return;
    }
    fputs("LCAP DATA ", stdout);
    for (int y = out_h - 1; y >= 0; y--) {
        const uint8_t *src = data + (size_t)(y * scale) * stride;
        uint8_t *dst = line;
        for (int x = 0; x < out_w; x++) {
            const int sx = x * scale;
            const uint16_t px = (uint16_t)(src[sx * 2] | (src[sx * 2 + 1] << 8));
            const uint8_t r = (uint8_t)((((px >> 11) & 0x1F) * 255) / 31);
            const uint8_t g = (uint8_t)((((px >> 5) & 0x3F) * 255) / 63);
            const uint8_t b = (uint8_t)(((px & 0x1F) * 255) / 31);
            dst[0] = b;   /* BMP stores BGR */
            dst[1] = g;
            dst[2] = r;
            dst += 3;
        }
        emit_base64(line, (size_t)row);
    }
    free(line);
    fputs("LCAP END\n", stdout);
    fflush(stdout);
}

/* -------------------------------------------------------------- commands --- */

static int cmd_shot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!bsp_display_lock(5000)) {
        ESP_LOGE(TAG, "LVGL lock timeout, screenshot skipped");
        fputs("LCAP FAIL lock\n", stdout);
        fflush(stdout);
        return 1;
    }
    lv_display_t *disp = lv_display_get_default();
    if (disp == NULL) {
        bsp_display_unlock();
        fputs("LCAP FAIL nodisp\n", stdout);
        fflush(stdout);
        return 1;
    }
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bsp_display_unlock();
    if (buf == NULL) {
        ESP_LOGE(TAG, "snapshot failed");
        fputs("LCAP FAIL snapshot\n", stdout);
        fflush(stdout);
        return 1;
    }
    s_last_w = (int)buf->header.w;
    s_last_h = (int)buf->header.h;
    ESP_LOGI(TAG, "snapshot %dx%d stride %u", s_last_w, s_last_h,
             (unsigned)buf->header.stride);
    emit_bmp_rgb565(buf->data, buf->header.stride, s_last_w, s_last_h, CAP_SCALE);
    lv_draw_buf_destroy(buf);
    return 0;
}

static int cmd_tap(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: tap <x> <y>\n");
        return 1;
    }
    s_tap_x = atoi(argv[1]);
    s_tap_y = atoi(argv[2]);
    s_tap_step = 1;
    printf("tap %d %d queued\n", (int)s_tap_x, (int)s_tap_y);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("heap free %u B, min %u B, psram %u B\n",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
        printf("display %dx%d, last shot %dx%d\n",
               (int)lv_display_get_horizontal_resolution(disp),
               (int)lv_display_get_vertical_resolution(disp),
               s_last_w, s_last_h);
    }
    return 0;
}

bool lw_capture_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "lw>";
    repl_cfg.max_cmdline_length = 64;
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if (esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl) != ESP_OK) {
        ESP_LOGW(TAG, "console REPL unavailable");
        return false;
    }
    const esp_console_cmd_t c_shot = { .command = "shot", .help = "capture the screen as a base64 BMP",
                                       .func = &cmd_shot };
    const esp_console_cmd_t c_tap = { .command = "tap", .help = "tap <x> <y>", .func = &cmd_tap };
    const esp_console_cmd_t c_stat = { .command = "status", .help = "heap and display state",
                                       .func = &cmd_status };
    esp_console_cmd_register(&c_shot);
    esp_console_cmd_register(&c_tap);
    esp_console_cmd_register(&c_stat);
    if (esp_console_start_repl(repl) != ESP_OK) {
        ESP_LOGW(TAG, "console REPL failed to start");
        return false;
    }
    ESP_LOGI(TAG, "console ready on UART0 (shot / tap / status)");
    return true;
}
