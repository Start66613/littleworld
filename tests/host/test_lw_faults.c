/* Host tests for fault injection and the degradation contracts it proves.
 *
 * Every test here has the same shape: make a real call fail, then assert that
 * the code that was already there kept the promise it makes in its comments.
 * The interesting assertions are never "the call returned an error" - they are
 * "the previous record is still readable", "the frame on screen survived",
 * "the registry still resolves ids", "the UI has a labelled fallback".
 *
 * The whole suite is built with LW_FAULT_HOST so the injection points are live.
 * On the device the same points compile to `false` unless CONFIG_LW_FAULT_INJECT
 * is enabled, which is why this cannot rot: the shipped image carries none of
 * it, and the tests carry all of it.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_fault.h"
#include "lw_resource.h"
#include "lw_resource_mount.h"
#include "lw_storage.h"
#include "lw_store.h"

#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int s_checks;
static int s_failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        s_checks++;                                                                     \
        if (!(cond)) {                                                                  \
            s_failures++;                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                               \
    } while (0)

/* ------------------------------------------------------------ the scheduler -- */

static void test_arm_consume_and_clear(void)
{
    lw_fault_clear();
    CHECK(lw_fault_remaining(LW_FAULT_STORE_READ) == 0u);
    CHECK(!lw_fault_should_fail(LW_FAULT_STORE_READ));

    /* Three shots means exactly three failures, never four. */
    lw_fault_arm(LW_FAULT_STORE_READ, 3);
    CHECK(lw_fault_remaining(LW_FAULT_STORE_READ) == 3u);
    CHECK(lw_fault_should_fail(LW_FAULT_STORE_READ));
    CHECK(lw_fault_should_fail(LW_FAULT_STORE_READ));
    CHECK(lw_fault_should_fail(LW_FAULT_STORE_READ));
    CHECK(!lw_fault_should_fail(LW_FAULT_STORE_READ));
    CHECK(lw_fault_remaining(LW_FAULT_STORE_READ) == 0u);
    CHECK(lw_fault_total() == 3u);

    /* Arming one point must not arm its neighbours. */
    lw_fault_clear();
    lw_fault_arm(LW_FAULT_CACHE_READ, 1);
    CHECK(lw_fault_should_fail(LW_FAULT_CACHE_READ));
    CHECK(!lw_fault_should_fail(LW_FAULT_STORE_WRITE));
    CHECK(lw_fault_total() == 1u);
}

static void test_zero_means_until_cleared(void)
{
    lw_fault_clear();
    lw_fault_arm(LW_FAULT_ASSET_ACQUIRE, 0);
    /* "Always" must not be confused with "disarmed" - that is the one encoding
     * bug that would make every injection test silently pass. */
    for (int i = 0; i < 100; i++) {
        CHECK(lw_fault_should_fail(LW_FAULT_ASSET_ACQUIRE));
    }
    CHECK(lw_fault_total() == 100u);
    lw_fault_clear();
    CHECK(!lw_fault_should_fail(LW_FAULT_ASSET_ACQUIRE));
}

static void test_invalid_point_is_ignored(void)
{
    lw_fault_clear();
    lw_fault_arm((lw_fault_point_t)99, 1);
    CHECK(!lw_fault_should_fail((lw_fault_point_t)99));
    CHECK(lw_fault_remaining((lw_fault_point_t)99) == 0u);
    lw_fault_arm((lw_fault_point_t)-1, 1);
    CHECK(!lw_fault_should_fail((lw_fault_point_t)-1));
    CHECK(lw_fault_total() == 0u);

    /* Every real point has a name that is not "unknown". */
    for (int i = 0; i < (int)LW_FAULT_COUNT; i++) {
        CHECK(strcmp(lw_fault_point_name((lw_fault_point_t)i), "unknown") != 0);
    }
    CHECK(strcmp(lw_fault_point_name((lw_fault_point_t)77), "unknown") == 0);
}

/* -------------------------------------------------- store degradation ----- */

/* A transport that keeps one record in memory, so a failed write can be shown
 * to leave the previous bytes intact. */
typedef struct {
    uint8_t record[LW_STORE_MAX_RECORD];
    size_t length;
    bool present;
    unsigned writes;
} fake_nvs_t;

static bool fake_read(void *context, uint8_t *out, size_t capacity, size_t *length)
{
    fake_nvs_t *nvs = (fake_nvs_t *)context;
    if (!nvs->present || nvs->length > capacity) return false;
    memcpy(out, nvs->record, nvs->length);
    *length = nvs->length;
    return true;
}

static bool fake_write(void *context, const uint8_t *data, size_t length)
{
    fake_nvs_t *nvs = (fake_nvs_t *)context;
    if (length > sizeof(nvs->record)) return false;
    memcpy(nvs->record, data, length);
    nvs->length = length;
    nvs->present = true;
    nvs->writes++;
    return true;
}

static void test_failed_write_preserves_the_old_record(void)
{
    fake_nvs_t nvs;
    memset(&nvs, 0, sizeof(nvs));
    lw_store_io_t io = {&nvs, fake_read, fake_write};

    uint8_t first[16], second[16], back[16];
    for (int i = 0; i < 16; i++) {
        first[i] = (uint8_t)(0x10 + i);
        second[i] = (uint8_t)(0xA0 + i);
    }

    CHECK(lw_store_save(&io, 7, 1, first, sizeof(first)) == LW_STORE_OK);
    CHECK(nvs.writes == 1u);

    /* Inject one write failure: the save must report an error and the transport
     * must not be touched, so the first record is still the one that loads. */
    lw_fault_clear();
    lw_fault_arm(LW_FAULT_STORE_WRITE, 1);
    CHECK(lw_store_save(&io, 7, 1, second, sizeof(second)) == LW_STORE_IO_ERROR);
    CHECK(nvs.writes == 1u);

    memset(back, 0, sizeof(back));
    CHECK(lw_store_load(&io, 7, 1, back, sizeof(back)) == LW_STORE_OK);
    CHECK(memcmp(back, first, sizeof(first)) == 0);

    /* And the next attempt succeeds, so the failure did not latch anything. */
    CHECK(lw_store_save(&io, 7, 1, second, sizeof(second)) == LW_STORE_OK);
    CHECK(nvs.writes == 2u);
    CHECK(lw_store_load(&io, 7, 1, back, sizeof(back)) == LW_STORE_OK);
    CHECK(memcmp(back, second, sizeof(second)) == 0);
    lw_fault_clear();
}

static void test_failed_read_reports_missing_not_corrupt(void)
{
    fake_nvs_t nvs;
    memset(&nvs, 0, sizeof(nvs));
    lw_store_io_t io = {&nvs, fake_read, fake_write};

    uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[8];
    CHECK(lw_store_save(&io, 3, 1, payload, sizeof(payload)) == LW_STORE_OK);

    /* A transport that refuses to answer is "no record", the same answer a
     * blank NVS gives. It must NOT be reported as a corrupt record, because the
     * caller's reaction differs: defaults for missing, warning for corrupt. */
    lw_fault_clear();
    lw_fault_arm(LW_FAULT_STORE_READ, 1);
    memset(out, 0xEE, sizeof(out));
    CHECK(lw_store_load(&io, 3, 1, out, sizeof(out)) == LW_STORE_MISSING);
    CHECK(out[0] == 0xEE); /* payload untouched on failure */

    CHECK(lw_store_load(&io, 3, 1, out, sizeof(out)) == LW_STORE_OK);
    CHECK(memcmp(out, payload, sizeof(payload)) == 0);
    lw_fault_clear();
}

/* ------------------------------------------------ cache degradation ------- */

static bool cache_idle(lw_file_cache_t *cache)
{
    for (unsigned i = 0; i < 3000; i++) {
        lw_file_cache_stats_t stats = lw_file_cache_stats(cache);
        if (!stats.pending && !stats.loading) return true;
        Sleep(1);
    }
    return false;
}

static void test_failed_frame_read_keeps_the_destination(void)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "build/host_tests/fault-fixtures");
    _mkdir(dir);
    char path[600];
    snprintf(path, sizeof(path), "%s/frames.dat", dir);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    for (unsigned i = 0; i < 2048; i++) fputc((int)(i & 0xFFu), f);
    fclose(f);

    lw_file_cache_t *cache = lw_file_cache_open(dir, 8192);
    CHECK(cache != NULL);
    if (!cache) return;

    uint8_t frame[512];
    CHECK(lw_file_cache_read(cache, "frames.dat", 0, frame, sizeof(frame)));
    uint8_t before[512];
    memcpy(before, frame, sizeof(before));

    /* The read fails: the caller gets false and its buffer is untouched, so a
     * sprite that reads into its own frame keeps showing the old pixels. */
    lw_fault_clear();
    lw_fault_arm(LW_FAULT_CACHE_READ, 1);
    memset(frame, 0x5A, sizeof(frame));
    CHECK(!lw_file_cache_read(cache, "frames.dat", 512, frame, sizeof(frame)));
    CHECK(frame[0] == 0x5A);

    lw_file_cache_stats_t stats = lw_file_cache_stats(cache);
    CHECK(stats.failures >= 1u);
    CHECK(stats.reads >= 2u);

    /* The failure is not sticky. */
    CHECK(lw_file_cache_read(cache, "frames.dat", 512, frame, sizeof(frame)));
    CHECK(frame[0] == 0x00);
    lw_fault_clear();
    (void)cache_idle(cache);
    lw_file_cache_close(cache);
}

/* The mount path has three ways to end up with no card: none inserted, the
 * recovery policy disabling it, and an injected failure. All three must land in
 * the same place - a null root, the MOUNT_FAILED status and an app that keeps
 * running on the embedded pack - because that is the fallback the no-card
 * acceptance run is supposed to prove. */
static void test_mount_failure_takes_the_real_no_card_path(void)
{
    lw_fault_clear();
    lw_resource_force_no_medium(false);

    /* 1. Injected mount failure. */
    lw_fault_arm(LW_FAULT_MEDIUM_MOUNT, 1);
    CHECK(!lw_resource_mount());
    CHECK(!lw_resource_medium_mounted());
    CHECK(lw_resource_root() == NULL || lw_resource_root()[0] == '\0');
    CHECK(lw_resource_report()->status == LW_RESOURCE_MOUNT_FAILED);
    lw_fault_clear();

    /* 2. The recovery policy's forced no-card boot, same result. */
    lw_resource_force_no_medium(true);
    CHECK(!lw_resource_mount());
    CHECK(!lw_resource_medium_mounted());
    CHECK(lw_resource_report()->status == LW_RESOURCE_MOUNT_FAILED);

    /* 3. And clearing the override lets the real environment decide again, so
     *    the switch is not a one-way latch that silently disables the card. */
    lw_resource_force_no_medium(false);
    CHECK(lw_resource_mount() == (getenv("LW_RESOURCE_ROOT") != NULL && getenv("LW_RESOURCE_ROOT")[0] != '\0'));
}

int main(void)
{
    test_arm_consume_and_clear();
    test_zero_means_until_cleared();
    test_invalid_point_is_ignored();
    test_failed_write_preserves_the_old_record();
    test_failed_read_reports_missing_not_corrupt();
    test_failed_frame_read_keeps_the_destination();
    test_mount_failure_takes_the_real_no_card_path();

    if (s_failures == 0) {
        printf("FAULT SCHEDULER TESTS PASSED (%d checks)\n", s_checks);
        return 0;
    }
    printf("FAULT SCHEDULER TESTS FAILED (%d checks, %d failure(s))\n", s_checks, s_failures);
    return 1;
}
