/* Little World - host unit tests for the platform independent core.
 *
 * Build & run with tools/run_host_tests.ps1 (MSVC x64).
 * Exit code 0 = every check passed.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lw/lw_memories.h"
#include "lw/lw_pet.h"
#include "lw/lw_state.h"
#include "lw/lw_time.h"

static int g_checks;
static int g_failures;

#define CHECK(cond)                                              \
    do {                                                         \
        g_checks++;                                              \
        if (!(cond)) {                                           \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                        \
        }                                                        \
    } while (0)

#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                              \
        g_checks++;                                                                   \
        long _a = (long)(actual);                                                     \
        long _e = (long)(expected);                                                   \
        if (_a != _e) {                                                               \
            printf("FAIL %s:%d  %s == %s (actual %ld, expected %ld)\n", __FILE__, __LINE__, #actual, \
                   #expected, _a, _e);                                                \
            g_failures++;                                                             \
        }                                                                             \
    } while (0)

/* ------------------------------------------------------------------ time */

static void test_time(void)
{
    CHECK(lw_is_leap_year(2024));
    CHECK(!lw_is_leap_year(2025));
    CHECK(lw_is_leap_year(2000));
    CHECK(!lw_is_leap_year(1900));

    CHECK_EQ(lw_days_in_month(2024, 2), 29);
    CHECK_EQ(lw_days_in_month(2025, 2), 28);
    CHECK_EQ(lw_days_in_month(2025, 4), 30);
    CHECK_EQ(lw_days_in_month(2025, 12), 31);

    CHECK_EQ(lw_days_from_civil(1970, 1, 1), 0);
    CHECK_EQ(lw_days_from_civil(1970, 1, 2), 1);
    CHECK_EQ(lw_days_from_civil(2000, 3, 1), 11017);

    lw_date_t date;
    lw_civil_from_days(0, &date);
    CHECK_EQ(date.year, 1970);
    CHECK_EQ(date.month, 1);
    CHECK_EQ(date.day, 1);

    lw_civil_from_days(11017, &date);
    CHECK_EQ(date.year, 2000);
    CHECK_EQ(date.month, 3);
    CHECK_EQ(date.day, 1);

    /* round trip across leap days and year boundaries */
    const int samples[] = {0, 59, 60, 365, 366, 11016, 19723, 20000};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        lw_civil_from_days(samples[i], &date);
        CHECK_EQ(lw_days_from_civil(date.year, date.month, date.day), samples[i]);
    }

    lw_date_t a = {2025, 12, 31};
    lw_date_t b = {2026, 1, 1};
    CHECK_EQ(lw_days_between(a, b), 1);
    CHECK_EQ(lw_days_between(b, a), -1);

    lw_date_t tomorrow = lw_date_add_days(a, 1);
    CHECK_EQ(tomorrow.year, 2026);
    CHECK_EQ(tomorrow.month, 1);
    CHECK_EQ(tomorrow.day, 1);

    CHECK(lw_date_valid(a));
    lw_date_t bad = {2025, 2, 29};
    CHECK(!lw_date_valid(bad));

    /* anniversary: before, on and after the date */
    lw_date_t start = {2024, 10, 25};
    lw_date_t before = {2026, 10, 20};
    lw_date_t on = {2026, 10, 25};
    lw_date_t after = {2026, 10, 26};

    lw_date_t next = lw_next_anniversary(start, before);
    CHECK_EQ(next.year, 2026);
    CHECK_EQ(next.month, 10);
    CHECK_EQ(next.day, 25);
    CHECK_EQ(lw_days_until_anniversary(start, before), 5);

    CHECK_EQ(lw_days_until_anniversary(start, on), 0);

    next = lw_next_anniversary(start, after);
    CHECK_EQ(next.year, 2027);
    CHECK_EQ(next.month, 10);
    CHECK_EQ(next.day, 25);

    /* 29 February start date falls back to 28 February in non leap years */
    lw_date_t leap_start = {2024, 2, 29};
    lw_date_t in_2025 = {2025, 2, 1};
    next = lw_next_anniversary(leap_start, in_2025);
    CHECK_EQ(next.year, 2025);
    CHECK_EQ(next.month, 2);
    CHECK_EQ(next.day, 28);
}

/* ------------------------------------------------------------------- pet */

static void test_pet(void)
{
    lw_pet_t pet;
    lw_pet_init(&pet, 1000);
    CHECK_EQ(pet.fullness, 72);
    CHECK_EQ(pet.energy, 68);
    CHECK_EQ(pet.affection, 60);
    CHECK_EQ(pet.mood, LW_PET_MOOD_CALM);
    CHECK_EQ(lw_pet_mood(&pet, 1000), LW_PET_MOOD_CALM);

    /* one hour of awake time decays fullness and energy */
    lw_pet_tick(&pet, 1000 + 3600);
    CHECK(pet.fullness < 72);
    CHECK(pet.energy < 68);
    CHECK_EQ(pet.last_update, 1000 + 3600);

    /* feeding tops up but never above 100 */
    for (int i = 0; i < 10; i++) {
        (void)lw_pet_interact(&pet, LW_PET_ACTION_FEED, 1000 + 3600);
    }
    CHECK_EQ(pet.fullness, 100);

    /* playing costs energy and gives the playing mood */
    lw_pet_t playable;
    lw_pet_init(&playable, 1000);
    CHECK(lw_pet_interact(&playable, LW_PET_ACTION_PLAY, 1000));
    CHECK_EQ(playable.mood, LW_PET_MOOD_PLAYING);
    CHECK(lw_pet_mood(&playable, 1000) == LW_PET_MOOD_PLAYING);
    CHECK(playable.energy < 68);
    /* the temporary mood expires */
    CHECK(lw_pet_mood(&playable, 1000 + 60) != LW_PET_MOOD_PLAYING);

    /* too tired to play */
    lw_pet_t tired;
    lw_pet_init(&tired, 1000);
    tired.energy = 5;
    CHECK(!lw_pet_interact(&tired, LW_PET_ACTION_PLAY, 1000));
    CHECK(lw_pet_mood(&tired, 1000) == LW_PET_MOOD_SLEEPY);

    /* sleeping regenerates energy and wakes up when full */
    lw_pet_t sleeper;
    lw_pet_init(&sleeper, 1000);
    sleeper.energy = 10;
    CHECK(lw_pet_interact(&sleeper, LW_PET_ACTION_SLEEP, 1000));
    CHECK_EQ(sleeper.sleeping, 1);
    CHECK(lw_pet_mood(&sleeper, 1000) == LW_PET_MOOD_SLEEPING);
    lw_pet_tick(&sleeper, 1000 + 4 * 3600);
    CHECK(sleeper.energy >= 98);
    CHECK_EQ(sleeper.sleeping, 0);

    /* tapping while asleep does nothing, petting wakes the pet */
    lw_pet_t napper;
    lw_pet_init(&napper, 1000);
    (void)lw_pet_interact(&napper, LW_PET_ACTION_SLEEP, 1000);
    CHECK(!lw_pet_interact(&napper, LW_PET_ACTION_TAP, 1000 + 10));
    CHECK(lw_pet_interact(&napper, LW_PET_ACTION_PET, 1000 + 20));
    CHECK_EQ(napper.sleeping, 0);
    CHECK_EQ(napper.mood, LW_PET_MOOD_HAPPY);

    /* a very long absence is capped instead of wiping the pet */
    lw_pet_t away;
    lw_pet_init(&away, 1000);
    lw_pet_tick(&away, 1000 + 60u * 24u * 3600u);
    CHECK(away.fullness >= 0);
    CHECK_EQ(away.last_update, 1000 + 60u * 24u * 3600u);

    /* mood names exist for every mood */
    for (int mood = 0; mood < LW_PET_MOOD_COUNT; mood++) {
        CHECK(lw_pet_mood_name((lw_pet_mood_t)mood) != NULL);
    }
}

/* ----------------------------------------------------------------- state */

static void test_state(void)
{
    lw_state_t state;
    lw_state_defaults(&state, 1700000000u);
    CHECK_EQ(state.settings.sound, 1);
    CHECK_EQ(state.settings.animations, 1);
    CHECK_EQ(state.settings.theme, 0);
    CHECK_EQ(state.anniversary.year, 2024);
    CHECK_EQ(state.anniversary.month, 10);
    CHECK_EQ(state.anniversary.day, 25);

    uint8_t blob[LW_SAVE_BYTES];
    CHECK_EQ(lw_save_encode(&state, blob, sizeof(blob)), LW_SAVE_BYTES);
    CHECK_EQ(lw_save_encode(&state, blob, 4), -1);

    state.pet.affection = 91;
    state.pet.interactions = 1234;
    state.settings.theme = 2;
    state.settings.sound = 0;
    state.surprise_unlocked = 1;
    state.map_visited = 0x1F;
    state.music_volume = 33;
    state.arcade_best[0] = 2048;
    state.arcade_best[1] = 120;
    state.arcade_best[2] = 300;
    state.anniversary.year = 2023;
    state.anniversary.month = 5;
    state.anniversary.day = 20;

    CHECK_EQ(lw_save_encode(&state, blob, sizeof(blob)), LW_SAVE_BYTES);

    lw_state_t decoded;
    CHECK_EQ(lw_save_decode(&decoded, blob, sizeof(blob)), 0);
    CHECK_EQ(decoded.pet.affection, 91);
    CHECK_EQ(decoded.pet.interactions, 1234);
    CHECK_EQ(decoded.settings.theme, 2);
    CHECK_EQ(decoded.settings.sound, 0);
    CHECK_EQ(decoded.surprise_unlocked, 1);
    CHECK_EQ(decoded.map_visited, 0x1F);
    CHECK_EQ(decoded.music_volume, 33);
    CHECK_EQ(decoded.arcade_best[0], 2048);
    CHECK_EQ(decoded.arcade_best[1], 120);
    CHECK_EQ(decoded.arcade_best[2], 300);
    CHECK_EQ(decoded.anniversary.year, 2023);
    CHECK_EQ(decoded.anniversary.month, 5);
    CHECK_EQ(decoded.anniversary.day, 20);

    /* corruption, wrong size, wrong magic and wrong version are rejected */
    uint8_t corrupt[LW_SAVE_BYTES];
    memcpy(corrupt, blob, sizeof(corrupt));
    corrupt[12] ^= 0x5A;
    CHECK_EQ(lw_save_decode(&decoded, corrupt, sizeof(corrupt)), -1);

    CHECK_EQ(lw_save_decode(&decoded, blob, sizeof(blob) - 1), -1);

    memcpy(corrupt, blob, sizeof(corrupt));
    corrupt[0] = 'X';
    CHECK_EQ(lw_save_decode(&decoded, corrupt, sizeof(corrupt)), -1);

    memcpy(corrupt, blob, sizeof(corrupt));
    corrupt[4] = 9;
    CHECK_EQ(lw_save_decode(&decoded, corrupt, sizeof(corrupt)), -1);

    /* CRC-32 known answer */
    const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(lw_crc32(digits, sizeof(digits)), 0xCBF43926u);

    /* invalid dates in a record fall back to the demo date */
    lw_state_defaults(&state, 0);
    state.anniversary.year = 2025;
    state.anniversary.month = 2;
    state.anniversary.day = 30;
    CHECK_EQ(lw_save_encode(&state, blob, sizeof(blob)), LW_SAVE_BYTES);
    CHECK_EQ(lw_save_decode(&decoded, blob, sizeof(blob)), 0);
    CHECK_EQ(decoded.anniversary.month, 10);
}

/* -------------------------------------------------------------- memories */

static void test_memories(void)
{
    CHECK(lw_memories_count() >= 5);
    for (int i = 0; i < lw_memories_count(); i++) {
        const lw_memory_t *memory = lw_memories_get(i);
        CHECK(memory != NULL);
        CHECK(lw_date_valid(memory->date));
        CHECK(memory->title != NULL && memory->title[0] != '\0');
        CHECK(memory->body != NULL && memory->body[0] != '\0');
    }
    CHECK(lw_memories_get(-1) == NULL);
    CHECK(lw_memories_get(lw_memories_count()) == NULL);

    CHECK_EQ(lw_places_count(), 5);
    for (int i = 0; i < lw_places_count(); i++) {
        const lw_place_t *place = lw_places_get(i);
        CHECK(place != NULL && place->name != NULL && place->name[0] != '\0');
    }

    CHECK(lw_memories_welcome() != NULL);
    CHECK(lw_surprise_message() != NULL);
}

int main(void)
{
    test_time();
    test_pet();
    test_state();
    test_memories();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures != 0) {
        printf("HOST TESTS FAILED\n");
        return 1;
    }
    printf("HOST TESTS PASSED\n");
    return 0;
}
