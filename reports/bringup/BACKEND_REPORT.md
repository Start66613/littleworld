# ESP32-P4 backend bring-up report

Branch `integration/p4-backend-bringup`, cut from `agent/littleworld-os-v2`
(`31dba23d8f14e414b95dad46c8426cd3e61583c8`).
Author: DSH (embedded backend). Codex's OS V2 sources were not modified.

## 1. What OS V2 now boots from

`firmware/main/app_main.c` only does board bring-up: BSP display with
`ROTATE_180`, triple-partial anti-tear, GT911 mirrors left at 0 (the rotation
already maps touch), backlight on, LVGL lock, then `lw_boot_start()`.

`firmware/main/lw_boot.c` owns the policy:

| Concern | Implementation |
| --- | --- |
| Boot choice | Kconfig `LW_BOOT_MODE` → `LW_BOOT_MODE_OS_V2` (default) or `LW_BOOT_MODE_LEGACY` |
| OS entry | `lw_os_start(&s_system, NULL)`; NULL selects the labelled no-art fallback |
| Failure path | if `lw_os_start` returns false → log + `lw_ui_start()` legacy shell |
| Platform hooks | `lw_platform_set()` with NVS-backed storage, uptime, log, and a time hook that reports "no source" |
| RPG storage | `lw_os_rpg_storage()` with a **static** `lw_rpg_view_host_t` (the OS keeps the pointer) |
| Diagnostics | `lw_diag_log_boot()` before the OS starts |

`CONFIG_LW_ENABLE_AUDIO` (Kconfig, default y) gates the ES8311 probe.

## 2. Persistence

New component `lw_backend` (no ESP dependency in the framed part):

* `lw_store.[ch]` — framed record: magic `LWST`, version, kind, payload length,
  payload, CRC-32. Status codes distinguish `MISSING`, `BAD_MAGIC`,
  `BAD_VERSION`, `BAD_KIND`, `BAD_LENGTH`, `BAD_CRC`, `TOO_SHORT`, `IO`.
* `lw_store_nvs.[ch]` — ESP-IDF transport. Calls `nvs_flash_init()` **once** and
  never erases: `ESP_ERR_NVS_NO_FREE_PAGES` and `ESP_ERR_NVS_NEW_VERSION_FOUND`
  are reported as unavailable so the existing saves survive.
* Namespaces: `lwo2.sys/prefs` (24-byte system record) and `lwo2.rpg/save`
  (40-byte RPG record). The legacy `littleworld/save` blob that holds the pet
  and anniversary data is **never** opened for writing by this code.
* Record version is `1`; an older or foreign version falls back to defaults
  instead of migrating or rewriting.
* A failed write returns false; `lw_system` keeps its dirty flag so the next
  save retries. Nothing reports success it did not get.

The previous `nvs_flash_erase()` on init failure is gone (grep: no call remains
in `firmware/`).

## 3. Hardware services

| Service | State | Evidence |
| --- | --- | --- |
| Panel + touch | works (unchanged BSP path) | app_main mirrors the verified earlier bring-up |
| Backlight | **report Unsupported** | BSP drives GPIO32 through LEDC with `output_invert = 1`; polarity vs "100 %" could not be verified without the board, so the boot default from `bsp_display_backlight_on()` is kept and no write is attempted |
| Audio | probe only | `bsp_audio_codec_speaker_init()` + `esp_codec_dev_set_out_vol()`; volume reaches the codec only if the handle opens, otherwise volume is a stored preference and the UI must say so |
| Wi-Fi | **report Unsupported** | ESP32-P4 has no radio; the C6/`esp_hosted` path is not integrated in this build, so no scan/connect is attempted |
| Time | **report Unsupported** | no RTC cell and no SNTP session; `lw_hw_time_get()` returns false so the OS shows its clearly labelled demo clock |

No pin numbers were invented: every GPIO used here comes from the Waveshare BSP
headers (`BSP_LCD_BACKLIGHT`, `BSP_I2S_*`, `BSP_I2C_*`, `BSP_SD_*`).

## 4. Diagnostics

`lw_diag.[ch]`: reset reason, free internal heap, free PSRAM, largest free
block, NVS state and reason, plus `lw_diag_log_page()` for infrequent page
changes. No logging inside the LVGL refresh path.

## 5. Verification actually executed

| Suite | Command | Result |
| --- | --- | --- |
| Core model (time, pet, state) | `tools/run_host_tests.ps1` | **142 checks, 0 failures** |
| Store + system services | `tools/run_backend_tests.ps1` | **29 + 22 checks, 0 failures** |
| Duo model | `tools/run_core_regression.ps1` | **82 checks, 0 failures** |
| RPG core + demo data | `tools/run_core_regression.ps1` | **85 checks, 0 failures** |
| Storage / file cache | `tools/run_core_regression.ps1` | **33 checks, 0 failures**, handles 59 → 59 |

Total **393 checks, 0 failures**. Logs: `reports/logs/backend-tests.log`,
`reports/logs/core-regression.log`, `reports/logs/firmware-build.log`.

Firmware build (`esp32p4`, profile `rev3_x`, 32 MB flash):

* `build/little_world.bin` — **1,565,264 bytes**, 24.9 % of the 6 MB `factory`
  partition (`0x10000`, size `0x600000`).
* Image header: chip `esp32p4` (0x0012), revision **v3.0 (300) .. v3.99 (399)**,
  flash 32 MB, DIO.
* SHA-256 `2f5fce0e4163af50fbcf976144a5445665484a3f4db16b15f536b4791e389a93`.
* Symbols present in `little_world.map`: `lw_os_start`, `lw_os_stop`,
  `lw_boot_start`, `lw_store_save`, `lw_store_load`, `lw_system_save`,
  `lw_diag_log_boot`, `lw_hw_begin`, `lw_rpg_view_create`.

## 6. Status matrix

| Item | Implemented | Compiles | Host-tested | On board | Confirmed on hardware |
| --- | --- | --- | --- | --- | --- |
| OS V2 boot path | yes | yes | n/a | **no** | **no** |
| Legacy fallback | yes | yes | n/a | no | no |
| Framed record store | yes | yes | yes (29) | no | no |
| System prefs over NVS | yes | yes | yes (22) | no | no |
| RPG save over NVS | yes | yes | yes (85 RPG core) | no | no |
| No-erase NVS policy | yes | yes | yes (store tests) | no | no |
| Backlight | adapter only | yes | n/a | **no** | **no — polarity unverified** |
| Audio | probe only | yes | n/a | **no** | **no** |
| Wi-Fi | unsupported | yes | n/a | **no** | **no** |
| Time | unsupported → demo clock | yes | yes (core) | no | no |
| Diagnostics | yes | yes | n/a | no | no |

## 7. Not done on purpose

* No flashing, no `erase-flash`, no partition change, no eFuse, no C6 firmware
  update (user was away; explicit confirmation is required).
* No TF-card work: no mount, no format, no photo access.
* No changes to `lw_os.c`, `lw_os_design.c`, `lw_rpg_view.c` or any other Codex
  UI file. The only contact point is the public `lw_os.h` API.
* No new RPG view, no second file cache, no Windows shell.

## 8. Next steps (needs the board and the user)

1. Flash on the user's confirmation and read the boot log:
   `boot: reset reason`, `NVS ready`, `network: … Unsupported`, `time: …`.
2. Verify touch mapping and the backlight polarity (write 100 → 40 → 100 and
   watch the panel).
3. Only after the polarity is proven, flip `caps.brightness` back on in
   `lw_hw_begin()`.
4. Wire the C6/`esp_hosted` path in a dedicated round (do not touch C6 firmware
   without approval).
