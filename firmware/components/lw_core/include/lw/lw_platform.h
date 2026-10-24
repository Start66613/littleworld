/* Little World - platform abstraction.
 *
 * The UI never calls ESP-IDF or Win32 directly: the program that hosts it
 * (firmware app_main or the Windows simulator) installs an implementation.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Wall clock. Returns false when the device has no trustworthy time yet. */
    bool (*get_time)(struct tm *out);
    /** Read a named blob (save file). Returns false when absent. */
    bool (*storage_read)(const char *key, void *buffer, size_t *length);
    /** Write a named blob. */
    bool (*storage_write)(const char *key, const void *buffer, size_t length);
    /** Monotonic seconds since boot (used by the demo clock). May be NULL. */
    uint32_t (*uptime_seconds)(void);
    /** Optional human readable log line. */
    void (*log)(const char *message);
} lw_platform_t;

void lw_platform_set(const lw_platform_t *platform);
const lw_platform_t *lw_platform_get(void);

#ifdef __cplusplus
}
#endif
