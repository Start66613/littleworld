# 实板测试结果（V4.3 联合版，ESP32-P4）

板子：Waveshare ESP32-P4-WIFI6-Touch-LCD-7B，1024×600，ESP32-P4 rev v3.2，MAC e8:f6:0a:ec:1e:44，32 MB Flash。
串口：COM5（USB-Enhanced-SERIAL CH343，VID_1A86&PID_55D3），115200。
镜像：`firmware/build/os/little_world.bin` 2,357,328 B，烧录走 App-only（0x10000），分区表与 bootloader 未动。
TF 卡：F: LITTLEWORLD FAT32，`/sdcard/littleworld/` 19 文件 75,074,074 B（含私有 `qianqian-base.lwp`）。

结果列只有三种值：**PASS**（亲眼/实测确认）、**PARTIAL**（部分确认，注明缺口）、**NOT TESTED**（没做）。

## A. 显示

| # | 检查 | 结果 | 证据 |
| --- | --- | --- | --- |
| A1 | 正常启动 | PASS | 多次冷启动与 RTS 复位后串口均到 `boot: Little World ready, free heap 23505935 B`，无重启循环，`recovery: boot 1, previous ended cleanly after 0 ms, streak 0, safe mode off` |
| A2 | 分辨率正确 | PASS | `status` → `display 1024x600`；截图 1024×600 全是有效像素，无黑边 |
| A3 | 背景完整 | PASS | 串口 `lw_res: resources: verified, 7 theme(s) available` + `art: card: 6/6 packs [card]`；截图里客厅场景铺满整屏（`board/21-desktop-now.png`） |
| A4 | 不花屏/蓝屏 | PASS | 修复前确实蓝屏重启循环（见 FLASH_RECORD 的三条 panic），修完 6 轮页面 soak 无 panic、无花屏 |
| A5 | 不明显闪烁 | NOT TESTED | 需要人眼在静态画面连续观察 60 秒；我只能证明截图内容稳定，证明不了"看不出闪" |
| A6 | 亮度变化 | NOT TESTED | 亮度滑块没有在板子上拖动验证过；串口只有启动时 `Setting LCD backlight: 100%` |

## B. 触摸

| # | 检查 | 结果 | 证据 |
| --- | --- | --- | --- |
| B1 | Dock 图标进页面 | PASS | 六个入口逐个点，`status` 依次回 `page 4/5/6/7/2/1`（相册/回忆/音乐/游戏/设置/世界），与 `lw_os.h:6` 的页面枚举一致 |
| B2 | 底部区域上滑展开导航 | PASS | `tap 512 575` → `dock open` |
| B3 | 4.5 秒后自动收起 | PASS | `dock open` 后 6 秒再读 → `dock closed` |
| B4 | 控制中心开关 | PARTIAL | 打开验证过（`tap 956 24` → `overlay 1`，九宫格面板截图正常）；"重复开关 20 次不卡"没有逐次计数做过 |
| B5 | 顶部状态条点击 | PASS | `tap 956 24` 命中 `OC_CONTROL`（`lw_os.c:189` 的 900,2,112,44 按钮），`overlay 1` |
| B6 | 设置页各分区 | PARTIAL | 设置页能进（`page 2`），日期与时间分区截图过；其余分区没有逐一点开 |
| B7 | 弹窗（奖励/信纸）能被点击 | NOT TESTED | 没有触发过奖励弹窗 |
| B8 | 锁屏上滑解锁 | NOT TESTED | 没有主动锁屏再解锁走一遍 |

## C. 美术

| # | 检查 | 结果 | 证据 |
| --- | --- | --- | --- |
| C1 | 男方正常显示 | PASS | `windows/board/10-idle-anim-a.png`、`11-idle-anim-b.png` 里男方为正式像素角色，不是占位图 |
| C2 | 千千正常显示 | PASS | 同上两张截图，两人姿势不同步（帧索引 7/4 → 3/4），说明各自在播 |
| C3 | 沙发坐姿正确 | PASS | `15-pet.png`、`18-pet-page-final.png`：双人坐在沙发坐垫上，脚踩地面，不悬空、不陷进去 |
| C4 | 小狗在狗窝里 | PASS | 同两张宠物页截图：小狗趴在窝里，窝、前缘、影子是一体的组合帧 |
| C5 | 时间与字体正确 | PASS | 桌面截图顶部 `19:30`，带"· 演示"类限定词；`diag` → `clock: 19:30 demo, not trusted, dusk, theme 2`。中文正常渲染，无方框 |
| C6 | 昼夜切换 | PARTIAL | 设置页五个时段预览按钮存在且可点（`v43-time-settings.png`）；真实时间切换无法验证（本板无 RTC，时钟是 demo 态） |
| C7 | UI 皮肤显示 | PASS | `12-control-center.png`：九宫格皮肤面板、亮度 72%/音量 40%、Wi-Fi 与音频标注"尚未接入" |

## D. 稳定性

| # | 检查 | 结果 | 证据 |
| --- | --- | --- | --- |
| D1 | 页面连续切换 30 次不崩 | PASS | 跑了 6 轮 × 8 次跳页 = 48 次切换，无 panic、无重启；结束时 `resources: reads=2104 failures=0` |
| D2 | 控制中心反复开关 20 次 | NOT TESTED | 只开关过几次 |
| D3 | 多次进出 RPG | PARTIAL | 进出过 3 次（`page 9` ↔ 其他页），正常；没到"多次"的强度 |
| D4 | TF 卡读取（拔插各一次） | PARTIAL | 插卡状态全程正常；拔卡只在恢复模式/无卡构建里间接验证过，没有在正常运行时热拔插 |
| D5 | PSRAM 余量 | PASS | `psram: free=21960980 largest=20971520`（约 21 MB 空闲，最大连续块 20 MB） |
| D6 | 内存峰值/最大连续块 | PASS | `heap: free=22017019 min=21141112 largest=34816`；修 SD DMA 前这里会掉到 `largest=30720` 并开始报 `err=0x101` |
| D7 | 启动原因与复位原因 | PASS | 正常启动 `reset_reason=1`（POWERON），复位后 `recovery: boot 1 … streak 0, safe mode off`，没有异常累积 |

## 本轮修掉的两个真实缺陷（都是只有插卡才暴露）

1. **main 任务栈溢出**：启动日志打印时 `Guru Meditation Error: Core 0 panic'ed (Stack protection fault)`，`Detected in task "main"` at `_vfprintf_r`。栈只有 12,288 B，插卡后多走 `lw_content_load` 与六个包的注册。提到 32,768 B 后消失。
2. **SD 读卡 DMA 内存不足**：`E sdmmc_cmd: allocate_dma_buf: not enough mem, err=0x101`，`resources: reads=4102 failures=198`。BSP 没给 host 配 `dma_aligned_buffer`，每次非对齐多块读都临时申请内部 DMA 内存。`lw_sd_mount_existing()` 现在预留 64/32/16/8 KiB 中能拿到的最大一块（实测拿到 32 KiB）挂到 `card->host.dma_aligned_buffer`，同一套操作变成 `reads=2104 failures=0`。

两个都进了 `e9e82b5`，已推送。

## 还没做的（不要当成通过）

- A5 闪烁、A6 亮度手感：必须人眼看。
- B7 奖励弹窗、B8 锁屏解锁、D2 控制中心 20 次：没有专门跑。
- D4 运行时热拔插 TF 卡。
- Wi-Fi：os 构建里 `wifi=no-backend`，`CONFIG_LW_NET_WIFI_ENABLE` 没开，本轮没有接。
- 音频输出：没有 PCM 源，`audio=codec` 只说明编解码器初始化了。
- 全屏连续滑动/拖拽下的帧率：只测了静止与点按，没有做持续滑动手势。

## 复现命令

```powershell
# 只读识别
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 -Probe -Port COM5
# 只写 app（0x10000），不碰 nvs/分区表/bootloader/C6
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 -Stage app -Port COM5 -Execute -Authorized
# 读状态（含 page/overlay/dock）
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\console_cmd.ps1' -Port COM5 -Command 'status' -Seconds 5"
# 一次会话内按序操作（避免每步重开端口丢状态）
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\console_seq.ps1' -Port COM5 -Steps @('tap 512 575|800','tap 308 541|900','status|600')"
# 截图（512×300，抓完用 PIL 放大到 1024×600 看）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\board_shot.ps1 -Port COM5 -Command "shot 2" -Out reports\board\x.bmp
```
