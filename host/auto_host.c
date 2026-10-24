/* Little World - scripted regression run for the Windows build.
 *
 * Drives the real UI (same code as the firmware) with synthetic touches, writes
 * screenshots and checks that switching pages does not grow the object tree.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "lvgl.h"
#include "lw_app_state.h"
#include "win_display.h"

typedef enum { STEP_WAIT, STEP_CLICK, STEP_SWIPE, STEP_CLICK_TEXT, STEP_SHOT, STEP_EXPECT_APP } step_kind_t;

typedef struct {
    step_kind_t kind;
    int a;
    int b;
    int c;
    int d;
    int ms;
    const char *text;
} step_t;

static const step_t s_steps[] = {
    {STEP_WAIT, 0, 0, 0, 0, 900, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "01-home"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "去陪陪它"},
    {STEP_WAIT, 0, 0, 0, 0, 900, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "02-pet"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "喂食"},
    {STEP_WAIT, 0, 0, 0, 0, 500, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "03-pet-fed"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "玩耍"},
    {STEP_WAIT, 0, 0, 0, 0, 500, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "04-pet-playing"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "首页"},
    {STEP_WAIT, 0, 0, 0, 0, 700, NULL},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "回忆"},
    {STEP_WAIT, 0, 0, 0, 0, 900, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "05-anniversary"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "时间线"},
    {STEP_WAIT, 0, 0, 0, 0, 600, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "06-timeline"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "惊喜盒子"},
    {STEP_WAIT, 0, 0, 0, 0, 600, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "07-surprise"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "打开盒子"},
    {STEP_WAIT, 0, 0, 0, 0, 700, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "08-surprise-open"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "小地图"},
    {STEP_WAIT, 0, 0, 0, 0, 600, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "09-map"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "家"},
    {STEP_WAIT, 0, 0, 0, 0, 600, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "10-place"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "返回"},
    {STEP_WAIT, 0, 0, 0, 0, 400, NULL},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "相册"},
    {STEP_WAIT, 0, 0, 0, 0, 700, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "11-album"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "音乐"},
    {STEP_WAIT, 0, 0, 0, 0, 700, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "12-music"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "游戏"},
    {STEP_WAIT, 0, 0, 0, 0, 700, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "13-arcade"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "2048"},
    {STEP_WAIT, 0, 0, 0, 0, 700, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "14-2048"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "返回游戏中心"},
    {STEP_WAIT, 0, 0, 0, 0, 500, NULL},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "设置"},
    {STEP_WAIT, 0, 0, 0, 0, 800, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "15-settings"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "首页"},
    {STEP_WAIT, 0, 0, 0, 0, 600, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "16-home-again"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "游戏"},
    {STEP_WAIT, 0, 0, 0, 0, 500, NULL},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "贪吃蛇"},
    {STEP_WAIT, 0, 0, 0, 0, 400, NULL},
    {STEP_SWIPE, 512, 430, 512, 240, 0, NULL},
    {STEP_WAIT, 0, 0, 0, 0, 150, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "17-snake"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "返回游戏中心"},
    {STEP_WAIT, 0, 0, 0, 0, 500, NULL},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "打砖块"},
    {STEP_WAIT, 0, 0, 0, 0, 700, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "18-breakout-ready"},
    {STEP_CLICK, 512, 400, 0, 0, 0, NULL},
    {STEP_WAIT, 0, 0, 0, 0, 1400, NULL},
    {STEP_SHOT, 0, 0, 0, 0, 0, "19-breakout-playing"},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "返回游戏中心"},
    {STEP_WAIT, 0, 0, 0, 0, 500, NULL},
    {STEP_CLICK_TEXT, 0, 0, 0, 0, 0, "首页"},
    {STEP_WAIT, 0, 0, 0, 0, 500, NULL},
};

static void pump(int ms)
{
    int elapsed = 0;
    while (elapsed < ms) {
        lv_timer_handler();
        Sleep(5);
        elapsed += 5;
    }
}

static lv_obj_t *find_label(lv_obj_t *root, const char *text)
{
    uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(root, i);
        if (lv_obj_check_type(child, &lv_label_class) && strcmp(lv_label_get_text(child), text) == 0) {
            return child;
        }
        lv_obj_t *found = find_label(child, text);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static void click_at(int x, int y)
{
    lw_host_pointer_t *pointer = lw_host_pointer();
    pointer->x = x;
    pointer->y = y;
    pointer->pressed = 1;
    pump(120);
    pointer->pressed = 0;
    pump(180);
}

static void swipe(int x1, int y1, int x2, int y2)
{
    lw_host_pointer_t *pointer = lw_host_pointer();
    pointer->x = x1;
    pointer->y = y1;
    pointer->pressed = 1;
    pump(90);
    for (int step = 1; step <= 4; step++) {
        pointer->x = x1 + (x2 - x1) * step / 4;
        pointer->y = y1 + (y2 - y1) * step / 4;
        pump(40);
    }
    pointer->pressed = 0;
    pump(260);
}

static int click_text(const char *text)
{
    lv_obj_t *label = find_label(lv_screen_active(), text);
    if (label == NULL) {
        printf("WARN: no widget labelled '%s'\n", text);
        return 0;
    }
    lv_area_t area;
    lv_obj_get_coords(label, &area);
    click_at((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
    return 1;
}

static int count_objects(lv_obj_t *obj)
{
    int count = 1;
    uint32_t children = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < children; i++) {
        count += count_objects(lv_obj_get_child(obj, i));
    }
    return count;
}

int lw_auto_run(const char *output_dir)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[512];
    int failures = 0;
    int missing = 0;

    for (unsigned i = 0; i < sizeof(s_steps) / sizeof(s_steps[0]); i++) {
        const step_t *step = &s_steps[i];
        switch (step->kind) {
        case STEP_WAIT:
            pump(step->ms);
            break;
        case STEP_CLICK:
            click_at(step->a, step->b);
            break;
        case STEP_SWIPE:
            swipe(step->a, step->b, step->c, step->d);
            break;
        case STEP_CLICK_TEXT:
            if (!click_text(step->text)) {
                missing++;
            }
            break;
        case STEP_SHOT:
            snprintf(path, sizeof(path), "%s\\%s.bmp", output_dir, step->text);
            if (lw_host_screenshot_bmp(path) == 0) {
                printf("saved %s\n", path);
            } else {
                printf("FAILED to save %s\n", path);
                failures++;
            }
            break;
        case STEP_EXPECT_APP:
        default:
            break;
        }
    }

    /* lifecycle: open the pet page and come back ten times */
    int before = 0;
    click_text("首页");
    pump(600);
    before = count_objects(lv_screen_active());
    for (int i = 0; i < 10; i++) {
        click_text("去陪陪它");
        pump(400);
        click_text("首页");
        pump(400);
    }
    int after = count_objects(lv_screen_active());
    snprintf(path, sizeof(path), "%s\\20-after-cycles.bmp", output_dir);
    lw_host_screenshot_bmp(path);
    printf("objects: %d -> %d\n", before, after);
    if (before != after) {
        printf("FAIL: object growth across page switches\n");
        failures++;
    }

    if (missing > 0) {
        printf("FAIL: %d widget(s) were not found by label\n", missing);
        failures++;
    }

    printf("\nAUTO RUN: %s (%d failure(s))\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
