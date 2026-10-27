# V4.3 联合集成与实机验收 —— 交付索引

分支 `integration/os-v4-3-board-smoke`，工作区 `D:\LittleWorld-V43-Board`。
本目录只记录**真实跑过**的东西；没上板的一律写 NOT TESTED。

## 一句话结论

**固件已经在 ESP32-P4 真机上跑起来了，动画、翻页、宠物页、控制中心都实测通过；还剩闪烁/亮度手感、奖励弹窗、锁屏解锁几项必须人眼或未触发的用例。Wi-Fi 和音频仍是未接状态。**

- App 镜像 `firmware/build/os/little_world.bin` **2,357,328 B**，App-only 烧到 `0x10000`，9 次烧录全部 `Hash of data verified` + `esptool exit=0`。
- 板子 ESP32-P4 rev **v3.2**，32 MB Flash，MAC `e8:f6:0a:ec:1e:44`，串口 COM5（CH343）。
- TF 卡 F: LITTLEWORLD **FAT32**，`/sdcard/littleworld/` 19 文件 75,074,074 B（含私有 `qianqian-base.lwp`），按固件判定逻辑复核 `LW_RESOURCE_OK`。
- 板端启动：`resources: verified, 7 theme(s) available` → `art: card: 6/6 packs [card]` → `boot: Little World ready`，无 panic、无重启循环。
- Windows 联合版从本分支重建，OS V2 752 项 + 模型/存储 222 项检查全绿，15 张真实帧缓冲截图在 `windows/`，另有 12 张板端截图在 `windows/board/`。

## 板上实测修掉的两个真实缺陷

1. **插卡启动 panic**：`Stack protection fault` in `main` at `_vfprintf_r`。原因是 `lw_content_load` 在栈上开了 32 KiB 缓冲（已改 `static`），且 main 主栈只有 12,288 B（已提到 32,768 B）。无卡时走不到这段代码，所以只有真插卡才暴露。
2. **SD 读卡 DMA 内存不足**：`E sdmmc_cmd: allocate_dma_buf: not enough mem, err=0x101`，`resources: reads=4102 failures=198`。BSP 没配 `dma_aligned_buffer`，每次非对齐多块读都临时申请内部 DMA 内存。现在预留 32 KiB 常驻缓冲，同一套操作 `reads=2104 failures=0`。

另外还修了动画不播（NVS 里 `settings.animations=0`）与开动画后卡顿（缓存槽位与预算不足），帧率 2 → 10，详见 `PERFORMANCE.md`。

## 文档

| 文件 | 内容 |
| --- | --- |
| `MERGE_RECORD.md` | 基线、合并提交、四处冲突的逐段解决方式 |
| `BUILD_AND_ARTIFACTS.md` | 构建命令、日志、镜像 SHA-256、分区适配、ELF 符号证据 |
| `SD_CARD_PACKAGE.md` | TF 卡包构成、每个文件的 SHA-256、**要复制到哪里** |
| `SD_DEPLOY.md` | 实际部署记录：FAT32 新卡、19 文件校验、exFAT/长文件名两个不兼容点 |
| `WINDOWS_JOINT.md` | 联合版截图与它们证明了什么（含已知缺陷） |
| `FLASH_RECORD.md` | 9 次烧录的记录、只读前置检查、回滚步骤 |
| `BOARD_TEST.md` | **实板测试结果**：A 显示 / B 触摸 / C 美术 / D 稳定性，逐项 PASS/PARTIAL/NOT TESTED |
| `PERFORMANCE.md` | 动画不播与卡顿的根因、改动、前后对比 |
| `OPEN_ISSUES.md` | 仍未验证的、仍存在的缺陷、有意推迟的 |

## 插上板子之后的三步

1. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 -ListPorts`
   认 VID：开发板是 `VID_1A86`（CH343），COM7/COM8 是蓝牙串行，别选错。
2. `... -Probe -Port COM5` —— 只读，识别芯片/Flash/MAC，不写任何东西。
3. 确认无误后再 `... -Stage app -Port COM5 -Execute -Authorized`。

接线用标 **USB/UART** 的那个 Type-C（板载 CH343P），不是 USB-OTG 那个。
控制台 115200 8N1。

## 上板后可以直接用的命令

```powershell
# 状态（含 page / overlay / dock）
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\console_cmd.ps1' -Port COM5 -Command 'status' -Seconds 5"
# 诊断
... -Command 'diag' -Seconds 5
# 帧率 / 调度 / 动画
... -Command 'fps' -Seconds 3
... -Command 'sched' -Seconds 3
... -Command 'anim' -Seconds 3
# 动画开关（写进 NVS 存档）
... -Command 'anim on' -Seconds 3
# 一次会话内按序点按
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\console_seq.ps1' -Port COM5 -Steps @('tap 512 575|800','tap 308 541|900','status|600')"
```
