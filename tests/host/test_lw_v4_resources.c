/* Host tests for the V4 resource layer: the LWPX pack reader, the .lwr scene
 * reader and the manifest/version gate.
 *
 * These run on the build machine because the interesting failures are format
 * bugs, not hardware bugs: a truncated frame, a clip offset past EOF, a pack
 * from a newer pipeline, a missing card. The device adds exactly one variable
 * on top of this - a real card - and that is what the board acceptance list
 * covers.
 *
 * The pack fixtures are built here byte by byte from the documented layout, so
 * this file doubles as an executable specification of LWPX0001 / LWSR0001.
 *
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lw_assets.h"
#include "lw_pack.h"
#include "lw_resource.h"
#include "lw_resource_mount.h"

/* --------------------------------------------------------------- stdout --- */
/* printf into a redirected handle is fully buffered, so a crash would swallow
 * every FAIL line and leave only an exit code. Flush after each check. */
static unsigned checks, failures;
#define CHECK(name, value)                 \
    do {                                   \
        checks++;                          \
        if (!(value)) {                    \
            failures++;                    \
            printf("FAIL: %s\n", name);    \
        }                                  \
        fflush(stdout);                    \
    } while (0)

/* ------------------------------------------------------------ fixtures --- */

#define FRAME_BYTES 196608u /* 256x256 RGB565 + A8 */
#define RECORD_BYTES 108u
#define HEADER_BYTES 16u

static char s_dir[512];
static char s_path[768];

static const char *fixture_dir(void)
{
    if (!s_dir[0]) snprintf(s_dir, sizeof(s_dir), "%s", getenv("LW_RESOURCE_ROOT"));
    return s_dir;
}

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void put32(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

typedef struct {
    const char *name;
    unsigned frames;
    int anchor_y;
    unsigned loop;
    unsigned duration;
} clip_spec_t;

/* Writes a LWPX pack whose frame payload is a recognisable pattern per frame so
 * the reader can be checked for off-by-one frame addressing.
 *
 * Geometry is a parameter, not a constant: the real V4.3 packs are 256x256
 * (characters), 320x320 (daily/dogbed), 544x352 (UI sheet) and 512x544 (skin
 * atlas), and a loader that only accepts 256x256 rejects three of the four. */
static bool write_pack_geometry(const char *fileName, const clip_spec_t *clips, unsigned clip_count,
                                bool truncate, unsigned width, unsigned height)
{
    snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), fileName);
    FILE *f = fopen(s_path, "wb");
    if (!f) return false;
    size_t frame_bytes = (size_t)width * height * 3u;
    uint8_t header[HEADER_BYTES] = {0};
    memcpy(header, "LWPX0001", 8);
    put16(header + 8, width);
    put16(header + 10, height);
    put16(header + 12, clip_count);
    fwrite(header, 1, sizeof(header), f);

    unsigned offset = HEADER_BYTES + RECORD_BYTES * clip_count;
    for (unsigned i = 0; i < clip_count; i++) {
        uint8_t record[RECORD_BYTES] = {0};
        memcpy(record, clips[i].name, strlen(clips[i].name));
        put16(record + 32, clips[i].frames);
        put16(record + 34, (unsigned)(width / 2u));
        put16(record + 36, (unsigned)clips[i].anchor_y);
        record[38] = (uint8_t)clips[i].loop;
        put32(record + 40, offset);
        for (unsigned k = 0; k < clips[i].frames && k < 32u; k++) put16(record + 44 + k * 2, clips[i].duration);
        fwrite(record, 1, sizeof(record), f);
        offset += clips[i].frames * (unsigned)frame_bytes;
    }

    unsigned frames = 0;
    for (unsigned i = 0; i < clip_count; i++) frames += clips[i].frames;
    uint8_t *frame = malloc(frame_bytes);
    if (!frame) { fclose(f); return false; }
    for (unsigned n = 0; n < frames; n++) {
        memset(frame, (int)(n & 0xffu), frame_bytes);
        size_t write = truncate && n + 1u == frames ? frame_bytes / 2u : frame_bytes;
        fwrite(frame, 1, write, f);
    }
    free(frame);
    fclose(f);
    return true;
}

static bool write_pack(const char *fileName, const clip_spec_t *clips, unsigned clip_count,
                       bool truncate)
{
    return write_pack_geometry(fileName, clips, clip_count, truncate, 256u, 256u);
}

/* ---- V4.5 geometry trailer ------------------------------------------------
 * The 108-byte clip record is full and frame offsets are absolute, so the
 * content box travels in a trailer instead. These helpers write it, and the
 * tests below insist that both the trailer-present and trailer-absent paths
 * still read frames correctly - the six packs already on the card have no
 * trailer and must keep working. */
#define TRAILER_HEADER_BYTES 12u
#define TRAILER_RECORD_BYTES 10u

typedef struct { unsigned x, y, w, h, prescaled; } bbox_spec_t;

static bool append_trailer(const char *fileName, const bbox_spec_t *boxes, unsigned clip_count,
                           unsigned kind)
{
    snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), fileName);
    FILE *f = fopen(s_path, "r+b");
    if (!f) return false;
    uint8_t tag[2];
    put16(tag, kind);
    if (fseek(f, 14, SEEK_SET) != 0 || fwrite(tag, 1, sizeof(tag), f) != sizeof(tag)) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    uint8_t header[TRAILER_HEADER_BYTES] = {0};
    memcpy(header, "LWTR0001", 8);
    put16(header + 8, clip_count);
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) { fclose(f); return false; }
    for (unsigned i = 0; i < clip_count; i++) {
        uint8_t record[TRAILER_RECORD_BYTES] = {0};
        put16(record + 0, boxes[i].x);
        put16(record + 2, boxes[i].y);
        put16(record + 4, boxes[i].w);
        put16(record + 6, boxes[i].h);
        put16(record + 8, boxes[i].prescaled);
        if (fwrite(record, 1, sizeof(record), f) != sizeof(record)) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

/* Overwrite bytes counted back from EOF, so a test can corrupt exactly one field
 * of the trailer without rebuilding the whole fixture. */
static bool patch_tail(const char *fileName, long offset_from_end, const uint8_t *data, size_t bytes)
{
    snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), fileName);
    FILE *f = fopen(s_path, "r+b");
    if (!f) return false;
    bool ok = fseek(f, offset_from_end, SEEK_END) == 0 && fwrite(data, 1, bytes, f) == bytes;
    fclose(f);
    return ok;
}

static bool write_scene(const char *fileName, unsigned width, unsigned height)
{
    snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), fileName);
    FILE *f = fopen(s_path, "wb");
    if (!f) return false;
    uint8_t header[HEADER_BYTES] = {0};
    memcpy(header, "LWSR0001", 8);
    put16(header + 8, width);
    put16(header + 10, height);
    fwrite(header, 1, sizeof(header), f);
    size_t bytes = (size_t)width * height * 2u;
    uint8_t *pixels = calloc(1, bytes);
    if (!pixels) { fclose(f); return false; }
    fwrite(pixels, 1, bytes, f);
    free(pixels);
    fclose(f);
    return true;
}

static bool write_text(const char *fileName, const char *text)
{
    snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), fileName);
    FILE *f = fopen(s_path, "wb");
    if (!f) return false;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return true;
}

static bool write_manifest(unsigned schema, unsigned version)
{
    char text[256];
    snprintf(text, sizeof(text), "{\"schema\": %u, \"version\": %u, \"themes\": 3}\n", schema, version);
    return write_text(LW_RESOURCE_MANIFEST_NAME, text);
}

/* ----------------------------------------------------------- pack tests --- */

static void test_pack_ok(void)
{
    static const clip_spec_t clips[] = {
        {"idle", 2, 225, 1, 100},
        {"blink", 2, 225, 0, 125},
        {"wave", 3, 226, 0, 100},
    };
    CHECK("write pack", write_pack("male.lwp", clips, 3, false));

    /* A pack opened without a cache still has to serve frames: the Windows host
     * uses this path, and so does any board build that could not allocate the
     * cache. It blocks, but it must not silently return nothing. */
    lw_pack_t cold = {0};
    CHECK("open without cache", lw_pack_open(fixture_dir(), "male.lwp", "male", NULL, &cold) ==
                                    LW_PACK_OK);
    const lw_asset_spec_t *cold_spec = lw_assets_find("male.idle");
    static uint8_t cold_buffer[FRAME_BYTES];
    memset(cold_buffer, 0xEE, sizeof(cold_buffer));
    CHECK("uncached read works", cold_spec && cold_spec->read(cold_spec->context, 0, cold_buffer,
                                                              sizeof(cold_buffer)));
    CHECK("uncached frame content", cold_buffer[0] == 0x00u &&
                                       cold_buffer[FRAME_BYTES - 1] == 0x00u);
    CHECK("uncached frame address", cold_spec &&
                                        cold_spec->read(cold_spec->context, 1, cold_buffer,
                                                        sizeof(cold_buffer)) &&
                                        cold_buffer[0] == 0x01u);

    /* The board path: one PSRAM file cache in front of the pack file. */
    lw_file_cache_t *cache = lw_file_cache_open(fixture_dir(), 4u * FRAME_BYTES);
    CHECK("cache opened", cache != NULL);
    lw_pack_registry_reset();
    lw_pack_t pack = {0};
    lw_pack_status_t status = lw_pack_open(fixture_dir(), "male.lwp", "male", cache, &pack);    CHECK("open pack", status == LW_PACK_OK);
    CHECK("clip count", pack.clips == 3);
    CHECK("frame count", pack.frames == 7);
    CHECK("named count", pack.named_count == 3);
    CHECK("clip name kept", pack.named && !strcmp(pack.named[0].name, "idle"));
    CHECK("blink name kept", pack.named && !strcmp(pack.named[1].name, "blink"));
    CHECK("wave name kept", pack.named && !strcmp(pack.named[2].name, "wave"));
    CHECK("idle loops", pack.named && pack.named[0].clip.loop);
    CHECK("blink does not loop", pack.named && !pack.named[1].clip.loop);
    CHECK("wave frames", pack.named && pack.named[2].clip.count == 3u);
    CHECK("blink duration", pack.named && pack.named[1].clip.duration_ms &&
          pack.named[1].clip.duration_ms[1] == 125u);
    CHECK("asset id prefixed", pack.named && !strcmp(pack.named[0].clip.asset_id, "male.idle"));
    CHECK("registry registered", lw_assets_find("male.wave") != NULL);
    CHECK("anchor propagated", lw_assets_find("male.wave") &&
          lw_assets_find("male.wave")->anchor_y == 226);
    CHECK("format is rgb565a8", lw_assets_find("male.blink") &&
          lw_assets_find("male.blink")->format == LV_COLOR_FORMAT_RGB565A8);

    /* Frame addressing: frames are numbered continuously across the whole
     * pack, so idle owns 0-1, blink 2-3 and wave 4-6. A wrong stride or a
     * per-clip frame origin shows up here and nowhere else. */
    const lw_asset_spec_t *spec = lw_assets_find("male.wave");
    CHECK("frame stride", spec && lw_asset_frame_bytes(spec) == FRAME_BYTES);
    static uint8_t buffer[FRAME_BYTES];
    memset(buffer, 0xEE, sizeof(buffer));
    bool r0 = spec && spec->read(spec->context, 0, buffer, sizeof(buffer));
    CHECK("read frame 0", r0);
    CHECK("frame 0 content", buffer[0] == 0x04u && buffer[FRAME_BYTES - 1] == 0x04u);
    CHECK("read frame 2", spec && spec->read(spec->context, 2, buffer, sizeof(buffer)));
    CHECK("frame 2 content", buffer[0] == 0x06u && buffer[FRAME_BYTES - 1] == 0x06u);
    CHECK("frame past end refused", spec && !spec->read(spec->context, 3, buffer, sizeof(buffer)));
    CHECK("short buffer refused", spec && !spec->read(spec->context, 0, buffer, 16));
}

static void test_pack_append(void)
{
    /* qianqian keeps her base pack and gains wave: the same actor with a second
     * file appends instead of replacing. */
    static const clip_spec_t base[] = {
        {"idle", 1, 199, 1, 125},
        {"touch", 1, 199, 0, 125},
    };
    static const clip_spec_t extra[] = {
        {"wave", 2, 199, 0, 100},
    };
    lw_pack_registry_reset();
    CHECK("append base", write_pack("q-base.lwp", base, 2, false));
    CHECK("append extra", write_pack("q-wave.lwp", extra, 1, false));
    CHECK("add base", lw_pack_registry_add(fixture_dir(), "q-base.lwp", "qianqian", NULL, NULL) == LW_PACK_OK);
    lw_pack_status_t added = lw_pack_registry_add(fixture_dir(), "q-wave.lwp", "qianqian", NULL, NULL);
    CHECK("add extra", added == LW_PACK_OK);
    unsigned count = 0;
    const lw_named_clip_t *clips = lw_pack_registry_clips("qianqian", &count);
    CHECK("appended clip count", count == 3);
    CHECK("base clip preserved", clips && !strcmp(clips[0].name, "idle"));
    CHECK("wave appended", clips && !strcmp(clips[2].name, "wave"));

    /* The specs handed to lw_assets must be ONE contiguous table that stays
     * alive at a stable address: lw_assets only stores the pointer. Asserting
     * the pointer alone would not catch a reordered or copied table, and a
     * dangling table crashes the board at the first clip lookup. The table has
     * to be registered first, because lw_assets_find only sees a live one. */
    CHECK("register combined", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));
    const lw_asset_spec_t *base_spec = lw_assets_find("qianqian.idle");
    const lw_asset_spec_t *touch_spec = lw_assets_find("qianqian.touch");
    const lw_asset_spec_t *wave_spec = lw_assets_find("qianqian.wave");
    CHECK("combined specs contiguous", base_spec && touch_spec && wave_spec &&
                                           touch_spec == base_spec + 1 &&
                                           wave_spec == base_spec + 2);
    CHECK("wave spec frames", wave_spec && wave_spec->frames == 2u);
    CHECK("wave spec anchor", wave_spec && wave_spec->anchor_y == 199);

    CHECK("wave resolvable", lw_assets_find("qianqian.wave") != NULL);
    CHECK("touch resolvable", lw_assets_find("qianqian.touch") != NULL);
    lw_pack_registry_release();
    lw_pack_registry_reset();
}

static void test_pack_append_reads_right_file(void)
{
    /* Regression: an actor's SECOND pack used to overwrite the path shared by
     * the first pack's specs, because every source pointed at one per-actor
     * buffer. The art then came from the wrong file at the right offset - no
     * error, no log, just the wrong character. Reading a frame is the only
     * check that catches it; counting clips does not.
     *
     * The two fixtures are sized so the failure is unambiguous: base carries
     * two clips, so its second clip sits at offset 232 + FRAME_BYTES, which is
     * past the end of the one-clip extra pack. If the base spec is still
     * pointing at the extra file the read fails outright instead of returning
     * plausible pixels. */
    static const clip_spec_t base[] = {
        {"idle", 1, 199, 1, 125},
        {"touch", 1, 199, 0, 125},
    };
    static const clip_spec_t extra[] = {
        {"wave", 1, 199, 0, 100},
    };
    lw_pack_registry_reset();
    CHECK("two-file base written", write_pack("r-base.lwp", base, 2, false));
    CHECK("two-file extra written", write_pack("r-wave.lwp", extra, 1, false));
    CHECK("two-file base added",
          lw_pack_registry_add(fixture_dir(), "r-base.lwp", "qianqian", NULL, NULL) == LW_PACK_OK);
    CHECK("two-file extra added",
          lw_pack_registry_add(fixture_dir(), "r-wave.lwp", "qianqian", NULL, NULL) == LW_PACK_OK);
    CHECK("two-file register", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));

    /* Base clip 2 lives at 232 + FRAME_BYTES; only r-base.lwp is that long. */
    lw_asset_lease_t *touch = lw_asset_acquire("qianqian.touch", 0u);
    CHECK("first pack still readable after append", touch != NULL);
    if (touch) {
        const lv_image_dsc_t *image = lw_asset_image(touch);
        CHECK("first pack pixels come from its own file",
              image && image->data && image->data[0] == 1u);
        lw_asset_release(touch);
    }
    /* The appended clip must still resolve against its own file. */
    lw_asset_lease_t *wave = lw_asset_acquire("qianqian.wave", 0u);
    CHECK("appended pack readable", wave != NULL);
    if (wave) {
        const lv_image_dsc_t *image = lw_asset_image(wave);
        CHECK("appended pixels come from its own file",
              image && image->data && image->data[0] == 0u);
        lw_asset_release(wave);
    }

    lw_pack_registry_reset();
    snprintf(s_path, sizeof(s_path), "%s/r-base.lwp", fixture_dir());
    remove(s_path);
    snprintf(s_path, sizeof(s_path), "%s/r-wave.lwp", fixture_dir());
    remove(s_path);
}

static void test_pack_reset_refuses_live_lease(void)
{
    /* Regression: reset used to clear the spec table even when lw_assets had
     * refused to detach, leaving the registry pointing at zeroed specs whose
     * ids are NULL - the exact shape of the V3.1 hardware panic. A refused
     * reset must change nothing at all. */
    static const clip_spec_t clips[] = {
        {"idle", 1, 199, 1, 125},
    };
    lw_pack_registry_reset();
    CHECK("lease-guard fixture", write_pack("r-lease.lwp", clips, 1, false));
    CHECK("lease-guard add",
          lw_pack_registry_add(fixture_dir(), "r-lease.lwp", "qianqian", NULL, NULL) == LW_PACK_OK);
    CHECK("lease-guard register", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));

    lw_asset_lease_t *lease = lw_asset_acquire("qianqian.idle", 0u);
    CHECK("lease held", lease != NULL);
    CHECK("reset refused while a lease is live", !lw_pack_registry_reset());
    /* The whole point: the table is still intact and still findable. */
    CHECK("registry survived the refused reset", lw_assets_find("qianqian.idle") != NULL);
    CHECK("registry stats still count the lease", lw_assets_stats().live_leases == 1u);
    if (lease) lw_asset_release(lease);

    CHECK("reset succeeds once the lease is gone", lw_pack_registry_reset());
    CHECK("registry empty after a real reset", lw_assets_find("qianqian.idle") == NULL);

    snprintf(s_path, sizeof(s_path), "%s/r-lease.lwp", fixture_dir());
    remove(s_path);
}

static void test_pack_rejections(void)
{
    /* Every one of these must fail WITHOUT leaving a half-registered actor. */
    lw_pack_registry_reset();

    static const clip_spec_t dup[] = {
        {"wave", 1, 225, 0, 100},
        {"wave", 1, 225, 0, 100},
    };
    CHECK("duplicate fixture", write_pack("dup.lwp", dup, 2, false));
    CHECK("duplicate clip refused",
          lw_pack_registry_add(fixture_dir(), "dup.lwp", "male", NULL, NULL) == LW_PACK_ERR_DUPLICATE);
    CHECK("duplicate left nothing", lw_assets_find("male.wave") == NULL);

    static const clip_spec_t zero[] = {{"idle", 0, 225, 1, 100}};
    CHECK("zero-frame fixture", write_pack("zero.lwp", zero, 1, false));
    CHECK("zero frames refused",
          lw_pack_registry_add(fixture_dir(), "zero.lwp", "male", NULL, NULL) == LW_PACK_ERR_FRAMES);

    static const clip_spec_t slow[] = {{"idle", 1, 225, 1, 5000}};
    CHECK("slow fixture", write_pack("slow.lwp", slow, 1, false));
    CHECK("bad duration refused",
          lw_pack_registry_add(fixture_dir(), "slow.lwp", "male", NULL, NULL) ==
              LW_PACK_ERR_DURATION);

    static const clip_spec_t good[] = {{"idle", 3, 225, 1, 100}};
    CHECK("truncated fixture", write_pack("cut.lwp", good, 1, true));
    lw_pack_t pack = {0};
    /* The header still claims three full frames but the file stops halfway
     * through the last one. The parser must notice at parse time, not when a
     * sprite reaches for that frame mid-animation. */
    lw_pack_status_t truncated = lw_pack_registry_add(fixture_dir(), "cut.lwp", "male", NULL, &pack);
    CHECK("truncated pack refused", truncated == LW_PACK_ERR_OFFSET);
    CHECK("truncated left nothing registered", lw_assets_find("male.idle") == NULL);
    lw_pack_registry_reset();

    CHECK("missing file refused",
          lw_pack_registry_add(fixture_dir(), "nope.lwp", "male", NULL, NULL) == LW_PACK_ERR_OPEN);

    /* A renamed LWFA is not an LWPX: the extension must not be trusted. */
    CHECK("renamed lwfa fixture", write_text("fake.lwp", "LWFA0001 not really a pack"));
    CHECK("wrong magic refused",
          lw_pack_registry_add(fixture_dir(), "fake.lwp", "male", NULL, NULL) == LW_PACK_ERR_MAGIC);
    lw_pack_registry_reset();
}

/* ------------------------------------------------- dynamic geometry tests --- */

/* The four geometries the V4.3 art actually uses. Before the loader read the
 * header, three of these were rejected as bad-geometry on hardware while the
 * Windows build - which always read the header - showed the art fine. That
 * asymmetry is the bug this test locks down. */
static void test_pack_geometry(void)
{
    static const struct { const char *file; const char *actor; const char *clip;
                          unsigned width, height; int anchor; } shapes[] = {
        {"g-char.lwp", "male", "idle", 256u, 256u, 225},
        {"g-daily.lwp", "v42", "couple.seated", 320u, 320u, 274},
        {"g-ui.lwp", "v42ui", "sheet", 544u, 352u, 176},
        {"g-skin.lwp", "v43skin", "atlas", 512u, 544u, 272},
    };
    lw_pack_registry_reset();
    for (unsigned i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        clip_spec_t clip = {shapes[i].clip, 1, shapes[i].anchor, 0, 100};
        char label[64];
        snprintf(label, sizeof(label), "geometry %ux%u written", shapes[i].width, shapes[i].height);
        CHECK(label, write_pack_geometry(shapes[i].file, &clip, 1, false, shapes[i].width,
                                         shapes[i].height));
        snprintf(label, sizeof(label), "geometry %ux%u accepted", shapes[i].width, shapes[i].height);
        CHECK(label, lw_pack_registry_add(fixture_dir(), shapes[i].file, shapes[i].actor, NULL,
                                          NULL) == LW_PACK_OK);
    }

    /* All four geometries co-exist in one registry: the pack families are
     * different actors, so nothing collides. Nothing is visible through
     * lw_assets_find() until the pending table is published. */
    CHECK("four pack families publish", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));

    for (unsigned i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        char id[64];
        snprintf(id, sizeof(id), "%s.%s", shapes[i].actor, shapes[i].clip);
        const lw_asset_spec_t *spec = lw_assets_find(id);
        char label[64];
        snprintf(label, sizeof(label), "geometry %ux%u in spec", shapes[i].width, shapes[i].height);
        CHECK(label, spec && spec->width == shapes[i].width && spec->height == shapes[i].height);
        /* The frame stride is derived from the spec, so a loader that stored the
         * right width and height but a constant stride still fails here. */
        size_t expect = (size_t)shapes[i].width * shapes[i].height * 3u;
        snprintf(label, sizeof(label), "geometry %ux%u frame bytes", shapes[i].width, shapes[i].height);
        CHECK(label, lw_asset_frame_bytes(spec) == expect);
    }

    const lw_asset_spec_t *skin = lw_assets_find("v43skin.atlas");
    CHECK("skin atlas registered", skin != NULL && skin->width == 512u && skin->height == 544u);
    /* The atlas is 835,584 B; the old 786,432 B budget refused exactly this.
     * One acquire, one release: a leaked lease would block the reset below and
     * poison every later test in the file. */
    lw_asset_lease_t *lease = lw_asset_acquire("v43skin.atlas", 0);
    CHECK("skin atlas is acquirable", lease != NULL);
    if (lease) lw_asset_release(lease);
    lw_pack_registry_reset();

    /* A pack that claims more than the registry accepts must fail at parse
     * time, not at register time, so the reason names the file. */
    static const clip_spec_t wide = {"wide", 1, 10, 0, 100};
    CHECK("over-wide fixture", write_pack_geometry("g-wide.lwp", &wide, 1, false, 2000u, 64u));
    CHECK("over-wide refused", lw_pack_registry_add(fixture_dir(), "g-wide.lwp", "male", NULL,
                                                    NULL) == LW_PACK_ERR_GEOMETRY);
    static const clip_spec_t tall = {"tall", 1, 10, 0, 100};
    CHECK("over-tall fixture", write_pack_geometry("g-tall.lwp", &tall, 1, false, 64u, 700u));
    CHECK("over-tall refused", lw_pack_registry_add(fixture_dir(), "g-tall.lwp", "male", NULL,
                                                    NULL) == LW_PACK_ERR_GEOMETRY);
    /* Zero geometry is the classic uninitialised header. */
    static const clip_spec_t empty = {"empty", 1, 0, 0, 100};
    CHECK("zero-geometry fixture", write_pack_geometry("g-zero.lwp", &empty, 1, false, 0u, 0u));
    CHECK("zero geometry refused", lw_pack_registry_add(fixture_dir(), "g-zero.lwp", "male", NULL,
                                                        NULL) == LW_PACK_ERR_GEOMETRY);
    lw_pack_registry_reset();
}

/* ---- V4.5: geometry trailer ---------------------------------------------- */

static void test_pack_geometry_trailer(void)
{
    lw_pack_registry_reset();

    /* One clip whose content box is a small corner of the canvas. The trailer is
     * what turns "blend the whole 256x256 canvas" into "blend 40x30 pixels", so
     * the spec has to carry it verbatim. */
    static const clip_spec_t clips[] = {
        {"idle", 2, 225, 1, 100},
        {"wave", 1, 226, 0, 100},
    };
    static const bbox_spec_t boxes[] = {
        {75u, 17u, 105u, 208u, 0u},
        {0u, 0u, 256u, 256u, 1u},
    };
    CHECK("trailer fixture written", write_pack("t-geom.lwp", clips, 2, false));
    CHECK("trailer appended", append_trailer("t-geom.lwp", boxes, 2, 1u));
    CHECK("trailer pack accepted",
          lw_pack_registry_add(fixture_dir(), "t-geom.lwp", "male", NULL, NULL) == LW_PACK_OK);
    CHECK("trailer pack published", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));

    const lw_asset_spec_t *idle = lw_assets_find("male.idle");
    CHECK("bbox x", idle && idle->bbox_x == 75u);
    CHECK("bbox y", idle && idle->bbox_y == 17u);
    CHECK("bbox w", idle && idle->bbox_w == 105u);
    CHECK("bbox h", idle && idle->bbox_h == 208u);
    CHECK("bbox not prescaled", idle && idle->prescaled == 0u);
    const lw_asset_spec_t *wave = lw_assets_find("male.wave");
    CHECK("prescale flag", wave && wave->prescaled == 1u);

    /* Frames must still be readable: the trailer sits past the payload, and a
     * reader that forgot to bound clips by the trailer start would read bbox
     * bytes as pixels. */
    lw_asset_lease_t *lease = lw_asset_acquire("male.idle", 0);
    CHECK("trailer pack serves frames", lease != NULL);
    if (lease) lw_asset_release(lease);

    /* Geometry the draw path derives from the box. */
    lw_asset_draw_t g = {0};
    lw_asset_draw_geometry(idle, 288u, &g);
    CHECK("bbox draw keeps scale", g.scale == 288u);
    /* 105*288/256 = 118.1 -> 118, 208*288/256 = 234 exactly. */
    CHECK("bbox draw width", g.obj_w == 118u);
    CHECK("bbox draw height", g.obj_h == 234u);
    /* The object origin sits at the content origin, so the anchor distance is
     * scaled(anchor) - scaled(bbox_x) = 144 - 84 = 60, NOT the unscaled anchor
     * minus the box origin. Getting this wrong moves the sprite by the scale
     * correction on the desktop and nowhere else, which is why it is pinned. */
    CHECK("bbox draw anchor dx", g.anchor_dx == 60);
    CHECK("bbox draw anchor dy", g.anchor_dy == (int32_t)((225 * 288 + 128) >> 8) -
                                               (int32_t)((17 * 288 + 128) >> 8));
    /* LV_ALIGN_CENTER adds obj_w/2 - img_w/2 = 59 - 128 = -69, and the content
     * sits 84 px into the image, so the offset has to cancel both: -84 + 69. */
    CHECK("bbox draw offset x", g.offset_x == -15);
    CHECK("bbox draw offset y", g.offset_y == -(int32_t)((17 * 288 + 128) >> 8) -
                                               (234 / 2 - 256 / 2));

    /* A prescaled asset must draw at 1:1 with no offset, whatever scale the
     * sprite asks for: that is the whole point of baking the correction in. */
    lw_asset_draw_geometry(wave, 282u, &g);
    CHECK("prescaled forces 1x", g.scale == 256u);
    CHECK("prescaled full box", g.obj_w == 256u && g.obj_h == 256u);
    CHECK("prescaled no offset", g.offset_x == 0 && g.offset_y == 0);
    CHECK("prescaled no anchor shift", g.anchor_dx == 128 && g.anchor_dy == 226);

    /* No metadata at all: exactly the V4.4 draw. The object stays at the decoded
     * canvas size and no offset is applied, because LVGL clips to obj->coords and
     * V4.4 relied on that: a 288/256 sprite had its rightmost and bottom 32
     * destination columns clipped away. Sizing the object to 288 here instead
     * would reveal pixels V4.4 never drew, which is what made 125 of 156 host
     * screenshots differ from HEAD. */
    static const clip_spec_t plain[] = {{"idle", 1, 225, 1, 100}};
    CHECK("legacy fixture written", write_pack("t-plain.lwp", plain, 1, false));
    lw_pack_registry_reset();
    CHECK("legacy pack accepted",
          lw_pack_registry_add(fixture_dir(), "t-plain.lwp", "male", NULL, NULL) == LW_PACK_OK);
    CHECK("legacy pack published", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));
    const lw_asset_spec_t *legacy = lw_assets_find("male.idle");
    CHECK("legacy has no bbox", legacy && legacy->bbox_w == 0u && legacy->bbox_h == 0u);
    lw_asset_draw_geometry(legacy, 288u, &g);
    CHECK("legacy keeps canvas-sized object", g.obj_w == 256u && g.obj_h == 256u);
    CHECK("legacy applies no offset", g.offset_x == 0 && g.offset_y == 0);
    CHECK("legacy keeps requested scale", g.scale == 288u);
    CHECK("legacy anchor is plain scale", g.anchor_dx == 144 && g.anchor_dy == 253);

    /* ---- rejection paths ------------------------------------------------- */
    lw_pack_registry_reset();
    static const clip_spec_t one[] = {{"idle", 1, 225, 1, 100}};
    static const bbox_spec_t outside[] = {{200u, 17u, 105u, 208u, 0u}}; /* 200+105 > 256 */
    CHECK("outside-box fixture", write_pack("t-outside.lwp", one, 1, false));
    CHECK("outside-box trailer", append_trailer("t-outside.lwp", outside, 1, 1u));
    CHECK("outside box refused", lw_pack_registry_add(fixture_dir(), "t-outside.lwp", "male", NULL,
                                                      NULL) == LW_PACK_ERR_TRAILER);

    static const bbox_spec_t good[] = {{75u, 17u, 105u, 208u, 0u}};
    CHECK("bad-magic fixture", write_pack("t-badmagic.lwp", one, 1, false));
    CHECK("bad-magic trailer", append_trailer("t-badmagic.lwp", good, 1, 1u));
    static const uint8_t junk[8] = {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};
    /* The trailer header is the last 12 + 1*10 = 22 bytes; its first 8 are the
     * magic. */
    CHECK("bad-magic patched", patch_tail("t-badmagic.lwp", -(long)(22), junk, sizeof(junk)));
    CHECK("bad magic refused", lw_pack_registry_add(fixture_dir(), "t-badmagic.lwp", "male", NULL,
                                                    NULL) == LW_PACK_ERR_TRAILER);

    CHECK("clip-count-mismatch fixture", write_pack("t-count.lwp", one, 1, false));
    CHECK("clip-count-mismatch trailer", append_trailer("t-count.lwp", good, 1, 1u));
    static const uint8_t wrong_count[2] = {2u, 0u}; /* says 2 clips, trailer holds 1 */
    CHECK("clip-count patched", patch_tail("t-count.lwp", -(long)(22) + 8, wrong_count, 2u));
    CHECK("clip-count mismatch refused", lw_pack_registry_add(fixture_dir(), "t-count.lwp", "male",
                                                              NULL, NULL) == LW_PACK_ERR_TRAILER);

    /* An unknown trailer kind must be refused rather than ignored: a future
     * trailer may change how frames are interpreted. */
    CHECK("unknown-kind fixture", write_pack("t-kind.lwp", one, 1, false));
    CHECK("unknown-kind trailer", append_trailer("t-kind.lwp", good, 1, 2u));
    CHECK("unknown trailer kind refused", lw_pack_registry_add(fixture_dir(), "t-kind.lwp", "male",
                                                               NULL, NULL) == LW_PACK_ERR_TRAILER);

    /* A clip that claims to start inside the trailer is corrupt, not merely
     * odd: it would read bbox bytes as pixels. */
    CHECK("overlap fixture", write_pack("t-overlap.lwp", one, 1, false));
    CHECK("overlap trailer", append_trailer("t-overlap.lwp", good, 1, 1u));
    static const uint8_t shifted_offset[4] = {0x00u, 0x10u, 0x00u, 0x00u}; /* 0x1000, past EOF */
    CHECK("overlap offset patched",
          patch_tail("t-overlap.lwp", -(long)(22) - (long)(RECORD_BYTES) + 40 + (long)0, NULL, 0));
    {
        snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), "t-overlap.lwp");
        FILE *f = fopen(s_path, "r+b");
        CHECK("overlap reopen", f != NULL);
        if (f) {
            /* The single clip record starts at 16; its offset field is at +40. */
            bool ok = fseek(f, 16 + 40, SEEK_SET) == 0 &&
                      fwrite(shifted_offset, 1, sizeof(shifted_offset), f) == sizeof(shifted_offset);
            fclose(f);
            CHECK("overlap offset written", ok);
        }
    }
    CHECK("offset into trailer refused", lw_pack_registry_add(fixture_dir(), "t-overlap.lwp", "male",
                                                              NULL, NULL) == LW_PACK_ERR_OFFSET);
    lw_pack_registry_reset();
}

/* --------------------------------------------------------- resource tests --- */

static void test_pack_clip_budget(void)
{
    /* The frozen V4.3 character set is six files: male.lwp (7 clips),
     * qianqian-base.lwp (13 clips), qianqian-wave.lwp (1), daily.lwp (8),
     * ui.lwp (1) and skin.lwp (1) - 31 ids in 5 actor slots. Both caps used to
     * be too small for that: MAX_CLIPS 8 refused qianqian's base pack outright
     * (bad-clip-count, so she silently fell back to one embedded idle frame)
     * and MAX_ACTORS 2 refused the three visual packs. This test writes packs
     * with the real record counts and insists every one of them parses and is
     * addressable, because "the pack is on the card" and "the pack is usable"
     * are different claims and only the second one shows up on screen. */
    lw_pack_registry_reset();

    /* 13 clips with the real names and frame counts of qianqian-base.lwp. */
    static const clip_spec_t base[] = {
        {"idle",            8, 226, 1, 125},
        {"sleep",           4, 216, 1, 200},
        {"sleep_enter",    13, 226, 0, 100},
        {"sleep_idle",      8, 216, 1, 200},
        {"sleep_turn",     16, 216, 0, 120},
        {"walk_down",       8, 226, 1, 100},
        {"walk_down_left",  8, 226, 1, 100},
        {"walk_down_right", 8, 226, 1, 100},
        {"walk_left",       8, 226, 1, 100},
        {"walk_right",      8, 226, 1, 100},
        {"walk_up",         8, 226, 1, 100},
        {"walk_up_left",    8, 226, 1, 100},
        {"walk_up_right",   8, 226, 1, 100},
    };
    CHECK("13-clip base pack written",
          write_pack_geometry("c-base.lwp", base, 13, false, 256u, 256u));
    CHECK("13-clip base pack accepted",
          lw_pack_registry_add(fixture_dir(), "c-base.lwp", "qianqian", NULL, NULL) == LW_PACK_OK);
    unsigned count = 0;
    const lw_named_clip_t *clips = lw_pack_registry_clips("qianqian", &count);
    CHECK("13-clip base pack keeps all clips", count == 13);
    CHECK("last base clip addressable", clips && !strcmp(clips[12].name, "walk_up_right"));

    /* The wave pack appends to the same actor: 13 + 1 must still fit, which is
     * the case the old cap could not represent at all. */
    static const clip_spec_t extra[] = {{"wave", 16, 226, 0, 100}};
    CHECK("wave append written", write_pack_geometry("c-wave.lwp", extra, 1, false, 256u, 256u));
    CHECK("wave append accepted",
          lw_pack_registry_add(fixture_dir(), "c-wave.lwp", "qianqian", NULL, NULL) == LW_PACK_OK);
    clips = lw_pack_registry_clips("qianqian", &count);
    CHECK("base plus wave is 14 clips", count == 14);

    /* Five actor slots: two characters plus the three visual namespaces. Each
     * gets its own geometry, exactly like the real files. */
    static const clip_spec_t one[] = {{"only", 1, 100, 0, 100}};
    static const struct { const char *file, *actor; unsigned w, h; } families[] = {
        {"c-male.lwp", "male", 256u, 256u},
        {"c-v42.lwp", "v42", 320u, 320u},
        {"c-v42ui.lwp", "v42ui", 544u, 352u},
        {"c-v43skin.lwp", "v43skin", 512u, 544u},
    };
    for (unsigned i = 0; i < sizeof(families) / sizeof(families[0]); i++) {
        char label[72];
        snprintf(label, sizeof(label), "%s written", families[i].file);
        CHECK(label, write_pack_geometry(families[i].file, one, 1, false, families[i].w,
                                         families[i].h));
        snprintf(label, sizeof(label), "%s accepted", families[i].file);
        CHECK(label, lw_pack_registry_add(fixture_dir(), families[i].file, families[i].actor, NULL,
                                          NULL) == LW_PACK_OK);
    }
    CHECK("all 31 specs publish", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));
    CHECK("base idle resolvable", lw_assets_find("qianqian.idle") != NULL);
    CHECK("base walk resolvable", lw_assets_find("qianqian.walk_up_right") != NULL);
    CHECK("wave still resolvable", lw_assets_find("qianqian.wave") != NULL);
    CHECK("daily family resolvable", lw_assets_find("v42.only") != NULL);
    CHECK("ui family resolvable", lw_assets_find("v42ui.only") != NULL);
    CHECK("skin family resolvable", lw_assets_find("v43skin.only") != NULL);

    /* A pack that still exceeds the clip cap must be refused by name, not
     * silently truncated: truncation would leave an actor with a partial clip
     * table and no error anywhere. */
    lw_pack_registry_reset();
    static clip_spec_t too_many[33];
    for (unsigned i = 0; i < 33; i++) {
        static char names[33][16];
        snprintf(names[i], sizeof(names[i]), "clip%u", i);
        too_many[i] = (clip_spec_t){names[i], 1, 10, 0, 100};
    }
    CHECK("33-clip pack written", write_pack_geometry("c-over.lwp", too_many, 33, false, 64u, 64u));
    CHECK("33 clips refused", lw_pack_registry_add(fixture_dir(), "c-over.lwp", "male", NULL, NULL)
                                  == LW_PACK_ERR_CLIP_COUNT);
    lw_pack_registry_reset();
}

static void test_resource_ok(void)
{
    CHECK("scene 0", write_scene("scene-0.lwr", 1024, 600));
    CHECK("scene 1", write_scene("scene-1.lwr", 1024, 600));
    CHECK("scene 2", write_scene("scene-2.lwr", 1024, 600));
    CHECK("thumb 0", write_scene("scene-0-thumb.lwr", 256, 150));
    CHECK("thumb 1", write_scene("scene-1-thumb.lwr", 256, 150));
    CHECK("thumb 2", write_scene("scene-2-thumb.lwr", 256, 150));
    CHECK("manifest", write_manifest(LW_RESOURCE_SCHEMA, LW_RESOURCE_PACK_VERSION));

    lw_resource_set_root(fixture_dir());
    CHECK("verify ok", lw_resource_verify());
    const lw_resource_report_t *report = lw_resource_report();
    CHECK("status ok", report->status == LW_RESOURCE_OK);
    CHECK("three themes", report->themes == 7u);
    CHECK("verified flag", report->verified);
    CHECK("root echoed", !strcmp(report->root, fixture_dir()));
    CHECK("no fallback", !lw_resource_fallback());
}

static void test_resource_version_gate(void)
{
    /* A newer pack may contain clips this firmware cannot play. Refusing is the
     * only safe answer; half-reading it would produce silent wrong art. */
    CHECK("newer manifest", write_manifest(LW_RESOURCE_SCHEMA, LW_RESOURCE_PACK_VERSION + 1u));
    CHECK("newer pack refused", !lw_resource_verify());
    CHECK("newer pack status", lw_resource_report()->status == LW_RESOURCE_VERSION_MISMATCH);
    CHECK("newer pack falls back", lw_resource_fallback());
    CHECK("newer status text", strcmp(lw_resource_status_text(LW_RESOURCE_VERSION_MISMATCH), "") != 0);

    CHECK("wrong schema", write_manifest(LW_RESOURCE_SCHEMA + 1u, LW_RESOURCE_PACK_VERSION));
    CHECK("wrong schema refused", !lw_resource_verify());

    CHECK("old pack accepted", write_manifest(LW_RESOURCE_SCHEMA, 1u));
    CHECK("old pack verifies", lw_resource_verify());
}

static void test_resource_missing_and_broken(void)
{
    CHECK("manifest restored", write_manifest(LW_RESOURCE_SCHEMA, LW_RESOURCE_PACK_VERSION));

    snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), LW_RESOURCE_MANIFEST_NAME);
    CHECK("remove manifest", remove(s_path) == 0);
    CHECK("manifest missing refused", !lw_resource_verify());
    CHECK("manifest missing status",
          lw_resource_report()->status == LW_RESOURCE_MANIFEST_MISSING);

    CHECK("corrupt manifest", write_text(LW_RESOURCE_MANIFEST_NAME, "this is not json"));
    CHECK("corrupt manifest refused", !lw_resource_verify());
    CHECK("corrupt manifest status",
          lw_resource_report()->status == LW_RESOURCE_MANIFEST_INVALID);

    CHECK("manifest restored again", write_manifest(LW_RESOURCE_SCHEMA, LW_RESOURCE_PACK_VERSION));
    /* Missing payload with a valid manifest is a broken medium, not a version
     * problem, and the UI text differs. */
    snprintf(s_path, sizeof(s_path), "%s/scene-2.lwr", fixture_dir());
    CHECK("remove scene 2", remove(s_path) == 0);
    CHECK("partial payload verifies", lw_resource_verify());
    CHECK("only two themes", lw_resource_report()->themes == 3u);
    CHECK("scene 2 restored", write_scene("scene-2.lwr", 1024, 600));

    /* A theme with a wrong geometry header must not count as present. */
    CHECK("bad geometry scene", write_scene("scene-1.lwr", 512, 300));
    CHECK("bad geometry verifies", lw_resource_verify());
    CHECK("theme 1 dropped", (lw_resource_report()->themes & 2u) == 0u);
    CHECK("scene 1 restored", write_scene("scene-1.lwr", 1024, 600));
}

static void test_resource_no_medium(void)
{
    lw_resource_set_root(NULL);
    CHECK("no root refused", !lw_resource_verify());
    CHECK("no root status", lw_resource_report()->status == LW_RESOURCE_NOT_CONFIGURED);
    CHECK("no root falls back", lw_resource_fallback());
    CHECK("embedded root non-empty", lw_resource_embedded_root()[0] != '\0');
}

/* ---- embedded LWFA fallback ---------------------------------------------
 * The app image always carries an LWFA pack so the board shows real characters
 * with no card. LWFA is a different container from LWPX (32-byte records, its
 * own per-clip geometry, payload already in the flash mapping), so it gets its
 * own tests rather than being assumed to work because LWPX does. */

#define FA_HEADER_BYTES 16u
#define FA_RECORD_BYTES 32u

typedef struct {
    const char *name;
    unsigned frames;
    unsigned width, height;
    int anchor_x, anchor_y;
    unsigned loop;
    unsigned duration;
} fa_clip_t;

/* Builds an LWFA v1 blob in memory. Frame payload is one byte per frame so the
 * reader can be checked for frame addressing without a 600 KB buffer. */
static uint8_t *build_lwfa(const fa_clip_t *clips, unsigned clip_count, size_t *out_size,
                           bool bad_magic, bool wrong_total, unsigned truncate_frames)
{
    size_t header = FA_HEADER_BYTES + (size_t)FA_RECORD_BYTES * clip_count;
    size_t payload = 0;
    for (unsigned i = 0; i < clip_count; i++) {
        payload += (size_t)clips[i].width * clips[i].height * 3u * clips[i].frames;
    }
    size_t total = header + payload;
    if (truncate_frames) total -= (size_t)truncate_frames * 3u;
    uint8_t *blob = calloc(1, total);
    if (!blob) return NULL;
    memcpy(blob, bad_magic ? "LWFA0002" : "LWFA0001", 8);
    put16(blob + 8, 1u);
    put16(blob + 10, clip_count);
    put32(blob + 12, wrong_total ? (unsigned)(total + 1u) : (unsigned)total);
    unsigned offset = (unsigned)header;
    for (unsigned i = 0; i < clip_count; i++) {
        uint8_t *record = blob + FA_HEADER_BYTES + (size_t)i * FA_RECORD_BYTES;
        memcpy(record, clips[i].name, strlen(clips[i].name));
        put16(record + 12, clips[i].frames);
        put16(record + 14, clips[i].width);
        put16(record + 16, clips[i].height);
        put16(record + 18, (unsigned)clips[i].anchor_x);
        put16(record + 20, (unsigned)clips[i].anchor_y);
        record[22] = (uint8_t)clips[i].loop;
        put16(record + 24, clips[i].duration);
        put32(record + 28, offset);
        /* Payload byte n of frame f is f, so a mis-addressed read is visible. */
        for (unsigned f = 0; f < clips[i].frames; f++) {
            size_t frame_bytes = (size_t)clips[i].width * clips[i].height * 3u;
            if (offset + frame_bytes <= total) memset(blob + offset, (int)f, frame_bytes);
            offset += (unsigned)frame_bytes;
        }
    }
    *out_size = total;
    return blob;
}

static void test_embedded_pack(void)
{
    /* Real geometry of the shipped pack: male 108x212 one frame, qianqian
     * 104x202 eight frames. A loader that assumes 256x256 fails here. */
    const fa_clip_t clips[2] = {
        {"male", 1u, 108u, 212u, 54, 209, 1u, 500u},
        {"qianqian", 8u, 104u, 202u, 51, 199, 1u, 125u},
    };

    size_t size = 0;
    uint8_t *blob = build_lwfa(clips, 2u, &size, false, false, 0u);
    const size_t blob_size = size; /* the rejection fixtures below reuse `size` */
    CHECK("embedded fixture built", blob != NULL && size > 0);
    if (!blob) return;

    lw_pack_registry_reset();
    lw_pack_t info;
    CHECK("embedded male accepted",
          lw_pack_registry_add_embedded(blob, size, "male", "idle", &info) == LW_PACK_OK);
    CHECK("embedded male clip count", info.clips == 1u);
    CHECK("embedded male frame count", info.frames == 1u);
    CHECK("embedded male publishes idle", info.named && info.named[0].name &&
          !strcmp(info.named[0].name, "idle"));
    CHECK("embedded male asset id is actor-prefixed", info.named &&
          !strcmp(info.named[0].clip.asset_id, "male.idle"));
    CHECK("embedded male keeps its own geometry", info.named && info.named[0].clip.count == 1u);

    CHECK("embedded qianqian accepted",
          lw_pack_registry_add_embedded(blob, size, "qianqian", "idle", &info) == LW_PACK_OK);
    CHECK("embedded qianqian frame count", info.frames == 8u);
    CHECK("embedded qianqian named count", info.named_count == 1u);

    CHECK("both actors registered", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));

    /* The spec table must carry the pack's geometry, not LWPX's 256x256. */
    const lw_asset_spec_t *male = lw_assets_find("male.idle");
    CHECK("male spec present", male != NULL);
    if (male) {
        CHECK("male spec width", male->width == 108u);
        CHECK("male spec height", male->height == 212u);
        CHECK("male spec anchor x", male->anchor_x == 54);
        CHECK("male spec anchor y", male->anchor_y == 209);
        CHECK("male spec format", male->format == LV_COLOR_FORMAT_RGB565A8);
    }
    const lw_asset_spec_t *qian = lw_assets_find("qianqian.idle");
    CHECK("qianqian spec present", qian != NULL);
    if (qian) {
        CHECK("qianqian spec width", qian->width == 104u);
        CHECK("qianqian spec height", qian->height == 202u);
        CHECK("qianqian spec frames", qian->frames == 8u);
    }

    /* Frame addressing: frame f is filled with byte f. */
    lw_asset_lease_t *lease = lw_asset_acquire("qianqian.idle", 5u);
    CHECK("embedded frame 5 leased", lease != NULL);
    if (lease) {
        const lv_image_dsc_t *image = lw_asset_image(lease);
        CHECK("embedded frame 5 pixels", image && image->data && image->data[0] == 5u);
        CHECK("embedded frame 5 tail", image && image->data[image->data_size - 1u] == 5u);
        lw_asset_release(lease);
    }
    CHECK("embedded frame past end refused", lw_asset_acquire("qianqian.idle", 8u) == NULL);

    /* Refill reuses the lease's pixel buffer: same pointer, new contents, and
     * the live-lease accounting must not move (that is the whole point of the
     * fix - a 196 KB frame buffer was allocated and freed per animation frame). */
    lw_asset_lease_t *live = lw_asset_acquire("qianqian.idle", 3u);
    CHECK("refill fixture leased", live != NULL);
    if (live) {
        uint32_t leases_before = lw_assets_stats().live_leases;
        size_t bytes_before = lw_assets_stats().live_bytes;
        const uint8_t *pixels_before = lw_asset_image(live)->data;
        CHECK("refill accepted", lw_asset_refill(live, 6u));
        CHECK("refill reuses the same buffer", lw_asset_image(live)->data == pixels_before);
        CHECK("refill actually reloaded the frame", lw_asset_image(live)->data[0] == 6u);
        CHECK("refill keeps the lease accounting", lw_assets_stats().live_leases == leases_before &&
                                                  lw_assets_stats().live_bytes == bytes_before);
        CHECK("refill past the end refused", !lw_asset_refill(live, 8u));
        CHECK("refused refill leaves the frame intact", lw_asset_image(live)->data[0] == 6u);
        lw_asset_release(live);
    }
    CHECK("refill rejects a null lease", !lw_asset_refill(NULL, 0u));

    /* Rejections: each must fail without disturbing the table already built. */
    lw_pack_registry_reset();
    lw_pack_t rejected;
    CHECK("good blob accepted on a fresh registry",
          lw_pack_registry_add_embedded(blob, blob_size, "male", "idle", &rejected) == LW_PACK_OK);
    lw_pack_registry_reset();
    uint8_t *bad = build_lwfa(clips, 2u, &size, true, false, 0u);
    CHECK("wrong magic refused",
          bad && lw_pack_registry_add_embedded(bad, size, "male", "idle", &rejected) == LW_PACK_ERR_MAGIC);
    free(bad);
    bad = build_lwfa(clips, 2u, &size, false, true, 0u);
    CHECK("wrong total size refused",
          bad && lw_pack_registry_add_embedded(bad, size, "male", "idle", &rejected) == LW_PACK_ERR_TRUNCATED);
    free(bad);
    bad = build_lwfa(clips, 2u, &size, false, false, 4u);
    CHECK("short payload refused",
          bad && lw_pack_registry_add_embedded(bad, size, "qianqian", "idle", &rejected) == LW_PACK_ERR_TRUNCATED);
    free(bad);
    CHECK("unknown actor refused",
          lw_pack_registry_add_embedded(blob, blob_size, "nobody", "idle", &rejected) == LW_PACK_ERR_CLIP_NAME);
    CHECK("null blob refused",
          lw_pack_registry_add_embedded(NULL, blob_size, "male", "idle", &rejected) == LW_PACK_ERR_ARGUMENT);

    lw_pack_registry_reset();
    free(blob);
}

/* A card pack and the embedded pack must coexist in ONE spec table, because
 * lw_assets_register() replaces the whole registry. This is the case the board
 * actually runs when a card is present. */
static void test_card_plus_embedded(void)
{
    const clip_spec_t card_clips[2] = {
        {"idle", 2u, 225, 1u, 100u},
        {"wave", 3u, 225, 0u, 100u},
    };
    CHECK("card fixture written", write_pack("male.lwp", card_clips, 2u, false));

    lw_pack_registry_reset();
    lw_pack_t info;
    CHECK("card male opened",
          lw_pack_registry_add(fixture_dir(), "male.lwp", "male", NULL, &info) == LW_PACK_OK);
    CHECK("card male has idle and wave", info.named_count == 2u);

    const fa_clip_t embedded[1] = {{"qianqian", 8u, 104u, 202u, 51, 199, 1u, 125u}};
    size_t size = 0;
    uint8_t *blob = build_lwfa(embedded, 1u, &size, false, false, 0u);
    CHECK("embedded fixture built", blob != NULL);
    if (blob) {
        CHECK("embedded qianqian added beside the card pack",
              lw_pack_registry_add_embedded(blob, size, "qianqian", "idle", &info) == LW_PACK_OK);
        CHECK("registry holds both actors", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));
        /* The card clip and the embedded clip must both be findable at once: a
         * second registration would have silently dropped the first table. */
        CHECK("card clip survived the embedded add", lw_assets_find("male.wave") != NULL);
        CHECK("card idle survived too", lw_assets_find("male.idle") != NULL);
        CHECK("embedded clip landed in the same table", lw_assets_find("qianqian.idle") != NULL);
        lw_pack_registry_reset();
        free(blob);
    }
    snprintf(s_path, sizeof(s_path), "%s/male.lwp", fixture_dir());
    remove(s_path);
}

int main(void)
{
    if (!getenv("LW_RESOURCE_ROOT")) {
        printf("LW_RESOURCE_ROOT is not set\n");
        return 1;
    }

    test_pack_ok();
    test_pack_append();
    test_pack_append_reads_right_file();
    test_pack_reset_refuses_live_lease();
    test_pack_rejections();
    test_pack_geometry();
    test_pack_geometry_trailer();
    test_pack_clip_budget();
    test_resource_ok();
    test_resource_version_gate();
    test_resource_missing_and_broken();
    test_resource_no_medium();
    test_embedded_pack();
    test_card_plus_embedded();

    printf("%u checks, %u failure(s)\n", checks, failures);
    if (failures) return 1;
    printf("V4 RESOURCE TESTS PASSED\n");
    return 0;
}







