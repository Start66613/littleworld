/* Host tests for the Little World connectivity services (Wi-Fi, time, audio).
 * Pure C, MSVC /W4 /WX. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>

#include "lw_audio.h"
#include "lw_time_svc.h"
#include "lw_wifi.h"

static int g_checks;
static int g_failures;
static const char *g_case = "";

static void begin(const char *name)
{
    g_case = name;
}

static void check(int condition, const char *what)
{
    g_checks++;
    if (!condition) {
        g_failures++;
        printf("FAIL [%s] %s\n", g_case, what);
    }
}

static void check_str(const char *actual, const char *expected, const char *what)
{
    g_checks++;
    if (actual == NULL || strcmp(actual, expected) != 0) {
        g_failures++;
        printf("FAIL [%s] %s: expected '%s' got '%s'\n", g_case, what, expected, actual ? actual : "(null)");
    }
}

/* ------------------------------------------------------------------ Wi-Fi -- */

static void test_wifi_unsupported(void)
{
    begin("wifi/unsupported");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, false);
    check(lw_wifi_state(&wifi) == LW_WIFI_UNSUPPORTED, "starts unsupported");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_NOT_SUPPORTED, "error set");
    check(!lw_wifi_enable(&wifi, true, 0), "enable refused");
    check(!lw_wifi_scan(&wifi, 0), "scan refused");
    check(!lw_wifi_connect(&wifi, "home", "pw", 0), "connect refused");
    check(lw_wifi_take_action(&wifi, NULL, NULL) == LW_WIFI_ACTION_NONE, "no action");
}

static void test_wifi_scan(void)
{
    begin("wifi/scan");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    check(lw_wifi_state(&wifi) == LW_WIFI_OFF, "starts off");
    check(lw_wifi_enable(&wifi, true, 100), "enable ok");
    check(lw_wifi_state(&wifi) == LW_WIFI_IDLE, "idle after enable");
    check(lw_wifi_scan(&wifi, 200), "scan accepted");
    check(lw_wifi_state(&wifi) == LW_WIFI_SCANNING, "scanning");
    check(!lw_wifi_scan(&wifi, 210), "second scan busy");
    char ssid[LW_WIFI_SSID_MAX];
    const char *pw = (const char *)1;
    check(lw_wifi_take_action(&wifi, ssid, &pw) == LW_WIFI_ACTION_SCAN, "action scan");
    check(pw == NULL, "no password for scan");

    lw_wifi_ap_t aps[3];
    memset(aps, 0, sizeof(aps));
    strcpy(aps[0].ssid, "weak");
    aps[0].rssi = -80;
    strcpy(aps[1].ssid, "strong");
    aps[1].rssi = -40;
    aps[1].secured = true;
    strcpy(aps[2].ssid, "strong");
    aps[2].rssi = -60; /* duplicate, weaker: must not replace */

    lw_wifi_on_scan_done(&wifi, wifi.scan_generation, aps, 3, true);
    check(lw_wifi_state(&wifi) == LW_WIFI_IDLE, "back to idle");
    check(lw_wifi_scan_count(&wifi) == 2, "duplicates collapsed");
    check_str(lw_wifi_scan_at(&wifi, 0)->ssid, "strong", "sorted strongest first");
    check(lw_wifi_scan_at(&wifi, 0)->rssi == -40, "strongest rssi kept");
    check_str(lw_wifi_scan_at(&wifi, 1)->ssid, "weak", "second entry");
    check(lw_wifi_scan_at(&wifi, 2) == NULL, "out of range");

    lw_wifi_event_t ev;
    check(lw_wifi_next_event(&wifi, &ev), "scan event queued");
    check(ev.type == LW_WIFI_EV_SCAN_DONE, "event type");
    check(ev.ap_count == 2, "event count");

    /* A stale completion must be ignored. */
    lw_wifi_on_scan_done(&wifi, wifi.scan_generation - 1u, aps, 1, true);
    check(lw_wifi_scan_count(&wifi) == 2, "stale scan ignored");
}

static void test_wifi_connect(void)
{
    begin("wifi/connect");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    lw_wifi_enable(&wifi, true, 0);
    check(!lw_wifi_connect(&wifi, "", "pw", 0), "empty ssid refused");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_INVALID_ARGS, "invalid args reported");
    check(lw_wifi_connect(&wifi, "home", "secret", 1000), "connect accepted");
    check(lw_wifi_state(&wifi) == LW_WIFI_CONNECTING, "connecting");
    char ssid[LW_WIFI_SSID_MAX];
    const char *pw = NULL;
    check(lw_wifi_take_action(&wifi, ssid, &pw) == LW_WIFI_ACTION_CONNECT, "action connect");
    check_str(ssid, "home", "target ssid");
    check_str(pw, "secret", "password forwarded");
    check(lw_wifi_credentials_held(&wifi), "credentials in ram");

    uint32_t gen = wifi.generation;
    lw_wifi_on_connected(&wifi, gen, "192.168.1.23", 1500);
    check(lw_wifi_state(&wifi) == LW_WIFI_CONNECTED, "connected");
    check_str(lw_wifi_ip(&wifi), "192.168.1.23", "ip stored");
    lw_wifi_event_t ev;
    check(lw_wifi_next_event(&wifi, &ev) && ev.type == LW_WIFI_EV_CONNECTED, "connected event");

    /* A late callback for the previous attempt is dropped. */
    lw_wifi_on_connected(&wifi, gen - 1u, "10.0.0.1", 1600);
    check_str(lw_wifi_ip(&wifi), "192.168.1.23", "stale connect ignored");

    check(lw_wifi_disconnect(&wifi, 1700), "disconnect ok");
    check(lw_wifi_state(&wifi) == LW_WIFI_IDLE, "idle after disconnect");
    check(!lw_wifi_credentials_held(&wifi), "password wiped");
    check(lw_wifi_take_action(&wifi, NULL, NULL) == LW_WIFI_ACTION_DISCONNECT, "action disconnect");
}

static void test_wifi_failures(void)
{
    begin("wifi/failures");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    lw_wifi_enable(&wifi, true, 0);

    /* Wrong password is terminal. */
    lw_wifi_connect(&wifi, "home", "bad", 1000);
    lw_wifi_on_connect_failed(&wifi, wifi.generation, LW_WIFI_ERR_AUTH_FAILED, 1200);
    check(lw_wifi_state(&wifi) == LW_WIFI_FAILED, "auth failure is final");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_AUTH_FAILED, "auth error kept");

    /* A timeout retries with backoff, then gives up after three attempts. */
    lw_wifi_connect(&wifi, "home", "good", 2000);
    uint32_t gen = wifi.generation;
    lw_wifi_on_connect_failed(&wifi, gen, LW_WIFI_ERR_TIMEOUT, 2100);
    check(lw_wifi_state(&wifi) == LW_WIFI_CONNECTING, "still connecting during backoff");
    check(wifi.attempts == 1, "one attempt used");
    lw_wifi_tick(&wifi, 2500);
    check(lw_wifi_take_action(&wifi, NULL, NULL) == LW_WIFI_ACTION_NONE, "no retry before backoff");
    lw_wifi_tick(&wifi, 4100);
    check(lw_wifi_take_action(&wifi, NULL, NULL) == LW_WIFI_ACTION_CONNECT, "retry issued after backoff");
    check(wifi.generation != gen, "generation bumped for the retry");

    gen = wifi.generation;
    lw_wifi_on_connect_failed(&wifi, gen, LW_WIFI_ERR_TIMEOUT, 4200);
    check(wifi.attempts == 2, "two attempts used");
    gen = wifi.generation;
    lw_wifi_on_connect_failed(&wifi, gen, LW_WIFI_ERR_TIMEOUT, 4300);
    check(lw_wifi_state(&wifi) == LW_WIFI_FAILED, "failed after three attempts");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_RETRIES_EXHAUSTED, "retries exhausted reported");

    /* Link loss after a good connection. */
    lw_wifi_connect(&wifi, "home", "good", 5000);
    lw_wifi_on_connected(&wifi, wifi.generation, "192.168.1.9", 5100);
    while (lw_wifi_next_event(&wifi, NULL)) {
        /* drain everything queued by the earlier attempts */
    }
    lw_wifi_on_disconnected(&wifi, wifi.generation, LW_WIFI_ERR_LINK_LOST, 5200);
    check(lw_wifi_state(&wifi) == LW_WIFI_IDLE, "idle after link loss");
    lw_wifi_event_t ev;
    check(lw_wifi_next_event(&wifi, &ev) && ev.type == LW_WIFI_EV_DISCONNECTED, "disconnected event");
    check_str(lw_wifi_ip(&wifi), "", "ip cleared");
}

static void test_wifi_timeouts(void)
{
    begin("wifi/timeouts");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    lw_wifi_enable(&wifi, true, 0);
    lw_wifi_scan(&wifi, 100);
    lw_wifi_tick(&wifi, 100 + LW_WIFI_SCAN_TIMEOUT_MS - 1u);
    check(lw_wifi_state(&wifi) == LW_WIFI_SCANNING, "scan still running");
    lw_wifi_tick(&wifi, 100 + LW_WIFI_SCAN_TIMEOUT_MS);
    check(lw_wifi_state(&wifi) == LW_WIFI_IDLE, "scan timed out");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_SCAN_FAILED, "scan failure reported");

    lw_wifi_connect(&wifi, "home", "pw", 20000);
    lw_wifi_tick(&wifi, 20000 + LW_WIFI_CONNECT_TIMEOUT_MS);
    check(wifi.attempts >= 1, "connect timeout counted as an attempt");
    check(lw_wifi_state(&wifi) == LW_WIFI_CONNECTING, "retrying after timeout");
}

static void test_wifi_events_overflow(void)
{
    begin("wifi/events");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    lw_wifi_enable(&wifi, true, 0);
    for (unsigned i = 0; i < LW_WIFI_MAX_EVENTS + 3u; i++) {
        lw_wifi_connect(&wifi, "home", "pw", 0);
        lw_wifi_on_connect_failed(&wifi, wifi.generation, LW_WIFI_ERR_AUTH_FAILED, 0);
    }
    check(lw_wifi_dropped_events(&wifi) == 3u, "oldest events dropped");
    unsigned drained = 0;
    lw_wifi_event_t ev;
    while (lw_wifi_next_event(&wifi, &ev)) {
        drained++;
    }
    check(drained == LW_WIFI_MAX_EVENTS, "queue capped");
}

static void test_wifi_disable(void)
{
    begin("wifi/enable-off");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    lw_wifi_enable(&wifi, true, 0);
    lw_wifi_connect(&wifi, "home", "pw", 0);
    lw_wifi_on_connected(&wifi, wifi.generation, "192.168.1.5", 10);
    check(lw_wifi_enable(&wifi, false, 20), "disable ok");
    check(lw_wifi_state(&wifi) == LW_WIFI_OFF, "off");
    check_str(lw_wifi_ip(&wifi), "", "ip cleared on disable");
    check(!lw_wifi_credentials_held(&wifi), "credentials wiped on disable");
    check(lw_wifi_take_action(&wifi, NULL, NULL) == LW_WIFI_ACTION_DISCONNECT, "disconnect requested");
    check(lw_wifi_enable(&wifi, false, 30), "idempotent disable");
}

/* ------------------------------------------------------------------- time -- */

static void test_time_demo(void)
{
    begin("time/demo");
    lw_time_service_t svc;
    lw_time_service_init(&svc, LW_TIME_DEFAULT_TZ_MINUTES);
    check(lw_time_state(&svc) == LW_TIME_DEMO, "starts demo");
    char hm[6];
    lw_time_service_hm(&svc, 0, hm);
    check_str(hm, "--:--", "no time label");
    lw_clock_t clock;
    check(!lw_time_service_clock(&svc, 0, &clock), "no clock in demo");
    check(!lw_time_service_date(&svc, 0, NULL), "no date in demo");
}

static void test_time_sync(void)
{
    begin("time/sync");
    lw_time_service_t svc;
    lw_time_service_init(&svc, 480);
    check(lw_time_service_request_sync(&svc, 1000), "sync accepted");
    check(lw_time_state(&svc) == LW_TIME_SYNCING, "syncing");
    check(!lw_time_service_request_sync(&svc, 1100), "second sync refused");

    /* 2024-10-27T11:30:00Z == 1730028600. UTC+8 -> 19:30 on the 27th. */
    lw_time_service_on_sync_ok(&svc, 1730028600, 2000);
    check(lw_time_state(&svc) == LW_TIME_SYNCED, "synced");
    char hm[6];
    lw_time_service_hm(&svc, 2000, hm);
    check_str(hm, "19:30", "local hour");
    lw_clock_t clock;
    check(lw_time_service_clock(&svc, 2000, &clock), "clock available");
    check(clock.year == 2024 && clock.month == 10 && clock.day == 27, "calendar date");
    check(clock.hour == 19 && clock.minute == 30, "local time");
    check(clock.weekday == 0, "sunday");

    char label[32];
    lw_time_service_date_label(&svc, 2000, label, sizeof(label));
    check_str(label, "10月27日 周日", "date label");

    /* Drift: 90 s of uptime must show 19:31. */
    lw_time_service_hm(&svc, 92000, hm);
    check_str(hm, "19:31", "clock advances with uptime");

    lw_date_t date;
    check(lw_time_service_date(&svc, 92000, &date), "date available");
    check(date.month == 10 && date.day == 27, "date fields");
}

static void test_time_zone(void)
{
    begin("time/zone");
    lw_time_service_t svc;
    lw_time_service_init(&svc, 0);
    lw_time_service_on_sync_ok(&svc, 1730028600, 0);
    char hm[6];
    lw_time_service_hm(&svc, 0, hm);
    check_str(hm, "11:30", "utc label");
    lw_time_service_set_zone(&svc, 480);
    lw_time_service_hm(&svc, 0, hm);
    check_str(hm, "19:30", "utc+8 label");
    lw_time_service_set_zone(&svc, -300);
    lw_time_service_hm(&svc, 0, hm);
    check_str(hm, "06:30", "utc-5 label");
    lw_time_service_set_zone(&svc, 9999);
    check(lw_time_service_zone(&svc) == 840, "zone clamped high");
    lw_time_service_set_zone(&svc, -9999);
    check(lw_time_service_zone(&svc) == -840, "zone clamped low");
}

static void test_time_stale(void)
{
    begin("time/stale");
    lw_time_service_t svc;
    lw_time_service_init(&svc, 480);
    lw_time_service_on_sync_ok(&svc, 1730028600, 0);
    lw_time_service_on_network_lost(&svc, 1000);
    check(lw_time_state(&svc) == LW_TIME_STALE, "stale after link loss");
    char hm[6];
    lw_time_service_hm(&svc, 1000, hm);
    check_str(hm, "19:30", "stale clock still readable");

    lw_time_service_on_sync_ok(&svc, 1730028600, 5000);
    lw_time_service_tick(&svc, 5000 + LW_TIME_RESYNC_MS - 1u);
    check(lw_time_state(&svc) == LW_TIME_SYNCED, "still fresh before 6h");
    lw_time_service_tick(&svc, 5000 + LW_TIME_RESYNC_MS);
    check(lw_time_state(&svc) == LW_TIME_STALE, "stale after 6h");
}

static void test_time_failure_and_restore(void)
{
    begin("time/failure");
    lw_time_service_t svc;
    lw_time_service_init(&svc, 480);
    lw_time_service_request_sync(&svc, 0);
    lw_time_service_tick(&svc, LW_TIME_SYNC_TIMEOUT_MS);
    check(lw_time_state(&svc) == LW_TIME_DEMO, "no fallback clock -> demo");
    check(lw_time_error(&svc) == LW_TIME_ERR_TIMEOUT, "timeout reported");

    lw_time_service_on_sync_ok(&svc, 1730028600, 100000);
    lw_time_service_request_sync(&svc, 200000);
    lw_time_service_on_sync_failed(&svc, LW_TIME_ERR_NETWORK, 210000);
    check(lw_time_state(&svc) == LW_TIME_STALE, "known time survives a failed resync");

    lw_time_service_t restored;
    lw_time_service_init(&restored, 480);
    lw_time_service_restore(&restored, 1730028600, 0);
    check(lw_time_state(&restored) == LW_TIME_STALE, "restored clock is stale");
    check(restored.rtc_restored, "rtc flag");
    char hm[6];
    lw_time_service_hm(&restored, 0, hm);
    check_str(hm, "19:30", "restored clock readable");
}

/* ------------------------------------------------------------------ audio -- */

static void test_audio_unavailable(void)
{
    begin("audio/unavailable");
    lw_audio_t audio;
    lw_audio_init(&audio, false);
    check(lw_audio_state(&audio) == LW_AUDIO_UNAVAILABLE, "unavailable");
    check(!lw_audio_start(&audio), "start refused");
    check(lw_audio_write(&audio, (const uint8_t *)"abcd", 4) == 0, "write refused");
    check(lw_audio_error(&audio) == LW_AUDIO_ERR_NOT_SUPPORTED, "error reported");
    lw_audio_set_sink(&audio, true);
    check(lw_audio_state(&audio) == LW_AUDIO_READY, "ready once a sink appears");
}

static void test_audio_pipeline(void)
{
    begin("audio/pipeline");
    lw_audio_t audio;
    lw_audio_init(&audio, true);
    check(lw_audio_state(&audio) == LW_AUDIO_READY, "ready");
    check(lw_audio_write(&audio, (const uint8_t *)"abcd", 4) == 0, "write needs playing");
    check(lw_audio_start(&audio), "start ok");
    check(lw_audio_state(&audio) == LW_AUDIO_PLAYING, "playing");

    uint8_t pcm[200];
    for (unsigned i = 0; i < sizeof(pcm); i++) {
        pcm[i] = (uint8_t)i;
    }
    check(lw_audio_write(&audio, pcm, 200) == 200, "200 bytes queued");
    check(lw_audio_free(&audio) == LW_AUDIO_RING_BYTES - 200u, "free space tracked");

    uint8_t out[64];
    check(lw_audio_read(&audio, out, 64) == 64, "64 bytes consumed");
    check(out[0] == 0 && out[63] == 63, "bytes in order");

    check(lw_audio_pause(&audio), "pause ok");
    check(lw_audio_state(&audio) == LW_AUDIO_PAUSED, "paused");
    check(lw_audio_read(&audio, out, 16) == 0, "no reads while paused");
    check(lw_audio_write(&audio, pcm, 16) == 0, "no writes while paused");
    check(lw_audio_resume(&audio), "resume ok");
    check(lw_audio_state(&audio) == LW_AUDIO_PLAYING, "playing again");

    check(lw_audio_stop(&audio), "stop ok");
    check(lw_audio_state(&audio) == LW_AUDIO_READY, "ready after stop");
    check(audio.used == 0, "buffer flushed on stop");
}

static void test_audio_limits(void)
{
    begin("audio/limits");
    lw_audio_t audio;
    lw_audio_init(&audio, true);
    lw_audio_start(&audio);
    static uint8_t big[LW_AUDIO_RING_BYTES + 500];
    unsigned accepted = lw_audio_write(&audio, big, sizeof(big));
    check(accepted == LW_AUDIO_RING_BYTES, "ring filled exactly");
    check(lw_audio_error(&audio) == LW_AUDIO_ERR_FULL, "overflow reported");
    check(audio.dropped_bytes == 500u, "dropped bytes counted");
    check(lw_audio_write(&audio, big, 10) == 0, "full ring refuses");

    lw_audio_set_muted(&audio, true);
    check(lw_audio_write(&audio, big, 32) == 32, "muted writes are consumed");
    check(audio.dropped_bytes == 532u, "muted bytes dropped");
    lw_audio_set_muted(&audio, false);

    lw_audio_on_underrun(&audio);
    check(audio.underruns == 1, "underrun counted");
    lw_audio_on_drained(&audio);
    check(lw_audio_state(&audio) == LW_AUDIO_DONE, "done when drained");
    lw_audio_on_io_error(&audio);
    check(lw_audio_state(&audio) == LW_AUDIO_FAILED, "io error");
    check(lw_audio_error(&audio) == LW_AUDIO_ERR_IO, "io error reported");
    lw_audio_set_volume(&audio, 250);
    check(lw_audio_volume(&audio) == 100u, "volume clamped");
}

int main(void)
{
    test_wifi_unsupported();
    test_wifi_scan();
    test_wifi_connect();
    test_wifi_failures();
    test_wifi_timeouts();
    test_wifi_events_overflow();
    test_wifi_disable();

    test_time_demo();
    test_time_sync();
    test_time_zone();
    test_time_stale();
    test_time_failure_and_restore();

    test_audio_unavailable();
    test_audio_pipeline();
    test_audio_limits();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
