/* SPDX-License-Identifier: MIT */
#include "lw_system.h"
#include "lw/lw_state.h"
#include <string.h>
static unsigned bound(int n,unsigned min) { return n<(int)min ? min : n>100 ? 100u : (unsigned)n; }
static void changed(lw_system_t *s) { s->revision++; s->dirty=true; }
void lw_system_encode(const lw_preferences_t *p,uint8_t out[LW_SYS_PREF_BYTES])
{
    memset(out,0,LW_SYS_PREF_BYTES); memcpy(out,"LWO2",4); out[4]=1;
    out[5]=p->brightness; out[6]=p->volume; out[7]=p->theme; out[8]=p->wallpaper;
    out[9]=(uint8_t)(p->muted|p->dnd<<1|p->widgets<<2);
    uint32_t crc=lw_crc32(out,20); for (unsigned i=0;i<4;i++) out[20+i]=(uint8_t)(crc>>(i*8));
}
bool lw_system_decode(lw_preferences_t *p,const uint8_t in[LW_SYS_PREF_BYTES])
{
    uint32_t crc=0; for (unsigned i=0;i<4;i++) crc|=(uint32_t)in[20+i]<<(i*8);
    if (memcmp(in,"LWO2",4)||in[4]!=1||lw_crc32(in,20)!=crc||in[5]<10||in[5]>100||in[6]>100||in[7]>2||in[8]>2||(in[9]&~7)) return false;
    *p=(lw_preferences_t){in[5],in[6],in[7],in[8],(in[9]&1)!=0,(in[9]&2)!=0,(in[9]&4)!=0}; return true;
}
void lw_system_init(lw_system_t *s,const lw_system_backend_t *backend)
{
    memset(s,0,sizeof(*s)); if (backend) s->backend=*backend;
    s->prefs=(lw_preferences_t){72,40,0,0,false,false,true};
    uint8_t bytes[LW_SYS_PREF_BYTES]; lw_preferences_t loaded;
    if (s->backend.load && s->backend.load(s->backend.context,bytes) && lw_system_decode(&loaded,bytes)) s->prefs=loaded;
    s->wifi_enabled=true; s->wifi=s->backend.network_available ? LW_WIFI_IDLE : LW_WIFI_UNSUPPORTED; s->revision=1;
    if (s->backend.brightness) (void)s->backend.brightness(s->backend.context,s->prefs.brightness);
    if (s->backend.volume) (void)s->backend.volume(s->backend.context,s->prefs.volume,s->prefs.muted);
}
bool lw_system_brightness(lw_system_t *s,int n)
{
    unsigned value=bound(n,10); if (!s->backend.brightness || !s->backend.brightness(s->backend.context,value)) return false;
    if (s->prefs.brightness!=value) { s->prefs.brightness=(uint8_t)value; changed(s); } return true;
}
bool lw_system_volume(lw_system_t *s,int n)
{
    unsigned value=bound(n,0); if (!s->backend.volume || !s->backend.volume(s->backend.context,value,s->prefs.muted)) return false;
    if (s->prefs.volume!=value) { s->prefs.volume=(uint8_t)value; changed(s); } return true;
}
bool lw_system_mute(lw_system_t *s,bool muted)
{
    if (!s->backend.volume || !s->backend.volume(s->backend.context,s->prefs.volume,muted)) return false;
    if (s->prefs.muted!=muted) { s->prefs.muted=muted; changed(s); } return true;
}
void lw_system_theme(lw_system_t *s,unsigned theme,unsigned wallpaper)
{ if (theme<3 && wallpaper<3) { s->prefs.theme=(uint8_t)theme; s->prefs.wallpaper=(uint8_t)wallpaper; changed(s); } }
void lw_system_dnd(lw_system_t *s,bool on) { s->prefs.dnd=on; changed(s); }
void lw_system_widgets(lw_system_t *s,bool on) { s->prefs.widgets=on; changed(s); }
bool lw_system_save(lw_system_t *s)
{
    if (!s->dirty) return true;
    uint8_t bytes[LW_SYS_PREF_BYTES]; lw_system_encode(&s->prefs,bytes);
    if (!s->backend.store || !s->backend.store(s->backend.context,bytes)) { s->store_failed=true; return false; }
    s->dirty=false; s->store_failed=false; return true;
}
void lw_system_wifi_enable(lw_system_t *s,bool on)
{
    if (!on) lw_system_wifi_disconnect(s,false);
    s->wifi_enabled=on; s->wifi=!on ? LW_WIFI_OFF : s->backend.network_available ? LW_WIFI_IDLE : LW_WIFI_UNSUPPORTED;
    s->revision++;
}
bool lw_system_wifi_scan(lw_system_t *s)
{
    if (!s->wifi_enabled || !s->backend.network_available || s->wifi==LW_WIFI_CONNECTING) return false;
    if (!s->backend.simulated && (!s->backend.wifi_scan || !s->backend.wifi_scan(s->backend.context))) return false;
    s->wifi=LW_WIFI_SCANNING; s->network_ms=0; s->revision++; return true;
}
void lw_system_scan_result(lw_system_t *s,const lw_network_t *aps,unsigned count)
{
    if (!s->wifi_enabled || s->wifi!=LW_WIFI_SCANNING || (!aps && count)) return;
    s->network_count=count>8 ? 8 : count;
    for (unsigned i=0;i<s->network_count;i++) { s->networks[i]=aps[i]; s->networks[i].ssid[32]=0; }
    s->wifi=s->ssid[0] ? LW_WIFI_CONNECTED : LW_WIFI_IDLE; s->revision++;
}
bool lw_system_wifi_connect(lw_system_t *s,unsigned index,const char *password)
{
    if (!s->wifi_enabled || !s->backend.network_available || index>=s->network_count || !password) return false;
    size_t len=strlen(password); if (len>63 || (s->networks[index].secured && len<8)) return false;
    if (!s->backend.simulated && (!s->backend.wifi_connect || !s->backend.wifi_connect(s->backend.context,s->networks[index].ssid,password))) return false;
    strcpy(s->ssid,s->networks[index].ssid); s->mock_reject=s->backend.simulated && !strcmp(password,"wrongpass");
    s->wifi=LW_WIFI_CONNECTING; s->network_ms=0; s->revision++; return true;
}
void lw_system_connection_result(lw_system_t *s,bool ok)
{
    if (s->wifi!=LW_WIFI_CONNECTING) return;
    s->wifi=ok ? LW_WIFI_CONNECTED : LW_WIFI_FAILED; if (!ok) s->ssid[0]=0; s->revision++;
}
void lw_system_wifi_disconnect(lw_system_t *s,bool forget)
{
    if (!s->backend.simulated && s->backend.wifi_disconnect) (void)s->backend.wifi_disconnect(s->backend.context,forget);
    s->ssid[0]=0; s->network_ms=0; s->wifi=s->wifi_enabled ? LW_WIFI_IDLE : LW_WIFI_OFF; s->revision++;
}
void lw_system_tick(lw_system_t *s,uint32_t ms)
{
    if (!s->backend.simulated) return;
    s->network_ms+=ms;
    if (s->wifi==LW_WIFI_SCANNING && s->network_ms>=600) {
        const lw_network_t aps[]={ {"Little Home",-42,true},{"Sakura Studio",-64,true},{"Guest Garden",-80,false} };
        lw_system_scan_result(s,aps,3);
    } else if (s->wifi==LW_WIFI_CONNECTING && s->network_ms>=1000) lw_system_connection_result(s,!s->mock_reject);
}
