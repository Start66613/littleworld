/* NVS transport for lw_store records. ESP-IDF only.
 *
 * Never erases: a full or foreign-version NVS partition is reported as
 * unavailable so existing pet, anniversary and RPG data survive. Each consumer
 * gets its own namespace and key, which keeps the legacy "littleworld" blob
 * untouched and independent.
 *
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lw_store.h"

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* Outcome of the one-time NVS bring-up probe. */
typedef enum {
    LW_NVS_READY = 0,
    LW_NVS_UNAVAILABLE_NO_FREE_PAGES, /* partition full: do not erase automatically */
    LW_NVS_UNAVAILABLE_NEW_VERSION,   /* written by a newer IDF: do not erase automatically */
    LW_NVS_UNAVAILABLE_ERROR
} lw_nvs_state_t;

typedef struct {
    const char *namespace_name; /* <= 15 characters */
    const char *key;            /* <= 15 characters */
} lw_store_nvs_key_t;

/* Initialises NVS exactly once and never calls nvs_flash_erase(). */
lw_nvs_state_t lw_store_nvs_begin(void);
lw_nvs_state_t lw_store_nvs_state(void);
/* Human readable reason, safe to log. NULL when ready. */
const char *lw_store_nvs_reason(void);
/* Raw esp_err_t of the last failing call, 0 when none. */
esp_err_t lw_store_nvs_last_error(void);

/* Transport bound to one namespace/key pair. The key must outlive the io struct.
 * The returned pointer refers to one shared static buffer, so copy the struct
 * (as lw_boot.c does) before asking for a second key. */
const lw_store_io_t *lw_store_nvs_io(const lw_store_nvs_key_t *key);

#endif /* ESP_PLATFORM */
