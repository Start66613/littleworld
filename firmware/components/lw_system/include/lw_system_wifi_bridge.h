/* Glue between the lw_system UI facade and the lw_net connectivity
 * services (Wi-Fi landing, 2026-11-09).
 *
 * Why this exists: the facade and the services grew up separately. The
 * facade's backend never got its wifi_scan/wifi_connect/wifi_disconnect
 * callbacks installed, its network_available came from the hardware
 * caps - which are always false for Wi-Fi by design ("the backend
 * never claims a radio; the capability comes from the connectivity
 * service") - and nothing ever called lw_system_scan_result() /
 * lw_system_connection_result() for the real (non-simulated) path:
 * lw_system_tick() only fabricates results for the simulated backend.
 * So even with a working C6 backend the facade sat at UNSUPPORTED and
 * refused every scan and connect, and had a link come up on its own,
 * the facade would never have heard about it.
 *
 * The bridge closes both directions:
 *   facade -> services: the three backend callbacks forward scan /
 *     connect / disconnect requests to lw_services, and sync()
 *     reconciles the facade's on/off switch into the radio.
 *   services -> facade: sync() delivers finished scan lists and
 *     connect outcomes, and mirrors link changes the user did not
 *     start here (link lost, automatic reconnect, a link the C6 kept
 *     across a P4 reboot) through lw_system_link_lost()/link_up().
 *
 * Threading: the callbacks run wherever the facade's are called (the
 * UI thread in practice) and only queue requests in the model - the
 * services poll performs the adapter calls. sync() must run on the UI
 * thread, like every other lw_system mutation; the boot code calls it
 * from the same timer that publishes capabilities and time.
 *
 * Credentials: passwords pass through to the model, which keeps them
 * in RAM only and zeroizes them; the bridge never logs them. "Forget
 * network" maps to a plain disconnect - there is no stored credential
 * to erase, by design.
 *
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lw_services.h"
#include "lw_system.h"

typedef struct {
    uint32_t scan_generation; /* model scan generation last delivered */
    bool enabled;             /* enable flag last pushed to the services */
    bool enabled_valid;
} lw_system_wifi_bridge_t;

void lw_system_wifi_bridge_init(lw_system_wifi_bridge_t *bridge);

/* Backend callbacks for lw_system_backend_t. The backend context must
 * be the lw_services_t (lw_system_wifi_bridge_install sets it). */
bool lw_system_wifi_bridge_scan(void *context);
bool lw_system_wifi_bridge_connect(void *context, const char *ssid, const char *password);
bool lw_system_wifi_bridge_disconnect(void *context, bool forget);

/* Sets backend->context and the three Wi-Fi callbacks. Call before
 * lw_system_init(). Other backend fields are left untouched. */
void lw_system_wifi_bridge_install(lw_system_backend_t *backend, lw_services_t *services);

/* Reconcile facade and services state. Call regularly on the UI thread
 * (the boot code uses its existing capabilities timer). now_ms is the
 * services clock, lw_services_now_ms(). */
void lw_system_wifi_bridge_sync(lw_system_wifi_bridge_t *bridge, lw_system_t *system,
                                lw_services_t *services, uint32_t now_ms);
