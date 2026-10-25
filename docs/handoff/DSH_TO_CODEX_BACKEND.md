# DSH → Codex: backend handoff

Branch: `integration/p4-backend-bringup` (cut from `agent/littleworld-os-v2`
`31dba23`). No file under `lw_ui/` was modified.

## What was added

```
firmware/main/app_main.c          board bring-up only (BSP display, touch, backlight, LVGL lock)
firmware/main/lw_boot.c           boot policy, platform hooks, NVS adapters, diagnostics
firmware/main/Kconfig.projbuild   LW_BOOT_MODE (OS_V2 default / LEGACY), LW_ENABLE_AUDIO
firmware/main/CMakeLists.txt      links lw_backend + lw_system + lw_ui + nvs_flash
firmware/components/lw_backend/
    lw_store.[ch]                 ESP-free framed record (magic, version, kind, CRC-32)
    lw_store_nvs.[ch]             NVS transport, never erases, separate namespaces
    lw_hw.[ch]                    BSP adapters: brightness, ES8311 probe, wifi/time reasons
    lw_diag.[ch]                  boot diagnostics (heap, PSRAM, largest block, NVS state)
tests/host/test_lw_store.c        substitute NVS: first boot, corruption, full, write fail, reboot
tests/host/test_lw_system.c       service model behaviour
tools/run_backend_tests.ps1       builds and runs both with MSVC /W4 /WX
tools/run_core_regression.ps1     duo + RPG + storage + framed store + system (200 checks)
```

## Contract the OS can rely on

* `lw_os_start(system, art)` is called with a `lw_system_t` whose backend has
  `simulated = false`, `network_available = false`, `brightness = NULL`,
  `volume = NULL` unless the codec actually opened, and `load`/`store` backed by
  `lwo2.sys/prefs`.
* `lw_os_rpg_storage()` receives a **static** host descriptor that stays valid
  for the whole session; `load` returns false for "no save yet", `store`
  returns false on any write failure.
* Nothing in the backend reports success it did not get: brightness, volume,
  Wi-Fi and time each either work or are reported as unsupported, with a log
  line that names the reason.

## Storage layout (do not change without a migration plan)

| Namespace | Key | Bytes | Owner |
| --- | --- | --- | --- |
| `lwo2.sys` | `prefs` | 24 | `lw_system` (brightness, volume, mute, theme, dnd, animations) |
| `lwo2.rpg` | `save` | 40 | RPG view via `lw_rpg_view_host_t` |
| `littleworld` | `save` | 72 | legacy pet + anniversary blob — **untouched** |

Record frame: `"LWST"` + u16 version + u8 kind + u16 length + payload + u32
CRC-32 (little endian). A record with a different version, kind or length is
rejected and the caller falls back to defaults; nothing is migrated in place.

## Things that need your decision

1. **Character art on device.** `lw_os_start(..., NULL)` means "no art pack".
   When the art pack exists, pass `lw_actor_art_t[2]` here — the call site is
   one line in `lw_boot.c::start_os()`.
2. **Wi-Fi ownership.** The C6/`esp_hosted` integration is a separate round. If
   you would rather own it, say so and I will keep `lw_hw.c` reporting
   Unsupported.
3. **Brightness.** I keep it Unsupported because the BSP uses LEDC on GPIO32
   with `output_invert = 1` and I could not verify the polarity. Once someone
   confirms "100 % = brightest" on the board, set `caps.brightness` in
   `lw_hw_begin()`.
4. **Page-change diagnostics.** `lw_diag_log_page()` exists but is not wired
   into your page switch; if you want it, call it from `lw_os_open()`.

## Build facts

* `esp32p4`, `sdkconfig.defaults` + `sdkconfig.defaults.rev3_x`, 32 MB flash.
* `build/little_world.bin` 1,565,264 B, image header v3.0–v3.99.
* Host suites: 393 checks, 0 failures (142 core, 51 backend, 200 regression).

## Deliberately untouched

`firmware/components/lw_ui/**` (including `lw_os.c`, `lw_os_design.c`,
`lw_rpg_view.c`), the RPG view implementation, the file cache, the Windows
shell, and `main`'s history. Only public headers are consumed.
