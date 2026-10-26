/* Boot-loop recovery policy.
 *
 * The board keeps one of these records in RTC no-init memory, which survives a
 * panic or a watchdog reset but not a power cycle. That is exactly the lifetime
 * needed: a firmware that crashes during boot reboots into the same crash
 * forever, and the user sees a screen that flashes and never comes up.
 * Counting consecutive abnormal boots and degrading the next one is what
 * turns "unusable board" into "board that comes up without the card".
 *
 * Why RTC memory and not NVS: counting a boot loop must not itself touch flash.
 * A board that panics before NVS is ready would otherwise reboot without ever
 * recording anything, which is the case this has to survive. RTC no-init also
 * clears on a power cycle, which is the correct reset: the user pulled power,
 * that is a human decision, not a crash loop.
 *
 * This file is platform-neutral on purpose. It has no ESP-IDF include, so the
 * same decision table runs in the host tests; the board file static-asserts
 * that esp_reset_reason_t still matches LW_RESET_* below.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LW_RECOVERY_MAGIC 0x4C575243u /* "LWRC" */

/* Two consecutive crashes enter safe mode: one panic is not enough evidence
 * (a one-off brownout must not cost the card), two in a row is a loop. */
#define LW_RECOVERY_SAFE_STREAK 2u

/* Reset reasons, ordered exactly like esp_reset_reason_t. */
enum {
    LW_RESET_UNKNOWN = 0,
    LW_RESET_POWERON = 1,
    LW_RESET_EXT = 2,
    LW_RESET_SW = 3,
    LW_RESET_PANIC = 4,
    LW_RESET_INT_WDT = 5,
    LW_RESET_TASK_WDT = 6,
    LW_RESET_WDT = 7,
    LW_RESET_DEEPSLEEP = 8,
    LW_RESET_BROWNOUT = 9,
    LW_RESET_SDIO = 10,
    LW_RESET_USB = 11,
    LW_RESET_JTAG = 12,
    LW_RESET_EFUSE = 13,
    LW_RESET_PWR_GLITCH = 14,
    LW_RESET_CPU_LOCKUP = 15,
};

/* 20 bytes, no pointers, safe to place in RTC no-init memory. */
typedef struct {
    uint32_t magic;          /* LW_RECOVERY_MAGIC when this record is ours */
    uint32_t check;          /* FNV-1a of the whole struct with this field zero */
    uint32_t last_uptime_ms; /* how long the previous boot had been running */
    uint16_t abnormal_streak;
    uint16_t safe_boots;  /* boots that ran degraded, for the report */
    uint16_t total_boots; /* every boot that used this record */
    uint16_t reserved;
} lw_recovery_t;

typedef struct {
    bool record_valid;      /* the RTC record was ours and its checksum matched */
    bool previous_abnormal; /* the previous boot ended in a crash */
    bool safe_mode;         /* this boot must run degraded */
    unsigned streak;        /* consecutive abnormal boots including this one */
    uint32_t previous_uptime_ms;
} lw_recovery_decision_t;

/* True for reset reasons that mean the previous boot died rather than ended.
 * Brownout and power glitch count: both are how a bad backlight load or a
 * marginal supply shows up. A plain power cycle does NOT count - it is how a
 * user recovers, and treating it as a crash would latch safe mode on a board
 * that is merely being unplugged. */
bool lw_recovery_reason_is_abnormal(uint32_t reset_reason);

/* Stable name for logs ("panic", "task-wdt", "power-on", ...). Never NULL. */
const char *lw_recovery_reason_name(uint32_t reset_reason);

/* Validate the record, classify this boot and update the counters.
 *
 * reset_reason is what esp_reset_reason() reports for THIS boot, and that call
 * answers "why did this boot start", i.e. how the previous run ended. So this
 * boot's own reason is the evidence; the record only has to carry the streak
 * across reboots.
 *
 * A record whose magic or checksum does not match is discarded rather than
 * trusted: RTC no-init memory holds arbitrary bytes after a power cycle, and a
 * garbage streak must not put a healthy board into safe mode. */
lw_recovery_decision_t lw_recovery_begin(lw_recovery_t *record, uint32_t reset_reason);

/* Record how long this boot has been up, so the next boot can log how long the
 * crashed run survived. Cheap (20 bytes, no flash); call it from a 1 s timer. */
void lw_recovery_tick(lw_recovery_t *record, uint32_t uptime_ms);

/* One line for the boot log. Always NUL-terminates. */
const char *lw_recovery_describe(const lw_recovery_t *record, const lw_recovery_decision_t *decision,
                                 char *buffer, size_t length);

#ifdef __cplusplus
}
#endif
