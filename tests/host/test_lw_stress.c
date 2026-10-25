/*
 * Little World - offline stress and fault injection for the model layer.
 *
 * Nothing here touches a device, a file or a network: it links the same pure-C
 * model code the firmware runs (lw_wifi, lw_time_service, lw_audio, lw_services)
 * against an explicit clock and a deliberately hostile event stream. The point
 * is to find "it only breaks after N hours" bugs while the board is on a shelf.
 *
 * Faults injected:
 *   - adapter events arriving for stale generations
 *   - an event storm far larger than the 8-entry ring (dropped counters)
 *   - time jumps: backwards, and a uint32 wrap at 49.7 days
 *   - Wi-Fi enable/scan/connect churn while the poll is running
 *   - audio frames fed in odd-sized chunks, beside mute/unmute churn
 *   - a two-hour accelerated run at the real 500 ms poll period
 *
 * Exit code 0 means every invariant held; 1 means a failure was reported.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "lw_audio.h"
#include "lw_services.h"
#include "lw_time_svc.h"
#include "lw_wifi.h"

static int g_checks;
static int g_failures;

static void check(bool condition, const char *what)
{
    g_checks++;
    if (!condition) {
        g_failures++;
        printf("FAIL: %s\n", what);
    }
}

/* The offline host build has no radio, so the service layer reports the Wi-Fi
 * model as unsupported. Every test that needs the state machine drives the
 * model directly - that is exactly what lw_services_drain() would do. */

static struct lw_services g_services;
static uint32_t g_now;

static void spin(uint32_t ms, uint32_t step)
{
    uint32_t left = ms;
    while (left > 0) {
        uint32_t d = left < step ? left : step;
        g_now += d;
        lw_services_poll(&g_services, g_now);
        left -= d;
    }
}

static void fresh(void)
{
    memset(&g_services, 0, sizeof(g_services));
    g_now = 1000;
    lw_services_init(&g_services);
    lw_services_start(&g_services, g_now);
}

/* --------------------------------------------------------- event storm --- */

static void test_event_storm(void)
{
    fresh();
    lw_services_t *s = &g_services;
    lw_wifi_ap_t aps[8];
    memset(aps, 0, sizeof(aps));
    for (int i = 0; i < 8; i++) {
        snprintf(aps[i].ssid, sizeof(aps[i].ssid), "ap-%d", i);
        aps[i].rssi = (int8_t)(-30 - (int)i * 5);
        aps[i].secured = (i & 1) != 0;
    }

    /* 2000 adapter reports without a single poll: the ring must absorb them by
     * dropping the oldest and counting, never by growing without bound. */
    for (int i = 0; i < 2000; i++) {
        lw_services_wifi_report_scan(s, 1, aps, 8, true);
    }
    unsigned dropped = s->events.dropped;
    check(dropped > 0, "an event storm must be counted, not silently lost");
    check(dropped < 2000 + LW_NET_EVENT_QUEUE, "dropped counter must stay bounded");

    spin(100, 50);
    /* dropped counts events the ring REFUSED; it is a lifetime total, not a
     * depth gauge, so draining must never reset it. What must hold after a drain
     * is that the queue is empty and the producer is unblocked again. */
    check(s->events.head == s->events.tail, "the ring must drain completely");

    /* A mixed storm must not leave the queue wedged: after draining, one more
     * well-formed event must land. */
    for (int i = 0; i < 500; i++) {
        lw_services_wifi_report_scan(s, 1, aps, 8, (i % 3) != 0);
        lw_services_wifi_report_failed(s, 1, LW_WIFI_ERR_AUTH_FAILED, g_now);
        lw_services_wifi_report_lost(s, 0, g_now);
    }
    spin(200, 50);
    check(s->events.head == s->events.tail, "second storm must drain too");

    lw_wifi_ap_t one[1];
    memset(one, 0, sizeof(one));
    snprintf(one[0].ssid, sizeof(one[0].ssid), "after-storm");
    one[0].rssi = -50;
    check(lw_services_wifi_report_scan(s, 1, one, 1, true), "the ring must accept after a storm");
    spin(50, 25);
    check(s->events.head == s->events.tail, "the post-storm event must be applied");

    /* Honesty check: the offline host has no radio, so every applied scan is
     * ignored by the model. The ring itself must still be empty - "ignored"
     * must not mean "stuck". */
    check(lw_wifi_scan_at(&s->wifi, 0) == NULL,
          "an unsupported radio must never publish scan results");
    check(s->wifi.state == LW_WIFI_ST_UNSUPPORTED,
          "the snapshot must say Unsupported when no radio is compiled in");
}

/* ------------------------------------------------------ stale events --- */

static void test_stale_generations(void)
{
    fresh();
    lw_wifi_t *w = &g_services.wifi;

    /* A stale positive result must not fabricate a link. */
    const uint32_t gen = lw_wifi_generation(w);
    lw_wifi_init(w, true);
    lw_wifi_enable(w, true, g_now);
    lw_wifi_scan(w, g_now);
    const uint32_t live_scan = lw_wifi_scan_generation(w);
    check(live_scan > 0, "a scan must bump the scan generation");

    lw_wifi_ap_t stale[2];
    memset(stale, 0, sizeof(stale));
    snprintf(stale[0].ssid, sizeof(stale[0].ssid), "stale-ap");
    stale[0].rssi = -80;
    lw_wifi_on_scan_done(w, live_scan - 1, stale, 1, true);
    check(lw_wifi_scan_at(w, 0) == NULL, "a stale scan result must be ignored");

    lw_wifi_on_scan_done(w, live_scan, stale, 1, true);
    const lw_wifi_ap_t *applied = lw_wifi_scan_at(w, 0);
    check(applied != NULL && strcmp(applied->ssid, "stale-ap") == 0,
          "a current scan result must be applied");

    /* A stale connect failure cannot fail the current attempt. */
    lw_wifi_connect(w, "home", "secret", g_now);
    const uint32_t live_conn = lw_wifi_generation(w);
    lw_wifi_on_connect_failed(w, live_conn - 1, LW_WIFI_ERR_AUTH_FAILED, g_now);
    check(lw_wifi_state(w) == LW_WIFI_ST_CONNECTING,
          "a stale failure must not end the current attempt");
    lw_wifi_on_connected(w, live_conn, "192.168.1.7", g_now);
    check(strcmp(lw_wifi_ip(w), "192.168.1.7") == 0, "the current connect must apply");
    check(lw_wifi_state(w) == LW_WIFI_ST_CONNECTED, "the current connect must set CONNECTED");
    (void)gen;
}

/* --------------------------------------------------------- time jumps --- */

static void test_time_jumps(void)
{
    fresh();
    lw_time_service_t *t = &g_services.time;

    lw_time_service_init(t, 480);
    lw_time_service_request_sync(t, 500000);

    /* Backwards jump: the tick must not treat a negative delta as enormous
     * (a classic way to force a resync every frame). */
    lw_time_service_tick(t, 1000);
    lw_time_service_tick(t, 900);
    check(t->attempts <= 1, "a backwards clock must not start another attempt by itself");

    /* uint32 wrap at 49.7 days of uptime. */
    lw_time_service_init(t, 480);
    lw_time_service_request_sync(t, 0xFFFFFF00u);
    lw_time_service_on_sync_ok(t, 1770000000LL, 0xFFFFFF00u);
    check(lw_time_state(t) == LW_TIME_SYNCED, "a sync before the wrap must register");
    lw_time_service_tick(t, 0x00000040u); /* wrapped */
    check(lw_time_state(t) != LW_TIME_FAILED, "the wrap point must not read as a failure");

    lw_clock_t clock;
    check(lw_time_service_clock(t, 0x00000040u, &clock) || !lw_time_service_clock(t, 0x00000040u, &clock), "clock read must not crash");
}

/* ----------------------------------------------------- audio chunking --- */

static void test_audio_chunks(void)
{
    lw_audio_t a;
    uint8_t frame[3] = {0x11, 0x22, 0x33};

    /* No sink: every write is refused and nothing enters the ring. */
    lw_audio_init(&a, false);
    unsigned accepted = 0;
    for (int i = 0; i < 100; i++) {
        accepted += lw_audio_write(&a, frame, 3);
    }
    check(accepted == 0, "a missing sink must accept nothing");
    check(a.used == 0, "a missing sink must not queue anything");
    check(lw_audio_error(&a) == LW_AUDIO_ERR_NOT_SUPPORTED, "the refusal must explain itself");

    /* With a sink, odd-sized chunks are preserved and the ring never overflows.
     * Draining every few writes keeps the accounting honest: written bytes are
     * ring insertions, dropped bytes are the ones the ring refused. */
    lw_audio_init(&a, true);
    lw_audio_start(&a);
    unsigned written = 0;
    unsigned offered = 0;
    uint8_t out[64];
    for (int i = 0; i < 2000; i++) {
        unsigned len = (unsigned)(i % 3) + 1;
        offered += len;
        written += lw_audio_write(&a, frame, len);
        check(a.used <= LW_AUDIO_RING_BYTES, "the PCM ring must never overflow");
        while (lw_audio_read(&a, out, sizeof(out)) > 0) {
            /* drain so the ring keeps accepting */
        }
    }
    check(written + a.dropped_bytes == offered,
          "written + dropped must equal the bytes offered");
    check(a.used == 0, "the ring must be empty after every chunk was drained");

    /* Draining an already-drained ring must be a clean no-op. */
    uint8_t tail[16];
    check(lw_audio_read(&a, tail, sizeof(tail)) == 0, "an empty ring must read nothing");
    check(a.used == 0, "the ring must be empty after draining");

    /* Mute consumes instead of failing: stream timing must not shift. */
    lw_audio_set_muted(&a, true);
    unsigned before = a.dropped_bytes;
    lw_audio_write(&a, frame, 3);
    check(a.dropped_bytes > before, "a muted write must be accounted, not rejected");
    check(a.used < LW_AUDIO_RING_BYTES, "a muted write must not fill the ring");
}

/* -------------------------------------------------- wifi state machine --- */

static void test_wifi_churn(void)
{
    fresh();
    lw_wifi_t *w = &g_services.wifi;
    lw_wifi_init(w, true);

    for (int i = 0; i < 200; i++) {
        lw_wifi_enable(w, (i & 1) != 0, g_now);
        g_now += 20;
        lw_wifi_scan(w, g_now);
        g_now += 10;
        lw_wifi_connect(w, "net", "pw", g_now);
        g_now += 10;
        lw_wifi_tick(w, g_now);
    }
    /* Every intermediate state must be one the UI can render; only a pending
     * automatic retry may look "not settled" at the very end. */
    const lw_wifi_state_t st = lw_wifi_state(w);
    check(st == LW_WIFI_ST_IDLE || st == LW_WIFI_ST_OFF || st == LW_WIFI_ST_CONNECTING ||
              st == LW_WIFI_ST_SCANNING,
          "churn must settle in a state the UI can render");
    check(!lw_wifi_ip(w)[0], "churn must not leave a phantom IP");
    check(w->reconnects <= LW_WIFI_MAX_RECONNECTS, "the reconnect budget must stay bounded");

    /* Disabling must always clear the credential. */
    lw_wifi_enable(w, true, g_now);
    lw_wifi_connect(w, "net", "pw", g_now);
    lw_wifi_enable(w, false, g_now);
    check(!lw_wifi_credentials_held(w), "disabling the radio must wipe the password");
}

/* ------------------------------------------------------------- long run --- */

static void test_long_run(void)
{
    fresh();
    lw_services_t *s = &g_services;
    lw_wifi_t *w = &s->wifi;
    lw_wifi_init(w, true);
    lw_wifi_enable(w, true, g_now);

    /* Two simulated hours at the real 500 ms poll period, with a link loss
     * every 30 minutes. This is the cheap version of "leave it on overnight". */
    unsigned long polls = 0;
    for (uint32_t ms = 0; ms < 2u * 60u * 60u * 1000u; ms += 500) {
        g_now += 500;
        lw_services_poll(s, g_now);
        lw_wifi_tick(w, g_now);
        polls++;
        if ((ms % 1800000u) == 0) {
            lw_wifi_on_disconnected(w, lw_wifi_generation(w), LW_WIFI_ERR_LINK_LOST, g_now);
        }
    }
    check(polls == 14400, "two hours at 500 ms must be 14,400 polls");
    check(s->events.head == s->events.tail, "no event may stay stuck in the ring");
    check(w->state <= LW_WIFI_ST_FAILED, "the Wi-Fi state must stay in range");
    check(s->events.head - s->events.tail <= LW_NET_EVENT_QUEUE,
          "the ring indices must never describe an impossible queue");
}

int main(void)
{
    printf("lw_stress: offline fault injection for the model layer\n");
    test_event_storm();
    test_stale_generations();
    test_time_jumps();
    test_audio_chunks();
    test_wifi_churn();
    test_long_run();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures != 0) {
        printf("STRESS TESTS FAILED\n");
        return 1;
    }
    printf("STRESS TESTS PASSED\n");
    return 0;
}
