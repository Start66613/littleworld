# Little World V3.2 — 明早板端测试单

> 这份测试单只写到"人手做什么、看什么、记什么"。所有判断标准在
> `DISPLAY_FLICKER_ANALYSIS.md` 第 4 节（判据矩阵）里，这里只做执行。
> 今晚没有任何镜像被烧写，也没有上电；下面 5 个镜像都是本轮离线编译产物，
> 其中 **只有 os-offline 是可用于日常的**，其余四个都是一次性对比镜像。

## 0. 准备（5 分钟）

```powershell
# 板子插上 USB（CH343，通常 COM5），先只读探一下，不写 flash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\LittleWorld-V32\tools\flash_v3.ps1 -Port COM5 -Probe
```

确认打印出 `ESP32-P4` / `revision v3.2` / `MAC` 之后再看下一步。
如果端口号变了，以 `-Probe` 输出为准。

本轮镜像（全部在 `D:\LittleWorld-V32\firmware\build\`）：

| 配置 | 路径 | 字节 | SHA-256 前 12 位 | 用途 |
| --- | --- | --- | --- | --- |
| os-offline | `os/little_world.bin` | 2,248,992 | `b60158420a85` | 日常/最终候选 |
| diag-a | `diag-a/little_world.bin` | 2,248,992 | `8af55911024b` | 频闪对照组 A |
| diag-b | `diag-b/little_world.bin` | 2,249,072 | `1e3d20640908` | 频闪对照组 B |
| diag-c | `diag-c/little_world.bin` | 2,245,472 | `f5e7ef859345` | 频闪对照组 C |
| os-wifi-experimental | `wifi/little_world.bin` | 2,558,064 | `d25afda0f4b8` | Wi-Fi 冒烟，**不日常** |

## 1. 频闪对比（P0，先做这个）

**目的**：把"频闪"拆成三个可判定的问题——是不是背光 PWM 在写、是不是局部刷新撕裂、
是不是整体负载/供电。

**顺序**：A → B → C。**每一步之间必须完整断电再上电**，不要用复位键，避免旧的
LEDC 配置残留。

对每个镜像只做三件事，各 30 秒：

1. **静置**：桌面不动，看 10 秒。记录：有没有闪、闪的节奏（每秒几次？）、
   闪的是整屏还是一块区域、闪的时候画面有没有内容变化。
2. **拖动亮度滑条**：从 10% 慢慢拖到 100% 再拖回来，拖 5 秒。
   记录：拖动过程中闪不闪，停下来之后还闪不闪。
3. **切页**：控制中心 → 设置 → 主题 → 回到桌面，每页停 2 秒。
   记录：哪一页闪得最明显。

判定表（摘自分析文档第 4 节）：

| A | B | C | 结论 | 下一步 |
| --- | --- | --- | --- | --- |
| 闪 | 不闪 | 不闪 | 背光占空比写入（C1/C2） | 把亮度改成吸收式写入 + 只在松手时写 |
| 闪 | 闪 | 不闪 | 局部刷新/绘制负载（C4） | 调 buffer_height 与刷新策略 |
| 闪 | 闪 | 闪 | 面板/时序/供电（C3/C5/C6） | 记录并考虑 DSI 时序或硬件排查 |
| 不闪 | 不闪 | 不闪 | 与负载或操作强相关 | 需要你重现时的具体操作步骤 |

**要在纸上记的原始现象**，别只写"闪"：闪动的频率（用手机慢动作视频录 3 秒最好）、
颜色（整屏发白/发灰/纯黑一帧）、是否有横条、是否随内容变化。

## 2. Wi-Fi 冒烟（P1，只在 os-wifi-experimental 上做）

前提：**先确认 C6 的存在**。这一步必须有人看着，因为它可能卡住或反复重启。

1. 烧 `wifi/little_world.bin`，用 `tools/serial_capture.ps1 -Port COM5 -Seconds 30` 抓日志。
2. 在日志里找这三行（顺序即预期）：
   - `esp_hosted: ...` 初始化成功，没有 `transport` 超时
   - `lw_net: wifi backend ready` 之类（如果没有这行，说明 `esp_wifi_start` 没成功）
   - 没有 `Guru Meditation` / `panic` / 反复重启
3. 进入 设置 → Wi-Fi：
   - 能扫描并且真的列出你家的 AP → 记为 **scan OK**
   - 能连上并拿到 IP → 记为 **connect OK**（这一步需要你输入真实密码，
     密码只在 RAM 里，断开即擦除；本轮代码不做持久化）
   - 连接失败时要看提示的错误是不是有意义的（`AUTH_FAILED` / `NO_AP` / `TIMEOUT`），
     **不能出现"连接成功"但其实没有 IP**。
4. 如果 esp_hosted 初始化就失败：**记录下来，不要更新 C6 固件**，也不要改 SDIO 引脚。
   那是下一步要和你一起决定的事。

判定：
- 扫描能用 → Wi-Fi 从"模拟"升级为"真机可扫描"，但**仍然不能叫"Wi-Fi 可用"**。
- 连上且有 IP → 才能写"Wi-Fi 真实可用"。
- 失败必须有明确的错误码，不允许静默。

## 3. 时间同步（P1，依赖第 2 步）

只有在 Wi-Fi 真的连上之后才有意义。看：
- 顶部时钟从 DEMO 时间跳到真实时间（说明 SNTP 成功）
- 断网后时间变成 STALE 而不是归零
- 设备重启后时间不会回到 2026 演示时钟（如果 RTC 可用）

注意：本轮 SNTP 适配层仍是占位实现（只检查系统时间 > 1.7e9），真机上很可能
表现为"看起来同步了但其实没等到应答"。这一步的结论请写成"显示正确/未验证/失败"，
不要写成"已修复"。

## 4. 音量与音频（P1）

ES8311 编解码器在之前的每次启动日志里都被探到了，但**没有 PCM 源**。
所以这一节只验证一件事：滑块能改、值能存、重启后还在，播放按钮不要假装在播。

## 5. 内存与稳定性（P0，日常镜像）

烧 os-offline，抓 10 分钟串口日志，看：
- `lw_diag` 行的 `heap free` / `min free` / `largest` 是否稳定（不掉 100 KB 以上）
- `psram free` 是否稳定
- 有没有 `Guru Meditation` / `Task stack overflow`

然后连续操作 20 分钟（切页、开关应用、进出设置），再看一次。关注 handles 是否
单调增长。

## 6. 回滚预案

任何一步出问题，回到上一个已知可用镜像：

```powershell
# 上一个已知可用镜像：V3.1 稳定版（release/os-v3.1-stable, version 7fbf877）
# D:\LittleWorld-V3\firmware\build\os\little_world.bin
# sha256 ef5202c965bc23db2cf2d870ed60f29abfa056f2b0f597c5e1aa94ab4cc66be2
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\LittleWorld-V32\tools\flash_v3.ps1 -Port COM5 -Bin D:\LittleWorld-V3\firmware\build\os\little_world.bin -Authorized
```

另外完整 flash 备份在 `D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`，
**NVS（0x9000）本轮任何脚本都不会碰**，存档不会丢。

## 7. 回来后告诉我什么

最少这四句：
1. 频闪在 A/B/C 三个镜像上分别是什么表现（一张表填完就行）。
2. 用手机慢动作拍一段能拍到闪的画面，比任何文字都有用。
3. Wi-Fi 扫描/连接分别到哪一步，失败时的错误码。
4. 你觉得最影响使用的那个现象。
