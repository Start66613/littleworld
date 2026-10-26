/* End-to-end test of the resource root contract against a REAL card layout.
 *
 * The bug this file exists for: lw_content_boot.c appended "littleworld/" to a
 * root that already ended in "littleworld", so the device asked for
 * /sdcard/littleworld/littleworld/content.lwcx while the packaging tool wrote
 * /sdcard/littleworld/content.lwcx. Every existing test passed, because none of
 * them ever built a directory tree shaped like the card - they all called
 * lw_pack_open() with a scratch directory and a bare file name.
 *
 * So this test builds the exact tree the card has:
 *
 *   <root>/littleworld-v4.json
 *   <root>/content.lwcx
 *   <root>/male.lwp
 *   <root>/scene-0.lwr        (and 1, 2)
 *   <root>/scene-0-thumb.lwr  (and 1, 2)
 *
 * ...then runs the shipping code in the order the device runs it: mount, verify,
 * resolve, load. The content file is produced by the real
 * tools/package_v4_content.py (the wrapper script does that and passes the path
 * in LW_CONTENT_SAMPLE), so the test proves the tool's output is locatable by
 * the firmware, not merely that a hand-written fixture parses.
 *
 * LW_CARD_ROOT names the directory to populate. It is deliberately the SAME
 * value the device would see as lw_resource_root(): the littleworld directory,
 * not its parent.
 *
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lw_content.h"
#include "lw_pack.h"
#include "lw_path.h"
#include "lw_resource.h"
#include "lw_resource_mount.h"
#include "lw_storage.h"

static unsigned checks, failures;
#define CHECK(name, value)              \
    do {                                \
        checks++;                       \
        if (!(value)) {                 \
            failures++;                 \
            printf("FAIL: %s\n", name); \
        }                               \
        fflush(stdout);                 \
    } while (0)

#define HEADER_BYTES 16u
#define RECORD_BYTES 108u
#define FRAME_BYTES 196608u /* 256x256 RGB565 + A8 */

static char s_card[512];

static const char *card(void) { return s_card; }

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

/* A one-clip pack: enough to prove the root the mount produced is the root the
 * pack reader opens files from. */
static bool write_pack(const char *name)
{
    char path[768];
    if (!lw_path_join(path, sizeof(path), card(), name)) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    uint8_t header[HEADER_BYTES] = {0};
    memcpy(header, "LWPX0001", 8);
    put16(header + 8, 256);
    put16(header + 10, 256);
    put16(header + 12, 1);
    fwrite(header, 1, sizeof(header), f);

    uint8_t record[RECORD_BYTES] = {0};
    memcpy(record, "idle", 4);
    put16(record + 32, 1);                       /* frames */
    put16(record + 34, 128);                     /* scale */
    put16(record + 36, 225);                     /* anchor y */
    record[38] = 1;                              /* loop */
    put32(record + 40, HEADER_BYTES + RECORD_BYTES); /* first frame offset */
    put16(record + 44, 500);                     /* duration ms */
    fwrite(record, 1, sizeof(record), f);

    uint8_t *frame = malloc(FRAME_BYTES);
    if (!frame) { fclose(f); return false; }
    memset(frame, 0x5A, FRAME_BYTES);
    fwrite(frame, 1, FRAME_BYTES, f);
    free(frame);
    fclose(f);
    return true;
}

static bool write_scene(const char *name, unsigned width, unsigned height)
{
    char path[768];
    if (!lw_path_join(path, sizeof(path), card(), name)) return false;
    FILE *f = fopen(path, "wb");
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

static bool write_text(const char *name, const char *text)
{
    char path[768];
    if (!lw_path_join(path, sizeof(path), card(), name)) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return true;
}

static bool copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }
    char buffer[4096];
    size_t got;
    bool ok = true;
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) { ok = false; break; }
    }
    fclose(in);
    fclose(out);
    return ok;
}

/* ------------------------------------------------------- the card itself --- */

static void build_card(void)
{
    char path[768];
    char text[256];

    /* The manifest the packager writes. */
    snprintf(text, sizeof(text), "{\"schema\":%u,\"version\":%u,\"pack\":\"littleworld-v4\","
                                 "\"themes\":3,\"characters\":2}",
             LW_RESOURCE_SCHEMA, LW_RESOURCE_PACK_VERSION);
    CHECK("manifest written", write_text(LW_RESOURCE_MANIFEST_NAME, text));

    for (unsigned t = 0; t < 3; t++) {
        char name[32];
        snprintf(name, sizeof(name), "scene-%u.lwr", t);
        CHECK("scene written", write_scene(name, 1024, 600));
        snprintf(name, sizeof(name), "scene-%u-thumb.lwr", t);
        CHECK("thumbnail written", write_scene(name, 256, 150));
    }

    CHECK("male.lwp written", write_pack("male.lwp"));
    CHECK("qianqian-wave.lwp written", write_pack("qianqian-wave.lwp"));

    /* The content file comes from the real packaging tool. If the wrapper did
     * not provide one, write a minimal valid file so this suite still runs
     * standalone - but say so, because then it is not the end-to-end case. */
    const char *sample = getenv("LW_CONTENT_SAMPLE");
    if (!lw_path_join(path, sizeof(path), card(), LW_CONTENT_FILE_NAME)) {
        CHECK("content path join", false);
        return;
    }
    if (sample && sample[0]) {
        CHECK("content copied from the packaging tool", copy_file(sample, path));
    } else {
        static const char minimal[] =
            "LWCX0001\nschema=1\ncontent_version=1\nanniversary=2024-10-25\n"
            "[memory]\ndate=2024-10-25\ntitle=t\nbody=b\n[/memory]\n";
        CHECK("content written from the built-in sample", write_text(LW_CONTENT_FILE_NAME, minimal));
        printf("note: LW_CONTENT_SAMPLE was not set, using the built-in sample\n");
    }
}

/* ------------------------------------------------------------- the checks --- */

/* The whole point: mount the tree, then ask the SHIPPING resolver for the
 * content path. It must be <card>/content.lwcx, one level, not two. */
static void test_mount_and_resolve(void)
{
    CHECK("mount succeeds", lw_resource_mount());
    CHECK("medium reported", lw_resource_medium_mounted());
    CHECK("root is the littleworld directory", !strcmp(lw_resource_root(), card()));
    CHECK("payload verified", lw_resource_report()->status == LW_RESOURCE_OK);
    CHECK("no fallback", !lw_resource_fallback());

    char path[768];
    CHECK("content path resolves", lw_content_resolve_path(path, sizeof(path), lw_resource_root()));

    char expected[768];
    snprintf(expected, sizeof(expected), "%s/%s", card(), LW_CONTENT_FILE_NAME);
    CHECK("content path is exactly <root>/content.lwcx", !strcmp(path, expected));

    /* The old bug, spelled out: the doubled directory must never appear. */
    char doubled[768];
    snprintf(doubled, sizeof(doubled), "%s/littleworld/%s", card(), LW_CONTENT_FILE_NAME);
    CHECK("resolved path is not the doubled path", strcmp(path, doubled) != 0);
    FILE *f = fopen(doubled, "rb");
    CHECK("no doubled directory exists on the card", f == NULL);
    if (f) fclose(f);

    /* And the file the resolver points at is really there. */
    f = fopen(path, "rb");
    CHECK("resolved content file exists", f != NULL);
    if (f) fclose(f);

    /* Every other consumer goes through the same rule. */
    CHECK("pack path resolves", lw_resource_path(path, sizeof(path), "male.lwp"));
    snprintf(expected, sizeof(expected), "%s/male.lwp", card());
    CHECK("pack path is <root>/male.lwp", !strcmp(path, expected));

    CHECK("scene path resolves", lw_resource_path(path, sizeof(path), "scene-0.lwr"));
    snprintf(expected, sizeof(expected), "%s/scene-0.lwr", card());
    CHECK("scene path is <root>/scene-0.lwr", !strcmp(path, expected));

    CHECK("thumbnail path resolves", lw_resource_path(path, sizeof(path), "scene-2-thumb.lwr"));
    snprintf(expected, sizeof(expected), "%s/scene-2-thumb.lwr", card());
    CHECK("thumbnail path is <root>/scene-2-thumb.lwr", !strcmp(path, expected));
}

/* Reading the content file through the resolved path is what the device does at
 * boot. The counts come from the sample the packaging tool produced. */
static void test_content_loads_from_the_card(void)
{
    char path[768];
    CHECK("content path resolves again", lw_content_resolve_path(path, sizeof(path), lw_resource_root()));
    CHECK("content loads", lw_content_load(path));

    const lw_content_report_t *report = lw_content_report();
    CHECK("content status ok", report->status == LW_CONTENT_OK);
    CHECK("content version 1", report->content_version == 1u);
    CHECK("report names the resolved path", !strcmp(report->source, path));

    const lw_content_t *content = lw_content_get();
    CHECK("three memories", content->memory_count == 3u);
    CHECK("two photos", content->photo_count == 2u);
    CHECK("one letter", content->letter_count == 1u);
    CHECK("two dialogues", content->dialogue_count == 2u);
    CHECK("anniversary read", content->anniversary.year == 2024 && content->anniversary.month == 10 &&
                                 content->anniversary.day == 25);
    CHECK("memories borrowable by the UI", lw_content_memories() != NULL);
    CHECK("memory count matches", lw_content_memory_count() == 3u);
}

/* The pack reader must open files relative to the same root, so a card whose
 * pack sits one level up from where the reader looks fails here. */
static void test_pack_opens_under_the_same_root(void)
{
    lw_pack_registry_reset();
    lw_pack_t info;
    lw_pack_status_t status = lw_pack_registry_add(lw_resource_root(), "male.lwp", "male", NULL, &info);
    CHECK("male pack opens", status == LW_PACK_OK);
    CHECK("male pack has one clip", info.named_count == 1u);

    /* lw_pack_registry_add() only fills the pending table; the registry is
     * published by register(), which is what the boot path calls once after
     * every pack has been added. */
    CHECK("registry publishes", lw_pack_registry_register(LW_PACK_LEASE_BUDGET));

    const lw_asset_spec_t *spec = lw_assets_find("male.idle");
    CHECK("idle clip registered", spec != NULL);

    static uint8_t frame[FRAME_BYTES];
    memset(frame, 0, sizeof(frame));
    CHECK("frame reads through the root", spec && spec->read(spec->context, 0, frame, sizeof(frame)));
    CHECK("frame content matches the fixture", frame[0] == 0x5Au && frame[FRAME_BYTES - 1u] == 0x5Au);
    lw_pack_registry_reset();
}

/* A root that ends in a separator is the shape a naive caller produces after
 * joining the mount point by hand; it must not become "root//file". */
static void test_trailing_separator_root(void)
{
    char slashed[768];
    snprintf(slashed, sizeof(slashed), "%s/", card());

    char path[768];
    CHECK("slashed root resolves", lw_content_resolve_path(path, sizeof(path), slashed));
    char expected[768];
    snprintf(expected, sizeof(expected), "%s/%s", card(), LW_CONTENT_FILE_NAME);
    CHECK("slashed root gives one separator", !strcmp(path, expected));

    char root_path[768];
    CHECK("slashed root works for packs too", lw_path_join(root_path, sizeof(root_path), slashed, "male.lwp"));
    snprintf(expected, sizeof(expected), "%s/male.lwp", card());
    CHECK("slashed root pack path", !strcmp(root_path, expected));
}

/* Bad roots must fail loudly at the resolver, not silently produce a path that
 * opens the wrong file. */
static void test_bad_roots_are_refused(void)
{
    char path[64];
    CHECK("empty root refused", !lw_content_resolve_path(path, sizeof(path), ""));
    CHECK("null root refused", !lw_content_resolve_path(path, sizeof(path), NULL));
    CHECK("bare slash root refused", !lw_content_resolve_path(path, sizeof(path), "/"));

    /* No root registered at all: the no-card state. */
    char saved[512];
    snprintf(saved, sizeof(saved), "%s", lw_resource_root());
    lw_resource_set_root(NULL);
    CHECK("unset root refuses the content path", !lw_content_resolve_path(path, sizeof(path), lw_resource_root()));
    CHECK("unset root is empty", lw_resource_root()[0] == '\0');
    lw_resource_set_root(saved);
    CHECK("root restored", !strcmp(lw_resource_root(), card()));

    /* Too small a buffer must fail rather than truncate. */
    char tiny[8];
    CHECK("overflow refused", !lw_content_resolve_path(tiny, sizeof(tiny), card()));
    CHECK("overflow leaves an empty string", tiny[0] == '\0');

    /* The join rule itself, independent of the content file name. */
    CHECK("dotdot component refused", !lw_path_is_safe_relative("../content.lwcx"));
    CHECK("nested dotdot refused", !lw_path_is_safe_relative("a/../../b"));
    CHECK("absolute refused", !lw_path_is_safe_relative("/content.lwcx"));
    CHECK("drive prefix refused", !lw_path_is_safe_relative("C:/content.lwcx"));
    CHECK("backslash refused", !lw_path_is_safe_relative("littleworld\\content.lwcx"));
    CHECK("plain name accepted", lw_path_is_safe_relative("content.lwcx"));
    CHECK("name with dots accepted", lw_path_is_safe_relative("notes..txt"));
    CHECK("subdirectory accepted", lw_path_is_safe_relative("photos/001.lwph"));
}

/* A card that only carries art (no content file) is the normal case; the
 * resolver must still work and the loader must report MISSING, not crash. */
static void test_card_without_content(void)
{
    char path[768];
    CHECK("content path still resolves", lw_content_resolve_path(path, sizeof(path), lw_resource_root()));
    CHECK("remove content", remove(path) == 0);

    CHECK("load reports missing", !lw_content_load(path));
    CHECK("status is missing", lw_content_report()->status == LW_CONTENT_MISSING);

    /* Put it back so a rerun of the suite starts from a complete card. */
    const char *sample = getenv("LW_CONTENT_SAMPLE");
    if (sample && sample[0]) {
        CHECK("content restored", copy_file(sample, path));
    } else {
        static const char minimal[] =
            "LWCX0001\nschema=1\ncontent_version=1\nanniversary=2024-10-25\n"
            "[memory]\ndate=2024-10-25\ntitle=t\nbody=b\n[/memory]\n";
        CHECK("content restored", write_text(LW_CONTENT_FILE_NAME, minimal));
    }
}

int main(void)
{
    const char *root = getenv("LW_CARD_ROOT");
    if (!root || !root[0]) {
        printf("LW_CARD_ROOT is not set\n");
        return 1;
    }
    snprintf(s_card, sizeof(s_card), "%s", root);

    build_card();
    test_mount_and_resolve();
    test_content_loads_from_the_card();
    test_pack_opens_under_the_same_root();
    test_trailing_separator_root();
    test_bad_roots_are_refused();
    test_card_without_content();

    printf("%u checks, %u failure(s)\n", checks, failures);
    if (failures) return 1;
    printf("CARD LAYOUT TESTS PASSED\n");
    return 0;
}
