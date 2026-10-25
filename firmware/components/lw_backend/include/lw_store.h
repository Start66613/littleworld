/* Framed, versioned persistence records with CRC. No ESP-IDF dependency:
 * the same code runs in the firmware and in host-side substitute tests.
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Record layout (little endian):
 *   0  magic  4  'L','W','R','2'
 *   4  version 1
 *   5  kind   1
 *   6  length 2
 *   8  payload [length]
 *   8+length crc32 over bytes [0, 8+length)
 */
#define LW_STORE_MAGIC0 'L'
#define LW_STORE_MAGIC1 'W'
#define LW_STORE_MAGIC2 'R'
#define LW_STORE_MAGIC3 '2'
#define LW_STORE_HEADER_BYTES 8u
#define LW_STORE_CRC_BYTES 4u
#define LW_STORE_MAX_PAYLOAD 64u
#define LW_STORE_MAX_RECORD (LW_STORE_HEADER_BYTES + LW_STORE_MAX_PAYLOAD + LW_STORE_CRC_BYTES)

typedef enum {
    LW_STORE_OK = 0,
    LW_STORE_MISSING,      /* no stored record: caller keeps its defaults */
    LW_STORE_BAD_MAGIC,    /* foreign or blank memory */
    LW_STORE_BAD_VERSION,  /* written by another format revision */
    LW_STORE_BAD_KIND,     /* record belongs to a different consumer */
    LW_STORE_BAD_LENGTH,   /* length does not match the caller contract */
    LW_STORE_BAD_CRC,      /* torn or corrupted write */
    LW_STORE_IO_ERROR      /* device read or write failed */
} lw_store_status_t;

/* Byte transport. read must copy a whole stored record and report its length;
 * write must persist all bytes or report failure. Neither may erase anything. */
typedef struct {
    void *context;
    bool (*read)(void *context, uint8_t *out, size_t capacity, size_t *length);
    bool (*write)(void *context, const uint8_t *data, size_t length);
} lw_store_io_t;

const char *lw_store_status_name(lw_store_status_t status);
bool lw_store_status_is_valid_record(lw_store_status_t status);

/* Writes a complete frame into out. Returns false when capacity is too small,
 * length exceeds LW_STORE_MAX_PAYLOAD, or payload is NULL. */
bool lw_store_encode(uint8_t kind, uint8_t version, const void *payload, size_t length,
                     uint8_t *out, size_t capacity, size_t *written);

/* Validates a frame for the expected kind/version/length and copies the payload. */
lw_store_status_t lw_store_decode(const uint8_t *raw, size_t raw_length, uint8_t kind, uint8_t version,
                                  void *payload, size_t length);

/* Loads and validates one record. Every failure leaves payload untouched and
 * reports why, so the caller can fall back to defaults without erasing data. */
lw_store_status_t lw_store_load(const lw_store_io_t *io, uint8_t kind, uint8_t version,
                                void *payload, size_t length);

/* Encodes and writes one record. A failed transport is reported as
 * LW_STORE_IO_ERROR; success is only returned after the transport accepted it. */
lw_store_status_t lw_store_save(const lw_store_io_t *io, uint8_t kind, uint8_t version,
                                const void *payload, size_t length);

#ifdef __cplusplus
}
#endif
