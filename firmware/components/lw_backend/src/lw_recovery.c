/* Boot-loop recovery policy. No ESP-IDF, no LVGL: this is the decision table
 * the board file feeds with the real reset reason.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_recovery.h"

#include <stdio.h>
#include <string.h>

/* FNV-1a over the record with the check field zeroed. A plain hash, not a CRC,
 * on purpose: the record is 20 bytes of our own writing, and the failure this
 * must catch is "RTC memory held something else", not bit rot in flash. */
static uint32_t record_check(const lw_recovery_t *record)
{
    lw_recovery_t copy = *record;
    copy.check = 0;
    const uint8_t *p = (const uint8_t *)&copy;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(copy); i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool record_is_ours(const lw_recovery_t *record)
{
    if (record == NULL) return false;
    if (record->magic != LW_RECOVERY_MAGIC) return false;
    return record->check == record_check(record);
}

static void record_seal(lw_recovery_t *record)
{
    record->magic = LW_RECOVERY_MAGIC;
    record->check = record_check(record);
}

bool lw_recovery_reason_is_abnormal(uint32_t reset_reason)
{
    switch (reset_reason) {
    case LW_RESET_PANIC:
    case LW_RESET_INT_WDT:
    case LW_RESET_TASK_WDT:
    case LW_RESET_WDT:
    case LW_RESET_BROWNOUT:
    case LW_RESET_PWR_GLITCH:
    case LW_RESET_CPU_LOCKUP:
        return true;
    default:
        return false;
    }
}

const char *lw_recovery_reason_name(uint32_t reset_reason)
{
    switch (reset_reason) {
    case LW_RESET_POWERON: return "power-on";
    case LW_RESET_EXT: return "external-pin";
    case LW_RESET_SW: return "software";
    case LW_RESET_PANIC: return "panic";
    case LW_RESET_INT_WDT: return "int-wdt";
    case LW_RESET_TASK_WDT: return "task-wdt";
    case LW_RESET_WDT: return "watchdog";
    case LW_RESET_DEEPSLEEP: return "deep-sleep";
    case LW_RESET_BROWNOUT: return "brownout";
    case LW_RESET_SDIO: return "sdio";
    case LW_RESET_USB: return "usb";
    case LW_RESET_JTAG: return "jtag";
    case LW_RESET_EFUSE: return "efuse";
    case LW_RESET_PWR_GLITCH: return "power-glitch";
    case LW_RESET_CPU_LOCKUP: return "cpu-lockup";
    case LW_RESET_UNKNOWN: return "unknown";
    default: return "unrecognised";
    }
}

lw_recovery_decision_t lw_recovery_begin(lw_recovery_t *record, uint32_t reset_reason)
{
    lw_recovery_decision_t decision = {false, false, false, 0, 0};
    if (record == NULL) return decision;

    const bool valid = record_is_ours(record);
    decision.record_valid = valid;

    if (!valid) {
        /* First boot after a power cycle, or the record was never written.
         * Start clean: arbitrary RTC bytes are not evidence of a crash loop. */
        memset(record, 0, sizeof(*record));
        record->total_boots = 1;
        record_seal(record);
        return decision;
    }

    /* esp_reset_reason() describes why THIS boot started, so it is the verdict
     * on how the previous run ended. */
    decision.previous_abnormal = lw_recovery_reason_is_abnormal(reset_reason);
    decision.previous_uptime_ms = record->last_uptime_ms;

    if (decision.previous_abnormal) {
        if (record->abnormal_streak < 0xFFFFu) record->abnormal_streak++;
    } else {
        /* Reaching a clean boot proves the firmware can finish bring-up, so
         * the loop is over. This is what stops safe mode latching forever. */
        record->abnormal_streak = 0;
    }
    decision.streak = record->abnormal_streak;
    decision.safe_mode = record->abnormal_streak >= LW_RECOVERY_SAFE_STREAK;
    if (decision.safe_mode && record->safe_boots < 0xFFFFu) record->safe_boots++;

    if (record->total_boots < 0xFFFFu) record->total_boots++;
    record->last_uptime_ms = 0;
    record_seal(record);
    return decision;
}

void lw_recovery_tick(lw_recovery_t *record, uint32_t uptime_ms)
{
    if (record == NULL || record->magic != LW_RECOVERY_MAGIC) return;
    record->last_uptime_ms = uptime_ms;
    record_seal(record);
}

const char *lw_recovery_describe(const lw_recovery_t *record, const lw_recovery_decision_t *decision,
                                 char *buffer, size_t length)
{
    static const char kUnavailable[] = "recovery: not tracked";
    if (buffer == NULL || length == 0) return kUnavailable;
    if (record == NULL || decision == NULL || !record_is_ours(record)) {
        (void)snprintf(buffer, length, "%s", kUnavailable);
        return buffer;
    }
    (void)snprintf(buffer, length,
                   "recovery: boot %u, previous %s after %u ms, streak %u, safe mode %s (%u so far)",
                   (unsigned)record->total_boots,
                   decision->previous_abnormal ? "crashed" : "ended cleanly",
                   (unsigned)decision->previous_uptime_ms, (unsigned)decision->streak,
                   decision->safe_mode ? "on" : "off", (unsigned)record->safe_boots);
    return buffer;
}
