/* Little World - boot-time wiring of the effective clock (lw_day) to the rest
 * of the system.
 *
 * Three clocks exist in this firmware and none of them may be presented as
 * something it is not:
 *   1. the hardware hook (lw_hw_time_get) - there is no RTC battery, so it says
 *      "no" and the UI falls back to the labelled demo clock;
 *   2. the SNTP service (lw_time_service in lw_net) - real seconds, but only
 *      after a successful sync, and it goes STALE when the link drops;
 *   3. the demo rehearsal clock - fixed at 19:30, always labelled as a demo.
 *
 * lw_day (lw_engine, host tested) is the ONE place that decides which of them is
 * in effect, what the day phase is and which theme follows. This file is the
 * only glue: it feeds the service state in and answers the platform hook out.
 *
 * Threading: lw_time_boot_update() is called from the LVGL task (the same timer
 * that publishes capabilities), and lw_time_boot_get_tm() is called from the
 * same task through lw_platform, so no lock is needed and no reader can observe
 * a half-applied update.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "lw_day.h"
#include "lw_services.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise to the demo clock. Call once, before the UI starts. */
void lw_time_boot_init(void);

/* Apply the service snapshot. Idempotent: calling it with the same state every
 * second must not reset the phase or the hysteresis timer. */
void lw_time_boot_update(const lw_net_status_t *status, uint32_t now_ms);

/* The platform wall-clock hook. Returns false unless the effective source is
 * trusted (REAL or MANUAL); the caller then shows the demo clock. */
bool lw_time_boot_get_tm(struct tm *out);

/* Read-only view for the UI and the console. */
const lw_day_t *lw_time_boot_day(void);

/* One-line human report: source, clock, phase, theme, next boundary. */
const char *lw_time_boot_report(void);

/* Short single-line form for the diagnostics snapshot: "19:30 demo, not
 * trusted, night, theme 1". Shares the state with lw_time_boot_report() but
 * does not touch its buffer, so a console command and the diagnostics can call
 * it in either order. */
void lw_time_boot_line(char *out, size_t len);

#ifdef __cplusplus
}
#endif
