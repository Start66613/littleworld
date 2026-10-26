/* Wi-Fi service: host-testable state machine plus an ESP-IDF adapter.
 *
 * The model is plain C with no IDF dependency. It owns the connection state,
 * the bounded scan list and the reconnect policy, and it drops events that
 * belong to a previous attempt (generation check) so a late callback can never
 * overwrite a newer state.
 *
 * The model has no side effects: it records an action and the glue layer
 * performs it (start a scan, start a connection, disconnect). ESP event
 * callbacks only push results back through lw_wifi_on_*(), which is safe from
 * any task because it never touches LVGL.
 *
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LW_WIFI_SSID_MAX 33   /* 32 bytes + NUL */
#define LW_WIFI_PASS_MAX 65   /* 64 bytes + NUL */
#define LW_WIFI_MAX_APS 8
#define LW_WIFI_MAX_EVENTS 8
#define LW_WIFI_MAX_ATTEMPTS 3
#define LW_WIFI_SCAN_TIMEOUT_MS 10000u
#define LW_WIFI_CONNECT_TIMEOUT_MS 15000u
#define LW_WIFI_BACKOFF_MS 2000u
/* Bounded automatic reconnects after a link that was already up drops. */
#define LW_WIFI_MAX_RECONNECTS 3

typedef enum {
    LW_WIFI_ST_UNSUPPORTED = 0, /* no radio backend compiled or the C6 link is absent */
    LW_WIFI_ST_OFF,
    LW_WIFI_ST_IDLE,
    LW_WIFI_ST_SCANNING,
    LW_WIFI_ST_CONNECTING,
    LW_WIFI_ST_CONNECTED,
    LW_WIFI_ST_FAILED
} lw_wifi_state_t;

typedef enum {
    LW_WIFI_ERR_NONE = 0,
    LW_WIFI_ERR_NOT_SUPPORTED,
    LW_WIFI_ERR_INVALID_ARGS,
    LW_WIFI_ERR_BUSY,
    LW_WIFI_ERR_SCAN_FAILED,
    LW_WIFI_ERR_NO_AP,
    LW_WIFI_ERR_AUTH_FAILED,
    LW_WIFI_ERR_TIMEOUT,
    LW_WIFI_ERR_DHCP_FAILED,
    LW_WIFI_ERR_LINK_LOST,
    LW_WIFI_ERR_RETRIES_EXHAUSTED,
    LW_WIFI_ERR_CANCELLED,
    LW_WIFI_ERR_INTERNAL
} lw_wifi_error_t;

typedef struct {
    char ssid[LW_WIFI_SSID_MAX];
    int8_t rssi;
    bool secured;
} lw_wifi_ap_t;

typedef enum {
    LW_WIFI_EV_SCAN_DONE = 1,
    LW_WIFI_EV_CONNECTED,
    LW_WIFI_EV_CONNECT_FAILED,
    LW_WIFI_EV_DISCONNECTED
} lw_wifi_event_type_t;

typedef struct {
    lw_wifi_event_type_t type;
    uint32_t generation;
    lw_wifi_error_t error;
    char ssid[LW_WIFI_SSID_MAX];
    char ip[16];
    unsigned ap_count;
} lw_wifi_event_t;

typedef enum {
    LW_WIFI_ACTION_NONE = 0,
    LW_WIFI_ACTION_SCAN,
    LW_WIFI_ACTION_CONNECT,
    LW_WIFI_ACTION_DISCONNECT
} lw_wifi_action_t;

typedef struct {
    lw_wifi_state_t state;
    lw_wifi_error_t error;
    bool enabled;          /* the radio should be on */
    bool supported;        /* a backend exists */
    bool credentials_held; /* a password is currently in RAM for this attempt */
    uint32_t generation;   /* bumped on every connect attempt or cancellation */
    uint32_t scan_generation;
    unsigned attempts;     /* reconnect attempts used for the current target */
    unsigned reconnects;   /* automatic link-loss reconnects used for this target */
    uint32_t started_ms;   /* start of the current SCANNING/CONNECTING phase */
    uint32_t retry_at_ms;  /* non-zero while waiting for the reconnect backoff */
    char target[LW_WIFI_SSID_MAX];
    char ip[16];
    char password[LW_WIFI_PASS_MAX]; /* RAM only, zeroized when the link ends */
    lw_wifi_ap_t aps[LW_WIFI_MAX_APS];
    unsigned ap_count;
    lw_wifi_action_t action;
    lw_wifi_event_t events[LW_WIFI_MAX_EVENTS];
    unsigned event_head, event_count;
    unsigned dropped_events;
} lw_wifi_t;

void lw_wifi_init(lw_wifi_t *wifi, bool supported);
void lw_wifi_set_supported(lw_wifi_t *wifi, bool supported);
bool lw_wifi_supported(const lw_wifi_t *wifi);
lw_wifi_state_t lw_wifi_state(const lw_wifi_t *wifi);
/* Captured by the adapter when a request starts; feed it back with the result. */
uint32_t lw_wifi_generation(const lw_wifi_t *wifi);
uint32_t lw_wifi_scan_generation(const lw_wifi_t *wifi);
lw_wifi_error_t lw_wifi_error(const lw_wifi_t *wifi);
const char *lw_wifi_error_name(lw_wifi_error_t error);
const char *lw_wifi_state_name(lw_wifi_state_t state);
unsigned lw_wifi_scan_count(const lw_wifi_t *wifi);
const lw_wifi_ap_t *lw_wifi_scan_at(const lw_wifi_t *wifi, unsigned index);
const char *lw_wifi_ip(const lw_wifi_t *wifi);
const char *lw_wifi_target(const lw_wifi_t *wifi);
bool lw_wifi_credentials_held(const lw_wifi_t *wifi);

/* Requests return false when refused; lw_wifi_error() then explains why. */
bool lw_wifi_enable(lw_wifi_t *wifi, bool enabled, uint32_t now_ms);
bool lw_wifi_scan(lw_wifi_t *wifi, uint32_t now_ms);
bool lw_wifi_connect(lw_wifi_t *wifi, const char *ssid, const char *password, uint32_t now_ms);
bool lw_wifi_disconnect(lw_wifi_t *wifi, uint32_t now_ms);
/* Drop credentials without touching the radio (used after a failed attempt). */
void lw_wifi_forget_password(lw_wifi_t *wifi);

/* Results reported by the adapter. `generation` must be the one captured when
 * the request was issued; stale generations are ignored. */
void lw_wifi_on_scan_done(lw_wifi_t *wifi, uint32_t generation, const lw_wifi_ap_t *aps, unsigned count,
                          bool ok);
void lw_wifi_on_connected(lw_wifi_t *wifi, uint32_t generation, const char *ip, uint32_t now_ms);
/* An unsolicited association: the radio came up already joined because the
 * hosted slave kept the credential of a previous boot. Accepted from IDLE (the
 * state right after enabling the radio) and from CONNECTED (a repeat probe).
 * `ssid` may be empty; it only feeds the label the UI shows. Returns true when
 * the state actually changed. */
bool lw_wifi_on_link_up(lw_wifi_t *wifi, const char *ssid, const char *ip, uint32_t now_ms);
void lw_wifi_on_connect_failed(lw_wifi_t *wifi, uint32_t generation, lw_wifi_error_t error, uint32_t now_ms);
void lw_wifi_on_disconnected(lw_wifi_t *wifi, uint32_t generation, lw_wifi_error_t error, uint32_t now_ms);

/* Time driven: scan/connect timeouts and the reconnect backoff. */
void lw_wifi_tick(lw_wifi_t *wifi, uint32_t now_ms);

/* Side effect requested by the model. The caller performs it and the model
 * keeps the credentials in RAM only while the action is pending. */
lw_wifi_action_t lw_wifi_take_action(lw_wifi_t *wifi, char ssid[LW_WIFI_SSID_MAX], const char **password);

/* Queue access for the UI thread. */
bool lw_wifi_next_event(lw_wifi_t *wifi, lw_wifi_event_t *out);
unsigned lw_wifi_dropped_events(const lw_wifi_t *wifi);
