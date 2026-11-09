/* Shared system state, separate from the legacy save. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LW_SYS_PREF_BYTES 24
typedef enum { LW_WIFI_OFF,LW_WIFI_IDLE,LW_WIFI_SCANNING,LW_WIFI_CONNECTING,LW_WIFI_CONNECTED,LW_WIFI_FAILED,LW_WIFI_UNSUPPORTED } lw_wifi_status_t;
typedef struct { char ssid[33]; int8_t rssi; bool secured; } lw_network_t;
typedef struct { uint8_t brightness,volume,theme,wallpaper; bool muted,dnd,widgets; } lw_preferences_t;
typedef struct {
    void *context;
    bool simulated,network_available;
    bool (*brightness)(void *,unsigned);
    bool (*volume)(void *,unsigned,bool);
    bool (*wifi_scan)(void *);
    bool (*wifi_connect)(void *,const char *,const char *);
    bool (*wifi_disconnect)(void *,bool);
    bool (*load)(void *,uint8_t [LW_SYS_PREF_BYTES]);
    bool (*store)(void *,const uint8_t [LW_SYS_PREF_BYTES]);
} lw_system_backend_t;
typedef struct {
    lw_preferences_t prefs;
    lw_system_backend_t backend;
    lw_network_t networks[8]; unsigned network_count;
    lw_wifi_status_t wifi;
    char ssid[33];
    bool wifi_enabled,locked,screen_off,playing,dirty,store_failed;
    uint32_t revision,network_ms;
    bool mock_reject;
} lw_system_t;
void lw_system_init(lw_system_t *,const lw_system_backend_t *);
bool lw_system_brightness(lw_system_t *,int);
bool lw_system_volume(lw_system_t *,int);
bool lw_system_mute(lw_system_t *,bool);
void lw_system_theme(lw_system_t *,unsigned,unsigned);
void lw_system_dnd(lw_system_t *,bool);
void lw_system_widgets(lw_system_t *,bool);
bool lw_system_save(lw_system_t *);
void lw_system_tick(lw_system_t *,uint32_t);
void lw_system_wifi_enable(lw_system_t *,bool);
bool lw_system_wifi_scan(lw_system_t *);
bool lw_system_wifi_connect(lw_system_t *,unsigned,const char *);
void lw_system_wifi_disconnect(lw_system_t *,bool);
/* Real adapters deliver asynchronous results on the UI thread. Never send credentials to logs. */
void lw_system_scan_result(lw_system_t *,const lw_network_t *,unsigned);
void lw_system_connection_result(lw_system_t *,bool);
/* Link changes the user did not initiate here: the radio dropped a link
 * that was up (link_lost) or finished associating on its own - the C6
 * kept a link across a P4 reboot, or an automatic reconnect succeeded
 * (link_up). Without these, the facade could only ever reflect actions
 * taken through its own scan/connect calls. */
void lw_system_link_lost(lw_system_t *);
void lw_system_link_up(lw_system_t *,const char *ssid);
/* Whether a radio backend exists at all. The hardware layer never
 * claims one (the P4 has no radio; Wi-Fi arrives via the C6), and the
 * connectivity services only know after they have started - which is
 * after this facade is created. Whoever owns both (the boot bridge)
 * pushes the answer in when it changes; until then the facade stays
 * honestly UNSUPPORTED and refuses scan/connect. */
void lw_system_set_network_available(lw_system_t *,bool);
void lw_system_encode(const lw_preferences_t *,uint8_t [LW_SYS_PREF_BYTES]);
bool lw_system_decode(lw_preferences_t *,const uint8_t [LW_SYS_PREF_BYTES]);
#ifdef ESP_PLATFORM
/* Caller owns an already prepared ES8311 codec. Does not initialize hardware or C6. */
lw_system_backend_t lw_system_bsp_backend(void *speaker_codec);
#endif
