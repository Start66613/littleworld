/* Host tests for the Little World connectivity services (Wi-Fi, time, audio).
 * Pure C, MSVC /W4 /WX. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>

#include "lw_audio.h"
#include "lw_services.h"
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_UNSUPPORTED, "starts unsupported");
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_OFF, "starts off");
    check(lw_wifi_enable(&wifi, true, 100), "enable ok");
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_IDLE, "idle after enable");
    check(lw_wifi_scan(&wifi, 200), "scan accepted");
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_SCANNING, "scanning");
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_IDLE, "back to idle");
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTING, "connecting");
    char ssid[LW_WIFI_SSID_MAX];
    const char *pw = NULL;
    check(lw_wifi_take_action(&wifi, ssid, &pw) == LW_WIFI_ACTION_CONNECT, "action connect");
    check_str(ssid, "home", "target ssid");
    check_str(pw, "secret", "password forwarded");
    check(lw_wifi_credentials_held(&wifi), "credentials in ram");

    uint32_t gen = wifi.generation;
    lw_wifi_on_connected(&wifi, gen, "192.168.1.23", 1500);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTED, "connected");
    check_str(lw_wifi_ip(&wifi), "192.168.1.23", "ip stored");
    lw_wifi_event_t ev;
    check(lw_wifi_next_event(&wifi, &ev) && ev.type == LW_WIFI_EV_CONNECTED, "connected event");

    /* A late callback for the previous attempt is dropped. */
    lw_wifi_on_connected(&wifi, gen - 1u, "10.0.0.1", 1600);
    check_str(lw_wifi_ip(&wifi), "192.168.1.23", "stale connect ignored");

    check(lw_wifi_disconnect(&wifi, 1700), "disconnect ok");
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_IDLE, "idle after disconnect");
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_FAILED, "auth failure is final");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_AUTH_FAILED, "auth error kept");

    /* A timeout retries with backoff, then gives up after three attempts. */
    lw_wifi_connect(&wifi, "home", "good", 2000);
    uint32_t gen = wifi.generation;
    lw_wifi_on_connect_failed(&wifi, gen, LW_WIFI_ERR_TIMEOUT, 2100);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTING, "still connecting during backoff");
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_FAILED, "failed after three attempts");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_RETRIES_EXHAUSTED, "retries exhausted reported");

    /* Link loss after a good connection. */
    lw_wifi_connect(&wifi, "home", "good", 5000);
    lw_wifi_on_connected(&wifi, wifi.generation, "192.168.1.9", 5100);
    while (lw_wifi_next_event(&wifi, NULL)) {
        /* drain everything queued by the earlier attempts */
    }
    lw_wifi_on_disconnected(&wifi, wifi.generation, LW_WIFI_ERR_LINK_LOST, 5200);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_IDLE, "idle after link loss");
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_SCANNING, "scan still running");
    lw_wifi_tick(&wifi, 100 + LW_WIFI_SCAN_TIMEOUT_MS);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_IDLE, "scan timed out");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_SCAN_FAILED, "scan failure reported");

    lw_wifi_connect(&wifi, "home", "pw", 20000);
    lw_wifi_tick(&wifi, 20000 + LW_WIFI_CONNECT_TIMEOUT_MS);
    check(wifi.attempts >= 1, "connect timeout counted as an attempt");
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTING, "retrying after timeout");
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
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_OFF, "off");
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

/* --------------------------------------------------------------- services -- */

static void test_services_requests(void)
{
    begin("services/requests");
    lw_services_t svc;
    lw_services_init(&svc);
    check(!svc.caps.wifi_backend, "no radio on the host stub");
    lw_services_start(&svc, 0);
    check(!lw_services_scan(&svc, 10), "scan refused without a backend");
    check(!lw_services_connect(&svc, "home", "pw", 10), "connect refused without a backend");
    check(!lw_services_request_time_sync(&svc, 10), "sync refused without SNTP");
    check(svc.caps.time_backend == false, "time backend reports unavailable");
}

static void test_services_events(void)
{
    begin("services/events");
    lw_services_t svc;
    lw_services_init(&svc);
    lw_services_start(&svc, 0);

    /* Take the stub out of the picture: the events are what is under test. */
    lw_wifi_init(&svc.wifi, true);
    check(lw_wifi_enable(&svc.wifi, true, 0), "radio enabled");
    check(lw_services_scan(&svc, 10), "scan accepted");

    lw_wifi_ap_t aps[1];
    memset(aps, 0, sizeof(aps));
    strncpy(aps[0].ssid, "home", sizeof(aps[0].ssid) - 1);
    aps[0].rssi = -48;
    aps[0].secured = true;

    uint32_t scan_gen = lw_services_scan_generation(&svc);
    check(lw_services_wifi_report_scan(&svc, scan_gen, aps, 1, true), "scan event queued");
    check(lw_wifi_scan_count(&svc.wifi) == 0, "nothing applied before the drain");
    check(lw_services_drain(&svc, 20) == 1, "one event applied");
    check(lw_wifi_scan_count(&svc.wifi) == 1, "scan applied by the drain");
    check(lw_wifi_scan_at(&svc.wifi, 0) != NULL, "access point visible");

    /* A result from an older request must not overwrite the current one. */
    check(lw_services_scan(&svc, 30), "second scan accepted");
    uint32_t second = lw_services_scan_generation(&svc);
    lw_services_wifi_report_scan(&svc, scan_gen, aps, 1, false);
    lw_services_drain(&svc, 40);
    check(lw_wifi_state(&svc.wifi) == LW_WIFI_ST_SCANNING, "stale scan result ignored");
    lw_services_wifi_report_scan(&svc, second, aps, 1, true);
    lw_services_drain(&svc, 50);
    check(lw_wifi_state(&svc.wifi) == LW_WIFI_ST_IDLE, "current scan result applied");

    check(lw_services_connect(&svc, "home", "secret", 60), "connect accepted");
    uint32_t gen = lw_services_wifi_generation(&svc);
    lw_services_wifi_report_connected(&svc, gen, "10.0.0.5", 70);
    lw_services_drain(&svc, 70);
    check(lw_wifi_state(&svc.wifi) == LW_WIFI_ST_CONNECTED, "connected after the drain");
    check_str(lw_wifi_ip(&svc.wifi), "10.0.0.5", "ip stored");

    /* A stale failure for a superseded attempt must not kill the live link. */
    lw_services_wifi_report_failed(&svc, gen - 1u, LW_WIFI_ERR_TIMEOUT, 80);
    lw_services_drain(&svc, 80);
    check(lw_wifi_state(&svc.wifi) == LW_WIFI_ST_CONNECTED, "stale failure ignored while connected");

    /* A drop reported as a failure while the link is up is a link loss. */
    lw_services_wifi_report_failed(&svc, gen, LW_WIFI_ERR_INTERNAL, 90);
    lw_services_drain(&svc, 90);
    check(lw_wifi_state(&svc.wifi) == LW_WIFI_ST_IDLE, "drop while connected leaves the link");
    check(lw_wifi_error(&svc.wifi) == LW_WIFI_ERR_LINK_LOST, "drop reported as link lost");
}

static void test_services_queue(void)
{
    begin("services/queue");
    lw_services_t svc;
    lw_services_init(&svc);
    lw_services_start(&svc, 0);

    lw_net_event_t event;
    memset(&event, 0, sizeof(event));
    event.kind = LW_NET_EV_SCAN_DONE;
    for (unsigned i = 0; i < LW_NET_EVENT_QUEUE; i++) {
        check(lw_services_push_event(&svc, &event), "queue accepts up to its capacity");
    }
    check(!lw_services_push_event(&svc, &event), "a full queue refuses the event");
    check(svc.events.dropped == 1u, "the drop is counted");
    check(lw_services_drain(&svc, 10) == LW_NET_EVENT_QUEUE, "every queued event is applied");
    check(lw_services_drain(&svc, 20) == 0u, "an empty queue applies nothing");
}

static void test_services_time_downgrade(void)
{
    begin("services/time-downgrade");
    lw_services_t svc;
    lw_services_init(&svc);
    lw_services_start(&svc, 0);
    lw_time_service_request_sync(&svc.time, 10);
    lw_time_service_on_sync_ok(&svc.time, 1761300000LL, 20);
    check(lw_time_state(&svc.time) == LW_TIME_SYNCED, "clock synced");

    /* Not connected: the poll loses the network and the clock becomes stale. */
    lw_services_poll(&svc, 30);
    check(lw_time_state(&svc.time) == LW_TIME_STALE, "clock downgraded to stale without a link");
    check(lw_time_service_utc(&svc.time, 40, NULL), "stale wall time is still usable");
}
static void test_wifi_reconnect_policy(void)
{
    begin("wifi/reconnect-policy");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    lw_wifi_enable(&wifi, true, 0);
    lw_wifi_connect(&wifi, "home", "good", 1000);
    lw_wifi_on_connected(&wifi, wifi.generation, "192.168.1.9", 1100);
    lw_wifi_event_t ev;
    while (lw_wifi_next_event(&wifi, &ev)) {
    }

    /* The first drop after a real link schedules one reconnect, not an instant one. */
    lw_wifi_on_disconnected(&wifi, wifi.generation, LW_WIFI_ERR_LINK_LOST, 2000);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_IDLE, "idle after link loss");
    check(wifi.reconnects == 1, "one reconnect budgeted");
    lw_wifi_tick(&wifi, 2000 + LW_WIFI_BACKOFF_MS - 1u);
    check(lw_wifi_take_action(&wifi, NULL, NULL) == LW_WIFI_ACTION_NONE, "no reconnect before backoff");
    uint32_t gen = wifi.generation;
    lw_wifi_tick(&wifi, 2000 + LW_WIFI_BACKOFF_MS);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTING, "reconnect attempt started");
    check(wifi.generation != gen, "generation bumped for the reconnect");
    char ssid[LW_WIFI_SSID_MAX];
    const char *pw = NULL;
    check(lw_wifi_take_action(&wifi, ssid, &pw) == LW_WIFI_ACTION_CONNECT, "reconnect action issued");
    check_str(ssid, "home", "same target retried");
    check_str(pw, "good", "stored credentials reused");

    /* A success resets the budget so a later drop gets a fresh set of attempts. */
    lw_wifi_on_connected(&wifi, wifi.generation, "192.168.1.10", 5000);
    check(wifi.reconnects == 0, "budget reset on success");

    /* Burning the whole budget ends in FAILED instead of retrying forever. */
    for (unsigned i = 1; i <= LW_WIFI_MAX_RECONNECTS; i++) {
        gen = wifi.generation;
        lw_wifi_on_disconnected(&wifi, gen, LW_WIFI_ERR_LINK_LOST, 6000u);
        check(wifi.reconnects == i, "reconnect counted");
        lw_wifi_tick(&wifi, wifi.retry_at_ms);
        check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTING, "retry attempt in flight");
    }
    gen = wifi.generation;
    lw_wifi_on_disconnected(&wifi, gen, LW_WIFI_ERR_LINK_LOST, 9000);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_FAILED, "failed once the budget is spent");
    check(lw_wifi_error(&wifi) == LW_WIFI_ERR_RETRIES_EXHAUSTED, "retries exhausted reported");
    check(!lw_wifi_credentials_held(&wifi), "password wiped after giving up");
    check(wifi.retry_at_ms == 0, "no further reconnect scheduled");

    /* With the radio switched off a drop must not schedule anything. */
    lw_wifi_enable(&wifi, true, 10000);
    lw_wifi_connect(&wifi, "home", "good", 10100);
    lw_wifi_on_connected(&wifi, wifi.generation, "192.168.1.12", 10200);
    lw_wifi_enable(&wifi, false, 10300);
    gen = wifi.generation;
    lw_wifi_on_disconnected(&wifi, gen, LW_WIFI_ERR_LINK_LOST, 10400);
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_OFF, "off stays off");
    check(wifi.retry_at_ms == 0, "no reconnect while disabled");
}

static void test_wifi_link_up(void)
{
    begin("wifi/link-up");
    lw_wifi_t wifi;
    lw_wifi_init(&wifi, true);
    check(!lw_wifi_on_link_up(&wifi, "cafe", "10.0.0.9", 10), "no link before the radio is on");

    lw_wifi_enable(&wifi, true, 20);
    lw_wifi_event_t ev;
    while (lw_wifi_next_event(&wifi, &ev)) {
    }
    /* An association that no request of this boot produced: accepted from IDLE. */
    check(lw_wifi_on_link_up(&wifi, "cafe", "10.0.0.9", 30), "unsolicited link accepted");
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTED, "state connected");
    check_str(lw_wifi_ip(&wifi), "10.0.0.9", "ip stored");
    check_str(lw_wifi_target(&wifi), "cafe", "ssid becomes the label");
    check(lw_wifi_next_event(&wifi, &ev) && ev.type == LW_WIFI_EV_CONNECTED, "ui told once");

    /* A repeat probe with the same address must not spam the event queue. */
    check(!lw_wifi_on_link_up(&wifi, "cafe", "10.0.0.9", 40), "same address is not a change");
    check(!lw_wifi_next_event(&wifi, &ev), "no duplicate event");

    /* A different address is a real change (DHCP renewal, roam). */
    check(lw_wifi_on_link_up(&wifi, "cafe", "10.0.0.10", 50), "new address is a change");
    check_str(lw_wifi_ip(&wifi), "10.0.0.10", "ip updated");

    /* A probe must never hijack an attempt that is in flight. */
    lw_wifi_connect(&wifi, "home", "pw", 60);
    check(!lw_wifi_on_link_up(&wifi, "cafe", "10.0.0.11", 70), "probe ignored while connecting");
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_CONNECTING, "attempt preserved");

    /* Nor revive a link the user just turned off. */
    lw_wifi_disconnect(&wifi, 80);
    lw_wifi_enable(&wifi, false, 90);
    check(!lw_wifi_on_link_up(&wifi, "cafe", "10.0.0.12", 100), "no link while the radio is off");
    check(lw_wifi_state(&wifi) == LW_WIFI_ST_OFF, "off stays off");
}

static void test_services_link_up_event(void)
{
    begin("services/link-up-event");
    lw_services_t svc;
    lw_services_init(&svc);
    lw_services_start(&svc, 0);
    lw_wifi_init(&svc.wifi, true);
    check(lw_wifi_enable(&svc.wifi, true, 0), "radio enabled");

    check(lw_services_wifi_report_link_up(&svc, "cafe", "10.0.0.9", 10), "link-up event queued");
    check(lw_wifi_state(&svc.wifi) == LW_WIFI_ST_IDLE, "not applied before the drain");
    check(lw_services_drain(&svc, 20) == 1u, "one event applied");
    check(lw_wifi_state(&svc.wifi) == LW_WIFI_ST_CONNECTED, "connected by the drain");
    check_str(lw_wifi_target(&svc.wifi), "cafe", "ssid carried through the event");

    /* The snapshot the UI reads is rebuilt by the poll. */
    lw_net_status_t status;
    lw_services_status(&svc, 30, &status);
    check(status.wifi_state == LW_WIFI_ST_CONNECTED, "snapshot shows the link");
    check_str(status.ssid, "cafe", "snapshot carries the ssid");
    check_str(status.ip, "10.0.0.9", "snapshot carries the address");
}

static void test_services_probe_cadence(void)
{
    begin("services/probe-cadence");
    lw_services_t svc;
    lw_services_init(&svc);
    /* Pretend the adapter is present and the station came up, which is the only
     * case where the poll is allowed to spend a probe. */
    svc.caps.wifi_backend = true;
    lw_wifi_init(&svc.wifi, true);
    lw_wifi_enable(&svc.wifi, true, 0);

    lw_services_poll(&svc, 100);
    check(svc.probe_at_ms == 100u + 1000u, "first poll schedules a probe");
    uint32_t first = svc.probe_at_ms;

    lw_services_poll(&svc, 500);
    check(svc.probe_at_ms == first, "no second probe inside the interval");
    lw_services_poll(&svc, first);
    check(svc.probe_at_ms == first + 1000u, "probe repeats on the cadence");

    /* With the radio off nothing is probed and the schedule is dropped. */
    lw_wifi_enable(&svc.wifi, false, 2000);
    lw_services_poll(&svc, 2000);
    check(svc.probe_at_ms == 0u, "no probe while the radio is off");

    /* A connect in flight is also not probed: the attempt owns the radio. */
    lw_wifi_enable(&svc.wifi, true, 3000);
    lw_wifi_connect(&svc.wifi, "home", "pw", 3000);
    lw_services_poll(&svc, 3000);
    check(svc.probe_at_ms == 0u, "no probe while connecting");
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
    test_wifi_reconnect_policy();
    test_wifi_link_up();

    test_time_demo();
    test_time_sync();
    test_time_zone();
    test_time_stale();
    test_time_failure_and_restore();

    test_audio_unavailable();
    test_audio_pipeline();
    test_audio_limits();

    test_services_requests();
    test_services_events();
    test_services_queue();
    test_services_time_downgrade();
    test_services_link_up_event();
    test_services_probe_cadence();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
