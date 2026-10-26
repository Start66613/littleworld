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
#include <stddef.h>

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

/* Writes a one-line description of the transport actually compiled in (or why
 * Wi-Fi is missing). Used by the boot log and by the readiness report, so a
 * build that claims Wi-Fi but has no usable host can be told apart from a
 * working one without a scope. Never returns a null terminator-free string. */
void lw_net_esp_wifi_describe(char *buf, size_t len);

/* Reads the ESP32-C6 coprocessor firmware version over the hosted link and
 * writes "major.minor.patch" into buf. READ ONLY: it issues the RPC query and
 * never starts an OTA, so a version mismatch is reported instead of "fixed".
 * Returns false (and writes "unknown") when the link cannot answer. */
bool lw_net_esp_wifi_coprocessor_version(char *buf, size_t len);

void lw_net_esp_wifi_attach(struct lw_services *services);

/* Asks the station whether it is already associated (a hosted slave can keep a
 * credential across reboots). Reports the link as an unsolicited event, so the
 * service layer and the UI never show "not connected" while traffic flows.
 * A build without a Wi-Fi stack implements it as a no-op. */
void lw_net_esp_wifi_probe_connected(void);
void lw_net_esp_time_attach(struct lw_services *services);

bool lw_net_esp_wifi_start_scan(void);
bool lw_net_esp_wifi_start_connect(const char *ssid, const char *password);
void lw_net_esp_wifi_stop(void);
void lw_net_esp_time_request(void);

#ifdef __cplusplus
}
#endif
