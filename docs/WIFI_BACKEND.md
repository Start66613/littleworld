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

## It runs on the board now (2026-11-18)

The link is up: `transport: capabilities: 0xd`, `station ready`, and the Wi-Fi
page lists real access points. Three things had to be true at once, and each of
them was found in a boot log rather than by reading code:

1. **`sdkconfig.extra.wifi` must not start with a BOM.** PowerShell's
   `Set-Content -Encoding utf8` writes one, Kconfig then ignores the *first*
   line — which is `CONFIG_LW_NET_WIFI_ENABLE=y` — and the adapter compiles as a
   stub while the log shows no `lw_net` line at all. After editing that file by
   script, check the first bytes are `43 4f 4e 46` (`CONF`).

2. **Long file names are mandatory.** A freshly generated `sdkconfig` defaults
   to `CONFIG_FATFS_LFN_NONE`. The card still mounts, but `littleworld-v4.json`
   and every `.lwp` are long names, so the manifest cannot be opened and the
   device reports no card while the medium is clearly mounted. Keep
   `CONFIG_FATFS_LFN_HEAP=y`, `CONFIG_FATFS_MAX_LFN=255`,
   `CONFIG_FATFS_API_ENCODING_UTF_8=y`.

3. **The radio must come up after the card, not before.** `esp_wifi_init()`
   makes hosted take SDIO DMA out of internal RAM; if that happens first,
   `sdmmc_cmd: allocate_dma_buf` fails and the card never mounts. `lw_art_boot()`
   owns the mount, so the link is started strictly afterwards.

### The link starts on demand

Bringing the radio up during boot also raced `main_task`'s first stdio
allocation: newlib grew a `FILE` lock from the same internal pool, and losing
that race called `abort()` inside `lock_init_generic` (`locks.c:77`). That is why
the board sometimes needed two resets before it stayed up.

Boot therefore does not touch the radio. `lw_system_backend_t.wifi_wake` points
at `wifi_wake_hook()` in `firmware/main/lw_boot.c`, which calls
`lw_net_esp_wifi_start_async(4000)`; `lw_system_wifi_enable()` and
`lw_system_wifi_scan()` call it while `network_available` is still false, and the
capability flips once the C6 answers. Cost: the first Wi-Fi interaction blocks
~2.2 s (`bring-up: wifi_init 2220 ms, wifi_start 99 ms`), boot drops from 3.9 s
to 1.6 s, and a boot can no longer abort.

### Queue sizes are not free money

`CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE` / `RX_Q_SIZE` default to 20. Trimming them to
10 (or turning on `..._OPTIMIZATION_RX_STREAMING_MODE`) makes the receive task
assert on an empty buffer pool — `sdio_drv.c:878 (rxbuff)` or
`sdio_push_data_to_queue ... (pkt_rxbuff)` — and the board reset-loops. Between
20/20 with `..._OPTIMIZATION_RX_NONE` and a dead board, take the queues.

### Internal RAM budget (measured, board, one boot)

| stage | free | largest block |
|---|---|---|
| after `hw` | ~97 KB | 32 KB |
| after `art` (card mounted) | ~58 KB | 30 KB |
| after `ui` | ~26 KB | 12 KB |
| after radio on demand | ~10 KB | 6 KB |

The speaker needs one contiguous internal DMA block of at least 8 KB
(`lw_hw.c` guards on this and skips the speaker instead of letting
`bsp_audio_codec_speaker_init()` panic). Anything added to boot that takes
internal RAM has to be measured against this table.

### The first end-to-end connection (2026-11-18, evening)

An open network was joined from the Wi-Fi page and the device got a lease:

```
I (2651821) H_API: esp_wifi_remote_connect
I (2656995) esp_netif_handlers: sta ip: 172.25.35.224, mask: 255.255.192.0, gw: 172.25.0.1
```

The page then showed `XTU-STUDENT · 已连接 · 50 dBm · 信号强 · 开放` with a check
mark, offered 断开 / 忘记网络 / 刷新, and the control centre's Wi-Fi tile
switched from `无线驱动尚未接入` to the real SSID.

Four separate faults had to be fixed before the first `scan done`, and each one
looked like "the radio is dead" from the outside:

1. **The hosted receive pool only used internal RAM.** `os_wrapper.h` defines
   `MEM_ALLOC(x)` as `heap_caps_aligned_alloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)`;
   the pool grows on demand and never returns blocks, so a busy scan hit NULL and
   asserted in `sdio_read_task`. The local patch lets it fall back to PSRAM
   (`SOC_PSRAM_DMA_CAPABLE` and `SOC_SDMMC_PSRAM_DMA_CAPABLE` are both set) — see
   `tools/patches/esp_hosted-psram-mempool.md`, which is the record because
   `managed_components/` is git-ignored and the patch must be re-applied after a
   dependency update.
2. **Event handlers were registered too early.** `attach()` runs before
   `esp_event_loop_create_default()`, so every registration failed and
   `s_handlers` blocked the retry: no `WIFI_EVENT` was ever delivered, scans
   started and were never answered. Registration moved into `wifi_bring_up()`.
3. **The model was born UNSUPPORTED.** It is created during boot, when the radio
   is deliberately off, and never re-evaluated the capability. It now re-syncs on
   every request before deciding the radio cannot scan.
4. **`esp_wifi_start()` returns early.** A scan issued in that gap is accepted and
   dropped; a binary semaphore given from `WIFI_EVENT_STA_START` makes
   `station ready` mean ready.

`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=6144` is also needed: the hosted receive
path runs on `sys_evt`, and 2304 bytes overflowed (`Stack protection fault` in
task `sys_evt`) once a scan really happened. Remember that an existing
`build/<cfg>/sdkconfig` keeps old values — the tree has to be regenerated for a
changed default to take effect.
