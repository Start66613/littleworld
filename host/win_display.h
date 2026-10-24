/* Little World - Windows host window + LVGL display driver (Win32 + GDI).
 *
 * No external dependency: LVGL renders into an RGB565 memory buffer and each
 * flush converts the damaged area into a 32-bit DIB which is blitted to the
 * window. Mouse input is fed to an LVGL pointer indev.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#include "lvgl.h"

#define LW_HOST_WIDTH  1024
#define LW_HOST_HEIGHT 600

typedef struct {
    int x;
    int y;
    int pressed;
} lw_host_pointer_t;

/** Create the LVGL display (1024x600, full-frame buffer) and the pointer indev. */
lv_display_t *lw_host_display_init(void);

/** Pointer state that the indev read callback samples. */
lw_host_pointer_t *lw_host_pointer(void);

/** Copy the current LVGL frame into a BMP file. Returns 0 on success. */
int lw_host_screenshot_bmp(const char *path);

/** Open the 1024x600 window and run until it is closed. Returns the process exit code. */
int lw_host_run_window(void);

/** Process events once (used by the window loop); returns 0 when the window closed. */
int lw_host_pump_once(void);
