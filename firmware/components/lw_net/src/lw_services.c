/* Little World - connectivity service glue. Pure C apart from the adapter calls.
 *
 * Threading: every mutation of the models happens either inside the service
 * lock (requests) or inside lw_services_poll() (events + ticking). Adapter
 * callbacks never touch the models: they push a lw_net_event_t into a small
 * lock-free ring that the poll drains, so a late ESP event can never rewrite a
 * request that is already being retried.
 *
 * SPDX-License-Identifier: MIT */
#include "lw_services.h"

#include <string.h>

#include "lw_net_esp.h"

#define TIME_SYNC_RETRY_MS 30000u

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define LW_SERVICES_LOCK() portENTER_CRITICAL(&s_lock)
#define LW_SERVICES_UNLOCK() portEXIT_CRITICAL(&s_lock)
#else
#define LW_SERVICES_LOCK() \
    do {                   \
    } while (0)
#define LW_SERVICES_UNLOCK() \
    do {                     \
    } while (0)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define LW_AT_LOAD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define LW_AT_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#else
#define LW_AT_LOAD(p) (*(p))
#define LW_AT_STORE(p, v) (*(p) = (v))
#endif

static uint32_t s_now_ms;

/* Defined below; used by the poll to refresh the snapshot the UI copies. */
static void build_status(const lw_services_t *services, uint32_t now_ms, lw_net_status_t *out);

static void copy_str(char *dst, unsigned cap, const char *src)
{
    if (cap == 0) {
        return;
    }
    unsigned i = 0;
    for (; src != NULL && src[i] != '\0' && i + 1u < cap; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ---------------------------------------------------------------- event ring */

static bool ring_push(lw_net_event_ring_t *ring, const lw_net_event_t *event)
{
    uint32_t head = LW_AT_LOAD(&ring->head);
    uint32_t tail = LW_AT_LOAD(&ring->tail);
    if (head - tail >= (uint32_t)LW_NET_EVENT_QUEUE) {
        ring->dropped++;
        return false;
    }
    ring->slot[head % LW_NET_EVENT_QUEUE] = *event;
    LW_AT_STORE(&ring->head, head + 1u);
    return true;
}

static bool ring_pop(lw_net_event_ring_t *ring, lw_net_event_t *event)
{
    uint32_t tail = LW_AT_LOAD(&ring->tail);
    uint32_t head = LW_AT_LOAD(&ring->head);
    if (tail == head) {
        return false;
    }
    *event = ring->slot[tail % LW_NET_EVENT_QUEUE];
    LW_AT_STORE(&ring->tail, tail + 1u);
    return true;
}

bool lw_services_push_event(lw_services_t *services, const lw_net_event_t *event)
{
    if (services == NULL || event == NULL) {
        return false;
    }
    return ring_push(&services->events, event);
}

static lw_net_event_t make_event(lw_net_event_kind_t kind, uint32_t generation, uint32_t scan_generation)
{
    lw_net_event_t event;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.generation = generation;
    event.scan_generation = scan_generation;
    return event;
}

bool lw_services_wifi_report_scan(lw_services_t *services, uint32_t scan_generation, const lw_wifi_ap_t *aps,
                                  unsigned count, bool ok)
{
    lw_net_event_t event = make_event(LW_NET_EV_SCAN_DONE, 0u, scan_generation);
    event.ok = ok;
    if (ok && aps != NULL) {
        if (count > LW_WIFI_MAX_APS) {
            count = LW_WIFI_MAX_APS;
        }
        for (unsigned i = 0; i < count; i++) {
            event.aps[i] = aps[i];
        }
        event.ap_count = count;
    } else {
        event.ap_count = 0;
    }
    return lw_services_push_event(services, &event);
}

bool lw_services_wifi_report_connected(lw_services_t *services, uint32_t generation, const char *ip, uint32_t now_ms)
{
    (void)now_ms; /* the poll stamps the event with its own clock */
    lw_net_event_t event = make_event(LW_NET_EV_CONNECTED, generation, 0u);
    event.ok = true;
    copy_str(event.ip, sizeof(event.ip), ip);
    return lw_services_push_event(services, &event);
}

bool lw_services_wifi_report_failed(lw_services_t *services, uint32_t generation, lw_wifi_error_t error,
                                    uint32_t now_ms)
{
    (void)now_ms;
    lw_net_event_t event = make_event(LW_NET_EV_CONNECT_FAILED, generation, 0u);
    event.ok = false;
    event.error = error;
    return lw_services_push_event(services, &event);
}

bool lw_services_wifi_report_lost(lw_services_t *services, uint32_t generation, uint32_t now_ms)
{
    (void)now_ms;
    lw_net_event_t event = make_event(LW_NET_EV_LINK_LOST, generation, 0u);
    event.ok = false;
    event.error = LW_WIFI_ERR_LINK_LOST;
    return lw_services_push_event(services, &event);
}

bool lw_services_time_report_sync(lw_services_t *services, int64_t utc_seconds, bool ok, uint32_t now_ms)
{
    (void)now_ms;
    lw_net_event_t event = make_event(LW_NET_EV_TIME_SYNC, 0u, 0u);
    event.ok = ok;
    event.utc_seconds = utc_seconds;
    return lw_services_push_event(services, &event);
}

unsigned lw_services_drain(lw_services_t *services, uint32_t now_ms)
{
    unsigned applied = 0;
    lw_net_event_t event;
    while (ring_pop(&services->events, &event)) {
        switch (event.kind) {
        case LW_NET_EV_SCAN_DONE:
            lw_wifi_on_scan_done(&services->wifi, event.scan_generation, event.aps, event.ap_count, event.ok);
            break;
        case LW_NET_EV_CONNECTED:
            lw_wifi_on_connected(&services->wifi, event.generation, event.ip, now_ms);
            break;
        case LW_NET_EV_CONNECT_FAILED:
            /* A drop that arrives while the link is up is not a failed attempt:
             * the association was already established, so it is a link loss. */
            if (lw_wifi_state(&services->wifi) == LW_WIFI_ST_CONNECTED) {
                lw_wifi_on_disconnected(&services->wifi, event.generation, LW_WIFI_ERR_LINK_LOST, now_ms);
                lw_time_service_on_network_lost(&services->time, now_ms);
            } else {
                lw_wifi_on_connect_failed(&services->wifi, event.generation, event.error, now_ms);
            }
            break;
        case LW_NET_EV_LINK_LOST:
            lw_wifi_on_disconnected(&services->wifi, event.generation, event.error, now_ms);
            lw_time_service_on_network_lost(&services->time, now_ms);
            break;
        case LW_NET_EV_TIME_SYNC:
            if (event.ok) {
                lw_time_service_on_sync_ok(&services->time, event.utc_seconds, now_ms);
            } else {
                lw_time_service_on_sync_failed(&services->time, LW_TIME_ERR_NETWORK, now_ms);
            }
            break;
        default:
            break;
        }
        applied++;
    }
    return applied;
}

/* ------------------------------------------------------------------- actions */

typedef struct {
    lw_wifi_action_t action;
    char ssid[LW_WIFI_SSID_MAX];
    char password[LW_WIFI_SSID_MAX];
} pending_action_t;

/* Must be called with the service lock held. */
static pending_action_t take_action(lw_services_t *services)
{
    pending_action_t pending;
    memset(&pending, 0, sizeof(pending));
    const char *password = NULL;
    pending.action = lw_wifi_take_action(&services->wifi, pending.ssid, &password);
    if (pending.action == LW_WIFI_ACTION_CONNECT && password != NULL) {
        copy_str(pending.password, sizeof(pending.password), password);
    }
    return pending;
}

static pending_action_t take_action_empty(void)
{
    pending_action_t pending;
    memset(&pending, 0, sizeof(pending));
    pending.action = LW_WIFI_ACTION_NONE;
    return pending;
}
/* Runs the adapter call. Never called with the lock held: the ESP-IDF side may
 * block for tens of milliseconds. */
static void run_action(pending_action_t *pending)
{
    switch (pending->action) {
    case LW_WIFI_ACTION_SCAN:
        lw_net_esp_wifi_start_scan();
        break;
    case LW_WIFI_ACTION_CONNECT:
        lw_net_esp_wifi_start_connect(pending->ssid, pending->password[0] != '\0' ? pending->password : NULL);
        break;
    case LW_WIFI_ACTION_DISCONNECT:
        lw_net_esp_wifi_stop();
        break;
    case LW_WIFI_ACTION_NONE:
        break;
    }
    memset(pending->password, 0, sizeof(pending->password));
}

void lw_services_init(lw_services_t *services)
{
    memset(services, 0, sizeof(*services));
    services->caps.wifi_backend = lw_net_esp_wifi_available();
    services->caps.time_backend = lw_net_esp_time_available();
    services->caps.audio_backend = lw_net_esp_audio_available();
    lw_wifi_init(&services->wifi, services->caps.wifi_backend);
    lw_time_service_init(&services->time, LW_TIME_DEFAULT_TZ_MINUTES);
    lw_audio_init(&services->audio, services->caps.audio_backend);
}

void lw_services_set_audio_backend(lw_services_t *services, bool present)
{
    if (services == NULL) {
        return;
    }
    services->caps.audio_backend = present;
    lw_audio_set_sink(&services->audio, present);
}

void lw_services_start(lw_services_t *services, uint32_t now_ms)
{
    if (services->started) {
        return;
    }
    services->started = true;
    services->last_poll_ms = now_ms;
    s_now_ms = now_ms;
    if (services->caps.wifi_backend) {
        lw_net_esp_wifi_attach(services);
        /* Attaching the adapter is what actually proves the radio is usable:
         * a build can have CONFIG_LW_NET_WIFI_ENABLE=y and still fail here
         * (no C6 answering on SDIO, hosted slave missing, wrong pinout). Ask
         * the adapter again, so "compiled in" never gets reported as "ready". */
        services->caps.wifi_backend = lw_net_esp_wifi_available();
        lw_wifi_set_supported(&services->wifi, services->caps.wifi_backend);
        if (services->caps.wifi_backend) {
            lw_wifi_enable(&services->wifi, true, now_ms);
            /* A hosted slave can come up already associated with the network of
             * a previous boot. Recording that link now means the first poll
             * publishes a real SSID instead of leaving the UI on "未连接". */
            lw_net_esp_wifi_probe_connected();
        }
    }
    if (services->caps.time_backend) {
        lw_net_esp_time_attach(services);
    }
}

void lw_services_poll(lw_services_t *services, uint32_t now_ms)
{
    pending_action_t pending;
    bool want_time_request = false;

    s_now_ms = now_ms;

    LW_SERVICES_LOCK();
    services->last_poll_ms = now_ms;
    lw_services_drain(services, now_ms);
    lw_wifi_tick(&services->wifi, now_ms);
    pending = take_action(services);

    /* Once the link is up, ask for the wall clock; retry while it is missing. */
    bool connected = lw_wifi_state(&services->wifi) == LW_WIFI_ST_CONNECTED;
    if (connected && services->caps.time_backend) {
        lw_time_state_t state = lw_time_state(&services->time);
        if (state == LW_TIME_DEMO || state == LW_TIME_FAILED) {
            if (services->sync_retry_at_ms == 0 || now_ms >= services->sync_retry_at_ms) {
                if (lw_time_service_request_sync(&services->time, now_ms)) {
                    want_time_request = true;
                }
                services->sync_retry_at_ms = now_ms + TIME_SYNC_RETRY_MS;
            }
        }
    } else if (!connected) {
        services->sync_retry_at_ms = 0;
        lw_time_service_on_network_lost(&services->time, now_ms);
    }
    lw_time_service_tick(&services->time, now_ms);
    build_status(services, now_ms, &services->snapshot);
    services->snapshot_valid = true;
    LW_SERVICES_UNLOCK();

    run_action(&pending);
    if (want_time_request) {
        lw_net_esp_time_request();
    }
}

static void build_status(const lw_services_t *services, uint32_t now_ms, lw_net_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->wifi_backend = services->caps.wifi_backend;
    out->wifi_enabled = services->wifi.enabled;
    out->wifi_state = lw_wifi_state(&services->wifi);
    out->wifi_error = lw_wifi_error(&services->wifi);
    out->ap_count = lw_wifi_scan_count(&services->wifi);
    copy_str(out->ssid, sizeof(out->ssid), lw_wifi_target(&services->wifi));
    copy_str(out->ip, sizeof(out->ip), lw_wifi_ip(&services->wifi));
    if (out->ap_count > 0) {
        const lw_wifi_ap_t *first = lw_wifi_scan_at(&services->wifi, 0);
        out->rssi = first != NULL ? first->rssi : 0;
    }

    out->time_state = lw_time_state(&services->time);
    out->time_error = lw_time_error(&services->time);
    out->zone_minutes = lw_time_service_zone(&services->time);
    out->time_valid = lw_time_service_utc(&services->time, now_ms, NULL);
    lw_time_service_hm(&services->time, now_ms, out->clock);
    lw_time_service_date_label(&services->time, now_ms, out->date_label, sizeof(out->date_label));

    out->audio_backend = services->caps.audio_backend;
    out->audio_state = lw_audio_state(&services->audio);
    out->volume = lw_audio_volume(&services->audio);
    out->muted = lw_audio_muted(&services->audio);
}

void lw_services_status(const lw_services_t *services, uint32_t now_ms, lw_net_status_t *out)
{
    if (out == NULL) {
        return;
    }
    if (services->snapshot_valid) {
        *out = services->snapshot;
        return;
    }
    /* Before the first poll (boot ordering) nobody else touches the models. */
    build_status(services, now_ms, out);
}

/* ------------------------------------------------------------------ requests */

bool lw_services_wifi_enable(lw_services_t *services, bool enabled, uint32_t now_ms)
{
    LW_SERVICES_LOCK();
    bool ok = lw_wifi_enable(&services->wifi, enabled, now_ms);
    pending_action_t pending = ok ? take_action(services) : take_action_empty();
    LW_SERVICES_UNLOCK();
    if (ok) {
        run_action(&pending);
    }
    return ok;
}

bool lw_services_scan(lw_services_t *services, uint32_t now_ms)
{
    LW_SERVICES_LOCK();
    bool ok = lw_wifi_scan(&services->wifi, now_ms);
    pending_action_t pending = ok ? take_action(services) : take_action_empty();
    LW_SERVICES_UNLOCK();
    if (ok) {
        run_action(&pending);
    }
    return ok;
}

bool lw_services_connect(lw_services_t *services, const char *ssid, const char *password, uint32_t now_ms)
{
    LW_SERVICES_LOCK();
    bool ok = lw_wifi_connect(&services->wifi, ssid, password, now_ms);
    pending_action_t pending = ok ? take_action(services) : take_action_empty();
    LW_SERVICES_UNLOCK();
    if (ok) {
        run_action(&pending);
    }
    return ok;
}

bool lw_services_disconnect(lw_services_t *services, uint32_t now_ms)
{
    LW_SERVICES_LOCK();
    bool ok = lw_wifi_disconnect(&services->wifi, now_ms);
    pending_action_t pending = ok ? take_action(services) : take_action_empty();
    LW_SERVICES_UNLOCK();
    if (ok) {
        run_action(&pending);
    }
    return ok;
}

bool lw_services_request_time_sync(lw_services_t *services, uint32_t now_ms)
{
    if (!services->caps.time_backend) {
        /* No SNTP in this build: say so instead of queueing a sync that can
         * never complete. The UI shows the reason from the snapshot. */
        LW_SERVICES_LOCK();
        lw_time_service_on_sync_failed(&services->time, LW_TIME_ERR_NOT_SUPPORTED, now_ms);
        LW_SERVICES_UNLOCK();
        return false;
    }
    LW_SERVICES_LOCK();
    bool ok = lw_time_service_request_sync(&services->time, now_ms);
    LW_SERVICES_UNLOCK();
    if (ok) {
        lw_net_esp_time_request();
    }
    return ok;
}

void lw_services_set_zone(lw_services_t *services, int32_t zone_minutes)
{
    LW_SERVICES_LOCK();
    lw_time_service_set_zone(&services->time, zone_minutes);
    LW_SERVICES_UNLOCK();
}

uint32_t lw_services_wifi_generation(const lw_services_t *services)
{
    return services != NULL ? lw_wifi_generation(&services->wifi) : 0u;
}

uint32_t lw_services_scan_generation(const lw_services_t *services)
{
    return services != NULL ? lw_wifi_scan_generation(&services->wifi) : 0u;
}

uint32_t lw_services_now_ms(void)
{
    return s_now_ms;
}
