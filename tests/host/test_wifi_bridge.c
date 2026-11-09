/* Host tests for the lw_system <-> lw_net Wi-Fi bridge (Wi-Fi landing,
 * 2026-11-09). The facade and the services are driven exactly the way
 * lw_boot.c drives them on the board: requests through the facade's
 * backend callbacks, the services polled on their own clock, outcomes
 * reported through the adapter report API, and the bridge synced on
 * the "UI thread" afterwards.
 *
 * The host stub has no radio, so - like test_lw_net.c - the test swaps
 * a supported model into the services after init and lets the
 * instrumented stub record the adapter calls the services issue.
 *
 * Pure C, no ESP-IDF or LVGL dependency. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>

#include "lw_services.h"
#include "lw_system.h"
#include "lw_system_wifi_bridge.h"
#include "lw_wifi.h"

/* Recorded by tests/host/lw_net_esp_stub.c. */
extern int lw_net_esp_stub_scan_calls;
extern int lw_net_esp_stub_connect_calls;
extern int lw_net_esp_stub_stop_calls;
extern char lw_net_esp_stub_last_ssid[33];
extern char lw_net_esp_stub_last_password[65];

static int g_checks;
static int g_failures;
static const char *g_case = "";

static void begin(const char *name) { g_case = name; }

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
    if (strcmp(actual, expected) != 0) {
        g_failures++;
        printf("FAIL [%s] %s: got \"%s\", want \"%s\"\n", g_case, what, actual, expected);
    }
}

typedef struct {
    lw_services_t svc;
    lw_system_t sys;
    lw_system_wifi_bridge_t bridge;
} fixture_t;

/* Same wiring as start_os() in lw_boot.c, with a supported model
 * standing in for the C6 backend the host does not have: the facade
 * is born with network_available=false (the hardware caps never
 * claim a radio), the services report the backend, and the first
 * sync upgrades the facade and reconciles its default-on switch
 * into the radio - the fixture deliberately does not enable the
 * model by hand. */
static void fixture_init(fixture_t *f, uint32_t now_ms)
{
    memset(f, 0, sizeof(*f));
    lw_services_init(&f->svc);
    lw_services_start(&f->svc, now_ms);
    lw_wifi_init(&f->svc.wifi, true);
    f->svc.caps.wifi_backend = true; /* the swap above is the backend */

    lw_system_backend_t backend = {0};
    backend.simulated = false;
    backend.network_available = false; /* as on the board; sync upgrades it */
    lw_system_wifi_bridge_init(&f->bridge);
    lw_system_wifi_bridge_install(&backend, &f->svc);
    lw_system_init(&f->sys, &backend);

    lw_services_poll(&f->svc, now_ms); /* sets the clock the callbacks read */
    lw_system_wifi_bridge_sync(&f->bridge, &f->sys, &f->svc, now_ms);
}

static void poll(fixture_t *f, uint32_t now_ms) { lw_services_poll(&f->svc, now_ms); }
static void sync(fixture_t *f, uint32_t now_ms)
{
    lw_system_wifi_bridge_sync(&f->bridge, &f->sys, &f->svc, now_ms);
}

/* Scan through the facade and deliver two APs; ends with the facade
 * back at IDLE holding the list. Shared by the connect cases. */
static void scan_two_aps(fixture_t *f, uint32_t now_ms)
{
    int scans_before = lw_net_esp_stub_scan_calls;
    poll(f, now_ms);
    check(lw_system_wifi_scan(&f->sys), "facade scan accepted");
    check(f->sys.wifi == LW_WIFI_SCANNING, "facade scanning");
    check(lw_net_esp_stub_scan_calls == scans_before + 1, "adapter asked to scan once");

    lw_wifi_ap_t aps[2];
    memset(aps, 0, sizeof(aps));
    strncpy(aps[0].ssid, "HomeNet", sizeof(aps[0].ssid) - 1);
    aps[0].rssi = -52;
    aps[0].secured = true;
    strncpy(aps[1].ssid, "Cafe", sizeof(aps[1].ssid) - 1);
    aps[1].rssi = -71;
    aps[1].secured = false;

    uint32_t gen = lw_services_scan_generation(&f->svc);
    check(lw_services_wifi_report_scan(&f->svc, gen, aps, 2, true), "scan result queued");
    poll(f, now_ms + 10);
    sync(f, now_ms + 10);
    check(f->sys.wifi == LW_WIFI_IDLE, "facade idle after scan");
    check(f->sys.network_count == 2, "both APs delivered");
    check_str(f->sys.networks[0].ssid, "HomeNet", "first AP ssid");
    check(f->sys.networks[0].rssi == -52, "first AP rssi");
    check(f->sys.networks[0].secured, "first AP secured");
    check_str(f->sys.networks[1].ssid, "Cafe", "second AP ssid");
    check(!f->sys.networks[1].secured, "second AP open");
}

/* 40 characters: longer than an SSID, well inside the 63 the UI and
 * the model accept. Regression bait for the services action queue,
 * which used to copy the password into a 33-byte buffer. */
static const char *LONG_PASSWORD = "0123456789abcdef0123456789abcdef01234567";

static void test_availability(void)
{
    begin("bridge/availability");
    fixture_t f;
    memset(&f, 0, sizeof(f));
    lw_services_init(&f.svc);
    lw_services_start(&f.svc, 1000);
    lw_wifi_init(&f.svc.wifi, true);
    f.svc.caps.wifi_backend = true;

    lw_system_backend_t backend = {0};
    backend.simulated = false;
    backend.network_available = false; /* the board's facade is born this way */
    lw_system_wifi_bridge_init(&f.bridge);
    lw_system_wifi_bridge_install(&backend, &f.svc);
    lw_system_init(&f.sys, &backend);
    check(f.sys.wifi == LW_WIFI_UNSUPPORTED, "facade unsupported before the first sync");
    check(!lw_system_wifi_scan(&f.sys), "scan refused while unsupported");

    lw_services_poll(&f.svc, 1000);
    sync(&f, 1000);
    check(f.sys.wifi == LW_WIFI_IDLE, "sync upgrades the facade once services report a radio");

    /* The offline build: no backend ever reports in, and the facade
     * must stay unsupported rather than pretend. */
    fixture_t g;
    memset(&g, 0, sizeof(g));
    lw_services_init(&g.svc);
    lw_services_start(&g.svc, 1000);
    lw_system_backend_t gbackend = {0};
    gbackend.simulated = false;
    gbackend.network_available = false;
    lw_system_wifi_bridge_init(&g.bridge);
    lw_system_wifi_bridge_install(&gbackend, &g.svc);
    lw_system_init(&g.sys, &gbackend);
    lw_services_poll(&g.svc, 1000);
    sync(&g, 1000);
    check(g.sys.wifi == LW_WIFI_UNSUPPORTED, "no backend, facade stays unsupported");
    check(!lw_system_wifi_scan(&g.sys), "scan still refused without a backend");
}

static void test_scan_connect_linkloss_reconnect(void)
{
    begin("bridge/scan-connect-reconnect");
    fixture_t f;
    fixture_init(&f, 1000);
    check(f.sys.wifi == LW_WIFI_IDLE, "facade idle at boot");
    check(f.sys.wifi_enabled, "facade switch on at boot");
    check(lw_wifi_state(&f.svc.wifi) == LW_WIFI_ST_IDLE, "sync enabled the radio");
    check(LONG_PASSWORD[40] == '\0', "long password is 40 chars");

    scan_two_aps(&f, 1100);

    /* Connect through the facade, as the password panel does. */
    poll(&f, 2000);
    check(lw_system_wifi_connect(&f.sys, 0, LONG_PASSWORD), "facade connect accepted");
    check(f.sys.wifi == LW_WIFI_CONNECTING, "facade connecting");
    check(lw_net_esp_stub_connect_calls == 1, "adapter asked to connect once");
    check_str(lw_net_esp_stub_last_ssid, "HomeNet", "adapter got the ssid");
    check_str(lw_net_esp_stub_last_password, LONG_PASSWORD, "password reaches adapter intact");

    uint32_t gen = lw_services_wifi_generation(&f.svc);
    check(lw_services_wifi_report_connected(&f.svc, gen, "192.168.1.8", 2100), "connect result queued");
    poll(&f, 2100);
    sync(&f, 2100);
    check(f.sys.wifi == LW_WIFI_CONNECTED, "facade connected");
    check_str(f.sys.ssid, "HomeNet", "facade shows the ssid");

    /* The link drops: the facade must not sit on "connected". */
    check(lw_services_wifi_report_lost(&f.svc, gen, 5000), "link loss queued");
    poll(&f, 5000);
    check(lw_wifi_state(&f.svc.wifi) == LW_WIFI_ST_IDLE, "model idle, reconnect scheduled");
    sync(&f, 5000);
    check(f.sys.wifi == LW_WIFI_IDLE, "facade mirrors the loss");
    check(f.sys.ssid[0] == '\0', "facade ssid cleared");

    /* The model's backoff expires inside the poll: a fresh connect
     * action goes to the adapter with the stored credentials, but the
     * facade stays IDLE - only the outcome is mirrored, not the
     * attempt. */
    poll(&f, 5000 + LW_WIFI_BACKOFF_MS);
    check(lw_wifi_state(&f.svc.wifi) == LW_WIFI_ST_CONNECTING, "model reconnecting");
    check(lw_net_esp_stub_connect_calls == 2, "adapter asked to reconnect");
    check_str(lw_net_esp_stub_last_password, LONG_PASSWORD, "reconnect reuses the password");
    sync(&f, 5000 + LW_WIFI_BACKOFF_MS);
    check(f.sys.wifi == LW_WIFI_IDLE, "facade does not flap during the retry");

    uint32_t gen2 = lw_services_wifi_generation(&f.svc);
    check(lw_services_wifi_report_connected(&f.svc, gen2, "192.168.1.9", 7200), "reconnect result queued");
    poll(&f, 7200);
    sync(&f, 7200);
    check(f.sys.wifi == LW_WIFI_CONNECTED, "facade back up after auto reconnect");
    check_str(f.sys.ssid, "HomeNet", "facade ssid restored");
}

static void test_connect_failure(void)
{
    begin("bridge/connect-failure");
    fixture_t f;
    fixture_init(&f, 1000);
    scan_two_aps(&f, 1100);

    poll(&f, 2000);
    check(lw_system_wifi_connect(&f.sys, 0, "wrong-password"), "facade connect accepted");
    uint32_t gen = lw_services_wifi_generation(&f.svc);
    /* Authentication failures are not retryable: the model settles in
     * FAILED at once, and the facade must show the failure panel. */
    check(lw_services_wifi_report_failed(&f.svc, gen, LW_WIFI_ERR_AUTH_FAILED, 2100), "failure queued");
    poll(&f, 2100);
    check(lw_wifi_state(&f.svc.wifi) == LW_WIFI_ST_FAILED, "model failed");
    sync(&f, 2100);
    check(f.sys.wifi == LW_WIFI_FAILED, "facade shows the failure");
    check(f.sys.ssid[0] == '\0', "facade ssid cleared on failure");
}

static void test_probe_link_up(void)
{
    begin("bridge/probe-link-up");
    fixture_t f;
    fixture_init(&f, 1000);
    check(f.sys.wifi == LW_WIFI_IDLE, "facade idle before the probe");

    /* The C6 kept the association across a P4 reboot; the services
     * probe surfaces it as a link-up report with no request of ours. */
    check(lw_services_wifi_report_link_up(&f.svc, "KeptNet", "192.168.1.20", 3000), "link-up queued");
    poll(&f, 3000);
    check(lw_wifi_state(&f.svc.wifi) == LW_WIFI_ST_CONNECTED, "model connected via probe");
    sync(&f, 3000);
    check(f.sys.wifi == LW_WIFI_CONNECTED, "facade shows the kept link");
    check_str(f.sys.ssid, "KeptNet", "facade shows the kept ssid");
}

static void test_enable_switch(void)
{
    begin("bridge/enable-switch");
    fixture_t f;
    fixture_init(&f, 1000);

    lw_system_wifi_enable(&f.sys, false);
    check(f.sys.wifi == LW_WIFI_OFF, "facade off");
    check(lw_net_esp_stub_stop_calls >= 1, "disconnect reached the adapter");
    sync(&f, 1100);
    check(!f.svc.wifi.enabled, "radio disabled by sync");
    check(lw_wifi_state(&f.svc.wifi) == LW_WIFI_ST_OFF, "model off");

    lw_system_wifi_enable(&f.sys, true);
    sync(&f, 1200);
    check(f.svc.wifi.enabled, "radio re-enabled by sync");
    check(lw_wifi_state(&f.svc.wifi) == LW_WIFI_ST_IDLE, "model idle again");
    poll(&f, 1300);
    check(lw_system_wifi_scan(&f.sys), "scan works after re-enable");
}

int main(void)
{
    test_availability();
    test_scan_connect_linkloss_reconnect();
    test_connect_failure();
    test_probe_link_up();
    test_enable_switch();
    printf("Wi-Fi bridge: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
