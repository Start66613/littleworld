/* Board-side wiring for the V4 diagnostics report.
 *
 * lw_diag_report.c is platform neutral and knows nothing about LVGL, the card
 * or the radio. This file is the other half: it fills in the providers from the
 * real subsystems, and it is the only part that may be ESP-only.
 *
 * Two rules the board taught us:
 *  - The LVGL provider is only safe on the LVGL task, so it takes the display
 *    lock. A console command runs on the console task; reading LVGL state from
 *    there without the lock is a use-after-free waiting to happen.
 *  - Nothing here allocates on the heap per call. The report is a fixed struct
 *    and the text/JSON buffers live on the caller's stack.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include "lw_diag_report.h"
#include "lw_services.h"
#include "lw_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hand the boot layer's live objects to the diagnostics. Called once, before
 * lw_diag_board_init(). Pointers must outlive the session (they are the boot
 * file's statics). */
void lw_diag_board_bind(lw_system_t *system, lw_services_t *services);

/* Registers the board providers and logs one line. Call once, after the UI and
 * the services exist (i.e. at the end of the boot path), on the LVGL task. */
void lw_diag_board_init(void);

/* True when board wiring is in place, so a console command can decide between
 * the full report and a bare allocator snapshot. */
bool lw_diag_board_ready(void);

/* Fill `out` with the full report. Safe from the console task: it takes the
 * display lock internally for the LVGL part. */
void lw_diag_board_report(lw_diag_report_t *out);

#ifdef __cplusplus
}
#endif
