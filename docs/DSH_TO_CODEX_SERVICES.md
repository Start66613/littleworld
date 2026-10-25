# DSH → Codex: connectivity services handoff

Branch: `integration/p4-connectivity-services` (off `130b5f6`, the p4-backend
round). No UI file from the OS V2 work was modified; `lw_os.c`,
`lw_os_design.c` and `lw_rpg_view.c` are untouched.

## What this round adds

```
firmware/components/lw_net/
  include/lw_wifi.h         Wi-Fi state machine API
  include/lw_time_svc.h     wall-clock service API
  include/lw_audio.h        PCM pipeline API
  include/lw_services.h     glue + snapshot the UI can read
  include/lw_net_esp.h      adapter boundary
  src/lw_wifi_model.c       pure C, host tested
  src/lw_time_service.c     pure C, host tested
  src/lw_audio.c            pure C, host tested
  src/lw_services.c         glue: runs actions, builds snapshot
  src/lw_net_esp.c          stubs today; esp_wifi implementation compiled only
                            when CONFIG_LW_NET_WIFI_ENABLE is set
  CMakeLists.txt
firmware/main/lw_boot.c     starts the services and a 500 ms esp_timer poll
firmware/main/Kconfig.projbuild  LW_NET_WIFI_ENABLE / LW_NET_SNTP_ENABLE (default n)
firmware/main/CMakeLists.txt     requires lw_net
tests/host/test_lw_net.c    131 checks
tools/run_net_tests.ps1     builds and runs them with MSVC
docs/WIFI_BACKEND.md, TIME_SERVICE.md, AUDIO_BACKEND.md, INTEGRATION_TESTS.md
```

## What Codex can rely on

```c
lw_net_status_t status;
lw_services_status(&services, now_ms, &status);
status.clock          /* "19:30" or "--:--" */
status.date_label     /* "10月25日 周日" or "" */
status.time_state     /* DEMO / SYNCING / SYNCED / STALE / FAILED */
status.wifi_state     /* UNSUPPORTED / OFF / IDLE / SCANNING / CONNECTING / ... */
status.wifi_backend   /* false on today's firmware: show "未接线", not a spinner */
status.ap_count, status.rssi, status.ssid, status.ip
status.audio_backend  /* false until the ES8311 path is opened */
```

Requests: `lw_services_wifi_enable()`, `lw_services_scan()`,
`lw_services_connect()`, `lw_services_disconnect()`,
`lw_services_request_time_sync()`, `lw_services_set_zone()`.

Polling happens on an `esp_timer` (500 ms), off the LVGL thread. The UI only
reads the snapshot, so no locking is needed.

## The next integration step (small, contained)

1. In `lw_os.c`'s status-bar build, replace the DEMO clock string with
   `status.clock` when `status.time_valid`, and keep the DEMO label otherwise.
2. In the control centre Wi-Fi card, render `status.wifi_state`; when
   `status.wifi_backend` is false, show "Wi-Fi 未接线" and disable the button.
3. Wire the settings Wi-Fi pane to `lw_services_scan()` and render
   `status.ap_count` entries from `lw_wifi_scan_at()`.
4. When touching the clock, keep `lw_system`'s preferences blob at 24 bytes and
   store nothing new there — the zone lives in `lwo2.time`.

## Deliberate limits

- Wi-Fi and SNTP are **off** in the shipped configuration; enabling them needs
  `esp_hosted` + `esp_wifi_remote` and a C6 firmware that matches. The adapter
  code for that is written but has never been compiled or run.
- No audio output yet: the ES8311/I2S path still has to be opened, and there is
  no PCM asset in the repo.
- Nothing was flashed to the board in this round.
