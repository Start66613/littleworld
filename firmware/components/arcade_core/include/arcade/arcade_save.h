/* Little World Arcade - versioned, checksummed persistence record.
 *
 * The record is a fixed 24 byte little endian blob that the platform layer
 * stores in NVS. Encoding/decoding is pure and unit tested so a corrupted or
 * foreign blob can never be applied to the game shell.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "arcade/arcade_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARCADE_SAVE_MAGIC    0x4C574152u /* "LWAR" */
#define ARCADE_SAVE_VERSION  1
/* Layout (little endian): magic u32 | version u8 | flags u8 | reserved u16 |
 * best[3] u32 | plays[3] u32 | crc32 u32 = 36 bytes. */
#define ARCADE_SAVE_BYTES    36

typedef struct {
    uint32_t best[ARCADE_GAME_COUNT]; /**< per game high score */
    uint32_t plays[ARCADE_GAME_COUNT];/**< per game round counter */
    uint8_t  sound_on;
} arcade_save_t;

/** Fill a record with defaults (all scores zero, sound on). */
void arcade_save_defaults(arcade_save_t *save);

/**
 * Serialise into @p out.
 * @return number of bytes written (ARCADE_SAVE_BYTES) or -1 on bad arguments.
 */
int arcade_save_encode(const arcade_save_t *save, uint8_t *out, size_t capacity);

/**
 * Parse a blob produced by arcade_save_encode().
 * @return 0 on success, -1 for wrong magic/size/version/checksum.
 */
int arcade_save_decode(arcade_save_t *save, const uint8_t *in, size_t length);

/**
 * Record a finished round.
 * @return 1 when @p score is a new record (and the record was updated).
 */
int arcade_save_submit(arcade_save_t *save, arcade_game_id_t game, uint32_t score);

/** CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320), also used by the tests. */
uint32_t arcade_crc32(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
