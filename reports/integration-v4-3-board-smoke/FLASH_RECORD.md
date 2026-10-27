# 烧录记录 —— 已实际写入（App-only）

**结论：已成功烧录，只写了 app 分区，其余一概没动。**

| 项目 | 实际值 |
| --- | --- |
| 板子 | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B，1024×600 |
| 端口 | COM5（USB-Enhanced-SERIAL CH343，VID_1A86&PID_55D3） |
| 芯片 | ESP32-P4 revision **v3.2**，MAC `e8:f6:0a:ec:1e:44` |
| Flash | 32 MB（Manufacturer 0xc8 Device 0x4019），esptool.py v4.12.0 |
| 方式 | `-Stage app`，只写 `0x00010000` |
| 最终镜像 | `firmware/build/os/little_world.bin` **2,357,328 B** |
| 写入结果 | `Wrote 2357328 bytes (1103848 compressed) at 0x00010000 in 30.4 seconds (effective 621.0 kbit/s)`，`Hash of data verified.`，`esptool exit=0 elapsed=41.4s` |
| 分区表 | sha256 `8404023636b162d4c94c1911a9ba1f152968769e86a683bbe2134e4fcf828fc6`，**与上次 V3.1 刷入的相同** → 兼容，无需改分区 |
| bootloader | 22,016 B sha256 `624d7f4b461a6d5dc05e3dfc30ab0152a241b63c692a49d84fca656121d240d1`，未写 |
| factory 占用 | 2,357,328 / 6,291,456 B（**37.5%**），剩 63% |

**没有做的事**：没有 `erase_flash`，没有擦 NVS（`0x9000..0xF000` 原样保留，旧存档与设置在重启后仍在），没有改分区表，没有动 `phy_init` / `storage`，没有更新 C6 固件，没有碰 eFuse / 安全启动 / Flash 加密，没有写整片备份。

## 本轮一共烧了几次

| # | 原因 | 镜像 |
| --- | --- | --- |
| 1 | 首次上板（带 FatFs 长文件名支持） | 2,352,784 B |
| 2 | 修 `lw_content.c` 的 32 KiB 栈缓冲引发的 panic | 2,352,784 B（同大小，栈帧消失、静态段增长） |
| 3 | 性能优化第一版（lease refill / label 去重 / 招手 2600 ms / FPS 统计） | 2,355,536 B |
| 4 | 加 `sched` 计时统计 | 2,356,064 B |
| 5 | 加 `anim` 帧索引打印 | 2,357,456 B |
| 6 | 缓存 16 槽 + 4 MiB 预算（修卡顿） | 2,356,928 B |
| 7 | `status` 加 page/overlay/dock | 2,356,976 B |
| 8 | SD DMA 预留缓冲（首版 64 KiB 拿不到） | 2,357,344 B |
| 9 | DMA 缓冲改自适应（拿到 32 KiB） | 2,357,328 B |

每次都是 App-only + 哈希校验，没有一次失败。

## 回滚

- 磁盘备份：`D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`（`p4-full-A.bin` / `p4-full-B.bin` 各 33,554,432 B + `flash_id.txt` + `manifest.json`）。
- 稳定固件：`D:\LittleWorld-V3\firmware\build\os\little_world.bin` 2,249,088 B sha256 `EF5202C965BC23DB2CF2D870ED60F29ABFA056F2B0F597C5E1AA94AB4CC66BE2`（V3.1，同分区表，直接 `-Stage app` 就能回去）。
- NVS 里是玩家存档（宠物、纪念日、RPG 进度），回滚固件不需要动它。

## 只读前置检查（烧录前做过）

| # | 项目 | 结果 |
| --- | --- | --- |
| 1 | COM 端口列表 | COM5 识别为 CH343；COM7/COM8 是蓝牙串行，工具拒绝自动误选 |
| 2 | 芯片识别 | `-Probe` → ESP32-P4 rev v3.2、32 MB |
| 3 | Flash 配置 | 镜像声明 32 MB DIO，与设备一致 |
| 4 | 分区兼容性 | 分区表 sha256 与历史刷入相同 |
| 5 | 备份可恢复 | 存在，见上 |
| 6 | 写入地址 | `0x002000` / `0x008000` / `0x010000`，本次只写 `0x010000` |
| 7 | 镜像 SHA-256 | 由 `tools/image_check.py` 校验，`3 image(s), 0 failure(s)` |
| 8 | 是否需要更新 TF 卡 | 已更新，见 `SD_DEPLOY.md` |

## 顺手修的工具 bug

`tools/flash_v4_safe.ps1` 之前每次成功烧录都报 FAIL：`Start-Process -PassThru` 之后没触碰 `$proc.Handle`，`WaitForExit()` 返回时 `$proc.ExitCode` 是空值，于是"esptool exit=" 空串被判成非 0。修法是写入前 `$null = $proc.Handle`。修完后 9 次烧录全部返回 `EXIT 0`。

## 已知现象（不是错误）

- `W (1244) ledc: GPIO 32 is not usable, maybe conflict with others`：背光 LEDC 的既有告警，V3.1 时代就有，亮度仍能设（启动时 `Setting LCD backlight: 100%`）。
- `W (1585) ldo: The voltage value 0 is out of the recommended range [500, 2700]`：BSP 的 LDO 配置告警，不影响 SD 挂载。
- `wifi=no-backend`：os 配置里没有编 Wi-Fi（`CONFIG_LW_NET_WIFI_ENABLE` 未开），属预期。
- `time=demo / demo 19:30`：无 RTC、无 SNTP，时钟是演示态。
