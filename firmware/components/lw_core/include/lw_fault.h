/* Compile-time-optional fault injection.
 *
 * The point of this file is to prove the degradation contracts that the rest of
 * the firmware already claims: a failed store write must not corrupt the old
 * record, a failed frame read must not lose the frame that is already on
 * screen, a refused asset lease must not clear the registry. Without a way to
 * make those calls fail on purpose, "handled" is only a comment.
 *
 * Two rules keep it honest:
 *   1. It is off by default. CONFIG_LW_FAULT_INJECT=n compiles every call site
 *      to `false`, so the shipped image contains no injection code at all.
 *   2. It never decides what to do about a failure. The injected call returns
 *      the same error a real device would, and the production code path has to
 *      deal with it exactly as it would in the field.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Enabled by CONFIG_LW_FAULT_INJECT on the device, and unconditionally in the
 * host test build (which defines LW_FAULT_ENABLED on the command line). */
#if defined(CONFIG_LW_FAULT_INJECT) || defined(LW_FAULT_HOST)
#define LW_FAULT_ENABLED 1
#else
#define LW_FAULT_ENABLED 0
#endif

typedef enum {
    LW_FAULT_STORE_READ = 0,   /* the transport refuses to hand back a record */
    LW_FAULT_STORE_WRITE,      /* the transport refuses to persist one */
    LW_FAULT_CACHE_READ,       /* a frame read from the card fails */
    LW_FAULT_ASSET_ACQUIRE,    /* a frame lease is refused */
    LW_FAULT_RESOURCE_VERIFY,  /* the card payload fails verification */
    LW_FAULT_MEDIUM_MOUNT,     /* the card cannot be mounted */
    LW_FAULT_COUNT
} lw_fault_point_t;

#if LW_FAULT_ENABLED

/* Arms one point. times == 0 means "every call until cleared". */
void lw_fault_arm(lw_fault_point_t point, unsigned times);

/* Disarms everything and zeroes the counters. */
void lw_fault_clear(void);

/* True when this call must fail. Consumes one shot. Never called when the
 * module is disabled. */
bool lw_fault_should_fail(lw_fault_point_t point);

/* Shots left for one point (0 when disarmed). */
unsigned lw_fault_remaining(lw_fault_point_t point);

/* Total injected failures since the last clear, for the test summary. */
unsigned lw_fault_total(void);

#else /* !LW_FAULT_ENABLED */

/* The disabled form is a macro, not a function, so the call site disappears. */
#define lw_fault_arm(point, times) ((void)0)
#define lw_fault_clear() ((void)0)
#define lw_fault_should_fail(point) false
#define lw_fault_remaining(point) 0u
#define lw_fault_total() 0u

#endif /* LW_FAULT_ENABLED */

/* Stable name for test output; available in both builds. */
const char *lw_fault_point_name(lw_fault_point_t point);

#ifdef __cplusplus
}
#endif
