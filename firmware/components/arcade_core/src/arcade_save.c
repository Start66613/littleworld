/* SPDX-License-Identifier: MIT */
#include "arcade/arcade_save.h"

#include <string.h>

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t get_u32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

uint32_t arcade_crc32(const uint8_t *data, size_t length)
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

void arcade_save_defaults(arcade_save_t *save)
{
    memset(save, 0, sizeof(*save));
    save->sound_on = 1;
}

int arcade_save_encode(const arcade_save_t *save, uint8_t *out, size_t capacity)
{
    if (save == NULL || out == NULL || capacity < ARCADE_SAVE_BYTES) {
        return -1;
    }
    memset(out, 0, ARCADE_SAVE_BYTES);
    put_u32(out + 0, ARCADE_SAVE_MAGIC);
    out[4] = (uint8_t)ARCADE_SAVE_VERSION;
    out[5] = (uint8_t)(save->sound_on ? 1u : 0u);
    for (int i = 0; i < ARCADE_GAME_COUNT; i++) {
        put_u32(out + 8 + i * 4, save->best[i]);
    }
    for (int i = 0; i < ARCADE_GAME_COUNT; i++) {
        put_u32(out + 20 + i * 4, save->plays[i]);
    }
    put_u32(out + ARCADE_SAVE_BYTES - 4, arcade_crc32(out, ARCADE_SAVE_BYTES - 4));
    return ARCADE_SAVE_BYTES;
}

int arcade_save_decode(arcade_save_t *save, const uint8_t *in, size_t length)
{
    if (save == NULL || in == NULL || length != ARCADE_SAVE_BYTES) {
        return -1;
    }
    if (get_u32(in + 0) != ARCADE_SAVE_MAGIC) {
        return -1;
    }
    if (in[4] != (uint8_t)ARCADE_SAVE_VERSION) {
        return -1;
    }
    if (get_u32(in + ARCADE_SAVE_BYTES - 4) != arcade_crc32(in, ARCADE_SAVE_BYTES - 4)) {
        return -1;
    }

    arcade_save_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    parsed.sound_on = (uint8_t)(in[5] & 1u);
    for (int i = 0; i < ARCADE_GAME_COUNT; i++) {
        parsed.best[i] = get_u32(in + 8 + i * 4);
        parsed.plays[i] = get_u32(in + 20 + i * 4);
    }
    *save = parsed;
    return 0;
}

int arcade_save_submit(arcade_save_t *save, arcade_game_id_t game, uint32_t score)
{
    if (save == NULL || game < 0 || game >= ARCADE_GAME_COUNT) {
        return 0;
    }
    save->plays[game]++;
    if (score > save->best[game]) {
        save->best[game] = score;
        return 1;
    }
    return 0;
}
