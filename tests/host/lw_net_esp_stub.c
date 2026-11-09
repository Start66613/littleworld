/* Little World - host stubs for the platform adapter boundary.
 *
 * On the ESP32-P4 the adapter lives in lw_net_esp.c (and, when the Wi-Fi stack is
 * compiled in, lw_net_wifi_esp.c). On the build machine there is no radio, no
 * SNTP client and no codec, so every capability reports itself unavailable and
 * the services fall back to their offline behaviour. Keeping the stubs here
 * means the same lw_services.c object links in both worlds.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_net_esp.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

bool lw_net_esp_wifi_available(void) { return false; }
void lw_net_esp_wifi_probe_connected(void) { }
bool lw_net_esp_time_available(void) { return false; }
bool lw_net_esp_audio_available(void) { return false; }

void lw_net_esp_wifi_attach(struct lw_services *services) { (void)services; }
void lw_net_esp_time_attach(struct lw_services *services) { (void)services; }

/* Recorded adapter calls, read by test_wifi_bridge.c. The services ignore
 * the return values (results arrive as events), so the stub accepts every
 * request and lets the test drive the outcome through the report API -
 * the same shape as the real adapter, minus the radio. */
int lw_net_esp_stub_scan_calls;
int lw_net_esp_stub_connect_calls;
int lw_net_esp_stub_stop_calls;
char lw_net_esp_stub_last_ssid[33];
char lw_net_esp_stub_last_password[65];

bool lw_net_esp_wifi_start_scan(void)
{
    lw_net_esp_stub_scan_calls++;
    return true;
}
bool lw_net_esp_wifi_start_connect(const char *ssid, const char *password)
{
    lw_net_esp_stub_connect_calls++;
    snprintf(lw_net_esp_stub_last_ssid, sizeof(lw_net_esp_stub_last_ssid), "%s", ssid ? ssid : "");
    snprintf(lw_net_esp_stub_last_password, sizeof(lw_net_esp_stub_last_password), "%s",
             password ? password : "");
    return true;
}

void lw_net_esp_wifi_stop(void) { lw_net_esp_stub_stop_calls++; }
void lw_net_esp_time_request(void) {}

void lw_net_esp_wifi_describe(char *buf, size_t len)
{
    if (buf != NULL && len > 0) {
        buf[0] = '\0';
    }
}

bool lw_net_esp_wifi_coprocessor_version(char *buf, size_t len)
{
    if (buf != NULL && len > 0) {
        buf[0] = '\0';
    }
    return false;
}

