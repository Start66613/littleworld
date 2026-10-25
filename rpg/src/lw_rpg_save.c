/* Little World RPG save record: fixed 40 bytes, magic + version + CRC-32.
 *
 * SPDX-License-Identifier: MIT
 */
#include "lw_rpg.h"

#include <string.h>

#define LW_RPG_SAVE_MAGIC 0x5052574Cu /* "LWRP" little endian */
#define LW_RPG_SAVE_VERSION 1u

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

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

uint32_t lw_rpg_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    if (data == NULL) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

int lw_rpg_save_encode(const lw_rpg_state_t *state, uint8_t *out, size_t capacity)
{
    if (state == NULL || out == NULL || capacity < LW_RPG_SAVE_BYTES) {
        return -1;
    }
    memset(out, 0, LW_RPG_SAVE_BYTES);
    put_u32(out + 0, LW_RPG_SAVE_MAGIC);
    put_u16(out + 4, LW_RPG_SAVE_VERSION);
    out[6] = (uint8_t)state->actors[LW_RPG_ACTOR_PLAYER].pos.x;
    out[7] = (uint8_t)state->actors[LW_RPG_ACTOR_PLAYER].pos.y;
    out[8] = (uint8_t)state->actors[LW_RPG_ACTOR_PARTNER].pos.x;
    out[9] = (uint8_t)state->actors[LW_RPG_ACTOR_PARTNER].pos.y;
    put_u16(out + 10, state->collected_mask);
    out[12] = state->quest_complete;
    put_u32(out + 13, state->unlocked_reward);
    put_u32(out + LW_RPG_SAVE_BYTES - 4, lw_rpg_crc32(out, LW_RPG_SAVE_BYTES - 4));
    return LW_RPG_SAVE_BYTES;
}

int lw_rpg_save_decode(lw_rpg_state_t *state, const uint8_t *in, size_t length)
{
    if (state == NULL || in == NULL || length != LW_RPG_SAVE_BYTES) {
        return -1;
    }
    if (get_u32(in) != LW_RPG_SAVE_MAGIC) {
        return -2;
    }
    if (get_u16(in + 4) != LW_RPG_SAVE_VERSION) {
        return -3;
    }
    uint32_t stored = get_u32(in + LW_RPG_SAVE_BYTES - 4);
    if (stored != lw_rpg_crc32(in, LW_RPG_SAVE_BYTES - 4)) {
        return -4;
    }

    const lw_rpg_room_t *room = state->world->room;
    int px = in[6];
    int py = in[7];
    int qx = in[8];
    int qy = in[9];
    if (!lw_rpg_walkable(room, px, py) || !lw_rpg_walkable(room, qx, qy)) {
        return -5;
    }

    state->actors[LW_RPG_ACTOR_PLAYER].pos.x = (int16_t)px;
    state->actors[LW_RPG_ACTOR_PLAYER].pos.y = (int16_t)py;
    state->actors[LW_RPG_ACTOR_PARTNER].pos.x = (int16_t)qx;
    state->actors[LW_RPG_ACTOR_PARTNER].pos.y = (int16_t)qy;
    for (int i = 0; i < LW_RPG_MAX_ACTORS; i++) {
        state->actors[i].moving = 0;
        state->actors[i].path_len = 0;
        state->actors[i].path_index = 0;
        state->actors[i].step_accum_ms = 0;
    }
    state->collected_mask = get_u16(in + 10);
    state->quest_complete = in[12];
    state->unlocked_reward = get_u32(in + 13);
    state->dialogue_active = 0;
    state->pending_item = 0;
    state->choice_pending = 0;
    return 0;
}
