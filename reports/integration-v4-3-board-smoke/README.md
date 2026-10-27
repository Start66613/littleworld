# V4.3 联合集成与实机验收 —— 交付索引

分支 `integration/os-v4-3-board-smoke`，工作区 `D:\LittleWorld-V43-Board`。
本目录只记录**真实跑过**的东西；没上板的一律写 NOT TESTED。

## 一句话结论

**固件已经准备好刷写，但还没有在 P4 上跑过。**
App 镜像 2,347,472 B（`92c05c2d…`）已通过镜像校验、分区适配、ELF 符号审计与真实资源字符串检查；
Windows 联合版是从本分支重新构建的，OS V2 752 项 + 模型/存储 222 项检查全绿，15 张真实帧缓冲截图在 `windows/`。
**唯一缺的一步是板子没插**：`flash_v4_safe.ps1` 的只读检查显示当前 0 个串口，所以烧录与实板测试都是 NOT TESTED。

## 文档

| 文件 | 内容 |
| --- | --- |
| `MERGE_RECORD.md` | 基线、合并提交、四处冲突的逐段解决方式 |
| `BUILD_AND_ARTIFACTS.md` | 构建命令、日志、镜像 SHA-256、分区适配、ELF 符号证据 |
| `SD_CARD_PACKAGE.md` | TF 卡包构成、每个文件的 SHA-256、**要复制到哪里** |
| `WINDOWS_JOINT.md` | 联合版截图与它们证明了什么（含已知缺陷） |
| `FLASH_RECORD.md` | 只读检查结果、写入计划、回滚步骤（未执行写入） |
| `BOARD_TEST.md` | 上板测试清单，全部 NOT TESTED，附可执行步骤 |
| `OPEN_ISSUES.md` | 未解决问题与已知缺陷 |

## 关键坐标

- 集成分支 HEAD：`041bf46`（父 `e70042b` 合并提交，再父 `05bfc36` DSH 引擎 / `2900569` Codex 视觉）
- 固件：`firmware/build/os/little_world.bin`，2,347,472 B，sha256 `92c05c2d26922935178bb5996cefcca1649c464964a7b77fa4a2faba5da19036`
- TF 卡包：`dist/littleworld-v4-sd/`（公开包，20 个文件共 52,863,139 B，其中卡上 18 个）
- 私有包：`dist/littleworld-v4-sd-private/`（含千千基础包，**不提交、不发布**）
- Windows 联合版：`build/host/LittleWorld.exe`，1,401,344 B，sha256 `9ed1414dd822d016ac31b61fd979ed52aa24b38d6113e1f787503bddb5b1c55c`
