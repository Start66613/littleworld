/* Framed records: validation order is magic, version, kind, length, CRC.
 * Every failure is visible to the caller; nothing is repaired or erased here.
 * SPDX-License-Identifier: MIT */
#include "lw_store.h"

#include <string.h>

#include "lw/lw_state.h"
#include "lw_fault.h"

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

const char *lw_store_status_name(lw_store_status_t status)
{
    switch (status) {
    case LW_STORE_OK: return "ok";
    case LW_STORE_MISSING: return "missing";
    case LW_STORE_BAD_MAGIC: return "bad-magic";
    case LW_STORE_BAD_VERSION: return "bad-version";
    case LW_STORE_BAD_KIND: return "bad-kind";
    case LW_STORE_BAD_LENGTH: return "bad-length";
    case LW_STORE_BAD_CRC: return "bad-crc";
    case LW_STORE_IO_ERROR: return "io-error";
    default: return "unknown";
    }
}

bool lw_store_status_is_valid_record(lw_store_status_t status)
{
    return status == LW_STORE_OK;
}

bool lw_store_encode(uint8_t kind, uint8_t version, const void *payload, size_t length,
                     uint8_t *out, size_t capacity, size_t *written)
{
    if (payload == NULL || out == NULL || length > LW_STORE_MAX_PAYLOAD) {
        return false;
    }
    size_t total = LW_STORE_HEADER_BYTES + length + LW_STORE_CRC_BYTES;
    if (capacity < total) {
        return false;
    }
    out[0] = (uint8_t)LW_STORE_MAGIC0;
    out[1] = (uint8_t)LW_STORE_MAGIC1;
    out[2] = (uint8_t)LW_STORE_MAGIC2;
    out[3] = (uint8_t)LW_STORE_MAGIC3;
    out[4] = version;
    out[5] = kind;
    write_u16(&out[6], (uint16_t)length);
    memcpy(&out[LW_STORE_HEADER_BYTES], payload, length);
    write_u32(&out[LW_STORE_HEADER_BYTES + length], lw_crc32(out, LW_STORE_HEADER_BYTES + length));
    if (written != NULL) {
        *written = total;
    }
    return true;
}

lw_store_status_t lw_store_decode(const uint8_t *raw, size_t raw_length, uint8_t kind, uint8_t version,
                                  void *payload, size_t length)
{
    if (raw == NULL || payload == NULL || length > LW_STORE_MAX_PAYLOAD) {
        return LW_STORE_BAD_LENGTH;
    }
    if (raw_length < LW_STORE_HEADER_BYTES + LW_STORE_CRC_BYTES) {
        return LW_STORE_BAD_LENGTH;
    }
    if (raw[0] != (uint8_t)LW_STORE_MAGIC0 || raw[1] != (uint8_t)LW_STORE_MAGIC1 ||
        raw[2] != (uint8_t)LW_STORE_MAGIC2 || raw[3] != (uint8_t)LW_STORE_MAGIC3) {
        return LW_STORE_BAD_MAGIC;
    }
    if (raw[4] != version) {
        return LW_STORE_BAD_VERSION;
    }
    if (raw[5] != kind) {
        return LW_STORE_BAD_KIND;
    }
    size_t stored = read_u16(&raw[6]);
    if (stored != length) {
        return LW_STORE_BAD_LENGTH;
    }
    size_t framed = LW_STORE_HEADER_BYTES + stored + LW_STORE_CRC_BYTES;
    if (raw_length < framed) {
        return LW_STORE_BAD_LENGTH;
    }
    if (raw_length > framed) {
        /* A trailing partial write means the transport kept an older, longer
         * record; refuse it instead of accepting stale bytes. */
        return LW_STORE_BAD_LENGTH;
    }
    uint32_t stored_crc = read_u32(&raw[LW_STORE_HEADER_BYTES + stored]);
    if (lw_crc32(raw, LW_STORE_HEADER_BYTES + stored) != stored_crc) {
        return LW_STORE_BAD_CRC;
    }
    memcpy(payload, &raw[LW_STORE_HEADER_BYTES], stored);
    return LW_STORE_OK;
}

lw_store_status_t lw_store_load(const lw_store_io_t *io, uint8_t kind, uint8_t version,
                                void *payload, size_t length)
{
    if (io == NULL || io->read == NULL || payload == NULL) {
        return LW_STORE_IO_ERROR;
    }
    uint8_t raw[LW_STORE_MAX_RECORD];
    size_t raw_length = 0;
    /* Injected transport failure. It reports the same thing a dead NVS reports,
     * so the caller's fallback is exercised, not a special test branch. */
    if (lw_fault_should_fail(LW_FAULT_STORE_READ)) {
        return LW_STORE_MISSING;
    }
    if (!io->read(io->context, raw, sizeof(raw), &raw_length)) {
        return LW_STORE_MISSING;
    }
    if (raw_length == 0) {
        return LW_STORE_MISSING;
    }
    if (raw_length > sizeof(raw)) {
        return LW_STORE_BAD_LENGTH;
    }
    return lw_store_decode(raw, raw_length, kind, version, payload, length);
}

lw_store_status_t lw_store_save(const lw_store_io_t *io, uint8_t kind, uint8_t version,
                                const void *payload, size_t length)
{
    if (io == NULL || io->write == NULL || payload == NULL) {
        return LW_STORE_IO_ERROR;
    }
    uint8_t raw[LW_STORE_MAX_RECORD];
    size_t raw_length = 0;
    if (!lw_store_encode(kind, version, payload, length, raw, sizeof(raw), &raw_length)) {
        return LW_STORE_BAD_LENGTH;
    }
    /* Injected write failure. The transport is never called, which is what a
     * full or write-protected partition looks like from here: the old record
     * must still be intact afterwards. */
    if (lw_fault_should_fail(LW_FAULT_STORE_WRITE)) {
        return LW_STORE_IO_ERROR;
    }
    if (!io->write(io->context, raw, raw_length)) {
        return LW_STORE_IO_ERROR;
    }
    return LW_STORE_OK;
}
