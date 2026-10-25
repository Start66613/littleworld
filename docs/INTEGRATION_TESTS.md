# Integration tests

What was run for the connectivity round, what it proves, and what it cannot.

## Host tests (pure C, MSVC `/W4 /WX`)

Run on 2026-10-25 from commit `ba545a1`, on the build machine (Windows, MSVC 19).

| Suite | Command | Result |
| --- | --- | --- |
| core (time/pet/state/memories) | `tools/run_host_tests.ps1` | 142 checks, 0 failures |
| store | `tools/run_backend_tests.ps1` | 29 checks, 0 failures |
| system model | `tools/run_backend_tests.ps1` | 22 checks, 0 failures |
| **services (Wi-Fi, time, audio)** | `tools/run_backend_tests.ps1` | **131 checks, 0 failures** |
| RPG vertical slice | `test_rpg` | 85 checks, 0 failures |

The services suite is the new one. It links `lw_wifi_model.c`,
`lw_time_service.c`, `lw_audio.c` and `lw_core/lw_time.c` into one binary and
drives them with an explicit clock — no sleeping, no radio, no codec. The
adapter boundary is satisfied by `tests/host/lw_net_esp_stub.c`, which reports
every capability as unavailable, so the "no radio" path is exercised by default.

Coverage highlights:

- scan list ordering, duplicate collapse, out-of-range reads;
- stale-generation callbacks ignored for both scan and connect;
- auth failure terminal, timeout retried three times with backoff, then
  `RETRIES_EXHAUSTED`;
- credentials zeroized on disconnect/disable;
- event queue capped at eight with dropped-oldest accounting;
- clock rendering at UTC, +8, −5; 2024-10-27T11:30Z → `19:30` and `10月27日 周日`;
- DEMO never invents a time; a failed resync keeps the old time as STALE;
- audio ring back-pressure, pause semantics, mute accounting, drain, IO failure.

Logs: `reports/logs/backend-tests.log`, `reports/logs/build-os.log`,
`reports/logs/build-legacy.log`, `reports/logs/build-noaudio.log`.

## Firmware builds

Three configurations are built from this branch to prove the service layer does
not break any boot mode. All three were built on 2026-10-25 and each image was
checked with `tools/image_check.py --expect-rev 302`.

| Build dir | Configuration | Image | Size | Result |
| --- | --- | --- | --- | --- |
| `build/os` | OS V2 desktop, audio probe on | `little_world.bin` | 1,569,280 B | PASS (rev 3.0–3.99) |
| `build/legacy` | `CONFIG_LW_BOOT_MODE_OS_V2=n` | `little_world.bin` | 1,323,264 B | PASS (rev 3.0–3.99) |
| `build/noaudio` | OS V2 on, `CONFIG_LW_ENABLE_AUDIO=n` | `little_world.bin` | 1,569,280 B | PASS (rev 3.0–3.99) |

```
os       eed272d46f4a321e4d556934101efcd03301ca1f16d707b6774c3e8455d732ad
legacy   7d99601d88ab23ba28a36e40eeb946d945c438a54328e3b8922d6ebe14003772
noaudio  a8058d2eb53b45d4685488073c2afe7928e5a254a045360e1dab7e8f568b6ce6
```

`os` and `noaudio` have the same size but different hashes: the audio probe is
compiled either way, only the boot-time call to the codec differs.

Two build failures were found and fixed during this round, and both are worth
recording because they are invisible in the default configuration:

1. `LW_WIFI_OFF/IDLE/...` were declared in both `lw_system.h` and `lw_wifi.h`;
   `lw_boot.c` includes both, so the OS build failed with `redeclaration of
   enumerator`. The new model now uses `LW_WIFI_ST_*`.
2. `CONFIG_LW_ENABLE_AUDIO` does not exist as a macro when the option is `n`,
   so `lw_hw_begin(CONFIG_LW_ENABLE_AUDIO)` failed in the `noaudio` build. It is
   now read through `#ifdef`.

## Deliberately not run

- **Flashing.** The board is not authorised for this round; nothing was written
  to the device, and no `erase_flash` was issued.
- **C6 firmware update.** Would change the radio; needs separate approval.
- **TF card work.** Out of scope this round.
- **Paid art generation.** No Meowa credits were spent.
- **Long run.** No 24 h soak on hardware; the stability work here is model-level
  (timeouts, backoff, bounded queues), not a device soak test.

## Known gaps

- The Wi-Fi adapter has never executed against a real C6 (see
  `docs/WIFI_BACKEND.md`).
- Audio has no PCM source yet, so no sound is produced (see
  `docs/AUDIO_BACKEND.md`).
- The SNTP adapter is a placeholder that reads the system clock instead of
  waiting for an SNTP callback (see `docs/TIME_SERVICE.md`).
- The OS still draws the DEMO clock: `lw_services_status()` exposes
  `clock`/`date_label`, but the UI does not read them yet. That adoption step is
  described in `docs/DSH_TO_CODEX_SERVICES.md`.
- `now_ms` is a `uint32` throughout the models; `esp_timer_get_time()/1000`
  wraps after ~49.7 days.

## OS V3 combined build (integration/os-v3-p4-combined)

Measured on 2026-10-25 in `D:\LittleWorld-V3`, merge commit `337f7f1` plus the
concurrency and reconnect fixes.

Host backend tests (MSVC `/W4 /WX`): **188 checks, 0 failures**
(store 29 + system 22 + services 137). The services suite grew from 131 to 137
with `wifi/reconnect-policy`: budget counting, backoff boundary, credentials
reused from RAM, budget reset on success, `RETRIES_EXHAUSTED` after three
losses, and "switch the radio off and nothing is scheduled".

Windows regression: `WINDOWS REGRESSION OK` — system 22, duo 82, RPG 85,
storage 33, OS V2 340 checks, 0 failures, 31 real LVGL screenshots
(`reports/ui-v2/os-v2/`, local only because that path is gitignored).

| Configuration | Image | Size | SHA-256 | Header check |
| --- | --- | --- | --- | --- |
| `build/os` | `little_world.bin` | 1,598,656 B | `0750cf158fa6f09f312a61300a1dab23ef0de496de14af1ac0dbc5017a674993` | PASS (rev 3.0-3.99) |
| `build/legacy` | `little_world.bin` | 1,349,008 B | `7e4d2bc5a4beee907e62ee328f1435646d0618edfa2eb1c7cd3cbfc5df1c8adb` | PASS (rev 3.0-3.99) |
| `build/noaudio` | `little_world.bin` | 1,598,656 B | `c78aa70adc5884199c7dd6706e96b2398a7c8fc0606af46b32bb2e98acdbdfdf` | PASS (rev 3.0-3.99) |

Partition fit: `factory` app at `0x10000`, size 6,291,456 B (6M); the `os` image
uses 25.4 % of it, so all three configurations fit without touching
`partitions.csv` (unchanged since the merge base).

Linkage evidence (`tools/v3_evidence.ps1`, `riscv32-esp-elf-nm` on
`little_world.elf`):

| Symbol | os | noaudio | legacy |
| --- | --- | --- | --- |
| `lw_os_start`, `lw_os_capabilities`, `lw_ui_start` | linked | linked | `lw_ui_start` only |
| `lw_services_poll`, `lw_services_status` | linked | linked | absent |
| `lw_wifi_init`, `lw_time_service_init`, `lw_audio_init`, `lw_net_esp_wifi_attach` | linked | linked | absent |
| `lw_boot_start`, `lw_store_load` | linked | linked | `lw_boot_start` |
| `lw_rpg_init`, `lw_pet_tick` | linked | linked | `lw_pet_tick` |

The legacy image links no OS or service code on purpose: `CONFIG_LW_BOOT_MODE_LEGACY`
starts the pre-OS shell, nothing references the services, and `--gc-sections`
removes them. That is the fallback being smaller (249,648 B less), not the
backend being broken.