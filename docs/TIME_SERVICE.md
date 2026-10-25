# Time service

Status: **implemented, tested on the host, wired into the boot path; SNTP is
compiled out by default because there is no network yet.**

## Problem it solves

Until this round the shell had exactly one clock: the DEMO clock in `lw_ui`
(`2026-10-25 19:30`), and `lw_hw_time_get()` reported "no source". An OS whose
status bar shows a made-up time is a demo, not a system. The time service keeps
the DEMO clock as the fallback but upgrades to real time as soon as one exists.

## States

```
DEMO ──request_sync──> SYNCING ──ok──> SYNCED ──6 h──> STALE
DEMO <──fail (nothing to fall back on)── FAILED/SYNCING
SYNCED ──link lost──> STALE
```

- **DEMO** — no usable timestamp. The UI keeps showing the DEMO clock and must
  label it as such.
- **SYNCED** — a SNTP answer or an RTC restore; the clock advances with uptime.
- **STALE** — a real timestamp that nobody re-validated this boot, or that is
  older than six hours. Still displayed, but the UI can mark it.
- **FAILED** — a sync was attempted and failed with no earlier time to fall
  back on.

`lw_time_service_utc()` extrapolates from the last sync with `now_ms`, so the
clock does not need a per-second RTC read. `lw_time_service_clock()` /
`_date()` / `_hm()` / `_date_label()` derive local fields from
`utc_seconds + tz_offset_min`.

## Where the timezone lives

The 24-byte preferences blob (`lwo2.prefs`) has a fixed layout and a CRC that
the already-flashed firmware depends on. The timezone is therefore **not** in
it: it belongs in its own record (`lwo2.time`) with the offset in minutes,
clamped to ±840. Default is UTC+08:00.

This keeps two contracts intact at once: old saves stay readable, and the
timezone can change without rewriting preferences.

## SNTP path

`CONFIG_LW_NET_SNTP_ENABLE` (default `n`, depends on the Wi-Fi backend) starts
`esp_netif_sntp` with `pool.ntp.org`. `lw_services_poll()` requests a sync once
the link is up, retries every 30 s while the clock is still DEMO, and calls
`lw_time_service_on_network_lost()` when the link drops so a synced clock
degrades to STALE instead of silently drifting.

## Tests

`tests/host/test_lw_net.c` (suite `time/*`): DEMO has no clock and prints
`--:--`; a sync at 2024-10-27T11:30Z renders as `19:30` at UTC+8 and
`2024-10-27` / `10月27日 周日`; uptime drift advances the minutes; zone changes
re-render (UTC, +8, −5) and clamp; staleness after link loss and after six
hours; a failed resync preserves a known time as STALE; an RTC restore is STALE
and marks `rtc_restored`.

## What is still missing

- No RTC on the board, so a cold boot without network always starts in DEMO.
- Nothing persists the last known UTC second yet; `lwo2.time` currently stores
  the zone only. That is the next small step if offline time matters.
