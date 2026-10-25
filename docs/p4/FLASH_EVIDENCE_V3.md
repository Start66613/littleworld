# OS V3 + DSH 合并固件刷机记录（2026-10-25）

本文件记录 `integration/os-v3-p4-combined` 在真实 ESP32-P4 板上的第一次写入与启动证据。
未执行 erase_flash，未写 nvs 分区，未更新 C6 固件。

## 1. 刷机参数

| 项 | 值 |
| --- | --- |
| 时间 | 2026-10-25 23:57 左右（本机时区 Asia/Shanghai） |
| 端口 | COM5（CH343，115200/460800） |
| 芯片 | ESP32-P4 rev v3.2，MAC `e8:f6:0a:ec:1e:44` |
| Flash | 32 MB（Manufacturer c8, Device 4019） |
| 脚本 | `tools/flash_v3.ps1 -Authorized -Port COM5`（先跑 `image_check.py --expect-rev 302`，PASS 才写） |

写入内容：

| 偏移 | 文件 | 大小 | SHA-256 |
| --- | --- | --- | --- |
| 0x2000 | bootloader/bootloader.bin | 22,016 B | `dc5fb8b504cf19befc15a7a650a5889a2381dbd98ead94681d81863a5f7bb87d` |
| 0x8000 | partition_table/partition-table.bin | 3,072 B | `8404023636b162d4c94c1911a9ba1f152968769e86a683bbe2134e4fcf828fc6` |
| 0x10000 | little_world.bin | 1,598,656 B | `0750cf158fa6f09f312a61300a1dab23ef0de496de14af1ac0dbc5017a674993` |

日志尾：`Wrote 1598656 bytes (868944 compressed) at 0x00010000 in 21.0 seconds` / `Hash of data verified.` / `Hard resetting via RTS pin...`，esptool 退出码 0。

## 2. 启动日志（复位后完整一轮，无 panic）

```
ESP-ROM:esp32p4-eco7-20260109
rst:0x1 (POWERON),boot:0xf (SPI_FAST_FLASH_BOOT)
I esp_psram: Found 32MB PSRAM device / Speed: 200MHz
I app_init: Project name: little_world
I app_init: App version: 337f7f1-dirty
I app_init: ESP-IDF: v5.5.5
I efuse_init: Chip rev: v3.2
I (1098) main_task: Calling app_main()
I (1101) littleworld: boot: reset reason 1, free heap 32464927 B
W (1113) ledc: GPIO 32 is not usable, maybe conflict with others
I (1128) ESP32_P4_EV: Install EK79007 LCD control panel
I (1306) ESP32_P4_EV: Display initialized
I (1322) ESP32_P4_EV: Touch 0x5d found
I (1333) GT911: TouchPad_ID:0x39,0x31,0x31
I (1350) esp_lvgl:touch: Touch input device registered successfully
I (1358) esp_lvgl:adapter: LVGL task started successfully
I (1363) ESP32_P4_EV: Setting LCD backlight: 100%
I (1370) lw_nvs: NVS ready
W (1370) lw_hw: brightness: LEDC polarity unverified, keeping the boot default
I (1400) ES8311: Work in Slave mode
W (1403) littleworld: prefs: missing, using defaults
I (1403) littleworld: connectivity: wifi=no-backend time=demo audio=codec
W (1409) littleworld: brightness: backend reports Unsupported
I (1414) littleworld: network: ESP32-C6 link is not integrated in this build; Wi-Fi reported Unsupported
I (1424) littleworld: time: no RTC and no SNTP session; demo clock shown
I (1430) lw_diag: reset_reason=1 boot_ms=391
I (1447) littleworld: capabilities: brightness=0 volume=2 wifi=0 audio=0 storage=0
I (1470) littleworld: boot: Little World ready, free heap 28600775 B
I (1470) main_task: Returned from app_main()
```

`lw_diag` 汇总另见日志：`heap free=28619051 min=28617196 largest=208896`、`psram free=28366676`、`nvs=ready`。

能力码：0=UNAVAILABLE，1=SIMULATED，2=READY。即亮度不可用、音量可用、Wi-Fi 不可用、音频后端不可用、存储不可用——与代码里 `lw_net` 未接 C6、音频无 PCM 源、无 TF 卡的实际情况一致，UI 不应显示为可用。

## 3. 旧存档未丢

刷机后只读回读 `0x9000 +0x6000`（`reports/logs/nvs-0x9000.bin`），仍能找到 `RVWL`（桌宠 72 B 存档）与 `lwo2` 记录；分区表重写但 nvs 偏移与长度不变，因此桌宠数值、偏好、RPG 存档都还在。

NVS 里 `RVWL` 条目数量较多（约 110 次历史写入），写入全部由交互触发（`lw_app_state_save()` 只在设置/桌宠/回忆/音乐/街机事件里调用），没有周期写。但长期交互仍会消耗 NVS 寿命，后续可以合并写入或加节流。

## 4. 需要人眼确认的部分

日志只能证明系统起来了，不能证明画面正确。需要现场确认：

1. 桌面是否显示 OS V3 新版界面（非旧版四宫格）；
2. 触控方向是否正确（早期 180° 镜像问题，已在上一轮改成 mirror 全 0）；
3. Wi-Fi / 相册 / 音乐入口是否显示"不可用"而不是假装可用；
4. 桌宠数值是否延续刷机前的存档（不是归零）。

## 5. 已知与本轮无关的日志噪声

- `ledc: GPIO 32 is not usable`：背光引脚无法被 LEDC 占用，亮度控制无效，屏幕固定 100%，与 P4 轮结论一致；
- `prefs: missing, using defaults`：24 B 偏好记录首次写入前为空，属正常；
- `app version: 337f7f1-dirty`：镜像在合并提交之后、提交 `ba94bbb` 时编译；`git diff ba94bbb 8a404cd -- firmware host tools tests` 为空，说明镜像里的源码与 `ba94bbb` 一致，横幅的 `-dirty` 只来自当时未提交的文档与预览图。
