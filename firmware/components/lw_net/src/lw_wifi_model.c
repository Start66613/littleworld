/* Wi-Fi connection state machine. Pure C, no IDF and no LVGL. SPDX-License-Identifier: MIT */
#include "lw_wifi.h"

#include <string.h>

/* ------------------------------------------------------------- helpers --- */

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void wipe_password(lw_wifi_t *wifi)
{
    memset(wifi->password, 0, sizeof(wifi->password));
    wifi->credentials_held = false;
}

static void push_event(lw_wifi_t *wifi, const lw_wifi_event_t *event)
{
    if (wifi->event_count == LW_WIFI_MAX_EVENTS) {
        /* Drop the oldest so the newest state always reaches the UI. */
        wifi->event_head = (wifi->event_head + 1u) % LW_WIFI_MAX_EVENTS;
        wifi->event_count--;
        wifi->dropped_events++;
    }
    unsigned slot = (wifi->event_head + wifi->event_count) % LW_WIFI_MAX_EVENTS;
    wifi->events[slot] = *event;
    wifi->event_count++;
}

static void emit_simple(lw_wifi_t *wifi, lw_wifi_event_type_t type, lw_wifi_error_t error)
{
    lw_wifi_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.generation = wifi->generation;
    event.error = error;
    copy_str(event.ssid, sizeof(event.ssid), wifi->target);
    copy_str(event.ip, sizeof(event.ip), wifi->ip);
    event.ap_count = wifi->ap_count;
    push_event(wifi, &event);
}

static void set_failed(lw_wifi_t *wifi, lw_wifi_error_t error)
{
    wifi->state = LW_WIFI_ST_FAILED;
    wifi->error = error;
    wifi->retry_at_ms = 0;
    wifi->action = LW_WIFI_ACTION_NONE; /* the attempt is over, nothing to run */
    emit_simple(wifi, LW_WIFI_EV_CONNECT_FAILED, error);
}

/* Insert keeping the list sorted by RSSI (strongest first), capped at
 * LW_WIFI_MAX_APS. Duplicate SSIDs keep the strongest entry. */
static void add_ap(lw_wifi_t *wifi, const lw_wifi_ap_t *ap)
{
    for (unsigned i = 0; i < wifi->ap_count; i++) {
        if (strcmp(wifi->aps[i].ssid, ap->ssid) == 0) {
            if (ap->rssi > wifi->aps[i].rssi) {
                wifi->aps[i] = *ap;
            }
            return;
        }
    }
    unsigned limit = wifi->ap_count < LW_WIFI_MAX_APS ? wifi->ap_count : LW_WIFI_MAX_APS - 1u;
    unsigned pos = 0;
    while (pos < limit && wifi->aps[pos].rssi >= ap->rssi) {
        pos++;
    }
    if (wifi->ap_count < LW_WIFI_MAX_APS) {
        wifi->ap_count++;
        limit = wifi->ap_count - 1u;
    }
    for (unsigned i = limit; i > pos; i--) {
        wifi->aps[i] = wifi->aps[i - 1u];
    }
    wifi->aps[pos] = *ap;
}

/* ---------------------------------------------------------------- API --- */

void lw_wifi_init(lw_wifi_t *wifi, bool supported)
{
    memset(wifi, 0, sizeof(*wifi));
    wifi->supported = supported;
    wifi->state = supported ? LW_WIFI_ST_OFF : LW_WIFI_ST_UNSUPPORTED;
    wifi->error = supported ? LW_WIFI_ERR_NONE : LW_WIFI_ERR_NOT_SUPPORTED;
    wifi->generation = 1u;
    wifi->scan_generation = 1u;
}

void lw_wifi_set_supported(lw_wifi_t *wifi, bool supported)
{
    if (wifi->supported == supported) {
        return;
    }
    wifi->supported = supported;
    if (!supported) {
        wifi->enabled = false;
        wifi->state = LW_WIFI_ST_UNSUPPORTED;
        wifi->error = LW_WIFI_ERR_NOT_SUPPORTED;
        wifi->generation++;
        wifi->action = LW_WIFI_ACTION_NONE;
        wifi->ap_count = 0;
        wifi->ip[0] = '\0';
        wipe_password(wifi);
    } else if (wifi->state == LW_WIFI_ST_UNSUPPORTED) {
        wifi->state = LW_WIFI_ST_OFF;
        wifi->error = LW_WIFI_ERR_NONE;
    }
}

bool lw_wifi_supported(const lw_wifi_t *wifi)
{
    return wifi->supported;
}

lw_wifi_state_t lw_wifi_state(const lw_wifi_t *wifi)
{
    return wifi->state;
}

lw_wifi_error_t lw_wifi_error(const lw_wifi_t *wifi)
{
    return wifi->error;
}

const char *lw_wifi_state_name(lw_wifi_state_t state)
{
    switch (state) {
    case LW_WIFI_ST_UNSUPPORTED:
        return "unsupported";
    case LW_WIFI_ST_OFF:
        return "off";
    case LW_WIFI_ST_IDLE:
        return "idle";
    case LW_WIFI_ST_SCANNING:
        return "scanning";
    case LW_WIFI_ST_CONNECTING:
        return "connecting";
    case LW_WIFI_ST_CONNECTED:
        return "connected";
    case LW_WIFI_ST_FAILED:
        return "failed";
    }
    return "?";
}

const char *lw_wifi_error_name(lw_wifi_error_t error)
{
    switch (error) {
    case LW_WIFI_ERR_NONE:
        return "none";
    case LW_WIFI_ERR_NOT_SUPPORTED:
        return "not-supported";
    case LW_WIFI_ERR_INVALID_ARGS:
        return "invalid-arguments";
    case LW_WIFI_ERR_BUSY:
        return "busy";
    case LW_WIFI_ERR_SCAN_FAILED:
        return "scan-failed";
    case LW_WIFI_ERR_NO_AP:
        return "no-access-point";
    case LW_WIFI_ERR_AUTH_FAILED:
        return "authentication-failed";
    case LW_WIFI_ERR_TIMEOUT:
        return "timeout";
    case LW_WIFI_ERR_DHCP_FAILED:
        return "dhcp-failed";
    case LW_WIFI_ERR_LINK_LOST:
        return "link-lost";
    case LW_WIFI_ERR_RETRIES_EXHAUSTED:
        return "retries-exhausted";
    case LW_WIFI_ERR_CANCELLED:
        return "cancelled";
    case LW_WIFI_ERR_INTERNAL:
        return "internal-error";
    }
    return "?";
}

unsigned lw_wifi_scan_count(const lw_wifi_t *wifi)
{
    return wifi->ap_count;
}

const lw_wifi_ap_t *lw_wifi_scan_at(const lw_wifi_t *wifi, unsigned index)
{
    if (index >= wifi->ap_count) {
        return NULL;
    }
    return &wifi->aps[index];
}

const char *lw_wifi_ip(const lw_wifi_t *wifi)
{
    return wifi->ip;
}

const char *lw_wifi_target(const lw_wifi_t *wifi)
{
    return wifi->target;
}

bool lw_wifi_credentials_held(const lw_wifi_t *wifi)
{
    return wifi->credentials_held;
}

bool lw_wifi_enable(lw_wifi_t *wifi, bool enabled, uint32_t now_ms)
{
    (void)now_ms;
    if (!wifi->supported) {
        wifi->error = LW_WIFI_ERR_NOT_SUPPORTED;
        return false;
    }
    if (enabled == wifi->enabled) {
        return true;
    }
    wifi->enabled = enabled;
    wifi->generation++;
    wifi->retry_at_ms = 0;
    wifi->attempts = 0;
    wifi->reconnects = 0;
    if (!enabled) {
        wifi->state = LW_WIFI_ST_OFF;
        wifi->error = LW_WIFI_ERR_NONE;
        wifi->ip[0] = '\0';
        wifi->action = LW_WIFI_ACTION_DISCONNECT;
        wipe_password(wifi);
        return true;
    }
    wifi->state = LW_WIFI_ST_IDLE;
    wifi->error = LW_WIFI_ERR_NONE;
    return true;
}

bool lw_wifi_scan(lw_wifi_t *wifi, uint32_t now_ms)
{
    if (!wifi->supported) {
        wifi->error = LW_WIFI_ERR_NOT_SUPPORTED;
        return false;
    }
    if (!wifi->enabled) {
        wifi->error = LW_WIFI_ERR_BUSY;
        return false;
    }
    if (wifi->state == LW_WIFI_ST_SCANNING || wifi->state == LW_WIFI_ST_CONNECTING) {
        wifi->error = LW_WIFI_ERR_BUSY;
        return false;
    }
    wifi->scan_generation++;
    wifi->state = LW_WIFI_ST_SCANNING;
    wifi->error = LW_WIFI_ERR_NONE;
    wifi->started_ms = now_ms;
    wifi->action = LW_WIFI_ACTION_SCAN;
    return true;
}

bool lw_wifi_connect(lw_wifi_t *wifi, const char *ssid, const char *password, uint32_t now_ms)
{
    if (!wifi->supported) {
        wifi->error = LW_WIFI_ERR_NOT_SUPPORTED;
        return false;
    }
    if (!wifi->enabled) {
        wifi->error = LW_WIFI_ERR_BUSY;
        return false;
    }
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) >= LW_WIFI_SSID_MAX) {
        wifi->error = LW_WIFI_ERR_INVALID_ARGS;
        return false;
    }
    if (password != NULL && strlen(password) >= LW_WIFI_PASS_MAX) {
        wifi->error = LW_WIFI_ERR_INVALID_ARGS;
        return false;
    }
    if (wifi->state == LW_WIFI_ST_SCANNING) {
        wifi->error = LW_WIFI_ERR_BUSY;
        return false;
    }
    /* A new request always invalidates whatever was in flight. */
    wifi->generation++;
    wifi->attempts = 0;
    wifi->reconnects = 0;
    wifi->retry_at_ms = 0;
    copy_str(wifi->target, sizeof(wifi->target), ssid);
    if (password != NULL && password[0] != '\0') {
        copy_str(wifi->password, sizeof(wifi->password), password);
        wifi->credentials_held = true;
    } else {
        wipe_password(wifi);
    }
    wifi->state = LW_WIFI_ST_CONNECTING;
    wifi->error = LW_WIFI_ERR_NONE;
    wifi->started_ms = now_ms;
    wifi->ip[0] = '\0';
    wifi->action = LW_WIFI_ACTION_CONNECT;
    return true;
}

bool lw_wifi_disconnect(lw_wifi_t *wifi, uint32_t now_ms)
{
    (void)now_ms;
    if (!wifi->supported) {
        wifi->error = LW_WIFI_ERR_NOT_SUPPORTED;
        return false;
    }
    bool was_active = wifi->state == LW_WIFI_ST_CONNECTED || wifi->state == LW_WIFI_ST_CONNECTING ||
                      wifi->state == LW_WIFI_ST_SCANNING || wifi->state == LW_WIFI_ST_FAILED;
    wifi->generation++;
    wifi->retry_at_ms = 0;
    wifi->attempts = 0;
    wifi->reconnects = 0;
    wifi->ip[0] = '\0';
    wifi->error = was_active ? LW_WIFI_ERR_CANCELLED : LW_WIFI_ERR_NONE;
    wifi->state = wifi->enabled ? LW_WIFI_ST_IDLE : LW_WIFI_ST_OFF;
    wifi->action = LW_WIFI_ACTION_DISCONNECT;
    wipe_password(wifi);
    return true;
}

void lw_wifi_forget_password(lw_wifi_t *wifi)
{
    wipe_password(wifi);
}

void lw_wifi_on_scan_done(lw_wifi_t *wifi, uint32_t generation, const lw_wifi_ap_t *aps, unsigned count,
                          bool ok)
{
    if (generation != wifi->scan_generation || wifi->state != LW_WIFI_ST_SCANNING) {
        return; /* stale or cancelled scan */
    }
    wifi->ap_count = 0;
    if (ok && aps != NULL) {
        for (unsigned i = 0; i < count; i++) {
            if (aps[i].ssid[0] == '\0') {
                continue;
            }
            add_ap(wifi, &aps[i]);
        }
    }
    wifi->state = wifi->enabled ? LW_WIFI_ST_IDLE : LW_WIFI_ST_OFF;
    wifi->error = ok ? LW_WIFI_ERR_NONE : LW_WIFI_ERR_SCAN_FAILED;
    {
        lw_wifi_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = LW_WIFI_EV_SCAN_DONE;
        event.generation = generation;
        event.error = wifi->error;
        event.ap_count = wifi->ap_count;
        push_event(wifi, &event);
    }
}

void lw_wifi_on_connected(lw_wifi_t *wifi, uint32_t generation, const char *ip, uint32_t now_ms)
{
    (void)now_ms;
    if (generation != wifi->generation || wifi->state != LW_WIFI_ST_CONNECTING) {
        return;
    }
    wifi->state = LW_WIFI_ST_CONNECTED;
    wifi->error = LW_WIFI_ERR_NONE;
    wifi->attempts = 0;
    wifi->reconnects = 0;
    wifi->retry_at_ms = 0;
    wifi->action = LW_WIFI_ACTION_NONE;
    copy_str(wifi->ip, sizeof(wifi->ip), ip);
    emit_simple(wifi, LW_WIFI_EV_CONNECTED, LW_WIFI_ERR_NONE);
}

void lw_wifi_on_connect_failed(lw_wifi_t *wifi, uint32_t generation, lw_wifi_error_t error, uint32_t now_ms)
{
    if (generation != wifi->generation || wifi->state != LW_WIFI_ST_CONNECTING) {
        return;
    }
    /* The attempt that produced this result is over: clear the pending action so
     * the adapter is not asked to connect again until the retry policy decides. */
    wifi->action = LW_WIFI_ACTION_NONE;
    /* Authentication failures will not fix themselves by retrying. */
    bool retryable = error != LW_WIFI_ERR_AUTH_FAILED && error != LW_WIFI_ERR_NO_AP &&
                     error != LW_WIFI_ERR_INVALID_ARGS;
    if (retryable && wifi->attempts + 1u < LW_WIFI_MAX_ATTEMPTS) {
        wifi->attempts++;
        wifi->retry_at_ms = now_ms + LW_WIFI_BACKOFF_MS * wifi->attempts;
        wifi->error = error;
        wifi->state = LW_WIFI_ST_CONNECTING;
        return;
    }
    if (retryable && wifi->attempts + 1u >= LW_WIFI_MAX_ATTEMPTS) {
        error = LW_WIFI_ERR_RETRIES_EXHAUSTED;
    }
    wifi->attempts++;
    set_failed(wifi, error);
    /* A failed attempt must not leave the password in RAM. */
    if (!wifi->enabled) {
        wipe_password(wifi);
    }
}

void lw_wifi_on_disconnected(lw_wifi_t *wifi, uint32_t generation, lw_wifi_error_t error, uint32_t now_ms)
{
    (void)now_ms;
    if (generation != wifi->generation) {
        return;
    }
    if (wifi->state != LW_WIFI_ST_CONNECTED && wifi->state != LW_WIFI_ST_CONNECTING) {
        return;
    }
    wifi->ip[0] = '\0';
    if (!wifi->enabled) {
        wifi->state = LW_WIFI_ST_OFF;
        wifi->error = LW_WIFI_ERR_NONE;
        wipe_password(wifi);
        return;
    }
    /* The link dropped after being up. The reconnect policy is bounded: while
     * the radio is on, credentials are still in RAM and the budget is not spent,
     * schedule one more attempt; otherwise stop and report it. */
    wifi->state = LW_WIFI_ST_IDLE;
    wifi->error = error == LW_WIFI_ERR_NONE ? LW_WIFI_ERR_LINK_LOST : error;
    emit_simple(wifi, LW_WIFI_EV_DISCONNECTED, wifi->error);
    if (!wifi->enabled || !wifi->credentials_held) {
        return;
    }
    if (wifi->reconnects >= LW_WIFI_MAX_RECONNECTS) {
        wifi->error = LW_WIFI_ERR_RETRIES_EXHAUSTED;
        set_failed(wifi, LW_WIFI_ERR_RETRIES_EXHAUSTED);
        wipe_password(wifi);
        return;
    }
    wifi->reconnects++;
    wifi->retry_at_ms = now_ms + LW_WIFI_BACKOFF_MS * wifi->reconnects;
}

void lw_wifi_tick(lw_wifi_t *wifi, uint32_t now_ms)
{
    if (wifi->state == LW_WIFI_ST_SCANNING && now_ms - wifi->started_ms >= LW_WIFI_SCAN_TIMEOUT_MS) {
        lw_wifi_on_scan_done(wifi, wifi->scan_generation, NULL, 0, false);
        return;
    }
    /* A scheduled reconnect after a link loss starts from IDLE. */
    if (wifi->state == LW_WIFI_ST_IDLE && wifi->retry_at_ms != 0 && now_ms >= wifi->retry_at_ms) {
        wifi->retry_at_ms = 0;
        wifi->generation++;
        wifi->state = LW_WIFI_ST_CONNECTING;
        wifi->error = LW_WIFI_ERR_NONE;
        wifi->started_ms = now_ms;
        wifi->action = LW_WIFI_ACTION_CONNECT;
        return;
    }
    if (wifi->state == LW_WIFI_ST_CONNECTING) {
        if (wifi->retry_at_ms != 0 && now_ms >= wifi->retry_at_ms) {
            wifi->retry_at_ms = 0;
            wifi->started_ms = now_ms;
            wifi->generation++;
            wifi->action = LW_WIFI_ACTION_CONNECT;
            return;
        }
        if (wifi->retry_at_ms == 0 && now_ms - wifi->started_ms >= LW_WIFI_CONNECT_TIMEOUT_MS) {
            lw_wifi_on_connect_failed(wifi, wifi->generation, LW_WIFI_ERR_TIMEOUT, now_ms);
        }
    }
}

lw_wifi_action_t lw_wifi_take_action(lw_wifi_t *wifi, char ssid[LW_WIFI_SSID_MAX], const char **password)
{
    lw_wifi_action_t action = wifi->action;
    wifi->action = LW_WIFI_ACTION_NONE;
    if (ssid != NULL) {
        copy_str(ssid, LW_WIFI_SSID_MAX, wifi->target);
    }
    if (password != NULL) {
        *password = wifi->credentials_held ? wifi->password : NULL;
    }
    return action;
}

bool lw_wifi_next_event(lw_wifi_t *wifi, lw_wifi_event_t *out)
{
    if (wifi->event_count == 0) {
        return false;
    }
    if (out != NULL) {
        *out = wifi->events[wifi->event_head];
    }
    wifi->event_head = (wifi->event_head + 1u) % LW_WIFI_MAX_EVENTS;
    wifi->event_count--;
    return true;
}

unsigned lw_wifi_dropped_events(const lw_wifi_t *wifi)
{
    return wifi->dropped_events;
}

uint32_t lw_wifi_generation(const lw_wifi_t *wifi)
{
    return wifi != NULL ? wifi->generation : 0u;
}

uint32_t lw_wifi_scan_generation(const lw_wifi_t *wifi)
{
    return wifi != NULL ? wifi->scan_generation : 0u;
}
