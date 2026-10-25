/* SPDX-License-Identifier: MIT */
#include "lw_char_pack.h"
#include <stdio.h>
#include <string.h>
#include "lw_assets.h"

#define LW_CHAR_MAX_CLIPS 2u
#define LW_CHAR_MAX_FRAMES 16u
#define LW_CHAR_RECORD_BYTES 32u
#define LW_CHAR_HEADER_BYTES 16u

typedef struct {
    const uint8_t *blob;
    size_t size;
    uint32_t offset;
    uint16_t frames, width, height;
    uint32_t frame_bytes;
} lw_char_source_t;

static const uint8_t *s_blob;
static size_t s_blob_size;
static lw_asset_spec_t s_specs[LW_CHAR_MAX_CLIPS];
static lw_char_source_t s_sources[LW_CHAR_MAX_CLIPS];
static char s_ids[LW_CHAR_MAX_CLIPS][24];
static char s_names[LW_CHAR_MAX_CLIPS][16];
static uint16_t s_sequences[LW_CHAR_MAX_CLIPS][LW_CHAR_MAX_FRAMES];
static uint16_t s_durations[LW_CHAR_MAX_CLIPS][LW_CHAR_MAX_FRAMES];
static lw_named_clip_t s_named[LW_CHAR_MAX_CLIPS];
static lw_actor_art_t s_art[2];
static unsigned s_clips;
static bool s_ready;

static uint16_t read_u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool read_frame(void *context, uint16_t frame, uint8_t *pixels, size_t bytes)
{
    const lw_char_source_t *source = context;
    if (!source || frame >= source->frames || bytes != source->frame_bytes) return false;
    uint64_t start = (uint64_t)source->offset + (uint64_t)frame * source->frame_bytes;
    if (start + bytes > s_blob_size) return false;
    memcpy(pixels, s_blob + start, bytes);
    return true;
}

static int index_of(const char *name)
{
    for (unsigned i = 0; i < s_clips; i++) {
        if (strcmp(s_names[i], name) == 0) return (int)i;
    }
    return -1;
}

bool lw_char_pack_register(const lw_char_pack_blob_t *blob)
{
    s_ready = false;
    if (!blob || !blob->data || blob->size < LW_CHAR_HEADER_BYTES) return false;
    const uint8_t *data = blob->data;
    if (memcmp(data, "LWFA0001", 8) != 0) return false;
    if (read_u16(data + 8) != 1) return false;
    unsigned clips = read_u16(data + 10);
    uint32_t total = read_u32(data + 12);
    if (clips < 2 || clips > LW_CHAR_MAX_CLIPS) return false;
    if (total != blob->size) return false;
    if (blob->size < LW_CHAR_HEADER_BYTES + LW_CHAR_RECORD_BYTES * clips) return false;

    s_blob = data;
    s_blob_size = blob->size;
    s_clips = clips;

    for (unsigned i = 0; i < clips; i++) {
        const uint8_t *record = data + LW_CHAR_HEADER_BYTES + i * LW_CHAR_RECORD_BYTES;
        const char *name = (const char *)record;
        size_t name_len = strnlen(name, 12);
        if (name_len == 0 || name_len >= 12 || name_len >= sizeof(s_names[i])) return false;
        unsigned frames = read_u16(record + 12);
        unsigned width = read_u16(record + 14);
        unsigned height = read_u16(record + 16);
        int16_t anchor_x = (int16_t)read_u16(record + 18);
        int16_t anchor_y = (int16_t)read_u16(record + 20);
        unsigned loop = record[22];
        unsigned duration = read_u16(record + 24);
        uint32_t offset = read_u32(record + 28);
        if (frames == 0 || frames > LW_CHAR_MAX_FRAMES) return false;
        if (width == 0 || height == 0 || width > 1024 || height > 600) return false;
        if (anchor_x < 0 || anchor_x > (int16_t)width || anchor_y < 0 || anchor_y > (int16_t)height) return false;
        if (loop > 1 || duration < 20 || duration > 2000) return false;
        uint64_t frame_bytes = (uint64_t)width * height * 3u;
        if (frame_bytes == 0 || frame_bytes > 0x7FFFFFFFu) return false;
        if (offset < LW_CHAR_HEADER_BYTES + LW_CHAR_RECORD_BYTES * clips) return false;
        if ((uint64_t)offset + frame_bytes * frames > blob->size) return false;

        memcpy(s_names[i], name, name_len);
        s_names[i][name_len] = '\0';
        snprintf(s_ids[i], sizeof(s_ids[i]), "%s.idle", s_names[i]);
        s_sources[i] = (lw_char_source_t){data, blob->size, offset, (uint16_t)frames,
                                          (uint16_t)width, (uint16_t)height, (uint32_t)frame_bytes};
        s_specs[i] = (lw_asset_spec_t){s_ids[i], (uint16_t)width, (uint16_t)height, (uint16_t)frames,
                                     anchor_x, anchor_y, LV_COLOR_FORMAT_RGB565A8, read_frame, &s_sources[i]};
        for (unsigned f = 0; f < frames; f++) {
            s_sequences[i][f] = (uint16_t)f;
            s_durations[i][f] = (uint16_t)duration;
        }
        s_named[i].name = "idle";   /* lw_os asks for the "idle" clip by name */
        s_named[i].clip = (lw_clip_t){s_ids[i], s_sequences[i], s_durations[i], (uint16_t)frames, loop != 0};
    }

    if (!lw_assets_register(s_specs, clips, 2u * 1024u * 1024u)) return false;

    int male = index_of("male");
    int qian = index_of("qianqian");
    if (male < 0 || qian < 0) {
        lw_assets_register(NULL, 0, 2u * 1024u * 1024u);
        return false;
    }
    s_art[0] = (lw_actor_art_t){"male", &s_named[male], 1, false};
    s_art[1] = (lw_actor_art_t){"qianqian", &s_named[qian], 1, false};
    s_ready = true;
    return true;
}

const lw_actor_art_t *lw_char_pack_art(void)
{
    return s_ready ? s_art : NULL;
}

void lw_char_pack_release(void)
{
    if (!s_ready) return;
    lw_assets_register(NULL, 0, 2u * 1024u * 1024u);
    s_ready = false;
    s_clips = 0;
    s_blob = NULL;
    s_blob_size = 0;
}
