/* SPDX-License-Identifier: MIT */
#include "lw_system_wifi_bridge.h"

#include <string.h>

void lw_system_wifi_bridge_init(lw_system_wifi_bridge_t *bridge)
{
    memset(bridge, 0, sizeof(*bridge));
}

bool lw_system_wifi_bridge_scan(void *context)
{
    if (!context) {
        return false;
    }
    return lw_services_scan((lw_services_t *)context, lw_services_now_ms());
}

bool lw_system_wifi_bridge_connect(void *context, const char *ssid, const char *password)
{
    if (!context || !ssid) {
        return false;
    }
    return lw_services_connect((lw_services_t *)context, ssid, password, lw_services_now_ms());
}

bool lw_system_wifi_bridge_disconnect(void *context, bool forget)
{
    (void)forget; /* credentials are RAM-only in the model; nothing stored to erase */
    if (!context) {
        return false;
    }
    return lw_services_disconnect((lw_services_t *)context, lw_services_now_ms());
}

void lw_system_wifi_bridge_install(lw_system_backend_t *backend, lw_services_t *services)
{
    backend->context = services;
    backend->wifi_scan = lw_system_wifi_bridge_scan;
    backend->wifi_connect = lw_system_wifi_bridge_connect;
    backend->wifi_disconnect = lw_system_wifi_bridge_disconnect;
}

void lw_system_wifi_bridge_sync(lw_system_wifi_bridge_t *bridge, lw_system_t *system,
                                lw_services_t *services, uint32_t now_ms)
{
    if (!bridge || !system || !services) {
        return;
    }

    /* Availability first: the facade is created before the services
     * start, so it is born UNSUPPORTED; upgrade (or downgrade) it the
     * moment the services' answer differs. */
    if (system->backend.network_available != services->caps.wifi_backend) {
        lw_system_set_network_available(system, services->caps.wifi_backend);
    }

    /* The facade owns the user's on/off switch; the radio lives in the
     * services. Push the switch across whenever it differs. */
    if (!bridge->enabled_valid || bridge->enabled != system->wifi_enabled) {
        bridge->enabled = system->wifi_enabled;
        bridge->enabled_valid = true;
        (void)lw_services_wifi_enable(services, system->wifi_enabled, now_ms);
    }

    const lw_wifi_state_t model = lw_wifi_state(&services->wifi);

    /* Scan results: deliver a finished scan exactly once per scan
     * generation, and only while the facade is still waiting for it.
     * Generations that finish while nobody waits are marked delivered
     * so a later scan request cannot pick up a stale list. */
    if (system->wifi == LW_WIFI_SCANNING) {
        if (model != LW_WIFI_ST_SCANNING &&
            services->wifi.scan_generation != bridge->scan_generation) {
            bridge->scan_generation = services->wifi.scan_generation;
            lw_network_t nets[8];
            unsigned count = services->wifi.ap_count;
            if (count > 8) {
                count = 8;
            }
            for (unsigned i = 0; i < count; i++) {
                memset(&nets[i], 0, sizeof(nets[i]));
                const char *ssid = services->wifi.aps[i].ssid;
                size_t len = strlen(ssid);
                if (len >= sizeof(nets[i].ssid)) {
                    len = sizeof(nets[i].ssid) - 1;
                }
                memcpy(nets[i].ssid, ssid, len);
                nets[i].rssi = services->wifi.aps[i].rssi;
                nets[i].secured = services->wifi.aps[i].secured;
            }
            lw_system_scan_result(system, nets, count);
        }
    } else if (model != LW_WIFI_ST_SCANNING) {
        bridge->scan_generation = services->wifi.scan_generation;
    }

    /* Connect outcome: the model retries internally (backoff, bounded
     * attempts) and only settles in CONNECTED or FAILED, so mirror
     * exactly those two terminal states. */
    if (system->wifi == LW_WIFI_CONNECTING) {
        if (model == LW_WIFI_ST_CONNECTED) {
            lw_system_connection_result(system, true);
        } else if (model == LW_WIFI_ST_FAILED) {
            lw_system_connection_result(system, false);
        }
    }

    /* A link the facade believes is up, but the radio dropped: mirror
     * the loss. The model may already be reconnecting; if it lands,
     * the link_up mirror below brings the facade back. */
    if (system->wifi == LW_WIFI_CONNECTED && model != LW_WIFI_ST_CONNECTED) {
        lw_system_link_lost(system);
    }

    /* A link that came up without a facade request: the C6 kept the
     * association across a P4 reboot (the services probe finds it), or
     * an automatic reconnect succeeded after link_lost above. */
    if (system->wifi == LW_WIFI_IDLE && model == LW_WIFI_ST_CONNECTED &&
        services->wifi.target[0]) {
        lw_system_link_up(system, services->wifi.target);
    }
}
