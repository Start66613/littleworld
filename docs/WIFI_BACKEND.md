# Wi-Fi backend

Status: **model implemented and tested on the host; the ESP-IDF adapter is
written but not compiled into the default build and has never run on the board.**

## Why it is off by default

The board is an ESP32-P4 with no on-die Wi-Fi. The radio is an ESP32-C6 behind
SDIO, which ESP-IDF speaks to through two managed components (`esp_hosted` and
`esp_wifi_remote`). Neither is installed in this repository, and the C6 was
never updated in this round, so `CONFIG_LW_NET_WIFI_ENABLE` defaults to `n`:

- without the flag the firmware links the stubs in `lw_net_esp.c`;
- `lw_wifi_available()` then returns false and the OS reports "radio
  unsupported" instead of pretending to scan.

That is deliberate: a fake "searching for networks" spinner would be worse than
an honest "not wired up".

## Layers

```
lw_wifi_model.c   pure C state machine, scan list, retry policy, event queue
lw_services.c     glue: owns the models, runs their actions, builds the snapshot
lw_net_esp.c      adapter boundary: stubs today, esp_wifi tomorrow
lw_net_wifi_esp.c (planned) real esp_wifi/esp_event/esp_netif implementation
```

The model never calls into ESP-IDF and never touches LVGL. It records an action
(`LW_WIFI_ACTION_SCAN`, `CONNECT`, `DISCONNECT`) and `lw_services_poll()` hands
it to the adapter, so the same code is driven by the host tests with no fake
radio at all.

## State machine

```
UNSUPPORTED ──(set_supported)──> OFF ──enable──> IDLE
IDLE ──scan──> SCANNING ──result/timeout──> IDLE
IDLE ──connect──> CONNECTING ──ok──> CONNECTED ──disconnect/link loss──> IDLE
CONNECTING ──auth fail──> FAILED
CONNECTING ──timeout ×3──> FAILED (retries exhausted)
```

Rules that matter in practice:

- **Generation guard.** Every connect attempt bumps `generation`; every scan
  bumps `scan_generation`. A late callback carrying an old generation is
  dropped, so a slow `ESP_WIFI_EVENT` can never resurrect a cancelled attempt.
- **Only retry what can heal.** `AUTH_FAILED`, `NO_AP` and `INVALID_ARGS` fail
  immediately. Timeouts, DHCP failures and internal errors retry up to three
  times with a 2 s × attempt backoff.
- **Bounded, sorted scan list.** Eight entries, strongest RSSI first,
  duplicate SSIDs collapsed to their strongest sighting.
- **Credentials live in RAM only.** The password is zeroized on disconnect,
  disable, and when the adapter reports the link is gone.
- **Bounded event queue.** Eight events; the oldest is dropped (and counted)
  so the newest state always reaches the UI.
- **Timeouts are time-driven.** 10 s for a scan, 15 s for a connect; the poll
  timer calls `lw_wifi_tick()`.

## Enabling the real backend

1. Add the managed components:

   ```powershell
   cd firmware
   idf.py add-dependency "espressif/esp_hosted"
   idf.py add-dependency "espressif/esp_wifi_remote"
   ```

2. Enable `CONFIG_LW_NET_WIFI_ENABLE` (and `CONFIG_LW_NET_SNTP_ENABLE` if the
   clock should come from the network).

3. Add `lw_net_wifi_esp.c` (planned file) or move the guarded implementation in
   `lw_net_esp.c` under the same flag. It registers `WIFI_EVENT` and
   `IP_EVENT_STA_GOT_IP`, converts `wifi_ap_record_t` into `lw_wifi_ap_t`, and
   reports back through `lw_services_wifi_report_*()`.

4. The C6 must be flashed with a hosted slave firmware that matches
   `esp_hosted`. **This round does not do that** — it needs explicit approval.

## Test coverage

`tests/host/test_lw_net.c` (suite `wifi/*`) covers: unsupported refusal, scan
ordering and duplicate collapse, stale results, connect/disconnect, stale
callbacks, auth failure being terminal, the three-attempt backoff, link loss,
scan and connect timeouts, event overflow and disable. Run it with
`tools/run_net_tests.ps1`.

Not covered, because it needs hardware: actual C6 association, DHCP timing,
regulatory domain, and the real reason codes the C6 emits.
