/* Fault injection, compiled out unless explicitly enabled.
 *
 * SPDX-License-Identifier: MIT */
#if defined(ESP_PLATFORM)
/* lw_fault.h decides whether the feature is on from CONFIG_LW_FAULT_INJECT, and
 * ESP-IDF does not auto-include sdkconfig.h in a plain C file. It must therefore
 * come FIRST: without it this translation unit compiles the disabled branch
 * while every caller (which does include sdkconfig.h) compiles the enabled one,
 * and the link fails with "undefined reference to lw_fault_should_fail" - which
 * is exactly what happened on the first diag-e build. */
#include "sdkconfig.h"
#endif

#include "lw_fault.h"

#include <stddef.h>

const char *lw_fault_point_name(lw_fault_point_t point)
{
    switch (point) {
    case LW_FAULT_STORE_READ: return "store-read";
    case LW_FAULT_STORE_WRITE: return "store-write";
    case LW_FAULT_CACHE_READ: return "cache-read";
    case LW_FAULT_ASSET_ACQUIRE: return "asset-acquire";
    case LW_FAULT_RESOURCE_VERIFY: return "resource-verify";
    case LW_FAULT_MEDIUM_MOUNT: return "medium-mount";
    default: return "unknown";
    }
}

#if LW_FAULT_ENABLED

#include <string.h>

/* A fixed table, not a linked list: the record must not allocate, because one
 * of the things under test is allocation failure. */
typedef struct {
    unsigned remaining; /* 0 = disarmed */
    unsigned fired;
} fault_slot_t;

static fault_slot_t s_slots[LW_FAULT_COUNT];
static unsigned s_total;

static bool valid(lw_fault_point_t point)
{
    return (int)point >= 0 && point < (int)LW_FAULT_COUNT;
}

void lw_fault_arm(lw_fault_point_t point, unsigned times)
{
    if (!valid(point)) return;
    /* 0 from the caller means "every call until cleared"; the slot stores 0 for
     * "disarmed", so the two must not share an encoding. */
    s_slots[point].remaining = (times == 0) ? 0xFFFFFFFFu : times;
}

void lw_fault_clear(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_total = 0;
}

bool lw_fault_should_fail(lw_fault_point_t point)
{
    if (!valid(point)) return false;
    fault_slot_t *slot = &s_slots[point];
    if (slot->remaining == 0) return false;
    /* 0 is the "every call" encoding, so a shot is only consumed by a finite
     * budget. A test that arms 3 sees exactly three failures, never four. */
    if (slot->remaining != 0xFFFFFFFFu) {
        slot->remaining--;
        if (slot->remaining == 0) slot->fired++;
    }
    s_total++;
    return true;
}

unsigned lw_fault_remaining(lw_fault_point_t point)
{
    if (!valid(point)) return 0;
    return s_slots[point].remaining == 0xFFFFFFFFu ? 0xFFFFFFFFu : s_slots[point].remaining;
}

unsigned lw_fault_total(void)
{
    return s_total;
}

#endif /* LW_FAULT_ENABLED */
