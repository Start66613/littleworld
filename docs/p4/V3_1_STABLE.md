# OS V3.1 稳定版交付说明

分支 `release/os-v3.1-stable`。目标只有一个：把 V3 从"能启动的演示"推进到"实板可用的稳定版"，
不再新增后端框架、测试桩和 demo 功能。

## 1. 这一版真正改了的东西

| 区域 | V3 现状 | V3.1 改动 |
|---|---|---|
| 角色资源 | 桌面显示紫色 TEST A 占位方块 | 真实像素角色（男孩 + 千千）嵌入固件，运行时由 `.lwfa` 资源包注册成 LVGL 图源 |
| 亮度 | `ledc` 抢 GPIO 32 失败，屏幕恒定 100% | 改用 BSP 的 `bsp_display_brightness_set()`，系统设置里的亮度滑杆才有意义 |
| 中文字体 | 18/22/28 只覆盖一部分字符串，曾出现方块 | 字体子集按源码全量重生成：18/22/28 各 665 个码位，44/88 各 100 个（数字 + 单位） |
| 44/88px 字体 | lv_font_conv 默认范围止于 0x7E，缺 `·` `×` | 显式字符集：可打印 ASCII + `· × ° ℃ …`，审计脚本按"数字用途"校验 |
| 验收证据 | 只有宿主 PNG，实板无证据链 | 板载 `shot` / `tap` / `status` 串口命令，LVGL 快照经 UART 以 base64 BMP 回传 |
| 资源包格式 | 无 | 32 字节定长记录 + RGB565+A8 帧数据，`tools/validate_character_pack.py` 独立校验 |

## 2. 角色资源链路（这是 V3.1 的核心）

- 素材：`assets/characters/male/male-anchor-aligned.png`（Codex 产出，`agent/codex-meowa-challenge`）
  与原始 DesktopPet 千千 idle 8 帧（`private/pixel-duo/qianqian/frames/idle`）。
- 打包：`tools/build_character_pack.py` 裁切、对齐、转 RGB565+A8，写出
  `firmware/main/art/characters.lwfa`（572,960 B，sha256 `05499d0c…`），由 CMake `EMBED_FILES` 编进 app 镜像。
- 格式（`LWFA0001`）：

  | 偏移 | 字段 |
  |---|---|
  | 0 | `char[8]` magic |
  | 8 | `u16` 版本 = 1 |
  | 10 | `u16` 片段数 |
  | 12 | `u32` 整个 blob 字节数 |
  | 16 起 | 每段 32 字节：`char[12]` 名称、`u16` 帧数、`u16` 宽、`u16` 高、`i16` 锚点 x、`i16` 锚点 y、`u8` 循环、`u8` 保留、`u16` 帧间隔 ms、`u16` 保留、`u32` 数据偏移 |
  | 数据 | 每帧先 RGB565（小端）后 A8 平面 |

- 加载：`lw_char_pack_register()` 校验 magic/版本/尺寸/锚点/区间，再注册成两个 LVGL 图源
  `male.idle`（108×212，1 帧）和 `qianqian.idle`（104×202，8 帧 ×125 ms），并把
  `lw_actor_art_t[2]` 交给 `lw_os_start()`；解析失败时返回 false，UI 回退占位图而不是崩溃。
- 尺度契约：脚下锚点落在 `y = h-3`，水平居中，1× 原生像素，不缩放。

> 本次自查发现并修掉一个会导致"上板还是占位图"的 bug：生成器按 28 字节写记录，却按 32 字节算偏移，
> 加载器按 32 字节解析。校验脚本把这个问题在刷机前抓了出来（`qian frames=202`、偏移越界），
> 修完重新打包后 1 000+ 项结构检查全部通过。

## 3. 板载验收命令

固件启动后 UART0 是 921600 的 `esp_console` REPL：

```
lw> shot        # LVGL 快照 -> 512x300 24bit BMP -> base64，约 7 s
lw> tap 512 96  # 合成一次按下/抬起，用来验证点击目标
lw> status      # 堆、PSRAM、显示尺寸、上一张快照尺寸
```

宿主侧：`tools/board_shot.ps1 -Port COM5 -Out reports/board/01-desktop.bmp`，
把 `LCAP BMP`/`LCAP DATA`/`LCAP END` 之间的 base64 还原成 BMP。

快照走 `lv_snapshot_take()`，与像素真正上屏的路径一致（RGB 面板没有可包装的 flush 回调，
`esp_lv_adapter_set_draw_bitmap_callbacks()` 在这种模式下不会被调用，所以没有走那条路）。

## 4. 已验证 / 未验证

已验证（可复现）：

- 宿主后端测试 `tools/run_backend_tests.ps1`：**188 项，0 失败**（store 29 + system 22 + net 137）。
- 宿主 UI 回归 `tools/run_windows_regression.ps1`：System 22、Duo 82、RPG 85、Storage 33、
  OS V2 340 项，全部 0 失败；31 张真实 LVGL 截图。
- 字体覆盖 `tools/audit_font_coverage.py`：18/22/28 全量覆盖，44/88 覆盖数字与单位。
- 资源包结构 `tools/validate_character_pack.py`：记录表、偏移、几何、锚点全部一致。
- 三套固件配置编译通过 + 镜像头校验（见下表）。

未验证（需要实板，刷机前必须说明）：

- 角色在实板上的实际观感（颜色、像素清晰度、千千 125 ms 动画节奏）。
- 亮度滑杆的真实效果（BSP API 已接，但未在板上看到亮度变化）。
- `shot` / `tap` 串口链路在真机上的稳定性与耗时。
- Wi-Fi / SNTP / 音频仍与此前一致：服务层已实现并测试，射频与出声未验证。

## 5. 镜像

| 配置 | 大小 | sha256 | app 分区占用 |
|---|---|---|---|
| os（默认，OS V3 桌面） | 2,248,768 B（2,196.1 KiB） | `72651da76ff55a51d6af757c069a941a0172e201024b2f1f1eab6fd4e6ff2c22` | 35.7% |
| legacy（旧 shell 回退） | 1,420,160 B（1,386.9 KiB） | `9df9b9c4419f4358a5ec25a195975f0a185496c20aa50e427f4f947aabd9578d` | 22.6% |
| noaudio（无音频编译） | 2,248,768 B（2,196.1 KiB） | `d91f84b0fc0b9c7f42c1f8bcddc3f450de063353054f1fb1f5571ee8739b76d5` | 35.7% |

三个镜像都通过 `tools/check_v3_images.ps1`（esp32p4 / rev 3.0-3.99 / 32MB / DIO，0 failure）。
app 分区 6 MiB = 6,291,456 B，os 与 noaudio 的镜像大小相同（音频代码被编译掉的部分被段对齐吸收），
但 sha256 不同，说明确实是两份不同的二进制。

链接证据（`firmware/build/os/little_world.map`）：

- `lw_char_pack_register` 0x480137ca、`lw_char_pack_art` 0x48013bfa、`lw_capture_console_start` 0x4800f1c2、
  `lw_services_poll` 0x48027c5a 都在 os 镜像里。
- 角色包 `LWFA0001` 魔数在 `little_world.bin` 偏移 25,444 处可见，即 572,960 B 真被编进 app 镜像。

## 6. 刷机前置条件

1. USB 串口重新出现在设备管理器（本轮刷机时 CH343 掉线，COM5 消失，未能完成实板验证）。
2. `tools/flash_v3.ps1 -Probe -Port COM5` 能读到 ESP32-P4 rev v3.2。
3. 确认不擦除 NVS（`0x9000` 不写），不更新 C6 固件。
