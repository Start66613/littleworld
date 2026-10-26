# Night Runner —— 整夜离线任务调度（Windows 计划任务 + 可恢复任务状态）

这一套东西回答一个具体问题：**"整夜自动执行"在这个工程里到底是哪一种自动化？**

答案分三类，必须分开说，任何文档、报告、对话都不许混为一谈：

| 类别 | 含义 | 谁在跑 | 断电/重启后 |
| --- | --- | --- | --- |
| **A. 单次计划脚本** | 到点由 Windows 任务计划程序启动一个已经验证过的离线脚本（编译、测试、出报告） | Windows，不是我 | 任务本身跑完就结束；结果留在磁盘上 |
| **B. 可恢复任务状态** | `night_plan.json` 记录每个任务的 status/开始/结束/产物 | 脚本读写，我看 | 下次任意进程启动时可以 `-Resume` 继续排队 |
| **C. 仅下次用户启动可恢复** | 需要我做判断的事（改代码、看现象、决定下一步） | 只有 DSH 会话里的我 | 只能等你下次开 DSH，我读 `night_plan.json` 接着干 |

**绝不允许**把 A 说成"AI 自主运行"。Windows 计划任务不会思考，它只是按点跑我提前写死并验证过的命令。

---

## 1. 文件

```
tools/nightly/
  night_plan.json        任务队列 + 依赖 + 状态（可恢复的真相来源）
  night_plan.schema.md   字段说明
  lw_night_runner.ps1    入口：-Action Status|RunNext|Resume|Install|Stop|Uninstall|Verify
  lw_night_task.ps1      定时任务入口：由计划任务调用，跑一个/多个任务，带超时与看门狗
  night_status.ps1       进度查看（人读）
  stop_nightly.ps1       安全停止 + 注销计划任务
  resume_nightly.ps1     恢复未完成任务
  README.md              本文件
logs/nightly/            每次运行的日志（run-<timestamp>.log + 任务级 jsonl）
reports/nightly/         每次运行的汇总（summary-<timestamp>.json/md）
```

## 2. 常用命令

```powershell
# 看一眼现在排到哪了
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\night_status.ps1

# 手动跑下一个到期任务（我自己夜里就是这么推进的）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\lw_night_runner.ps1 -Action RunNext

# 一切从头到期任务一路跑到底（直到没有到期任务）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\lw_night_runner.ps1 -Action RunDue

# 注册一次性计划任务（T0 之后 10 分钟跑一次，跑完自删）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\lw_night_runner.ps1 -Action Install

# 立刻验证它真的能跑（注册完必须做这一步，不能只看"已注册"）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\lw_night_runner.ps1 -Action Verify

# 安全停止：禁用计划任务并注销，不杀已经跑起来的编译
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\stop_nightly.ps1

# 恢复：把 running 且已超时的任务标回 pending，然后继续
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\nightly\resume_nightly.ps1
```

## 3. 十条硬约束与落地方式

| # | 约束 | 落地 |
| --- | --- | --- |
| 1 | 任务 ID 唯一 | `id` 形如 `T0-01-audit`，`Add-NightTask` 前查重，重复直接抛错 |
| 2 | 记录开始/结束/结果/产物 | 每任务一条 jsonl（`logs/nightly/tasks-<date>.jsonl`）+ `night_plan.json` 里的 `last_run` |
| 3 | 不允许无限重试 | `max_attempts`（默认 1），达到即 `failed`，不再自动调度 |
| 4 | 每阶段必须有超时 | `timeout_sec`，`Start-Process -Wait` 包一层墙钟看门狗，超时 `Stop-Process -Force` 并记 `timeout` |
| 5 | 同一 build 目录不得并发 | `lock_key` 全局互斥；运行前写 `logs/nightly/lock-<key>.lock`（含 pid），进程已死视为陈旧锁自动回收 |
| 6 | 异常退出不得破坏 git 工作树 | 运行前后各做一次 `git status --porcelain` 快照；不同则把该任务标 `dirty` 并在汇总里列出来，**不自动清理** |
| 7 | 绝不自动 `git reset --hard` / `git clean -fdx` | 脚本里根本没有这两个命令；`-Action` 只有上表那七个 |
| 8 | 绝不自动启动刷机脚本 | 黑名单：命令里出现 `flash_v3.ps1`、`serial_capture.ps1`、`esptool`、`idf.py flash`、`erase_flash`、`-Authorized` 一律拒绝执行并标 `blocked` |
| 9 | 不允许递归无限创建任务 | 脚本只能读 `night_plan.json`，不提供"运行时新增任务"的 API；`Install` 只注册一条固定命令 |
| 10 | 本轮计划任务一次性 + 到期自动清理 | 注册时 `-Once` + `-DeleteExpiredTaskAfter`；任务体最后一行调用 `Unregister-ScheduledTask` 自删 |

计划任务名统一前缀 `LittleWorld-Nightly-`，在任务计划程序里可见、可手动结束。

## 4. 时间线

`night_plan.json` 的阶段锚点是 `t0`（首次运行时间）。每个任务的 `due_after_min` 是相对 t0 的分钟数：

- T0 工程审查与任务初始化
- T+60 显示/背光专项
- T+150 刷机链路专项
- T+240 Wi-Fi Hosted 专项
- T+330 全量回归
- T+420 收尾清理

**这是检查点，不是必须空等。** 某一阶段提前做完，`RunDue` 会继续推进；没做完的留到下次。

## 5. 哪些事这个机制做不了

- 不能在没有 DSH 会话时"判断现象、改代码、决定下一步"——那需要我，属于类别 C。
- 不能在断电情况下跑任何东西。
- 不能操作板子：所有涉及串口、烧写、C6 的动作都在黑名单里，夜里永远不执行。
