# Integration tests

What was run for the connectivity round, what it proves, and what it cannot.

## Host tests (pure C, MSVC `/W4 /WX`)

| Suite | Command | Checks |
| --- | --- | --- |
| core (time/pet/state/memories) | `tools/run_host_tests.ps1` | 142 |
| store + system model | `tools/run_backend_tests.ps1` | 29 + 22 |
| **services (Wi-Fi, time, audio)** | `tools/run_net_tests.ps1` | **131** |
| RPG vertical slice | `tools/run_core_regression.ps1` / `test_rpg` | 85 |

The services suite is the new one. It links `lw_wifi_model.c`,
`lw_time_service.c`, `lw_audio.c` and `lw_core/lw_time.c` into one binary and
drives them with an explicit clock — no sleeping, no radio, no codec.

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

Logs: `reports/logs/net-tests.log`, `reports/logs/backend-tests.log`,
`reports/logs/host-tests.log`.

## Firmware builds

Three configurations are built from this branch to prove the service layer does
not break any boot mode:

| Build dir | Configuration | Purpose |
| --- | --- | --- |
| `build/os` | OS V2 desktop, audio probe on | the shipped configuration |
| `build/legacy` | `CONFIG_LW_BOOT_MODE_OS_V2=n` | legacy arcade shell still links |
| `build/minimal` | OS V2 off + `CONFIG_LW_ENABLE_AUDIO=n` | no-codec board |

Each build must end with `Project build complete` and an app image whose header
reports ESP32-P4 rev 3.0–3.99 (checked with `tools/image_check.py`).

## Deliberately not run

- **Flashing.** The board is not authorised for this round; nothing was written
  to the device, and no `erase_flash` was issued.
- **C6 firmware update.** Would change the radio; needs separate approval.
- **TF card work.** Out of scope this round.
- **Paid art generation.** No Meowa credits were spent.

## Known gaps

- The Wi-Fi adapter has never executed against a real C6 (see
  `docs/WIFI_BACKEND.md`).
- Audio has no PCM source yet, so no sound is produced (see
  `docs/AUDIO_BACKEND.md`).
- The OS still draws the DEMO clock: `lw_services_status()` exposes
  `clock`/`date_label`, but the UI does not read them yet. That adoption step is
  described in `docs/DSH_TO_CODEX_SERVICES.md`.
