/*
 * On-board acceptance evidence: screen snapshots and synthetic taps.
 *
 * The snapshot is taken through LVGL's own snapshot API, so it shows exactly
 * what the UI renders (the RGB panel path has no flush callback to wrap).
 * It never changes rendering, never allocates in the render path and does
 * nothing until a console command asks for it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the synthetic pointer device used by "tap". Call once, after the
 * UI exists and on the LVGL task. */
void lw_capture_attach_input(void);

/* Starts the UART0 console (shot / tap / status). Does not touch LVGL. */
bool lw_capture_console_start(void);

#ifdef __cplusplus
}
#endif
