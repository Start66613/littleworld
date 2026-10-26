/* Host tests for the boot-loop recovery policy.
 *
 * What has to be true, because the board cannot afford to get this wrong:
 *   - a power cycle never counts as a crash (unplugging is how a user recovers,
 *     and treating it as a crash would latch safe mode on a healthy board);
 *   - one crash is not enough to enter safe mode, two in a row is;
 *   - a single clean boot clears the streak, so a board that crashes once in a
 *     while never accumulates its way into safe mode;
 *   - garbage RTC memory is discarded, not trusted;
 *   - the counters survive a simulated reboot because the record is the only
 *     thing that does.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_recovery.h"

#include <stdio.h>
#include <string.h>

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

#define CHECK_STR_CONTAINS(haystack, needle)                                            \
    do {                                                                                \
        s_checks++;                                                                     \
        if (strstr((haystack), (needle)) == NULL) {                                      \
            s_failures++;                                                               \
            printf("FAIL %s:%d: '%s' not in '%s'\n", __FILE__, __LINE__, (needle), (haystack)); \
        }                                                                               \
    } while (0)

/* A reboot is simulated by keeping the record and calling begin() again with a
 * new reset reason - which is exactly what RTC no-init memory does. */
static lw_recovery_t s_rtc;

static void power_cycle(void)
{
    /* A real power cycle loses RTC no-init memory. */
    memset(&s_rtc, 0, sizeof(s_rtc));
}

static void test_reason_classification(void)
{
    CHECK(lw_recovery_reason_is_abnormal(LW_RESET_PANIC));
    CHECK(lw_recovery_reason_is_abnormal(LW_RESET_INT_WDT));
    CHECK(lw_recovery_reason_is_abnormal(LW_RESET_TASK_WDT));
    CHECK(lw_recovery_reason_is_abnormal(LW_RESET_WDT));
    CHECK(lw_recovery_reason_is_abnormal(LW_RESET_BROWNOUT));
    CHECK(lw_recovery_reason_is_abnormal(LW_RESET_PWR_GLITCH));
    CHECK(lw_recovery_reason_is_abnormal(LW_RESET_CPU_LOCKUP));

    /* None of these mean the firmware died. */
    CHECK(!lw_recovery_reason_is_abnormal(LW_RESET_POWERON));
    CHECK(!lw_recovery_reason_is_abnormal(LW_RESET_EXT));
    CHECK(!lw_recovery_reason_is_abnormal(LW_RESET_SW));
    CHECK(!lw_recovery_reason_is_abnormal(LW_RESET_DEEPSLEEP));
    CHECK(!lw_recovery_reason_is_abnormal(LW_RESET_USB));
    CHECK(!lw_recovery_reason_is_abnormal(LW_RESET_JTAG));
    CHECK(!lw_recovery_reason_is_abnormal(LW_RESET_UNKNOWN));

    CHECK(strcmp(lw_recovery_reason_name(LW_RESET_PANIC), "panic") == 0);
    CHECK(strcmp(lw_recovery_reason_name(LW_RESET_POWERON), "power-on") == 0);
    CHECK(lw_recovery_reason_name(LW_RESET_UNKNOWN) != NULL);
    CHECK(lw_recovery_reason_name(9999) != NULL); /* out of range must not crash */
}

static void test_first_boot_is_clean(void)
{
    power_cycle();
    lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    CHECK(!d.record_valid);
    CHECK(!d.previous_abnormal);
    CHECK(!d.safe_mode);
    CHECK(d.streak == 0);
    CHECK(s_rtc.total_boots == 1);
    CHECK(s_rtc.abnormal_streak == 0);
}

static void test_power_cycle_never_latches_safe_mode(void)
{
    /* Unplug/replug twenty times: each power cycle wipes the record, so the
     * streak can never grow. A board that is merely being switched off must
     * never come back degraded. */
    for (int i = 0; i < 20; i++) {
        power_cycle();
        lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
        CHECK(!d.safe_mode);
        CHECK(d.streak == 0);
    }
}

static void test_two_crashes_enter_safe_mode(void)
{
    power_cycle();

    /* Boot 1: clean, then it panics. */
    lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    CHECK(!d.safe_mode);
    lw_recovery_tick(&s_rtc, 4000);

    /* Boot 2: reason says the previous boot panicked. One crash, not yet a loop. */
    d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    CHECK(d.record_valid);
    CHECK(d.previous_abnormal);
    CHECK(d.streak == 1);
    CHECK(!d.safe_mode);
    CHECK(d.previous_uptime_ms == 4000);
    lw_recovery_tick(&s_rtc, 900);

    /* Boot 3: panicked again. Two in a row - safe mode. */
    d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    CHECK(d.streak == 2);
    CHECK(d.safe_mode);
    CHECK(s_rtc.safe_boots == 1);
    CHECK(d.previous_uptime_ms == 900);
    CHECK(s_rtc.total_boots == 3);
}

static void test_safe_mode_persists_while_crashing(void)
{
    power_cycle();
    (void)lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    (void)lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    CHECK(d.safe_mode);

    /* Still crashing: stays in safe mode and keeps counting. */
    d = lw_recovery_begin(&s_rtc, LW_RESET_TASK_WDT);
    CHECK(d.safe_mode);
    CHECK(d.streak == 3);
    CHECK(s_rtc.safe_boots == 2);
}

static void test_clean_boot_clears_the_streak(void)
{
    power_cycle();
    (void)lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    (void)lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    CHECK(d.safe_mode);

    /* Safe mode came up, then the board was reset by software (a deliberate
     * restart, not a crash). The streak must clear, otherwise safe mode would
     * latch until the next power cycle. */
    d = lw_recovery_begin(&s_rtc, LW_RESET_SW);
    CHECK(!d.previous_abnormal);
    CHECK(d.streak == 0);
    CHECK(!d.safe_mode);

    /* And a fresh crash after that is only one again. */
    d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    CHECK(d.streak == 1);
    CHECK(!d.safe_mode);
}

static void test_occasional_crash_never_accumulates(void)
{
    power_cycle();
    /* The first call after a power cycle starts a fresh record, so the loop
     * begins with an explicit clean boot and then alternates crash/clean. */
    (void)lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    for (int i = 0; i < 10; i++) {
        lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
        CHECK(d.streak == 1); /* one crash, then a clean boot, forever */
        CHECK(!d.safe_mode);
        d = lw_recovery_begin(&s_rtc, LW_RESET_SW);
        CHECK(d.streak == 0);
        CHECK(!d.safe_mode);
    }
}

static void test_garbage_record_is_discarded(void)
{
    power_cycle();
    /* RTC no-init memory after a power cycle holds arbitrary bytes. A streak of
     * 999 must not be believed. */
    memset(&s_rtc, 0xA5, sizeof(s_rtc));
    s_rtc.abnormal_streak = 999;
    lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    CHECK(!d.record_valid);
    CHECK(!d.safe_mode);
    CHECK(d.streak == 0);
    CHECK(s_rtc.abnormal_streak == 0);
    CHECK(s_rtc.total_boots == 1);

    /* A valid magic with a corrupted body is still refused. */
    lw_recovery_begin(&s_rtc, LW_RESET_SW);
    s_rtc.total_boots = 4321; /* check no longer matches */
    d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    CHECK(!d.record_valid);
    CHECK(!d.safe_mode);
    CHECK(d.streak == 0);
}

static void test_tick_is_safe_before_begin(void)
{
    power_cycle();
    /* Nothing armed: the timer must not stamp a bogus record. */
    lw_recovery_tick(&s_rtc, 1234);
    CHECK(s_rtc.magic == 0u);

    lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    lw_recovery_tick(&s_rtc, 1234);
    CHECK(s_rtc.last_uptime_ms == 1234u);

    /* NULL is tolerated by every entry point: these are called from a timer. */
    lw_recovery_tick(NULL, 1);
    lw_recovery_decision_t d = lw_recovery_begin(NULL, LW_RESET_PANIC);
    CHECK(!d.safe_mode);
    CHECK(!d.record_valid);
}

static void test_describe(void)
{
    power_cycle();
    (void)lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    lw_recovery_tick(&s_rtc, 2000);
    lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_PANIC);
    char line[192];
    const char *out = lw_recovery_describe(&s_rtc, &d, line, sizeof(line));
    CHECK(out == line);
    CHECK_STR_CONTAINS(line, "previous crashed");
    CHECK_STR_CONTAINS(line, "2000 ms");
    CHECK_STR_CONTAINS(line, "streak 1");
    CHECK_STR_CONTAINS(line, "safe mode off");

    /* A record that is not ours must describe itself as untracked, not as a
     * zero-filled "boot 0". */
    power_cycle();
    out = lw_recovery_describe(&s_rtc, &d, line, sizeof(line));
    CHECK(out == line);
    CHECK_STR_CONTAINS(line, "not tracked");

    /* Degenerate buffers must not overflow or return NULL. */
    CHECK(lw_recovery_describe(&s_rtc, &d, line, 1) != NULL);
    CHECK(line[0] == '\0');
    CHECK(lw_recovery_describe(NULL, NULL, line, sizeof(line)) != NULL);
}

static void test_brownout_counts_as_a_crash(void)
{
    power_cycle();
    (void)lw_recovery_begin(&s_rtc, LW_RESET_POWERON);
    lw_recovery_decision_t d = lw_recovery_begin(&s_rtc, LW_RESET_BROWNOUT);
    CHECK(d.previous_abnormal);
    CHECK(d.streak == 1);
    d = lw_recovery_begin(&s_rtc, LW_RESET_PWR_GLITCH);
    CHECK(d.safe_mode);
}

int main(void)
{
    test_reason_classification();
    test_first_boot_is_clean();
    test_power_cycle_never_latches_safe_mode();
    test_two_crashes_enter_safe_mode();
    test_safe_mode_persists_while_crashing();
    test_clean_boot_clears_the_streak();
    test_occasional_crash_never_accumulates();
    test_garbage_record_is_discarded();
    test_tick_is_safe_before_begin();
    test_describe();
    test_brownout_counts_as_a_crash();

    if (s_failures == 0) {
        printf("RECOVERY TESTS PASSED (%d checks)\n", s_checks);
        return 0;
    }
    printf("RECOVERY TESTS FAILED (%d checks, %d failure(s))\n", s_checks, s_failures);
    return 1;
}
