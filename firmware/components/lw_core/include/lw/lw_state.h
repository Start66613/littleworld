/* Little World - user settings and persisted state record.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lw/lw_pet.h"
#include "lw/lw_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t sound;      /* 0/1 */
    uint8_t animations; /* 0/1 */
    uint8_t theme;      /* 0 cream, 1 pink, 2 lavender */
    uint8_t demo_clock; /* 1 = the UI uses its demo clock */
} lw_settings_t;

typedef struct {
    lw_settings_t settings;
    lw_pet_t pet;
    lw_date_t anniversary;   /* the day the story started (editable) */
    uint8_t surprise_unlocked;
    uint8_t map_visited;     /* bitmask of visited map places */
    uint8_t music_volume;    /* 0..100 */
    uint8_t reserved;
    uint32_t arcade_best[3]; /* 2048 / snake / breakout */
} lw_state_t;

void lw_settings_defaults(lw_settings_t *settings);

/** Fill a state with defaults (pet created at @p now). */
void lw_state_defaults(lw_state_t *state, uint32_t now);

#define LW_SAVE_MAGIC 0x4C575652u /* "LWVR" */
#define LW_SAVE_VERSION 1
#define LW_SAVE_BYTES 72

/** Serialise into @p out; returns LW_SAVE_BYTES or -1. */
int lw_save_encode(const lw_state_t *state, uint8_t *out, size_t capacity);

/** Parse a save blob; returns 0 on success, -1 for magic/size/version/crc errors. */
int lw_save_decode(lw_state_t *state, const uint8_t *in, size_t length);

uint32_t lw_crc32(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
