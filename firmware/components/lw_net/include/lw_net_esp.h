/* Little World - platform adapter boundary for the connectivity services.
 *
 * lw_net_esp.c provides a compiled-in stub (everything unavailable) unless a
 * Wi-Fi stack is actually present. lw_net_wifi_esp.c provides the real ESP-IDF
 * implementation and is only compiled when CONFIG_LW_NET_WIFI_ENABLE is set and
 * esp_wifi.h can be found, so the default build never depends on the C6 stack.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How long a single SNTP wait may block before the service reports a failure. */
#ifndef LW_NET_SNTP_TIMEOUT_MS
#define LW_NET_SNTP_TIMEOUT_MS 15000
#endif

struct lw_services;

bool lw_net_esp_wifi_available(void);
bool lw_net_esp_time_available(void);
bool lw_net_esp_audio_available(void);

void lw_net_esp_wifi_attach(struct lw_services *services);
void lw_net_esp_time_attach(struct lw_services *services);

bool lw_net_esp_wifi_start_scan(void);
bool lw_net_esp_wifi_start_connect(const char *ssid, const char *password);
void lw_net_esp_wifi_stop(void);
void lw_net_esp_time_request(void);

#ifdef __cplusplus
}
#endif
