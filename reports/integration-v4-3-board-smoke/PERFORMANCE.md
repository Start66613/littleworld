# V4.3 实板性能：卡顿与动画不播的真实原因与修复

状态：**已在真机上修复并验证**（ESP32-P4 rev v3.2，MAC e8:f6:0a:ec:1e:44，COM5）。

## 一句话结论

用户看到的「卡炸了」和「打招呼动画看不到」是**两个不同的问题叠加**：

1. **动画根本没在播** —— NVS 里保存的设置是 `animations=0`。`update_actors()` 在动画关闭时对 idle 角色传入恒定的 `time=0`，所以帧索引永远是 0；同时 `lw_os_motion()` 对所有页面转场直接 return，界面「一点就跳」。这个状态**不在固件里，而在 NVS 里**，所以反复烧录都不会消失。
2. **真的卡** —— 帧缓存只有 4 个槽、总预算 1.5 MiB，而两个角色各 8 帧、每帧 196,608 B。缓存装不下一个完整动画周期，每一帧都退回 TF 卡同步读 196 KB（约 9 ms，且阻塞在 LVGL 线程上）。实测 10 秒静止桌面 `misses +143 / evictions +131`。

## 证据（全部来自真机串口）

### 修复前

```
anim: motion_mode=2 animations_enabled=0 raw_mode=0
anim 0: clip=male.idle frame=0 motion=0 animation_ms=5320 phase=0
anim 1: clip=qianqian.idle frame=0 motion=0 animation_ms=5320 phase=0
```

模型时钟在走（5320 ms），帧索引恒为 0。打开动画后：

```
sched: ticks=352 avg_gap=128 ms max_gap=662 ms (timer period 20 ms)
cache: hits=29 misses=809 evictions=741 resident=786432 budget=1572864
fps: 2
```

LVGL 定时器周期是 20 ms，实测平均间隔 128 ms、最差 662 ms —— 每一帧都在等卡。

### 修复后

```
anim: motion_mode=0 animations_enabled=1 raw_mode=0
anim 0: clip=male.idle frame=1 motion=0 animation_ms=87392 phase=0
anim 1: clip=qianqian.idle frame=3 motion=0 animation_ms=87392 phase=0
（2 秒后）
anim 0: clip=male.idle frame=1 motion=0 animation_ms=92952 phase=0
anim 1: clip=qianqian.idle frame=7 motion=0 animation_ms=92952 phase=0
```

```
sched: ticks=601 avg_gap=31 ms max_gap=441 ms (timer period 20 ms)
cache: hits=418 misses=136 evictions=97 resident=3145728 peak=3784704 budget=4194304
fps: 10
```

| 指标 | 修复前 | 修复后 |
|---|---|---|
| 角色帧索引 | 恒为 0 | 每 2 秒前进多帧，两角色各不相同 |
| 两次截图 SHA-256 | 完全相同 | 不同（`5620964D…` / `E4BBD32F…`） |
| LVGL tick 平均间隔 | 128 ms（最差 662 ms） | 31 ms（最差 441 ms） |
| 缓存命中/未命中（10 s） | hits 29 / misses 809 | hits 418 / misses 136 |
| 缓存驱逐 | 741 | 97 |
| FPS（静止桌面 5 s 窗口） | 2 | 10 |

`reports/integration-v4-3-board-smoke/windows/perf/01-anim-a.png` 与 `02-anim-b.png` 是连续两张真机截图，可直接看出两个角色的手臂姿势不同 —— 动画在播。

### 重启后仍然有效（关键验证）

因为失效状态原本存在 NVS 里，所以必须验证修复能跨重启存活。断电重启后立刻读：

```
anim: motion_mode=0 animations_enabled=1 raw_mode=0
anim 0: clip=male.idle frame=4 motion=0 animation_ms=28439 phase=0
anim 1: clip=qianqian.idle frame=3 motion=0 animation_ms=28439 phase=0
sched: ticks=222 avg_gap=36 ms max_gap=434 ms (timer period 20 ms)
fps: 15
cache: open hits=2400 misses=901 evictions=731 drops=0 resident=3145728 peak=3784704 budget=4194304
animation: sprites=2 running=2
```

`animations_enabled=1` 没有再退回 0 —— `anim on` 已经写进存档，不是只在 RAM 里生效。

### 招手动作实测时间线

点「见面打招呼」后连续采样，动作确实完整播放：

```
anim 0: clip=male.wave frame=1  motion=3 animation_ms=120   phase=3
anim 0: clip=male.wave frame=7  motion=3 animation_ms=720   phase=3
anim 0: clip=male.wave frame=13 motion=3 animation_ms=1320  phase=3
anim 0: clip=male.wave frame=15 motion=3 animation_ms=2260  phase=3
anim 0: clip=male.idle frame=4  motion=0 animation_ms=400   phase=0
```

帧 1 → 7 → 13 → 15 走完 16 帧，约 2600 ms 后回到 idle。100 ms 的采样间隔能稳定看到不同的帧号，说明抖动已经降到可以被观察的程度。

## 改了哪些代码

| 文件 | 改动 | 为什么 |
|---|---|---|
| `firmware/components/lw_storage/src/lw_storage.c` | `ENTRIES` 4 → 16 | 4 个槽装不下一个 8 帧周期；字节预算仍由 `lw_art.c` 控制，槽位数变多不会超额分配内存 |
| `firmware/main/lw_art.c` | `LW_ART_CACHE_BUDGET` 1536 KiB → 4 MiB | 两角色各 8 帧共 3,145,728 B，1.5 MiB 只够一个角色，必然互相驱逐 |
| `firmware/components/lw_ui/src/lw_assets.c` + `include/lw_assets.h` | 新增 `lw_asset_refill()`，lease 记住自己的 `spec` | 同一资产换帧不再 `lv_malloc`/`lv_free` 一个 196 KB 缓冲并 `lv_image_cache_drop`，改成原地重读像素 |
| `firmware/components/lw_ui/src/lw_sprite.c` | `present()` 同资产换帧走 refill + `lv_obj_invalidate` | 上面那条的调用点；`lv_image_set_src()` 第一行就 `lv_obj_invalidate`，所以换帧重绘无法避免，但可以避免重新分配 |
| `firmware/components/lw_ui/src/lw_os.c` | `refresh()` 改成「值变了才写」；`lw_ambience_refresh()` 只在昼夜相位变化时调用 | `lv_label_set_text_fmt()` 无条件 `lv_free` + 重排 + `lv_obj_invalidate`（`lv_label.c:139-172`、`:1059-1073`），每秒 5-6 个 label 会让整页重绘；`lw_ambience_refresh()` 每次 `set_style_bg_color`+`set_style_bg_opa`+`lv_obj_invalidate` 整块 1024×600 |
| `firmware/components/lw_duo/src/lw_duo.c` | `LW_DUO_GREET` 1600 ms → 2600 ms | wave clip 是 16 帧 × 100 ms = 1600 ms，原来的 1600 ms 窗口刚好在最后一帧结束，本来就看不清 |
| `firmware/components/lw_ui/src/lw_app_pet.c` | `LW_PET_WAVE_MS` 2600 ms（新增常量） | 宠物页「沙发上打招呼」原来同样是 1600 ms 窗口 |
| `firmware/components/lw_ui/src/lw_motion.c` + `include/lw_os_design.h` | 新增 `lw_os_animations_enabled()`、`lw_os_motion_mode_raw()`、`lw_os_animations_set(bool)` | `lw_os_motion_get()` 把「存档开关关闭」和「会话模式关闭」压成一个值，两者的修法完全不同；`lw_os_animations_set()` 提供一个不改 NVS 整区的修复入口 |
| `firmware/main/lw_capture.c` | 新增 `anim` / `sched` / `fps` 控制台命令 | 没有可量化的手段就只能猜；这三条命令是上面所有数字的来源 |
| `firmware/main/app_main.c` | 启动时 `esp_lv_adapter_fps_stats_enable(display,true)` | FPS 统计编译开关打开后需要显式启用；开销 <2 µs/帧（适配层 Kconfig 自己注明） |
| `firmware/main/CMakeLists.txt` | REQUIRES 加 `espressif__esp_lvgl_adapter` | 组件名必须是 `espressif__esp_lvgl_adapter`，写 `esp_lv_adapter` 会找不到 |
| `firmware/sdkconfig.os` | `CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS=y` | 否则 `fps` 命令只能打印 not compiled in |
| `tools/flash_v4_safe.ps1` | 写入前 `$null = $proc.Handle` | PowerShell 5.1 下不先碰 `.Handle` 时 `WaitForExit()` 后 `$proc.ExitCode` 为空，会把 esptool 已用哈希校验过的成功写入误报为失败（之前每次成功烧录都以 exit 3 结束） |

## 关于 FPS 数字怎么读

适配层的计数点只在 **flush 完成**时累加（`display_manager.c:77-86` 要求 `lv_display_flush_is_last(disp)`），每秒窗口算一次 `current_fps`。所以：

- 静止、没有任何重绘的界面：`fps: 0` —— 这**不是**卡，是没有新帧。
- 有动画在播：本机实测稳定在 10 左右。
- 这个数字衡量的是「每秒有多少完整帧被提交」，不是 LVGL 的渲染耗时。要看耗时请看 `sched` 的 `avg_gap`/`max_gap`。

## 仍然没解决的问题（不谎报）

1. **`max_gap` 仍有 441 ms**。平均值已经从 128 ms 降到 31 ms，但偶发的长间隔还在。下一次要查的是 `lw_visual_assets.c` 的整场景直读（1,228,800 B，不经缓存）与页面切换时的重建。
2. **昼夜场景切换仍会阻塞约 60 ms**（1.2 MB 整块 `fopen`/`fread`，`lw_visual_assets.c` 完全没有走 `lw_file_cache`），只是每秒只发生一次且只在相位变化时触发。
3. **PPA 硬件旋转未启用**。BSP 把 `enable_ppa_accel` 写死 false，且适配层要求 `LV_DRAW_SW_DRAW_UNIT_CNT == 1` 才能开 PPA（当前是 2）。180° 旋转目前是 CPU 逐像素软拷贝（`display_bridge_common.c:249` 的 ROTATE_180 分支）。这是已知的、尚未动的性能项。
4. **Wi-Fi 仍未接**（`wifi=no-backend`），与本轮性能工作无关，保持实验状态。
5. 本轮全部是**真机验证**，但只在 `os` 配置上验证过；`legacy` / `noaudio` / `diag-*` 未重新构建。

## 复现命令

```powershell
# 读动画状态（motion_mode / animations_enabled / 帧索引）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\console_cmd.ps1 -Port COM5 -Command "anim" -Seconds 5
# 把动画开关打开并存档（NVS 里的旧值只有这样才能清掉）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\console_cmd.ps1 -Port COM5 -Command "anim on" -Seconds 5
# LVGL 定时器真实间隔
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\console_cmd.ps1 -Port COM5 -Command "sched reset" -Seconds 4
# 帧率（需要界面正在动才有意义）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\console_cmd.ps1 -Port COM5 -Command "fps reset" -Seconds 4
```

## 镜像

- `firmware/build/os/little_world.bin` 2,356,928 B（0x23F6C0），factory 6,291,456 B 占用 37.5%
- 构建日志 `reports/logs/v43-board-os-perf6.log`
- 前一个中间镜像（只改缓存槽位、未改预算）2,356,800 B，日志 `reports/logs/v43-board-os-perf5.log`
