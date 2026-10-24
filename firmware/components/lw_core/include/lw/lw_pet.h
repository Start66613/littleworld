/* Little World - offline pet model (pure C, host tested).
 *
 * The pet is a small state machine: stats decay with wall-clock time, actions
 * nudge them, and the visible mood is derived. No LVGL, no platform calls.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LW_PET_MOOD_CALM = 0,
    LW_PET_MOOD_HAPPY,
    LW_PET_MOOD_CURIOUS,
    LW_PET_MOOD_SLEEPY,
    LW_PET_MOOD_PLAYING,
    LW_PET_MOOD_SLEEPING,
    LW_PET_MOOD_COUNT
} lw_pet_mood_t;

typedef enum {
    LW_PET_ACTION_TAP = 0,
    LW_PET_ACTION_PET,
    LW_PET_ACTION_FEED,
    LW_PET_ACTION_PLAY,
    LW_PET_ACTION_SLEEP,
    LW_PET_ACTION_WAKE,
    LW_PET_ACTION_COUNT
} lw_pet_action_t;

typedef struct {
    uint8_t fullness;     /* 0..100 */
    uint8_t energy;       /* 0..100 */
    uint8_t affection;    /* 0..100 */
    uint8_t sleeping;     /* 0/1 */
    lw_pet_mood_t mood;   /* cached mood after the last tick/action */
    uint32_t last_update; /* unix seconds of the last tick */
    uint32_t born_at;     /* unix seconds of the first init */
    uint32_t mood_until;  /* unix seconds until the temporary mood expires */
    uint32_t last_interaction;
    uint16_t interactions;
} lw_pet_t;

/** Start a fresh pet (or restore fields manually and call lw_pet_retick). */
void lw_pet_init(lw_pet_t *pet, uint32_t now);

/** Advance time: decay, sleep/wake, mood. Safe to call with any delta. */
void lw_pet_tick(lw_pet_t *pet, uint32_t now);

/** Apply an interaction. Returns true when it had a visible effect. */
bool lw_pet_interact(lw_pet_t *pet, lw_pet_action_t action, uint32_t now);

/** Mood derived from the current stats and timers (does not mutate). */
lw_pet_mood_t lw_pet_mood(const lw_pet_t *pet, uint32_t now);

const char *lw_pet_mood_name(lw_pet_mood_t mood);
const char *lw_pet_action_name(lw_pet_action_t action);

/** One letter state for the save record. */
void lw_pet_clamp(lw_pet_t *pet);

#ifdef __cplusplus
}
#endif
