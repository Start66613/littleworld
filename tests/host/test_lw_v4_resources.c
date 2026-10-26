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
 * the reader can be checked for off-by-one frame addressing. */
static bool write_pack(const char *fileName, const clip_spec_t *clips, unsigned clip_count,
                       bool truncate)
{
    snprintf(s_path, sizeof(s_path), "%s/%s", fixture_dir(), fileName);
    FILE *f = fopen(s_path, "wb");
    if (!f) return false;
    uint8_t header[HEADER_BYTES] = {0};
    memcpy(header, "LWPX0001", 8);
    put16(header + 8, 256);
    put16(header + 10, 256);
    put16(header + 12, clip_count);
    fwrite(header, 1, sizeof(header), f);

    unsigned offset = HEADER_BYTES + RECORD_BYTES * clip_count;
    for (unsigned i = 0; i < clip_count; i++) {
        uint8_t record[RECORD_BYTES] = {0};
        memcpy(record, clips[i].name, strlen(clips[i].name));
        put16(record + 32, clips[i].frames);
        put16(record + 34, 128);
        put16(record + 36, (unsigned)clips[i].anchor_y);
        record[38] = (uint8_t)clips[i].loop;
        put32(record + 40, offset);
        for (unsigned k = 0; k < clips[i].frames && k < 32u; k++) put16(record + 44 + k * 2, clips[i].duration);
        fwrite(record, 1, sizeof(record), f);
        offset += clips[i].frames * FRAME_BYTES;
    }

    unsigned frames = 0;
    for (unsigned i = 0; i < clip_count; i++) frames += clips[i].frames;
    uint8_t *frame = malloc(FRAME_BYTES);
    if (!frame) { fclose(f); return false; }
    for (unsigned n = 0; n < frames; n++) {
        memset(frame, (int)(n & 0xffu), FRAME_BYTES);
        size_t write = truncate && n + 1u == frames ? FRAME_BYTES / 2u : FRAME_BYTES;
        fwrite(frame, 1, write, f);
    }
    free(frame);
    fclose(f);
    return true;
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

/* --------------------------------------------------------- resource tests --- */

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

int main(void)
{
    if (!getenv("LW_RESOURCE_ROOT")) {
        printf("LW_RESOURCE_ROOT is not set\n");
        return 1;
    }

    test_pack_ok();
    test_pack_append();
    test_pack_rejections();
    test_resource_ok();
    test_resource_version_gate();
    test_resource_missing_and_broken();
    test_resource_no_medium();

    printf("%u checks, %u failure(s)\n", checks, failures);
    if (failures) return 1;
    printf("V4 RESOURCE TESTS PASSED\n");
    return 0;
}







