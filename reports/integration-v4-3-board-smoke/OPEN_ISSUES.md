# 未解决问题

板子已经上过了，下面按「板端仍未验证」「已修掉的」「仍存在的真实缺陷」「有意推迟」分类。

## 一、板端未验证（不是"通过"，是"没做"）

| # | 项目 | 为什么没做 | 需要什么 |
| --- | --- | --- | --- |
| U1 | 静态画面 60 秒不闪 | 需要人眼连续观察，串口与截图证明不了 | 人在板子前看 |
| U2 | 亮度滑块手感 | 没有在板上拖动过滑轨 | 拖控制中心亮度条 |
| U3 | 奖励弹窗点击 | 没有触发过奖励流程 | 打完一次 RPG 关卡 |
| U4 | 锁屏上滑解锁 | 没有主动锁屏再解锁 | 控制中心点锁屏 → 上滑 |
| U5 | 控制中心反复开关 20 次 | 只开关过几次 | 重复点 |
| U6 | 运行时热拔插 TF 卡 | 只在恢复模式/无卡构建里间接验证 | 运行中拔插 |
| U7 | 全屏连续滑动下的帧率 | 只测了静止与点按 | 拖动列表/滑动 |
| U8 | 真实时间与昼夜切换 | 无 RTC、无 SNTP，时钟是 demo 态 | 接 SNTP 或手动设定 |
| U9 | Wi-Fi | os 构建没编（`wifi=no-backend`） | 见 `WIFI_HOSTED_READINESS.md` |
| U10 | 音频输出 | 没有 PCM 源 | 放一段音频 |
| U11 | 六个包之外的可选资源缺失表现 | 卡上资源是齐的 | 手动删一个 `.lwp` 再启动 |

## 二、本轮已修掉的（有实证）

| # | 问题 | 修法 | 证据 |
| --- | --- | --- | --- |
| F1 | `lw_content_load` 用 32 KiB 栈缓冲 → 插卡启动 `Stack protection fault` panic | 改 `static` | 启动不再 panic |
| F2 | main 主栈 12,288 B 不够（插卡后多走内容加载与六包注册） | 提到 32,768 B | `boot: Little World ready` |
| F3 | SD 读卡 `allocate_dma_buf: not enough mem, err=0x101`，`resources: failures=198` | `lw_sd_mount_existing()` 预留 32 KiB DMA 缓冲挂到 `card->host.dma_aligned_buffer` | `reads=2104 failures=0` |
| F4 | 动画完全不动（帧索引恒为 0） | NVS 里 `settings.animations=0`；新增 `lw_os_animations_set()` 与 `anim on` 命令 | 帧索引 7/4 → 3/4，两角色不同步 |
| F5 | 开启动画后极度卡顿（tick 平均间隔 128 ms） | 缓存 4→16 槽、1.5→4 MiB | tick 33-37 ms，FPS 2→10 |
| F6 | 招手动画看不到 | GREET 1600 ms 与 16 帧×100 ms 刚好等长，改成 2600 ms（`:catalog` 与 `lw_app_pet.c` 同步） | 实测 16 帧走完约 2600 ms 后回 idle |
| F7 | 烧录脚本每次成功都报 FAIL | `$null = $proc.Handle` | 9 次烧录全部 EXIT 0 |
| F8 | TF 卡 exFAT 挂不上 + FatFs 长文件名为关 | `CONFIG_FATFS_LFN_HEAP=y`；新卡按 FAT32 重做 | `resources: verified, 7 theme(s)` |
| F9 | 截图工具周期性丢行 | 串口读改字节批量 + 1 MiB 缓冲 | 仍有少量丢失，见下 |

## 三、仍存在的真实缺陷（已定位，未修）

### 1. 截图流在无流控的 115200 上仍会丢少量行

`shot 2` 一次要发 3,001 行 base64（约 290 KB），串口没有硬件流控，连续满速发送时接收端会丢行。现在靠重试补（一次 pass 常缺 20-40 行，两三次能补齐），但偶尔补不齐。
**建议**：把控制台改成 `CONFIG_ESP_CONSOLE_UART_CUSTOM` + 921600（当前 `ESP_CONSOLE_UART_DEFAULT` 会把 `BAUDRATE` 强制回 115200，改 `sdkconfig.os` 无效，必须走 CUSTOM），或在协议里加窗口确认。

### 2. `lw_pack` 的命名空间上限是编译期常量

`LW_PACK_MAX_ACTORS` 5（male / qianqian / v42 / v42ui / v43skin）、`LW_PACK_MAX_SPECS` 64、`LW_PACK_MAX_FILES` 8。再加第 6 个视觉命名空间会静默失败（只进 `s_report`，界面少一块内容）。

### 3. 昼夜场景切换整块直读，阻塞约 60 ms

`lw_visual_assets.c` 的 `acquire()` 走 `fopen`+`fread` 整张 1024×600（1,228,800 B），不经 `lw_file_cache`。21 MB/s 下约 60 ms 的同步阻塞。**建议**：让场景也走 `lw_file_cache`，或预取下一相位。

### 4. PPA 硬件旋转未启用

BSP 把 `enable_ppa_accel` 写死 `false`（`esp32_p4_wifi6_touch_lcd_7b.c:567-574`），而 `lvgl_port_ppa_v9_init` 还要求 `LV_DRAW_SW_DRAW_UNIT_CNT==1`（当前是 2）。180° 旋转现在是 CPU 逐像素软拷贝，是渲染开销的主要来源之一。改动需要同时调 draw unit 数与 BSP 剖面，风险较高，本轮没做。

### 5. `max_gap` 有一次性 ~450 ms

`sched` 的 `max_gap` 一直停在 426-455 ms 且**不随采样窗口增长**，说明是启动阶段一次性的，不是运行中周期性卡顿。没做更细的分离（不知道具体落在 boot 动画还是首批资源加载）。

## 四、有意推迟的

- Wi-Fi Hosted：`os-wifi-experimental` 构建存在但从未在 C6 上跑过，C6 固件版本未知，不能更新。
- 音频输出链路：没有 PCM 源可测。
- legacy / noaudio / diag-a/b/c 五套配置本轮没重编，只有 os 有板端证据。
- Codex 侧的视觉打磨（Dock 文字拥挤）不在本轮范围。
