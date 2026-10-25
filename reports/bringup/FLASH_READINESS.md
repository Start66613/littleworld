# Flash readiness

Status: **not flashed.** The user was away for this round and explicitly
required confirmation before any write to the device.

## The eight gates

| # | Gate | State | Evidence / remaining work |
| --- | --- | --- | --- |
| 1 | The device on the port is really an ESP32-P4 | **not re-checked this round** | last verified on 2026-10-25 (rev v3.2, MAC `e8:f6:0a:ec:1e:44`); re-run `esptool.py --chip esp32p4 flash_id` when the board is connected |
| 2 | Correct port identified | **no** | board is not attached; the previous session used COM5 (CH343) |
| 3 | Flash size and partitions match | yes (static) | `sdkconfig.defaults`: 32 MB; `partitions.csv`: nvs 0x9000/0x6000, phy 0xf000/0x1000, factory 0x10000/6 MB, storage 0x610000/4 MB |
| 4 | Backup and recovery plan | yes (from earlier round) | 32 MiB full-flash backup verified at `D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`; firmware-only writes never touch `nvs` or `storage` |
| 5 | Firmware compiles | **yes** | `build/little_world.bin` 1,565,264 B, image header v3.0–v3.99, 24.9 % of the app partition |
| 6 | NVS erase risk checked | **yes** | `nvs_flash_erase()` no longer exists in `firmware/`; `lw_store_nvs_begin()` reports NO_FREE_PAGES / NEW_VERSION instead of erasing |
| 7 | Target write range confirmed | yes (static) | bootloader `0x2000`, partition table `0x8000`, app `0x10000`; `nvs` (0x9000) and `storage` (0x610000) are outside the write range |
| 8 | Explicit user confirmation for this flash | **no** | waiting for the user |

## Command that would be used (after gate 1, 2, 8)

```
cd D:\LittleWorld-P4\firmware
idf.py -B D:\LittleWorld-P4\build -p COM5 flash
```

This writes only the bootloader, the partition table and the app. It does not
erase the flash and does not touch `nvs` or `storage`.

## Forbidden in this round (unchanged)

`erase-flash`, full-chip erase, partition edits, eFuse writes, C6 firmware
updates, mounting or formatting the user's SD card, deleting photos.

## What to look for in the first boot log

1. `boot: reset reason N, free heap …` from `app_main`.
2. `NVS ready` (or a warning that says data was preserved).
3. `network: … Unsupported` and `time: …` — expected, not a failure.
4. `lw_os_start` must not log the legacy fallback. If it does, the OS rejected
   the system state and the log line right before it names the reason.
5. No `Guru Meditation`, no watchdog reset within the first minute.
