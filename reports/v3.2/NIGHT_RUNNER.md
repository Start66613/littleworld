# Night Runner 交付说明

生成于 2026-10-26，补的是上一轮漏掉的「整夜自动执行机制」那半段。

## 1. 先说结论：这套机制到底是哪一类自动化

任务原文要求先确认 DSH 有没有真正的自主调度能力，**不许假设**。我实测后的结论：

| 能力 | 有 / 无 | 依据 |
| --- | --- | --- |
| 原生计划任务（Agent 自己到点醒来） | **无** | 当前会话没有"延时唤醒自己"的能力，我不能在无人对话时被唤醒 |
| 后台 Agent 进程常驻 | **无** | 会话结束后没有进程替我继续思考 |
| 无人值守 CLI（可被脚本唤起继续编码） | **未验证，不使用** | 即使有，也需要凭据与认证方式确认；任务原文明确禁止把 API Key 写进脚本、禁止浏览器自动登录，所以这条不采用 |
| Windows 计划任务跑已验证脚本 | **有，已落地并验证** | 见下文第 4 节，有真实执行证据 |

所以本轮用的是任务原文给的 fallback：**A（当前会话连续执行）跑完全部开发任务 + B（Windows 计划任务跑已验证的离线脚本）+ C（`night_plan.json` 作为可恢复状态文件）**。

三类任务必须分开描述，不能混：

- **A. 已经是我做的（Agent 可继续开发）**：频闪分析、Wi-Fi Hosted 集成、源码修复、构建矩阵、测试、报告、提交。这些是"我"做的，不是计划任务做的。
- **B. 只是普通定时脚本**：`tools/nightly/lw_night_task.ps1` 到点启动一个**已经验证过**的离线命令（主机测试、镜像头校验、构建矩阵）。它不会思考、不会改代码、不会决定下一步，跑完自删。**不能说成 AI 自主运行。**
- **C. 只能等下次启动 DSH**：需要判断力的事，例如"看到频闪现象后决定改哪一层"、"读完板端日志决定回滚还是继续"。这些写进 `night_plan.json` 的 `kind: "agent"` 任务，我下次进来先读它接着干。

## 2. 文件清单

```
tools/nightly/
  night_plan.json         任务队列 + 依赖 + 状态（唯一真相来源）
  night_plan.schema.md    字段说明与状态语义
  lw_night_lib.ps1        共用库：计划读写、黑名单、锁、超时、git 快照
  lw_night_runner.ps1     入口：Status/RunNext/RunDue/Resume/Install/Verify/Stop/Uninstall/SelfTest
  lw_night_task.ps1       由计划任务调用的单次入口（含 -VerifyMode）
  lw_night_selftest.ps1   调度器自检（13 项）
  night_status.ps1        人读进度
  stop_nightly.ps1        安全停止 + 注销计划任务
  resume_nightly.ps1      断点恢复
  README.md               运行与停止说明
logs/nightly/             运行日志、任务级 jsonl、锁、自检结果
reports/nightly/          每次运行的 summary-<时间>.json
```

## 3. 十条约束的落地证据

| # | 约束 | 落地 | 自检覆盖 |
| --- | --- | --- | --- |
| 1 | 唯一任务 ID | `Save-NightPlan` 前不查重，但 `Update-TaskStatus` 遇到未知 id 抛错；自检检查全表无重复 | ✅ `unique task ids` |
| 2 | 记录开始/结束/结果/产物 | `last_run{started,ended,result,note,attempts}` + `logs/nightly/tasks-<date>.jsonl` + 产物存在性检查 | ✅ 间接 |
| 3 | 失败不无限重试 | `attempts >= max_attempts` 即不再调度 | ✅ `attempt cap stops retries` |
| 4 | 每阶段超时 | `timeout_sec` + `WaitForExit` 墙钟看门狗 + `taskkill /T /F` 杀进程树 | ✅ `timeout enforced`（3 s 超时 3.5 s 返回） |
| 5 | 同 build 目录不并发 | `lock_key` 互斥文件含 pid，活进程持有则拒绝；死 pid 视为陈旧锁回收 | ✅ `second lock refused` / `stale lock reclaimed` |
| 6 | 异常退出不破坏工作树 | 运行前后 `git status --porcelain` + HEAD 快照，不一致就记 `dirty` 点名，**不自动修** | ✅ `git drift detected` |
| 7 | 不执行 `git reset --hard` / `git clean -fdx` | 黑名单直接拒绝，脚本里根本没有这两个命令 | ✅ `destructive commands refused` |
| 8 | 不自动启动烧录脚本 | 黑名单：`flash_v3.ps1`、`serial_capture.ps1`、`board_shot.ps1`、`esptool`、`idf.py ... flash`、`erase_flash`、`-Authorized` | ✅ 6 条真实危险命令全部拒绝 |
| 9 | 不递归创建无穷任务 | 没有运行时新增任务的 API；`Install` 只注册一条固定命令；`schtasks /create`、`Register-ScheduledTask` 在黑名单里 | ✅ 同上 |
| 10 | 一次性执行 + 到期自动清理 | `-Once` 触发器 + `-DeleteExpiredTaskAfter` + 任务体自己 `Unregister-ScheduledTask`；已实测自删 | ✅ 第 4 节实测 |

## 4. 计划任务真实执行的证据（不是"已安排"）

名字：`LittleWorld-Nightly-Run`（前缀统一 `LittleWorld-Nightly-`，任务计划程序里可见可结束）。

验证方式：注册 → `Start-ScheduledTask` 立即触发 → 等待任务自己写出 marker → 比对 marker 的 pid 与本进程 pid 不同。

真实输出：

```
night task start pid=22592 verify=True
registered LittleWorld-Nightly-Run for verification
marker pid=59576 at=2026-10-26T11:56:23.4551355+08:00
self-deleted scheduled task (finished run)
NIGHTLY SCHEDULED RUN CONFIRMED
```

被启动的那个进程自己的日志（`logs/nightly/scheduled-20261026-115623.log`）：

```
[11:56:23] night task start pid=59576 verify=False
[11:56:23] marker written
[11:56:23] no due task; exiting
[11:56:23] summary D:\LittleWorld-V32\reports\nightly\summary-20261026-115623.json
[11:56:25] self-deleted scheduled task LittleWorld-Nightly-Run
```

关键点：pid 59576 ≠ 22592，说明**执行者是计划任务本身，不是我在当前进程里跑了一遍**。跑完自删，`Get-ScheduledTask -TaskName 'LittleWorld-Nightly*'` 返回空，系统里没有残留。

第一次尝试失败过，原因值得记录：`New-ScheduledTaskTrigger -Once` 生成的 XML 在部分 Windows 构建上缺 `EndBoundary`，`Register-ScheduledTask` 直接报 `0x80041319 The task XML is missing a required element`。加上显式 `EndBoundary` 后正常——这也顺带满足了第 10 条"自动过期"。

## 5. 本轮夜间时间线的实际执行

`night_plan.json` 里 9 个任务，7 个是我在会话里做完的（`kind: agent` 3 个 + 脚本任务的实际执行由我驱动），1 个是调度器自检，1 个是计划任务验证。

| 阶段 | 任务 | 结果 |
| --- | --- | --- |
| T0 | 工程审查与基线 | done，哈希与清单落在 `reports/v3.2` |
| T+60 | 主机回归（含压力套件） | done，29+22+188+2035 checks, 0 failures |
| T+60+15 | 诊断镜像重编 + 头校验 | done，2,248,992 / 2,249,072 / 2,245,472 B |
| T+150 | 刷机链路复盘 | done，串口全程未打开 |
| T+240 | Wi-Fi 实验镜像 | done，2,558,064 B，符号已核对 |
| T+330 | 全量矩阵 | done，五份镜像全 PASS |
| T+420 | 汇总与提交 | done，`006c974` 已推送 |
| T+425 | Night Runner 自检 | done，13 checks 0 failures |
| T+430 | 计划任务执行验证 | done，见第 4 节 |

时间线是检查点不是空等：上一阶段做完就直接推进下一阶段，没有为了"等 T+240"浪费任何时间。

## 6. 这套机制做不了什么（必须说清楚）

1. 不能在没人操作时**自己判断现象**。频闪到底是不是 PWM、明早板子该刷哪个镜像，这类判断只能我在会话里做。
2. 不能操作板子。所有串口、烧写、C6 操作都在黑名单里，夜里永远不执行。
3. 不能在我没启动 DSH 的情况下继续改代码。`night_plan.json` 里 `kind: agent` 的未完成任务，只能等你下次开 DSH 后由我接手。
4. 计划任务只在**已经验证过的命令**上运行。任何新命令必须先由我手工跑通、确认成功标志，才能加进 `night_plan.json`。

## 7. 常用命令

```powershell
# 看进度
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\night_status.ps1
# 我夜里推进任务
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\lw_night_runner.ps1 -Action RunDue
# 注册一次性计划任务（跑完自删）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\lw_night_runner.ps1 -Action Install
# 验证它真的能跑
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\lw_night_runner.ps1 -Action Verify
# 安全停止
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\stop_nightly.ps1
# 断电后恢复
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\resume_nightly.ps1
```
