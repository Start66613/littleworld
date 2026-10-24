/* SPDX-License-Identifier: MIT */
#include "lw/lw_state.h"

#include <string.h>

void lw_settings_defaults(lw_settings_t *settings)
{
    settings->sound = 1;
    settings->animations = 1;
    settings->theme = 0;
    settings->demo_clock = 1;
}

void lw_state_defaults(lw_state_t *state, uint32_t now)
{
    memset(state, 0, sizeof(*state));
    lw_settings_defaults(&state->settings);
    lw_pet_init(&state->pet, now);
    state->anniversary.year = 2024;
    state->anniversary.month = 10;
    state->anniversary.day = 25;
    state->music_volume = 60;
    state->surprise_unlocked = 0;
    state->map_visited = 0;
    state->arcade_best[0] = 0;
    state->arcade_best[1] = 0;
    state->arcade_best[2] = 0;
}

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
    out[2] = (uint8_t)((value >> 16) & 0xFF);
    out[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint16_t get_u16(const uint8_t *in)
{
    return (uint16_t)(in[0] | (in[1] << 8));
}

static uint32_t get_u32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

uint32_t lw_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

int lw_save_encode(const lw_state_t *state, uint8_t *out, size_t capacity)
{
    if (state == NULL || out == NULL || capacity < LW_SAVE_BYTES) {
        return -1;
    }
    memset(out, 0, LW_SAVE_BYTES);

    put_u32(out + 0, LW_SAVE_MAGIC);
    put_u16(out + 4, LW_SAVE_VERSION);

    out[6] = state->settings.sound;
    out[7] = state->settings.animations;
    out[8] = state->settings.theme;
    out[9] = state->settings.demo_clock;

    out[10] = state->pet.fullness;
    out[11] = state->pet.energy;
    out[12] = state->pet.affection;
    out[13] = state->pet.sleeping;
    out[14] = (uint8_t)state->pet.mood;
    put_u16(out + 15, state->pet.interactions);
    put_u32(out + 17, state->pet.last_update);
    put_u32(out + 21, state->pet.born_at);
    put_u32(out + 25, state->pet.mood_until);
    put_u32(out + 29, state->pet.last_interaction);

    put_u16(out + 33, (uint16_t)state->anniversary.year);
    out[35] = (uint8_t)state->anniversary.month;
    out[36] = (uint8_t)state->anniversary.day;

    out[37] = state->surprise_unlocked;
    out[38] = state->map_visited;
    out[39] = state->music_volume;
    out[40] = state->reserved;

    put_u32(out + 41, state->arcade_best[0]);
    put_u32(out + 45, state->arcade_best[1]);
    put_u32(out + 49, state->arcade_best[2]);

    put_u32(out + LW_SAVE_BYTES - 4, lw_crc32(out, LW_SAVE_BYTES - 4));
    return LW_SAVE_BYTES;
}

int lw_save_decode(lw_state_t *state, const uint8_t *in, size_t length)
{
    if (state == NULL || in == NULL || length != LW_SAVE_BYTES) {
        return -1;
    }
    if (get_u32(in + 0) != LW_SAVE_MAGIC) {
        return -1;
    }
    if (get_u16(in + 4) != LW_SAVE_VERSION) {
        return -1;
    }
    if (get_u32(in + LW_SAVE_BYTES - 4) != lw_crc32(in, LW_SAVE_BYTES - 4)) {
        return -1;
    }

    lw_state_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    parsed.settings.sound = in[6] ? 1 : 0;
    parsed.settings.animations = in[7] ? 1 : 0;
    parsed.settings.theme = in[8] > 2 ? 0 : in[8];
    parsed.settings.demo_clock = in[9] ? 1 : 0;

    parsed.pet.fullness = in[10] > 100 ? 100 : in[10];
    parsed.pet.energy = in[11] > 100 ? 100 : in[11];
    parsed.pet.affection = in[12] > 100 ? 100 : in[12];
    parsed.pet.sleeping = in[13] ? 1 : 0;
    parsed.pet.mood = (lw_pet_mood_t)(in[14] < LW_PET_MOOD_COUNT ? in[14] : LW_PET_MOOD_CALM);
    parsed.pet.interactions = get_u16(in + 15);
    parsed.pet.last_update = get_u32(in + 17);
    parsed.pet.born_at = get_u32(in + 21);
    parsed.pet.mood_until = get_u32(in + 25);
    parsed.pet.last_interaction = get_u32(in + 29);

    parsed.anniversary.year = (int)get_u16(in + 33);
    parsed.anniversary.month = in[35];
    parsed.anniversary.day = in[36];
    if (!lw_date_valid(parsed.anniversary)) {
        parsed.anniversary.year = 2024;
        parsed.anniversary.month = 10;
        parsed.anniversary.day = 25;
    }

    parsed.surprise_unlocked = in[37] ? 1 : 0;
    parsed.map_visited = in[38];
    parsed.music_volume = in[39] > 100 ? 100 : in[39];
    parsed.reserved = 0;
    parsed.arcade_best[0] = get_u32(in + 41);
    parsed.arcade_best[1] = get_u32(in + 45);
    parsed.arcade_best[2] = get_u32(in + 49);

    *state = parsed;
    return 0;
}
