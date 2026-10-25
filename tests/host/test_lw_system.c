/* Service failure, persistence and asynchronous network contracts. SPDX-License-Identifier: MIT */
#include "lw_system.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failed; static uint8_t record[LW_SYS_PREF_BYTES]; static bool io_ok=true;
#define CHECK(name,value) do { checks++; if (!(value)) { failed++; printf("FAIL: %s\n",name); } } while(0)
static bool light(void *c,unsigned n) { (void)c; return io_ok && n>=10 && n<=100; }
static bool sound(void *c,unsigned n,bool m) { (void)c; (void)m; return io_ok && n<=100; }
static bool load(void *c,uint8_t out[LW_SYS_PREF_BYTES]) { (void)c; memcpy(out,record,sizeof(record)); return true; }
static bool store(void *c,const uint8_t in[LW_SYS_PREF_BYTES]) { (void)c; if (!io_ok) return false; memcpy(record,in,sizeof(record)); return true; }
int main(void)
{
    lw_system_backend_t b={0}; b.simulated=true; b.network_available=true; b.brightness=light; b.volume=sound; b.load=load; b.store=store;
    lw_system_t s; lw_system_init(&s,&b);
    CHECK("invalid initial record defaults safely",s.prefs.brightness==72 && s.prefs.volume==40);
    CHECK("brightness has safe minimum",lw_system_brightness(&s,-500) && s.prefs.brightness==10);
    CHECK("brightness capped",lw_system_brightness(&s,200) && s.prefs.brightness==100);
    CHECK("volume can be zero",lw_system_volume(&s,-1) && s.prefs.volume==0);
    io_ok=false; CHECK("failed hardware operation does not change state",!lw_system_volume(&s,70) && s.prefs.volume==0);
    CHECK("failed store remains retryable",!lw_system_save(&s) && s.dirty && s.store_failed); io_ok=true;
    lw_system_volume(&s,40); lw_system_theme(&s,2,2); lw_system_dnd(&s,true); lw_system_widgets(&s,false); lw_system_mute(&s,true);
    CHECK("valid preferences save",lw_system_save(&s) && !s.dirty && !s.store_failed);
    lw_system_t restored; lw_system_init(&restored,&b);
    CHECK("full preference roundtrip",restored.prefs.volume==40 && restored.prefs.theme==2 && restored.prefs.wallpaper==2 && restored.prefs.dnd && restored.prefs.muted && !restored.prefs.widgets);
    record[6]^=1; lw_system_init(&restored,&b); CHECK("CRC corruption falls back",restored.prefs.volume==40 && restored.prefs.theme==0);
    lw_system_theme(&s,100,0); CHECK("invalid theme ignored",s.prefs.theme==2);
    CHECK("scan starts asynchronously",lw_system_wifi_scan(&s) && s.wifi==LW_WIFI_SCANNING && !s.network_count);
    lw_system_tick(&s,600); CHECK("mock scan produces declared APs",s.network_count==3 && s.wifi==LW_WIFI_IDLE);
    CHECK("short password rejected",!lw_system_wifi_connect(&s,0,"short"));
    CHECK("index bound checked",!lw_system_wifi_connect(&s,8,"password"));
    CHECK("connect pending",lw_system_wifi_connect(&s,0,"wrongpass") && s.wifi==LW_WIFI_CONNECTING);
    lw_system_tick(&s,1000); CHECK("failure event clears current SSID",s.wifi==LW_WIFI_FAILED && !s.ssid[0]);
    CHECK("retry accepted",lw_system_wifi_connect(&s,0,"demo-only")); lw_system_tick(&s,1000);
    CHECK("success event",s.wifi==LW_WIFI_CONNECTED && !strcmp(s.ssid,"Little Home"));
    lw_system_wifi_enable(&s,false); CHECK("disable disconnects",s.wifi==LW_WIFI_OFF && !s.ssid[0]);
    lw_system_connection_result(&s,true); CHECK("stale completion cannot reconnect",s.wifi==LW_WIFI_OFF);
    CHECK("off state cannot scan",!lw_system_wifi_scan(&s));
    b.simulated=false; b.network_available=false; lw_system_init(&s,&b); CHECK("unavailable real network is explicit",s.wifi==LW_WIFI_UNSUPPORTED && !lw_system_wifi_scan(&s));
    printf("System model: %u checks, %u failures\n",checks,failed); return failed ? 1 : 0;
}
