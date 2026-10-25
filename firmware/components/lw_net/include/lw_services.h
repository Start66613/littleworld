/* Little World - connectivity service glue.
 *
 * Owns the Wi-Fi, time and audio models, performs the side effects they ask
 * for through the adapter in lw_net_esp.c, and exposes one snapshot for the UI.
 * No LVGL here: the boot code calls lw_services_poll() from a timer.
 *
 * Concurrency contract
 * --------------------
 * The models are mutated by exactly one consumer: lw_services_poll(). Anything
 * that runs in another context (ESP-IDF event handlers, the IP event, any
 * future hosted callback) must describe what happened with a lw_net_event_t
 * and hand it over through lw_services_push_event(). The lock-free ring keeps
 * the producer free of the service state, so an event task can never observe a
 * half-applied scan or a request that is being retried.
 *
 * Requests coming from the UI change the models directly; they take the
 * service lock (a critical section on the device, a no-op on the host tests)
 * so they cannot interleave with a poll that is applying an event.
 *
 * SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lw_audio.h"
#include "lw_time_svc.h"
#include "lw_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wifi_backend;   /* an adapter is present */
    bool wifi_enabled;   /* radio on */
    bool time_backend;   /* SNTP available */
    bool audio_backend;  /* codec path available */
} lw_services_caps_t;

typedef struct {
    lw_wifi_state_t wifi_state;
    lw_wifi_error_t wifi_error;
    bool wifi_enabled;
    bool wifi_backend;
    char ssid[LW_WIFI_SSID_MAX];
    char ip[16];
    int8_t rssi;
    unsigned ap_count;

    lw_time_state_t time_state;
    lw_time_error_t time_error;
    bool time_valid;
    int32_t zone_minutes;
    char clock[6];       /* "19:30" or "--:--" */
    char date_label[32]; /* "10月25日 周日" or "" */

    bool audio_backend;
    lw_audio_state_t audio_state;
    unsigned volume;
    bool muted;
} lw_net_status_t;

/* Events produced outside the service task. */
typedef enum {
    LW_NET_EV_SCAN_DONE,
    LW_NET_EV_CONNECTED,
    LW_NET_EV_CONNECT_FAILED,
    LW_NET_EV_LINK_LOST,
    LW_NET_EV_TIME_SYNC,
} lw_net_event_kind_t;

#define LW_NET_EVENT_QUEUE 8

typedef struct {
    lw_net_event_kind_t kind;
    uint32_t generation;      /* Wi-Fi generation captured when the request started */
    uint32_t scan_generation; /* scan generation captured when the scan started */
    bool ok;
    lw_wifi_error_t error;
    char ip[16];
    int64_t utc_seconds;
    unsigned ap_count;
    lw_wifi_ap_t aps[LW_WIFI_MAX_APS];
} lw_net_event_t;

typedef struct {
    lw_net_event_t slot[LW_NET_EVENT_QUEUE];
    uint32_t head; /* producer writes, consumer reads */
    uint32_t tail; /* consumer writes, producer reads */
    uint32_t dropped;
} lw_net_event_ring_t;

typedef struct lw_services {
    lw_wifi_t wifi;
    lw_time_service_t time;
    lw_audio_t audio;
    lw_services_caps_t caps;
    bool started;
    uint32_t last_poll_ms;
    uint32_t sync_retry_at_ms;
    lw_net_event_ring_t events;
    /* Written only by lw_services_poll(), copied by the UI thread, so a screen
     * refresh can never observe a half-applied event. */
    lw_net_status_t snapshot;
    bool snapshot_valid;
} lw_services_t;

/** Reads the adapters' capabilities and initialises the models. */
void lw_services_init(lw_services_t *services);
/** Overrides the audio capability. The codec is owned by the board support
 *  package, not by this component, so the boot code reports what it probed. */
void lw_services_set_audio_backend(lw_services_t *services, bool present);
/** Starts the adapters that are available. Safe to call once. */
void lw_services_start(lw_services_t *services, uint32_t now_ms);
/** The only place that mutates the models. Drains adapter events, then ticks. */
void lw_services_poll(lw_services_t *services, uint32_t now_ms);
void lw_services_status(const lw_services_t *services, uint32_t now_ms, lw_net_status_t *out);

/** Requests. They return false when the layer is unavailable; the snapshot
 *  carries the reason through wifi_error / time_error. */
bool lw_services_wifi_enable(lw_services_t *services, bool enabled, uint32_t now_ms);
bool lw_services_scan(lw_services_t *services, uint32_t now_ms);
bool lw_services_connect(lw_services_t *services, const char *ssid, const char *password, uint32_t now_ms);
bool lw_services_disconnect(lw_services_t *services, uint32_t now_ms);
bool lw_services_request_time_sync(lw_services_t *services, uint32_t now_ms);
void lw_services_set_zone(lw_services_t *services, int32_t zone_minutes);

/** Generation the adapter must attach to the result of the current request. */
uint32_t lw_services_wifi_generation(const lw_services_t *services);
uint32_t lw_services_scan_generation(const lw_services_t *services);
/** Wall clock the poll loop uses; the adapter stamps its events with it so the
 *  timeouts in the model and the poll share one epoch. */
uint32_t lw_services_now_ms(void);

/** Adapter callbacks. They only enqueue; the poll applies them. Safe from any
 *  task, never touches LVGL. They return false when the queue is full. */
bool lw_services_push_event(lw_services_t *services, const lw_net_event_t *event);
bool lw_services_wifi_report_scan(lw_services_t *services, uint32_t scan_generation, const lw_wifi_ap_t *aps,
                                  unsigned count, bool ok);
bool lw_services_wifi_report_connected(lw_services_t *services, uint32_t generation, const char *ip, uint32_t now_ms);
bool lw_services_wifi_report_failed(lw_services_t *services, uint32_t generation, lw_wifi_error_t error,
                                    uint32_t now_ms);
bool lw_services_wifi_report_lost(lw_services_t *services, uint32_t generation, uint32_t now_ms);
bool lw_services_time_report_sync(lw_services_t *services, int64_t utc_seconds, bool ok, uint32_t now_ms);
/** Applies the queued events; called by lw_services_poll(), exported for tests. */
unsigned lw_services_drain(lw_services_t *services, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
