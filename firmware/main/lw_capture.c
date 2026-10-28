/*
 * On-board acceptance evidence: LVGL snapshots over the UART console.
 *
 *   shot [name]   render the active screen to a BMP, emit it base64-encoded
 *   tap X Y       synthesise one press/release at screen coordinates
 *   status        print heap, display size and the last captured frame
 *   sched         OS tick gaps: whether the LVGL task is being starved
 *
 * Protocol, one record per line, every line numbered so a lost line can be
 * recovered by re-running the command and merging by index:
 *   LCAP <index> <base64 chunk of 72 source bytes>
 *   LCAP END <line count>
 * The first chunk carries the 54-byte BMP header, the rest are pixel rows.
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

#include "sdkconfig.h"

#include "bsp/esp-bsp.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lw_diag_board.h"
#include "lw_content.h"
#include "lw_content_boot.h"
#include "lw_time_boot.h"
#include "lw_os.h"
#include "lw_os_design.h"
#include "esp_lv_adapter.h"
#if CONFIG_LW_FAULT_INJECT
#include "lw_fault.h"
#endif

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

static unsigned s_line;   /* index of the next LCAP line of the current shot */

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
            printf("LCAP %u %s\n", s_line++, line);
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
        printf("LCAP %u %s\n", s_line++, line);
    } else if (col > 0) {
        /* Flush the tail of a block whose length is a multiple of 3: without
           this the last 24 bytes of every 1536-byte row silently vanished and
           the emitted BMP was 7 KB shorter than the header claimed. */
        line[col] = '\0';
        printf("LCAP %u %s\n", s_line++, line);
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

    emit_base64(head, sizeof(head));

    uint8_t *line = NULL;
    line = (uint8_t *)calloc(1, (size_t)row);   /* zero padding keeps -Wmaybe-uninitialized happy */
    if (line == NULL) {
        ESP_LOGE(TAG, "no memory for a BMP row");
        fputs("LCAP FAIL rows\n", stdout);
        fflush(stdout);
        return;
    }
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
    printf("LCAP END %u\n", s_line);
    fflush(stdout);
}

/* -------------------------------------------------------------- commands --- */

static int cmd_shot(int argc, char **argv)
{
    int scale = CAP_SCALE;
    if (argc > 1) {
        int requested = atoi(argv[1]);
        if (requested >= 1 && requested <= 4) {
            scale = requested;
        }
    }
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
    s_line = 0;
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
    emit_bmp_rgb565(buf->data, buf->header.stride, s_last_w, s_last_h, scale);
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
    /* The page/overlay pair is what a board test needs to prove a tap actually
     * navigated. Screenshots are 40 s over the console, and the dock retracts
     * after 4.5 s, so both states are unobservable by picture alone. */
    printf("page %d, overlay %d, dock %s\n",
           (int)lw_os_page(), (int)lw_os_overlay(),
           lw_os_dock_expanded() ? "open" : "closed");
    return 0;
}

#if CONFIG_LW_DIAG_CONSOLE
/* Read-only diagnostics. These commands never write anything: not flash, not
 * NVS, not the card, not the radio. They exist so a board test can ask for the
 * numbers instead of guessing from a boot log. */
static int cmd_diag(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!lw_diag_board_ready()) {
        printf("diag: board wiring not initialised yet\n");
        return 1;
    }
    lw_diag_report_t report;
    lw_diag_board_report(&report);
    static char text[CONFIG_LW_DIAG_MAX_TEXT];
    lw_diag_text(&report, text, sizeof(text));
    fputs(text, stdout);
    fflush(stdout);
    return 0;
}

static int cmd_diagjson(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!lw_diag_board_ready()) {
        printf("diagjson: board wiring not initialised yet\n");
        return 1;
    }
    lw_diag_report_t report;
    lw_diag_board_report(&report);
    static char json[2048];
    if (lw_diag_json(&report, json, sizeof(json)) == 0u) {
        printf("diagjson: report does not fit the buffer\n");
        return 1;
    }
    /* One line, prefixed so the PC collector can pick it out of the console
     * echo without parsing anything else. */
    printf("LWDJ %s\n", json);
    fflush(stdout);
    return 0;
}

static int cmd_diagerr(int argc, char **argv)
{
    /* Records a marker in the report so a later diag read shows what the
     * operator was doing. Writes RAM only. */
    char text[96];
    int used = snprintf(text, sizeof(text), "operator mark");
    if (argc >= 2) {
        used = snprintf(text, sizeof(text), "operator mark: %s", argv[1]);
    }
    (void)used;
    lw_diag_note_error(text, (uint32_t)(esp_timer_get_time() / 1000));
    printf("diag: %s\n", text);
    return 0;
}

static int cmd_content(int argc, char **argv)
{
    /* Read-only view of the boot-time content load. The file itself is on the
     * card; this only reports what the last parse accepted, so an operator can
     * tell "no card" from "card present but the file is wrong". */
    (void)argc;
    (void)argv;
    printf("%s\n", lw_content_boot_report());
    const lw_content_report_t *report = lw_content_report();
    printf("  status=%s version=%u memories=%u photos=%u letters=%u dialogues=%u lines=%u bytes=%u\n",
           lw_content_status_text(report->status), report->content_version, report->memories,
           report->photos, report->letters, report->dialogues, report->lines, report->bytes);
    if (report->status != LW_CONTENT_OK) {
        printf("  rejected at line %u\n", report->rejected);
    }
    if (report->source[0]) {
        printf("  source=%s\n", report->source);
    }
    fflush(stdout);
    return 0;
}

static int cmd_clock(int argc, char **argv)
{
    /* The effective clock, not the raw service state: source, whether it may be
     * presented as the real time, phase and the next boundary. A console that
     * only printed the SNTP state would leave "what does the screen show"
     * unanswered, which is the question an operator actually has. */
    (void)argc;
    (void)argv;
    printf("%s\n", lw_time_boot_report());
    fflush(stdout);
    return 0;
}

static int cmd_anim(int argc, char **argv)
{
    /* Actor animation truth: which clip is playing, which frame index is
     * displayed, and how far the model clock has advanced. "sprites=2 running=2"
     * in diag only says the sprite objects are enabled; it cannot tell a frozen
     * animation (frame index stuck) from one whose frames happen to look alike. */
    if (argc > 1) {
        /* Repair path: the saved settings can carry animations=0, which freezes
         * every actor at frame 0 and makes all page transitions snap instead of
         * animating. That state survives reflashing because NVS is not erased,
         * so a console switch is the only way to undo it without a full reset. */
        if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0) {
            bool on = strcmp(argv[1], "on") == 0;
            lw_os_animations_set(on);
            printf("anim: animations %s (saved)\n", on ? "on" : "off");
        } else {
            printf("usage: anim [on|off]\n");
        }
        fflush(stdout);
        return 0;
    }
    const lw_duo_t *duo = lw_os_duo_state();
    /* Motion mode 2 (OFF) makes update_actors() feed seek() a constant time of 0
     * for an idle actor, so the frame index never leaves 0 even though the model
     * clock keeps advancing. Printing it here separates "the animation is frozen"
     * from "the animation was deliberately switched off in settings". */
    printf("anim: motion_mode=%u animations_enabled=%u raw_mode=%u\n", (unsigned)lw_os_motion_get(),
           lw_os_animations_enabled(), lw_os_motion_mode_raw());
    for (unsigned i = 0; i < 2; i++) {
        const char *clip = lw_os_actor_clip(i);
        uint16_t frame = lw_os_actor_frame(i);
        printf("anim %u: clip=%s frame=%u motion=%u animation_ms=%u phase=%u\n", (unsigned)i,
               clip ? clip : "(none)", (unsigned)frame,
               duo ? (unsigned)duo->actors[i].motion : 0u,
               duo ? (unsigned)duo->actors[i].animation_ms : 0u,
               duo ? (unsigned)duo->phase : 0u);
    }
    fflush(stdout);
    return 0;
}

static int cmd_rpg(int argc, char **argv)
{
    /* The acceptance question for RPG motion is not "did the tile counter move"
     * but "did the pixel position on the glass move continuously". So print all
     * three layers at once: the logical tile, the interpolated tile point, and
     * the x/y the LVGL object actually carries. A teleporting actor shows a
     * screen jump equal to a whole tile (56 px horizontally, 22 px vertically);
     * a smooth one never moves more than a fraction of that per frame. */
    (void)argc;
    (void)argv;
    const lw_rpg_state_t *state = lw_os_rpg_state();
    if (!state) {
        printf("rpg: page not open\n");
        fflush(stdout);
        return 0;
    }
    for (int i = 0; i < LW_RPG_MAX_ACTORS; i++) {
        float fx = 0.0f, fy = 0.0f;
        lw_rpg_actor_point(state, i, &fx, &fy);
        lv_obj_t *o = lw_os_rpg_actor((unsigned)i);
        printf("rpg %d: tile=%d,%d point=%.2f,%.2f screen=%d,%d moving=%d\n",
               i, (int)state->actors[i].pos.x, (int)state->actors[i].pos.y,
               (double)fx, (double)fy,
               o ? (int)lv_obj_get_x(o) : -1, o ? (int)lv_obj_get_y(o) : -1,
               (int)state->actors[i].moving);
    }
    fflush(stdout);
    return 0;
}

static int cmd_sched(int argc, char **argv)
{
    /* How often the OS tick really ran. The OS timer is registered for 20 ms;
     * if the average gap is 120 ms the LVGL task is starved and every animation
     * is being sampled that much slower - which is what "frames are missing"
     * actually looks like on the panel. "sched reset" starts a fresh window. */
    if (argc >= 2 && strcmp(argv[1], "reset") == 0) {
        lw_os_tick_stats_reset();
        printf("sched: reset ok\n");
        fflush(stdout);
        return 0;
    }
    uint32_t calls = 0, worst = 0, avg = 0;
    lw_os_tick_stats(&calls, &worst, &avg);
    printf("sched: ticks=%u avg_gap=%u ms max_gap=%u ms (timer period 20 ms)\n",
           (unsigned)calls, (unsigned)avg, (unsigned)worst);
    fflush(stdout);
    return 0;
}

static int cmd_boot(int argc, char **argv)
{
    /* Boot animation evidence. The overlay is designed for ~2000 ms in FULL
     * motion mode (1740 ms hold + 260 ms fade); what the panel actually showed
     * is this. frames/elapsed is the rate the overlay was really animated at:
     * 100 frames over 2000 ms means it ran at the intended 20 ms cadence, and
     * 20 frames means the scheduler stretched a 2 s intro into 2 s of stutter.
     * Numbers freeze when the overlay is deleted, so this can be read at any
     * time after boot. "boot" while it is still up reports live values. */
    (void)argc;
    (void)argv;
    lw_os_boot_stats_t st = {0};
    lw_os_boot_stats(&st);
    unsigned rate = st.elapsed_ms ? (unsigned)((uint64_t)st.frames * 1000u / st.elapsed_ms) : 0u;
    printf("boot: frames=%u elapsed=%u ms visible=%u ms fps=%u active=%d skipped=%d\n",
           (unsigned)st.frames, (unsigned)st.elapsed_ms, (unsigned)st.visible_ms,
           rate, st.active ? 1 : 0, st.skipped ? 1 : 0);
    fflush(stdout);
    return 0;
}

static int cmd_fps(int argc, char **argv)
{
    /* Real frame rate, measured by the LVGL adapter once a second. Without this
     * a performance claim is only an impression: the same screen can feel
     * "slow" because of one 300 ms repaint while the average is fine, or the
     * other way round. Reset with "fps reset" to measure a window. */
    lv_display_t *disp = s_snap_display ? s_snap_display : lv_display_get_default();
    if (disp == NULL) {
        printf("fps: no display\n");
        return 1;
    }
#if CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS
    if (argc >= 2 && strcmp(argv[1], "reset") == 0) {
        esp_err_t reset = esp_lv_adapter_fps_stats_reset(disp);
        printf("fps: reset %s\n", reset == ESP_OK ? "ok" : "failed");
        return reset == ESP_OK ? 0 : 1;
    }
    uint32_t fps = 0;
    esp_err_t err = esp_lv_adapter_get_fps(disp, &fps);
    if (err != ESP_OK) {
        printf("fps: unavailable (%s)\n", esp_err_to_name(err));
        return 1;
    }
    printf("fps: %u\n", (unsigned)fps);
#else
    (void)argc;
    (void)argv;
    printf("fps: statistics not compiled in (CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS)\n");
    return 1;
#endif
    fflush(stdout);
    return 0;
}
#endif /* CONFIG_LW_DIAG_CONSOLE */

#if CONFIG_LW_FAULT_INJECT
/* Fault injection console, only in a build that compiled the points in. The
 * command is intentionally awkward to type by accident and prints what it did,
 * because the whole point is that nobody mistakes an injected failure for a
 * real one in a log. */
static int cmd_fault(int argc, char **argv)
{
    if (argc < 2) {
        printf("fault: points are");
        for (int i = 0; i < (int)LW_FAULT_COUNT; i++) {
            printf(" %s", lw_fault_point_name((lw_fault_point_t)i));
        }
        printf("\nfault: <point> <times> | clear | status\n");
        return 1;
    }
    if (!strcmp(argv[1], "clear")) {
        lw_fault_clear();
        printf("fault: all points cleared\n");
        return 0;
    }
    if (!strcmp(argv[1], "status")) {
        printf("fault: %u injected since clear\n", lw_fault_total());
        for (int i = 0; i < (int)LW_FAULT_COUNT; i++) {
            printf("fault: %-16s remaining=%u\n", lw_fault_point_name((lw_fault_point_t)i),
                   lw_fault_remaining((lw_fault_point_t)i));
        }
        return 0;
    }
    int point = -1;
    for (int i = 0; i < (int)LW_FAULT_COUNT; i++) {
        if (!strcmp(argv[1], lw_fault_point_name((lw_fault_point_t)i))) {
            point = i;
        }
    }
    if (point < 0) {
        printf("fault: unknown point '%s'\n", argv[1]);
        return 1;
    }
    unsigned times = 1;
    if (argc >= 3) {
        times = (unsigned)strtoul(argv[2], NULL, 10);
    }
    lw_fault_arm((lw_fault_point_t)point, times);
    printf("fault: %s armed (%s)\n", argv[1], times == 0 ? "every call until cleared" : "limited shots");
    return 0;
}
#endif /* CONFIG_LW_FAULT_INJECT */

bool lw_capture_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "lw>";
    repl_cfg.max_cmdline_length = 64;
    /* lv_snapshot_take renders a whole screen through the software renderer and
       overflowed the default 4 KiB console stack on the real board (stack
       protection fault in task "console_repl"), so give the REPL real room. */
    repl_cfg.task_stack_size = 16384;
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
#if CONFIG_LW_DIAG_CONSOLE
    const esp_console_cmd_t c_diag = { .command = "diag", .help = "full read-only diagnostics block",
                                       .func = &cmd_diag };
    const esp_console_cmd_t c_diagjson = { .command = "diagjson", .help = "same report as one JSON line",
                                           .func = &cmd_diagjson };
    const esp_console_cmd_t c_diagerr = { .command = "diagerr", .help = "diagerr <text>: mark an operator note",
                                          .func = &cmd_diagerr };
    const esp_console_cmd_t c_content = { .command = "content",
                                          .help = "what the boot-time content file contained",
                                          .func = &cmd_content };
    const esp_console_cmd_t c_clock = { .command = "clock",
                                        .help = "effective clock: source, phase, next boundary",
                                        .func = &cmd_clock };
    const esp_console_cmd_t c_fps = { .command = "fps",
                                      .help = "measured frame rate (fps reset to start a window)",
                                      .func = &cmd_fps };
    const esp_console_cmd_t c_sched = { .command = "sched",
                                        .help = "OS tick gaps: is the LVGL task starved?",
                                        .func = &cmd_sched };
    const esp_console_cmd_t c_rpg = { .command = "rpg",
                                      .help = "rpg: tile, interpolated point and real screen position",
                                      .func = &cmd_rpg };
    const esp_console_cmd_t c_boot = { .command = "boot",
                                       .help = "boot: overlay frames, wall clock and real rate",
                                       .func = &cmd_boot };
    const esp_console_cmd_t c_anim = { .command = "anim",
                                       .help = "actor clip, frame index and model clock",
                                       .func = &cmd_anim };
    esp_console_cmd_register(&c_diag);
    esp_console_cmd_register(&c_diagjson);
    esp_console_cmd_register(&c_diagerr);
    esp_console_cmd_register(&c_content);
    esp_console_cmd_register(&c_clock);
    esp_console_cmd_register(&c_fps);
    esp_console_cmd_register(&c_sched);
    esp_console_cmd_register(&c_rpg);
    esp_console_cmd_register(&c_boot);
    esp_console_cmd_register(&c_anim);
#endif
#if CONFIG_LW_FAULT_INJECT
    const esp_console_cmd_t c_fault = { .command = "fault",
                                        .help = "fault <point> <times> | clear | status",
                                        .func = &cmd_fault };
    esp_console_cmd_register(&c_fault);
#endif
    if (esp_console_start_repl(repl) != ESP_OK) {
        ESP_LOGW(TAG, "console REPL failed to start");
        return false;
    }
    ESP_LOGI(TAG, "console ready on UART0 (shot / tap / status / diag / diagjson / content / clock / fps / sched)");
    return true;
}
