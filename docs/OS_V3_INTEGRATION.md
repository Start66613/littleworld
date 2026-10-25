# OS V3 + DSH backend integration

Short English summary of `docs/p4/OS_V3_INTEGRATION.md`.

## Merge

- Base A (frontend): `agent/os-v2-experience-v3` at `7f1a170` (Codex OS V3;
  that tip only adds docs and the user-authorized screenshots to `17c1545`).
- Base B (backend): `integration/p4-connectivity-services` at `904d7bc` (DSH services).
- Merge commits `337f7f1` (first, from `17c1545`) and `e7cd6af` (docs/screenshots
  tip), **zero conflicts**: the two sides touch disjoint file sets.
  The only shared file was `firmware/main/CMakeLists.txt`; the result keeps
  `lw_ui`, `lw_system`, `lw_backend` and `lw_net` in `REQUIRES`.
- `git diff 17c1545..7f1a170 -- firmware host tools tests` is empty, so the
  images built for this report are the same for both merge bases.
- `firmware/sdkconfig.os` is now tracked on purpose: the OS V3 default
  configuration must be reproducible.

## Defects fixed

1. **Disconnect semantics.** `WIFI_EVENT_STA_DISCONNECTED` used to be reported as
   a connect failure even after the link had been up. The adapter now records
   whether it ever got an IP and emits `LW_NET_EV_LINK_LOST`; the service layer
   downgrades the clock to `STALE` and lets the model run its reconnect policy.
2. **Concurrency.** `esp_event` callbacks and the `esp_timer` poll both mutated the
   models. Adapters now only push a `lw_net_event_t` into an 8-entry SPSC ring
   (drop-oldest, counted) under a tiny critical section; only
   `lw_services_poll()` drains it. The UI never touches a model: it copies
   `lw_net_status_t` from `lw_services_snapshot()`.
3. **Stale events.** The adapter captures `lw_wifi_generation()` /
   `lw_wifi_scan_generation()` when it starts a request and returns that value in
   the event; the model rejects anything that is not the current generation.
4. **Fake SNTP success.** The adapter used to claim success whenever the system
   clock was past 1.7e9. It now returns `LW_TIME_SYNC_NOT_SUPPORTED` unless the
   real `esp_netif_sntp` path is compiled in, and that path is **not** verified on
   hardware.

## Reconnect policy

Bounded and testable: a link loss schedules `LW_WIFI_BACKOFF_MS * n` (2 s, 4 s,
6 s) up to `LW_WIFI_MAX_RECONNECTS` (3) automatic attempts, reusing the password
held in RAM; then the model goes `FAILED` with `LW_WIFI_ERR_RETRIES_EXHAUSTED`,
wipes the password and stops. A drop while the radio is off schedules nothing.

## Capabilities

`lw_boot.c` builds `lw_os_capabilities_t` from `lw_hw_caps_t` and the service
state and applies it **on the LVGL task only**, as Codex's
`docs/handoff/DSH_UI_CAPABILITIES_V3.md` requires. `lw_os.c` hides the Wi-Fi
controls when the capability is `UNAVAILABLE`, so the UI cannot offer a button
that does nothing.

## Still not wired

C6 radio link (`esp_hosted` is absent locally, `CONFIG_LW_NET_WIFI_ENABLE=n`),
SNTP (compiled out, never run), audio output (no PCM source), storage (no TF card
by instruction), and `now_ms` wrap after ~49.7 days.

## Verification

Host backend tests: 188 checks, 0 failures. Windows regression: `WINDOWS
REGRESSION OK` (system 22, duo 82, RPG 85, storage 33, OS V2 340 checks, 31 real
LVGL screenshots). Firmware: three configurations built and header-checked; see
`docs/INTEGRATION_TESTS.md`. Nothing was flashed in this round.
