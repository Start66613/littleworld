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

bool lw_net_esp_wifi_available(void) { return false; }
bool lw_net_esp_time_available(void) { return false; }
bool lw_net_esp_audio_available(void) { return false; }

void lw_net_esp_wifi_attach(struct lw_services *services) { (void)services; }
void lw_net_esp_time_attach(struct lw_services *services) { (void)services; }

bool lw_net_esp_wifi_start_scan(void) { return false; }
bool lw_net_esp_wifi_start_connect(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    return false;
}

void lw_net_esp_wifi_stop(void) {}
void lw_net_esp_time_request(void) {}
