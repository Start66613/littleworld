# V4.3 联合集成与实机验收 —— 交付索引

分支 `integration/os-v4-3-board-smoke`，工作区 `D:\LittleWorld-V43-Board`。
本目录只记录**真实跑过**的东西；没上板的一律写 NOT TESTED。

## 一句话结论

**固件已经准备好刷写，TF 卡也已经准备好插上，但还没有在 P4 上跑过。**
App 镜像 2,352,784 B（`0114fe68…`，比上一版多了 FatFs 长文件名支持）已通过镜像校验、分区适配、ELF 符号审计与真实资源字符串检查；
TF 卡为 FAT32 / 59.45 GB，19 个资源文件（含私有千千基础包）全部 SHA-256 校验通过，见 `SD_DEPLOY.md`。
Windows 联合版是从本分支重新构建的，OS V2 752 项 + 模型/存储 222 项检查全绿，15 张真实帧缓冲截图在 `windows/`。
**唯一缺的一步是板子没插**：`flash_v4_safe.ps1` 的只读检查显示没有可用串口（COM7/COM8 是蓝牙），所以烧录与实板测试都是 NOT TESTED。

## 文档

| 文件 | 内容 |
| --- | --- |
| `MERGE_RECORD.md` | 基线、合并提交、四处冲突的逐段解决方式 |
| `BUILD_AND_ARTIFACTS.md` | 构建命令、日志、镜像 SHA-256、分区适配、ELF 符号证据 |
| `SD_CARD_PACKAGE.md` | TF 卡包构成、每个文件的 SHA-256、**要复制到哪里** |
| `SD_DEPLOY.md` | 本轮实际部署记录：FAT32 新卡、19 文件校验、exFAT/长文件名两个不兼容点 |
| `WINDOWS_JOINT.md` | 联合版截图与它们证明了什么（含已知缺陷） |
| `FLASH_RECORD.md` | 只读检查结果、写入计划、回滚步骤（未执行写入） |
| `BOARD_TEST.md` | 上板测试清单，全部 NOT TESTED，附可执行步骤 |
| `OPEN_ISSUES.md` | 未解决问题与已知缺陷 |

## 关键坐标

- 集成分支 HEAD：`a974bd5`（`041bf46` 之上的三次提交：`48ae519` 打开 FatFs 长文件名并部署 TF 卡、`dfeeed4` 附构建日志、`a974bd5` 按固件逻辑复核卡；`041bf46` 的父是 `e70042b` 合并提交，再父 `05bfc36` DSH 引擎 / `2900569` Codex 视觉）
- 固件：`firmware/build/os/little_world.bin`，2,352,784 B，sha256 `0114fe684c636d144a88b988ab3232a29d4ef7f87b1fb00d9d13be3d235c9f51`
- TF 卡（已部署）：`F:\littleworld\`，FAT32 / 59.45 GB，19 文件 / 75,074,074 B
- TF 卡包源：`dist/littleworld-v4-sd/`（公开包，20 个文件共 52,863,139 B，其中卡上 18 个）
- 私有包：`dist/littleworld-v4-sd-private/`（含千千基础包，**不提交、不发布**）
- Windows 联合版：`build/host/LittleWorld.exe`，1,401,344 B，sha256 `9ed1414dd822d016ac31b61fd979ed52aa24b38d6113e1f787503bddb5b1c55c`

## 插上板子之后的三步（已准备好，等硬件）

1. `powershell -File tools/flash_v4_safe.ps1 -ListPorts` → 确认出现的 COM 是 `VID_303A` / `1A86` / `10C4` / `0403` 之一，**不是**蓝牙的 BTHENUM。
2. `powershell -File tools/flash_v4_safe.ps1 -Probe -Port COMx` → 只读，读芯片型号/Revision/Flash/MAC，一个字都不写。
3. 把只读结果贴出来 → 确认无误后执行 `-Stage app -Execute -Authorized`（只写 0x10000，不碰 bootloader/分区表/NVS/C6）。

板子接哪个口：Waveshare ESP32-P4-WIFI6-Touch-LCD-7B 上标 **USB/UART** 的那个 Type-C（板载 CH343P 转串口），不是标 USB-OTG 的那个。插上后设备管理器里应出现 `USB-Enhanced-SERIAL CH343 (COMx)`，`GetPortNames()` 会多一个 COM。
