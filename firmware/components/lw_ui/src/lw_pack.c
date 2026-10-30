/* SPDX-License-Identifier: MIT */
/* Hardware-side LWPX pack loader.
 *
 * The LWPX container is NOT the embedded LWFA pack and is not treated as one:
 * it has its own header and 108-byte clip records. Frame geometry is read from
 * the header (u16 width at +8, u16 height at +10; a frame is width*height RGB565
 * pixels plus the same count of alpha bytes), which is what lets one loader
 * serve the 256x256 character packs, the 320x320 daily/dogbed pack, the 544x352
 * UI sheet and the 512x544 skin atlas without a transcode step. What it shares with the old pack is the contract the UI already
 * depends on: lw_assets registers a pointer-only spec table, every frame is
 * fetched through a lease (lw_asset_acquire -> spec->read -> lw_asset_release)
 * and lw_sprite owns the visible image. So this file only supplies specs that
 * read frames out of a file; it never keeps a whole pack in RAM.
 *
 * Frame reads go through lw_file_cache when the caller supplies one, which is
 * what gives the board its single-worker read-ahead and its PSRAM frame
 * buffers. Without a cache the read falls back to lw_asset_read_raw_file, which
 * is plain stdio and is what the Windows host already uses.
 */
#include "lw_pack.h"
#include "lw_path.h"

#include <stdio.h>
#include <string.h>

/* One actor slot per pack family, not one per character: the visual packs are
 * registered under their own names ("v43skin" for the UI atlas, "v42" for the
 * daily/dogbed clips, "v42ui" for the component sheet) because that is the id
 * prefix their consumers already look up. Two slots only ever covered the two
 * characters, which is why nothing else could be registered.
 *   male(7) + qianqian-base(13) + qianqian-wave(1) + v42(8) + v42ui(1)
 *   + v43skin(1) = 31 specs, which is why the spec table is a fixed 64 and no
 *   longer the product of the two caps (that product would tie the legal clip
 *   count of one actor to the total the registry can hold). */
#define LW_PACK_MAX_ACTORS 5u /* male, qianqian, v42, v42ui, v43skin */
#define LW_PACK_MAX_SPECS 64u /* same bound as lw_assets' MAX_ASSETS */
#define LW_PACK_PATH_BYTES 768u
#define LW_PACK_ID_BYTES 48u
#define LW_PACK_FILE_BYTES 128u
/* Resolved paths are kept per FILE, not per actor. A source record hands
 * lw_asset_read_raw_file a pointer to its path, and that pointer has to stay
 * valid and unchanged for the lifetime of the spec: an actor's second pack
 * would otherwise overwrite the buffer the first pack's frames still read
 * through, and every frame of the first clip would be fetched from the second
 * file at the first clip's offsets — wrong art, no error, no log. */
#define LW_PACK_MAX_FILES 8u

typedef struct {
    char id[LW_PACK_ID_BYTES];
    char file[LW_PACK_FILE_BYTES]; /* basename, kept stable: lw_assets stores &id */
    uint32_t offset;
    /* Bytes per frame. Both containers carry their own geometry: LWPX in the
     * header, LWFA per clip record (male 108x212, qianqian 104x202, skin
     * 512x544), so the size belongs to the source and must not be assumed at
     * read time. */
    uint32_t frame_bytes;
    /* Bytes per frame as STORED in the file. Zero means raw frames, which is
     * every pack before V4.6. Non-zero means the file holds stride-sized
     * deflate blocks (see tools/lwp_compress.py) and read_frame() has to
     * inflate them; frame_bytes above stays the decoded size the sprite
     * allocates. */
    uint32_t stored_bytes;
    uint16_t frames;
    /* Non-NULL for an embedded blob: frames are copied straight out of the
     * flash-mapped image, so there is no file to open and no cache to consult. */
    const uint8_t *memory;
    /* The cache this clip was parsed from. Bound per clip, not looked up by
     * file name at read time: two packs of the same actor may live in the same
     * file name from different roots, and a wrong binding reads the wrong art
     * with no error. */
    lw_file_cache_t *cache;
    /* Standard-C raw-frame provider, self-contained so lw_asset_read_raw_file
     * can take its address: it must outlive every lease made from this spec. */
    lw_asset_raw_file_t raw;
} lw_pack_source_t;

/* Everything below is file-scope on purpose: lw_assets_register() keeps the
 * caller's pointers and never copies the spec table, the ids or the clip
 * arrays, so a stack-local table would dangle the moment this function returns
 * (that exact bug shipped once in lw_char_pack.c). */
static lw_asset_spec_t s_specs[LW_PACK_MAX_SPECS];
static lw_pack_source_t s_sources[LW_PACK_MAX_SPECS];
static lw_named_clip_t s_named[LW_PACK_MAX_ACTORS][LW_PACK_MAX_CLIPS];
static uint16_t s_sequences[LW_PACK_MAX_ACTORS][LW_PACK_MAX_CLIPS][LW_PACK_MAX_FRAMES];
static uint16_t s_durations[LW_PACK_MAX_ACTORS][LW_PACK_MAX_CLIPS][LW_PACK_MAX_FRAMES];
static char s_clip_names[LW_PACK_MAX_ACTORS][LW_PACK_MAX_CLIPS][LW_PACK_NAME_BYTES];
static char s_actor_names[LW_PACK_MAX_ACTORS][24];
/* One resolved path per parsed FILE (see LW_PACK_MAX_FILES). Never indexed by
 * actor: appending a pack must not repoint an earlier pack's raw provider. */
static char s_file_paths[LW_PACK_MAX_FILES][LW_PACK_PATH_BYTES];
static char s_file_names[LW_PACK_MAX_FILES][LW_PACK_FILE_BYTES];
static unsigned s_file_count;
static lw_file_cache_t *s_caches[LW_PACK_MAX_ACTORS];
static unsigned s_actor_count;
static unsigned s_spec_count;
static bool s_registered;

const char *lw_pack_status_text(lw_pack_status_t status)
{
    switch (status) {
    case LW_PACK_OK: return "ok";
    case LW_PACK_ERR_ARGUMENT: return "argument";
    case LW_PACK_ERR_OPEN: return "open-failed";
    case LW_PACK_ERR_MAGIC: return "bad-magic";
    case LW_PACK_ERR_GEOMETRY: return "bad-geometry";
    case LW_PACK_ERR_CLIP_COUNT: return "bad-clip-count";
    case LW_PACK_ERR_RECORD: return "short-record";
    case LW_PACK_ERR_CLIP_NAME: return "bad-clip-name";
    case LW_PACK_ERR_FRAMES: return "bad-frame-count";
    case LW_PACK_ERR_ANCHOR: return "bad-anchor";
    case LW_PACK_ERR_DURATION: return "bad-duration";
    case LW_PACK_ERR_OFFSET: return "bad-frame-offset";
    case LW_PACK_ERR_TRUNCATED: return "truncated-pack";
    case LW_PACK_ERR_DUPLICATE: return "duplicate-clip";
    case LW_PACK_ERR_LOOP: return "bad-loop-flag";
    case LW_PACK_ERR_ACTORS: return "actor-table-full";
    case LW_PACK_ERR_REGISTER: return "register-rejected";
    case LW_PACK_ERR_TRAILER: return "bad-geometry-trailer";
    }
    return "unknown";
}

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static int16_t i16(const uint8_t *p) { return (int16_t)u16(p); }
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Inflate one deflate block into a frame buffer. The ESP32-P4 keeps miniz's
 * tinfl in ROM, so this costs no flash; the Windows host has no ROM and never
 * sees deflate packs (host/pixel_pack.c refuses them), so it reports failure
 * instead of pulling a decompressor into the simulator build. */
#ifdef ESP_PLATFORM
#include "miniz.h"
static size_t lwp_inflate(void *dst, size_t dst_len, const void *src, size_t src_len)
{
    size_t got = tinfl_decompress_mem_to_mem(dst, dst_len, src, src_len, 0);
    return got == (size_t)TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ? 0 : got;
}
#else
static size_t lwp_inflate(void *dst, size_t dst_len, const void *src, size_t src_len)
{
    (void)dst; (void)dst_len; (void)src; (void)src_len;
    return 0;
}
#endif

/* One frame of one clip. Runs on the LVGL thread only, because lw_asset_acquire
 * is called from lw_sprite_seek. */
static bool read_frame(void *context, uint16_t frame, uint8_t *pixels, size_t bytes)
{
    lw_pack_source_t *source = context;
    if (!source || frame >= source->frames || bytes != source->frame_bytes) return false;
    if (source->memory) {
        /* Embedded pack: the bytes are already in the flash mapping, so a frame
         * is a memcpy. This is the no-TF-card path and it must stay allocation
         * free and non-blocking. */
        memcpy(pixels, source->memory + (uint64_t)frame * source->frame_bytes, bytes);
        return true;
    }
    /* Bind the frame to the cache of the pack file it came from. The cache is
     * stored in the source itself, not looked up per call: the file name alone
     * is ambiguous, because the same clip table can be re-added from a rebuilt
     * pack and the per-file path record then describes a different file. */
    lw_file_cache_t *cache = source->cache;
    if (source->stored_bytes) {
        /* Deflate pack: the file holds stride-sized compressed blocks, so the
         * read is 15-48 KiB instead of 98-208 KiB and the frame is expanded
         * here. Reading through borrow() keeps the block in the cache and hands
         * the decoder a pinned pointer, so a decompressed frame costs no copy
         * out of the cache, only the inflate. */
        if (!cache) return false;
        uint64_t offset = (uint64_t)source->offset + (uint64_t)frame * source->stored_bytes;
        const uint8_t *block =
            lw_file_cache_borrow(cache, source->file, offset, source->stored_bytes);
        if (!block) return false;
        size_t got = lwp_inflate(pixels, bytes, block, source->stored_bytes);
        lw_file_cache_unborrow(cache, block);
        if (got != bytes) return false;
        unsigned next = (unsigned)((frame + 1u) % source->frames);
        (void)lw_file_cache_prefetch(cache, source->file,
                                     (uint64_t)source->offset + (uint64_t)next * source->stored_bytes,
                                     source->stored_bytes);
        return true;
    }
    uint64_t offset = (uint64_t)source->offset + (uint64_t)frame * source->frame_bytes;
    if (cache) {
        bool ok = lw_file_cache_read(cache, source->file, offset, pixels, bytes);
        if (ok) {
            /* Read-ahead the next frame of the same clip: the sprite wants it
             * 100-125 ms later and a blocking card read on the LVGL thread is
             * exactly what this cache exists to avoid. */
            unsigned next = (unsigned)((frame + 1u) % source->frames);
            (void)lw_file_cache_prefetch(cache, source->file,
                                         (uint64_t)source->offset + (uint64_t)next * source->frame_bytes,
                                         bytes);
        }
        return ok;
    }
    /* No cache: read straight from the pack file. This is the Windows host
     * path and the board's pre-cache path; it blocks, which is why the board
     * always hands lw_pack_open a cache. The provider needs a frame index
     * relative to this clip's own payload, not the pack-wide index. */
    bool raw_ok = lw_asset_read_raw_file(&source->raw, frame, pixels, bytes);
    return raw_ok;
}

/* Zero-copy variant of read_frame() for cached card packs.
 *
 * The sprite redraws straight out of the cache buffer instead of memcpy-ing the
 * frame into its own lease buffer first. On the RPG page that copy was 6-20 ms
 * per 97 KB frame in PSRAM and walking needs a frame every 50 ms per actor, which
 * was the entire hitch the user reported. Returns NULL whenever the bytes are not
 * (and cannot be) pinned - embedded packs, deflate packs (the cache holds
 * compressed blocks, not pixels), the host's stdio path, or a cache with nothing
 * evictable - and the caller then falls back to read_frame(). */
static const uint8_t *borrow_frame(void *context, uint16_t frame, size_t bytes)
{
    lw_pack_source_t *source = context;
    if (!source || !source->cache || !bytes || source->stored_bytes) return NULL;
    return lw_file_cache_borrow(source->cache, source->file,
                                (uint64_t)source->offset + (uint64_t)frame * source->frame_bytes,
                                bytes);
}

static void unborrow_frame(void *context, const uint8_t *pixels)
{
    lw_pack_source_t *source = context;
    if (!source || !source->cache || !pixels) return;
    lw_file_cache_unborrow(source->cache, pixels);
}

static void prefetch_frame(void *context, uint16_t frame)
{
    lw_pack_source_t *source = context;
    if (!source || !source->cache || frame >= source->frames) return;
    uint32_t bytes = source->stored_bytes ? source->stored_bytes : source->frame_bytes;
    uint64_t offset = (uint64_t)source->offset + (uint64_t)frame * bytes;
    (void)lw_file_cache_prefetch(source->cache, source->file, offset, bytes);
}

bool lw_pack_registry_reset(void)
{
    /* Detach first, and ONLY wipe the arrays if the detach really happened.
     *
     * lw_assets refuses to drop a table while a lease is live. If we ignored
     * that refusal and cleared s_specs anyway, the registry would be left
     * pointing at zeroed specs — ids become NULL and the next lw_assets_find()
     * strcmp()s through a null pointer. That is the same class of bug that
     * panicked the board in V3.1, so the failure is reported to the caller
     * instead of being papered over. */
    if (!lw_assets_register(NULL, 0, LW_PACK_LEASE_BUDGET)) return false;
    s_registered = false;
    s_actor_count = 0;
    s_spec_count = 0;
    s_file_count = 0;
    s_caches[0] = NULL;
    s_caches[1] = NULL;
    memset(s_specs, 0, sizeof(s_specs));
    memset(s_sources, 0, sizeof(s_sources));
    memset(s_named, 0, sizeof(s_named));
    memset(s_sequences, 0, sizeof(s_sequences));
    memset(s_durations, 0, sizeof(s_durations));
    memset(s_clip_names, 0, sizeof(s_clip_names));
    memset(s_actor_names, 0, sizeof(s_actor_names));
    memset(s_file_paths, 0, sizeof(s_file_paths));
    memset(s_file_names, 0, sizeof(s_file_names));
    return true;
}

static lw_pack_status_t parse(const char *root, const char *file, const char *actor_name,
                              lw_file_cache_t *cache, lw_pack_t *out)
{
    if (!root || !file || !actor_name || !file[0]) return LW_PACK_ERR_ARGUMENT;
    if (strlen(file) >= LW_PACK_FILE_BYTES) return LW_PACK_ERR_ARGUMENT;

    /* Find the actor slot first: an actor may arrive in several files (qianqian
     * keeps her base pack and gains a wave pack), and every file of one actor
     * must land in that actor's single slot. Matching on (actor, file) instead
     * would give the second file a NEW slot, splitting the actor's clip table
     * and silently losing the clips the first file installed. */
    unsigned slot = LW_PACK_MAX_ACTORS;
    for (unsigned i = 0; i < s_actor_count; i++) {
        if (!strcmp(s_actor_names[i], actor_name)) { slot = i; break; }
    }
    bool fresh = slot == LW_PACK_MAX_ACTORS;
    if (fresh) {
        if (s_actor_count >= LW_PACK_MAX_ACTORS) return LW_PACK_ERR_ACTORS;
        if (strlen(actor_name) >= sizeof(s_actor_names[0])) return LW_PACK_ERR_ARGUMENT;
        slot = s_actor_count;
    }

    char absolute[LW_PACK_PATH_BYTES];
    /* Same join rule as every other consumer (lw_path.h): a pack file name is
     * relative to the resource root and must not carry a path of its own. */
    if (!lw_path_join(absolute, sizeof(absolute), root, file)) return LW_PACK_ERR_ARGUMENT;
    size_t made = strlen(absolute);

    /* Reserve the per-FILE path record BEFORE anything is published. Every spec
     * of this file points into it (source->file / raw.path), so it must be a
     * distinct buffer per file and must never be recycled while the registry
     * is alive. */
    if (s_file_count >= LW_PACK_MAX_FILES) return LW_PACK_ERR_CLIP_COUNT;
    unsigned file_slot = s_file_count;
    memcpy(s_file_names[file_slot], file, strlen(file) + 1);
    memcpy(s_file_paths[file_slot], absolute, made + 1u);

    FILE *f = fopen(absolute, "rb");
    if (!f) return LW_PACK_ERR_OPEN;
    uint8_t header[LW_PACK_HEADER_BYTES];
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) { fclose(f); return LW_PACK_ERR_TRUNCATED; }
    if (memcmp(header, "LWPX0001", 8) != 0) { fclose(f); return LW_PACK_ERR_MAGIC; }
    /* Geometry comes from the file, not from a compile-time constant. Each
     * frame is width*height RGB565 pixels followed by the same count of alpha
     * bytes, so the stride the offsets are validated against is derived here
     * and then carried in the source record for every later frame read. */
    unsigned width = u16(header + 8);
    unsigned height = u16(header + 10);
    if (width == 0 || height == 0 || width > LW_PACK_MAX_WIDTH || height > LW_PACK_MAX_HEIGHT) {
        fclose(f);
        return LW_PACK_ERR_GEOMETRY;
    }
    uint64_t frame_bytes = (uint64_t)width * height * 3u;
    if (frame_bytes == 0 || frame_bytes > LW_PACK_MAX_FRAME_BYTES) {
        fclose(f);
        return LW_PACK_ERR_GEOMETRY;
    }
    unsigned clips = u16(header + 12);
    if (clips == 0 || clips > LW_PACK_MAX_CLIPS) { fclose(f); return LW_PACK_ERR_CLIP_COUNT; }
    if (s_spec_count + clips > LW_PACK_MAX_SPECS) { fclose(f); return LW_PACK_ERR_CLIP_COUNT; }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return LW_PACK_ERR_TRUNCATED; }
    long size = ftell(f);
    uint64_t payload_start = LW_PACK_HEADER_BYTES + (uint64_t)LW_PACK_RECORD_BYTES * clips;
    if (size < 0 || (uint64_t)size < payload_start || fseek(f, LW_PACK_HEADER_BYTES, SEEK_SET) != 0) {
        fclose(f);
        return LW_PACK_ERR_TRUNCATED;
    }

    /* Optional geometry trailer (see lw_pack.h). When it is present the trailer
     * occupies the tail of the file, so the payload must end before it: a clip
     * whose frames ran into the trailer would read bbox bytes as pixels, which
     * looks like corrupted art rather than an error. */
    uint64_t payload_end = (uint64_t)size;
    uint16_t trailer_bbox_x[LW_PACK_MAX_CLIPS] = {0};
    uint16_t trailer_bbox_y[LW_PACK_MAX_CLIPS] = {0};
    uint16_t trailer_bbox_w[LW_PACK_MAX_CLIPS] = {0};
    uint16_t trailer_bbox_h[LW_PACK_MAX_CLIPS] = {0};
    uint16_t trailer_prescaled[LW_PACK_MAX_CLIPS] = {0};
    /* Frame stride as stored in the file: 0 for raw frames (every pack before
     * V4.6), otherwise the size of one deflate block. Carried in the trailer's
     * reserved u16, which the loader has always read and no pack has used. */
    unsigned stored_stride = 0;
    if (u16(header + 14) == LW_PACK_TRAILER_GEOMETRY) {
        uint64_t trailer_bytes = LW_PACK_TRAILER_HEADER_BYTES +
                                 (uint64_t)LW_PACK_TRAILER_RECORD_BYTES * clips;
        if ((uint64_t)size < payload_start + trailer_bytes) { fclose(f); return LW_PACK_ERR_TRAILER; }
        uint64_t trailer_start = (uint64_t)size - trailer_bytes;
        if (fseek(f, (long)trailer_start, SEEK_SET) != 0) { fclose(f); return LW_PACK_ERR_TRAILER; }
        uint8_t trailer_header[LW_PACK_TRAILER_HEADER_BYTES];
        if (fread(trailer_header, 1, sizeof(trailer_header), f) != sizeof(trailer_header) ||
            memcmp(trailer_header, LW_PACK_TRAILER_MAGIC, 8) != 0 ||
            u16(trailer_header + 8) != clips) {
            fclose(f);
            return LW_PACK_ERR_TRAILER;
        }
        stored_stride = u16(trailer_header + 10);
        for (unsigned i = 0; i < clips; i++) {
            uint8_t rec[LW_PACK_TRAILER_RECORD_BYTES];
            if (fread(rec, 1, sizeof(rec), f) != sizeof(rec)) { fclose(f); return LW_PACK_ERR_TRAILER; }
            trailer_bbox_x[i] = (uint16_t)u16(rec + 0);
            trailer_bbox_y[i] = (uint16_t)u16(rec + 2);
            trailer_bbox_w[i] = (uint16_t)u16(rec + 4);
            trailer_bbox_h[i] = (uint16_t)u16(rec + 6);
            trailer_prescaled[i] = (uint16_t)u16(rec + 8);
            /* A box that starts or ends outside the canvas is a packing bug, not
             * a hint: refuse it here rather than clamping it silently at draw
             * time, where it would shift the sprite and look like a code fault. */
            if (trailer_bbox_w[i] && trailer_bbox_h[i] &&
                ((unsigned)trailer_bbox_x[i] + trailer_bbox_w[i] > width ||
                 (unsigned)trailer_bbox_y[i] + trailer_bbox_h[i] > height)) {
                fclose(f);
                return LW_PACK_ERR_TRAILER;
            }
        }
        payload_end = trailer_start;
        if (fseek(f, LW_PACK_HEADER_BYTES, SEEK_SET) != 0) { fclose(f); return LW_PACK_ERR_TRUNCATED; }
    } else if (u16(header + 14) != LW_PACK_TRAILER_NONE) {
        /* An unknown trailer kind must not be ignored: a future pack may store
         * something whose absence would change how frames are interpreted. */
        fclose(f);
        return LW_PACK_ERR_TRAILER;
    }

    /* A stride is only legal when it can hold one deflate block and it is
     * smaller than the decoded frame (otherwise the pack bought nothing and is
     * more likely a corrupt trailer than a deliberate layout). Deflate packs
     * also require a cache: decompression reads its input through the cache so
     * a block is fetched once, not once per decompress. */
    if (stored_stride) {
        if (!cache || stored_stride < 64u || stored_stride > LW_PACK_MAX_FRAME_BYTES ||
            (uint64_t)stored_stride > frame_bytes) {
            fclose(f);
            return LW_PACK_ERR_TRAILER;
        }
    }
    uint64_t file_stride = stored_stride ? stored_stride : frame_bytes;

    /* A second pack for the same actor APPENDS: qianqian keeps her base clips
     * and gains wave, instead of one pack silently replacing the other. */
    unsigned existing = 0;
    while (existing < LW_PACK_MAX_CLIPS && s_named[slot][existing].name) existing++;
    if (existing + clips > LW_PACK_MAX_CLIPS) { fclose(f); return LW_PACK_ERR_CLIP_COUNT; }

    /* The actor's slot is published before the clip loop so that the raw
     * provider can point at this file's resolved path from the very first
     * frame read. The path itself lives in the per-FILE record, not in a
     * per-actor one, so an append cannot repoint an earlier pack. */
    memcpy(s_actor_names[slot], actor_name, strlen(actor_name) + 1);
    s_caches[slot] = cache;
    if (fresh) s_actor_count++;

    unsigned first_spec = s_spec_count;
    unsigned first_clip = existing;
    unsigned frames_total = 0;
    lw_pack_status_t status = LW_PACK_OK;
    for (unsigned i = 0; i < clips && status == LW_PACK_OK; i++) {
        uint8_t record[LW_PACK_RECORD_BYTES];
        if (fread(record, 1, sizeof(record), f) != sizeof(record)) { status = LW_PACK_ERR_RECORD; break; }
        char name[LW_PACK_NAME_BYTES];
        memcpy(name, record, LW_PACK_NAME_BYTES);
        name[LW_PACK_NAME_BYTES - 1] = '\0';
        size_t name_len = strlen(name);
        if (name_len == 0 || name_len >= LW_PACK_NAME_BYTES - 1) { status = LW_PACK_ERR_CLIP_NAME; break; }
        unsigned frames = u16(record + 32);
        int anchor_x = i16(record + 34);
        int anchor_y = i16(record + 36);
        unsigned loop = record[38];
        uint32_t offset = u32(record + 40);
        if (frames == 0 || frames > LW_PACK_MAX_FRAMES) { status = LW_PACK_ERR_FRAMES; break; }
        if (anchor_x < 0 || anchor_y < 0 || anchor_x > (int)width || anchor_y > (int)height) {
            status = LW_PACK_ERR_ANCHOR;
            break;
        }
        if (loop > 1u) { status = LW_PACK_ERR_LOOP; break; }
        if (offset < payload_start ||
            (uint64_t)offset + (uint64_t)frames * file_stride > payload_end) {
            status = LW_PACK_ERR_OFFSET;
            break;
        }
        for (unsigned j = 0; j < existing; j++) {
            if (!strcmp(s_clip_names[slot][j], name)) { status = LW_PACK_ERR_DUPLICATE; break; }
        }
        if (status != LW_PACK_OK) break;
        for (unsigned k = 0; k < frames; k++) {
            unsigned duration = u16(record + 44 + k * 2);
            if (duration < 20 || duration > 2000) { status = LW_PACK_ERR_DURATION; break; }
            s_sequences[slot][existing][k] = (uint16_t)k;
            s_durations[slot][existing][k] = (uint16_t)duration;
        }
        if (status != LW_PACK_OK) break;

        unsigned spec = s_spec_count++;
        snprintf(s_sources[spec].id, sizeof(s_sources[spec].id), "%s.%s", actor_name, name);
        memcpy(s_sources[spec].file, file, strlen(file) + 1);
        s_sources[spec].offset = offset;
        s_sources[spec].frame_bytes = (uint32_t)frame_bytes;
        s_sources[spec].stored_bytes = (uint32_t)stored_stride;
        s_sources[spec].memory = NULL;
        s_sources[spec].frames = (uint16_t)frames;
        s_sources[spec].cache = cache;
        s_sources[spec].raw = (lw_asset_raw_file_t){
            s_file_paths[file_slot], offset, (uint32_t)file_stride, (uint16_t)frames};
        s_specs[spec] = (lw_asset_spec_t){s_sources[spec].id, (uint16_t)width,
                                          (uint16_t)height, (uint16_t)frames, (int16_t)anchor_x,
                                          (int16_t)anchor_y, LV_COLOR_FORMAT_RGB565A8,
                                          trailer_bbox_x[i], trailer_bbox_y[i],
                                          trailer_bbox_w[i], trailer_bbox_h[i],
                                          trailer_prescaled[i], read_frame,
                                          &s_sources[spec], borrow_frame, unborrow_frame,
                                          prefetch_frame};
        /* Clip names stay exactly as the pack spells them: the UI looks up
         * "idle", "blink", "wave" and "walk_<dir>" by name, so a silent rename
         * here would disable those paths with no visible error. */
        memcpy(s_clip_names[slot][existing], name, name_len + 1);
        s_named[slot][existing].name = s_clip_names[slot][existing];
        s_named[slot][existing].clip = (lw_clip_t){s_sources[spec].id, s_sequences[slot][existing],
                                                   s_durations[slot][existing], (uint16_t)frames,
                                                   loop != 0u};
        existing++;
        frames_total += frames;
    }
    fclose(f);

    if (status != LW_PACK_OK) {
        /* Roll back a partial pack so a corrupt file cannot leave half a clip
         * table registered. */
        for (unsigned i = first_clip; i < LW_PACK_MAX_CLIPS; i++) {
            s_named[slot][i].name = NULL;
            s_named[slot][i].clip = (lw_clip_t){0};
            memset(s_clip_names[slot][i], 0, LW_PACK_NAME_BYTES);
        }
        for (unsigned i = first_spec; i < LW_PACK_MAX_SPECS; i++) {
            memset(&s_sources[i], 0, sizeof(s_sources[i]));
            s_specs[i] = (lw_asset_spec_t){0};
        }
        s_spec_count = first_spec;
        /* Release the per-file record too: a rejected pack must not consume one
         * of the LW_PACK_MAX_FILES slots, otherwise four corrupt files in a row
         * would exhaust the table and a later good pack could never load. */
        if (s_file_count == file_slot + 1u) {
            memset(s_file_names[file_slot], 0, LW_PACK_FILE_BYTES);
            memset(s_file_paths[file_slot], 0, LW_PACK_PATH_BYTES);
            s_file_count = file_slot;
        }
        if (fresh) {
            /* Undo the slot publication as well, so a failed first pack leaves
             * no half-configured actor behind for the next add. */
            memset(s_actor_names[slot], 0, sizeof(s_actor_names[slot]));
            s_caches[slot] = NULL;
            s_actor_count--;
        }
        return status;
    }

    s_file_count = file_slot + 1u;

    if (out) {
        /* clips is what THIS file added, named_count is the actor's whole
         * table after the append; keeping them distinct stops a caller from
         * walking past the end of the table it was handed. */
        out->clips = (unsigned)(existing - first_clip);
        out->frames = frames_total;
        out->bytes = (unsigned)size;
        out->named = s_named[slot];
        out->named_count = existing;
    }
    return LW_PACK_OK;
}

lw_pack_status_t lw_pack_registry_add(const char *root, const char *file, const char *actor_name,
                                      lw_file_cache_t *cache, lw_pack_t *out)
{
    return parse(root, file, actor_name, cache, out);
}

/* ---- embedded LWFA fallback ---------------------------------------------
 * The app image always carries one small LWFA pack (firmware/main/art), so the
 * device shows real characters with no TF card. LWFA is a different container
 * from LWPX and is parsed here rather than in a second loader, because the two
 * must land in the SAME spec table: lw_assets_register() replaces the whole
 * registry, so one table per registration is the only way to publish both. */
#define LW_FA_HEADER_BYTES 16u
#define LW_FA_RECORD_BYTES 32u

lw_pack_status_t lw_pack_registry_add_embedded(const uint8_t *data, size_t size,
                                               const char *actor_name, const char *clip_name,
                                               lw_pack_t *out)
{
    if (!data || !actor_name || !clip_name || size < LW_FA_HEADER_BYTES) return LW_PACK_ERR_ARGUMENT;
    if (memcmp(data, "LWFA0001", 8) != 0) return LW_PACK_ERR_MAGIC;
    if (u16(data + 8) != 1u) return LW_PACK_ERR_MAGIC;
    unsigned clips = u16(data + 10);
    if (u32(data + 12) != size) return LW_PACK_ERR_TRUNCATED;
    if (clips == 0 || clips > LW_PACK_MAX_CLIPS) return LW_PACK_ERR_CLIP_COUNT;
    if (size < LW_FA_HEADER_BYTES + (size_t)LW_FA_RECORD_BYTES * clips) return LW_PACK_ERR_TRUNCATED;
    if (s_spec_count + 1u > LW_PACK_MAX_SPECS) return LW_PACK_ERR_CLIP_COUNT;

    /* One slot per actor, exactly like the file loader: an actor may receive an
     * embedded clip and a card clip in the same session. */
    unsigned slot = LW_PACK_MAX_ACTORS;
    for (unsigned i = 0; i < s_actor_count; i++) {
        if (!strcmp(s_actor_names[i], actor_name)) { slot = i; break; }
    }
    bool fresh = slot == LW_PACK_MAX_ACTORS;
    if (fresh) {
        if (s_actor_count >= LW_PACK_MAX_ACTORS) return LW_PACK_ERR_ACTORS;
        if (strlen(actor_name) >= sizeof(s_actor_names[0])) return LW_PACK_ERR_ARGUMENT;
        slot = s_actor_count;
    }
    unsigned existing = 0;
    while (existing < LW_PACK_MAX_CLIPS && s_named[slot][existing].name) existing++;
    if (existing >= LW_PACK_MAX_CLIPS) return LW_PACK_ERR_CLIP_COUNT;

    /* The LWFA record for this actor: the embedded pack names its clip after
     * the character ("male", "qianqian"), while the UI looks clips up as
     * "idle". The caller passes the name to publish, so the container's own
     * spelling never leaks into the UI contract. */
    const uint8_t *record = NULL;
    for (unsigned i = 0; i < clips; i++) {
        const uint8_t *candidate = data + LW_FA_HEADER_BYTES + (size_t)i * LW_FA_RECORD_BYTES;
        char name[13];
        memcpy(name, candidate, 12);
        name[12] = '\0';
        if (!strcmp(name, actor_name)) { record = candidate; break; }
    }
    if (!record) return LW_PACK_ERR_CLIP_NAME;

    unsigned frames = u16(record + 12);
    unsigned width = u16(record + 14);
    unsigned height = u16(record + 16);
    int anchor_x = i16(record + 18);
    int anchor_y = i16(record + 20);
    unsigned loop = record[22];
    unsigned duration = u16(record + 24);
    uint32_t offset = u32(record + 28);
    if (frames == 0 || frames > LW_PACK_MAX_FRAMES) return LW_PACK_ERR_FRAMES;
    if (width == 0 || height == 0 || width > 1024 || height > 600) return LW_PACK_ERR_GEOMETRY;
    if (anchor_x < 0 || anchor_y < 0 || anchor_x > (int)width || anchor_y > (int)height) return LW_PACK_ERR_ANCHOR;
    if (loop > 1u) return LW_PACK_ERR_LOOP;
    if (duration < 20 || duration > 2000) return LW_PACK_ERR_DURATION;
    uint64_t frame_bytes = (uint64_t)width * height * 3u;
    if (frame_bytes == 0 || frame_bytes > LW_PACK_MAX_FRAME_BYTES) return LW_PACK_ERR_GEOMETRY;
    if (offset < LW_FA_HEADER_BYTES + (uint64_t)LW_FA_RECORD_BYTES * clips) return LW_PACK_ERR_OFFSET;
    if ((uint64_t)offset + frame_bytes * frames > size) return LW_PACK_ERR_TRUNCATED;

    memcpy(s_actor_names[slot], actor_name, strlen(actor_name) + 1);
    s_caches[slot] = NULL;
    if (fresh) s_actor_count++;

    unsigned spec = s_spec_count++;
    snprintf(s_sources[spec].id, sizeof(s_sources[spec].id), "%s.%s", actor_name, clip_name);
    s_sources[spec].file[0] = '\0';
    s_sources[spec].offset = 0;
    s_sources[spec].frame_bytes = (uint32_t)frame_bytes;
    s_sources[spec].memory = data + offset;
    s_sources[spec].frames = (uint16_t)frames;
    s_sources[spec].cache = NULL;
    s_sources[spec].raw = (lw_asset_raw_file_t){NULL, 0, 0, 0};
    s_specs[spec] = (lw_asset_spec_t){s_sources[spec].id, (uint16_t)width, (uint16_t)height,
                                      (uint16_t)frames, (int16_t)anchor_x, (int16_t)anchor_y,
                                      LV_COLOR_FORMAT_RGB565A8,
                                      /* LWFA is already stored cropped to the union box
                                       * (tools/build_character_pack.py), so the whole frame
                                       * is content and there is nothing left to crop. Zero
                                       * bbox means "no metadata": the draw then keeps V4.4's
                                       * canvas-sized object, which is what the screenshots
                                       * were captured against. Declaring the full canvas as a
                                       * bbox would instead resize the object to
                                       * scale_px(width) and shift every actor. */
                                      0, 0, 0, 0, 0,
                                      read_frame, &s_sources[spec],
                                      /* No cache behind a flash-mapped pack, so
                                       * there is nothing to borrow, nothing to
                                       * unborrow and nothing to read ahead. */
                                      NULL, NULL, NULL};

    for (unsigned k = 0; k < frames; k++) {
        s_sequences[slot][existing][k] = (uint16_t)k;
        s_durations[slot][existing][k] = (uint16_t)duration;
    }
    memcpy(s_clip_names[slot][existing], clip_name, strlen(clip_name) + 1);
    s_named[slot][existing].name = s_clip_names[slot][existing];
    s_named[slot][existing].clip = (lw_clip_t){s_sources[spec].id, s_sequences[slot][existing],
                                               s_durations[slot][existing], (uint16_t)frames,
                                               loop != 0u};

    if (out) {
        out->clips = 1;
        out->frames = frames;
        out->bytes = (unsigned)frame_bytes * frames;
        out->named = s_named[slot];
        out->named_count = existing + 1u;
    }
    return LW_PACK_OK;
}

bool lw_pack_registry_register(size_t budget)
{
    if (!s_spec_count) return false;
    s_registered = lw_assets_register(s_specs, s_spec_count, budget);
    return s_registered;
}

const lw_named_clip_t *lw_pack_registry_clips(const char *actor_name, unsigned *count)
{
    for (unsigned i = 0; i < s_actor_count; i++) {
        if (strcmp(s_actor_names[i], actor_name) != 0) continue;
        unsigned n = 0;
        while (n < LW_PACK_MAX_CLIPS && s_named[i][n].name) n++;
        if (count) *count = n;
        return n ? s_named[i] : NULL;
    }
    if (count) *count = 0;
    return NULL;
}

void lw_pack_registry_release(void)
{
    if (!s_registered) return;
    if (!lw_assets_register(NULL, 0, LW_PACK_LEASE_BUDGET)) return;
    s_registered = false;
}

lw_pack_status_t lw_pack_open(const char *root, const char *file, const char *actor_name,
                              lw_file_cache_t *cache, lw_pack_t *out)
{
    /* A single-pack open replaces whatever was registered, so it must be able
     * to detach first. If a lease is still live the caller has not destroyed
     * its sprites; refuse instead of clearing the table under it. */
    if (!lw_pack_registry_reset()) return LW_PACK_ERR_REGISTER;
    lw_pack_status_t status = parse(root, file, actor_name, cache, out);
    if (status != LW_PACK_OK) return status;
    if (!lw_pack_registry_register(LW_PACK_LEASE_BUDGET)) return LW_PACK_ERR_REGISTER;
    return LW_PACK_OK;
}














