/* SPDX-License-Identifier: MIT */
#include "lw/lw_pet.h"

#include <string.h>

#define HOUR 3600u
#define DAY (24u * HOUR)

/* Gentle rates: a full bar lasts most of a day, sleep restores quickly. */
#define FULLNESS_DECAY_PER_HOUR 6
#define ENERGY_DECAY_PER_HOUR 5
#define ENERGY_REGEN_PER_HOUR 25
#define AFFECTION_FLOOR 20

static uint8_t clamp_u8(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return (uint8_t)value;
}

void lw_pet_clamp(lw_pet_t *pet)
{
    pet->fullness = clamp_u8(pet->fullness);
    pet->energy = clamp_u8(pet->energy);
    pet->affection = clamp_u8(pet->affection);
    pet->sleeping = pet->sleeping ? 1 : 0;
}

void lw_pet_init(lw_pet_t *pet, uint32_t now)
{
    memset(pet, 0, sizeof(*pet));
    pet->fullness = 72;
    pet->energy = 68;
    pet->affection = 60;
    pet->mood = LW_PET_MOOD_CALM;
    pet->last_update = now;
    pet->born_at = now;
    pet->last_interaction = now;
    pet->mood_until = 0;
}

lw_pet_mood_t lw_pet_mood(const lw_pet_t *pet, uint32_t now)
{
    if (pet->sleeping) {
        return LW_PET_MOOD_SLEEPING;
    }
    if (pet->mood_until > now) {
        return pet->mood;
    }
    if (pet->energy <= 22) {
        return LW_PET_MOOD_SLEEPY;
    }
    if (pet->affection >= 82) {
        return LW_PET_MOOD_HAPPY;
    }
    if (pet->last_interaction != 0 && now > pet->last_interaction &&
        (now - pet->last_interaction) > 150) {
        return LW_PET_MOOD_CURIOUS;
    }
    return LW_PET_MOOD_CALM;
}

void lw_pet_tick(lw_pet_t *pet, uint32_t now)
{
    if (pet->last_update != 0 && now > pet->last_update) {
        uint32_t delta = now - pet->last_update;
        if (delta > 30u * DAY) {
            delta = 30u * DAY; /* a month away must not wipe the pet out */
        }

        if (pet->sleeping) {
            pet->energy = clamp_u8(pet->energy + (int)((delta * ENERGY_REGEN_PER_HOUR) / HOUR));
            pet->fullness = clamp_u8(pet->fullness - (int)((delta * 4u) / HOUR));
            if (pet->energy >= 98) {
                pet->sleeping = 0;
                pet->last_interaction = now;
            }
        } else {
            pet->fullness = clamp_u8(pet->fullness - (int)((delta * FULLNESS_DECAY_PER_HOUR) / HOUR));
            pet->energy = clamp_u8(pet->energy - (int)((delta * ENERGY_DECAY_PER_HOUR) / HOUR));
            if (pet->affection > AFFECTION_FLOOR) {
                pet->affection = clamp_u8(pet->affection - (int)(delta / (6u * HOUR)));
            }
        }
        pet->last_update = now;
    } else if (pet->last_update == 0) {
        pet->last_update = now;
    }

    lw_pet_clamp(pet);
    pet->mood = lw_pet_mood(pet, now);
}

bool lw_pet_interact(lw_pet_t *pet, lw_pet_action_t action, uint32_t now)
{
    lw_pet_tick(pet, now);

    switch (action) {
    case LW_PET_ACTION_TAP:
        if (pet->sleeping) {
            return false;
        }
        pet->affection = clamp_u8(pet->affection + 1);
        pet->mood = LW_PET_MOOD_HAPPY;
        pet->mood_until = now + 20;
        break;
    case LW_PET_ACTION_PET:
        pet->sleeping = 0;
        pet->affection = clamp_u8(pet->affection + 3);
        pet->mood = LW_PET_MOOD_HAPPY;
        pet->mood_until = now + 30;
        break;
    case LW_PET_ACTION_FEED:
        pet->sleeping = 0;
        pet->fullness = clamp_u8(pet->fullness + 28);
        pet->affection = clamp_u8(pet->affection + 1);
        pet->mood = LW_PET_MOOD_HAPPY;
        pet->mood_until = now + 20;
        break;
    case LW_PET_ACTION_PLAY:
        if (pet->energy < 15) {
            pet->mood = LW_PET_MOOD_SLEEPY;
            pet->mood_until = now + 10;
            return false;
        }
        pet->sleeping = 0;
        pet->energy = clamp_u8(pet->energy - 18);
        pet->fullness = clamp_u8(pet->fullness - 4);
        pet->affection = clamp_u8(pet->affection + 5);
        pet->mood = LW_PET_MOOD_PLAYING;
        pet->mood_until = now + 45;
        break;
    case LW_PET_ACTION_SLEEP:
        pet->sleeping = 1;
        pet->mood = LW_PET_MOOD_SLEEPING;
        pet->mood_until = 0;
        break;
    case LW_PET_ACTION_WAKE:
        if (!pet->sleeping) {
            return false;
        }
        pet->sleeping = 0;
        pet->mood = LW_PET_MOOD_CALM;
        pet->mood_until = 0;
        break;
    default:
        return false;
    }

    pet->last_interaction = now;
    if (pet->interactions < 0xFFFF) {
        pet->interactions++;
    }
    return true;
}

const char *lw_pet_mood_name(lw_pet_mood_t mood)
{
    switch (mood) {
    case LW_PET_MOOD_HAPPY:
        return "开心";
    case LW_PET_MOOD_CURIOUS:
        return "好奇";
    case LW_PET_MOOD_SLEEPY:
        return "困倦";
    case LW_PET_MOOD_PLAYING:
        return "玩耍";
    case LW_PET_MOOD_SLEEPING:
        return "睡觉";
    case LW_PET_MOOD_CALM:
    default:
        return "安静";
    }
}

const char *lw_pet_action_name(lw_pet_action_t action)
{
    switch (action) {
    case LW_PET_ACTION_TAP:
        return "摸了摸";
    case LW_PET_ACTION_PET:
        return "抚摸";
    case LW_PET_ACTION_FEED:
        return "喂食";
    case LW_PET_ACTION_PLAY:
        return "玩耍";
    case LW_PET_ACTION_SLEEP:
        return "休息";
    case LW_PET_ACTION_WAKE:
        return "叫醒";
    default:
        return "互动";
    }
}
