# night_plan.json 字段说明

`night_plan.json` 是夜班任务的**唯一真相来源**。脚本读它、写它，人看它。

## 顶层

| 字段 | 含义 |
| --- | --- |
| `schema` | 固定 `littleworld.nightly/1`，脚本加载时校验，不匹配直接拒绝 |
| `plan_id` | 本次夜班计划的名字，用于日志与锁文件名 |
| `created_utc` | 计划生成时间 |
| `t0` | **首次有任务真正开始跑的时刻**。为 null 表示还没开跑；`-Action RunNext` 第一次成功执行任务时写入 |
| `phases` | 阶段锚点（相对 t0 的分钟数），只用于人类阅读与 `night_status.ps1` 显示 |
| `tasks` | 任务数组，按 `due_after_min` 升序 |

## 任务字段

| 字段 | 必填 | 含义 |
| --- | --- | --- |
| `id` | 是 | 唯一 ID，`<phase>-<序号>-<短名>`。重复会抛错 |
| `phase` | 是 | 所属阶段 id，必须存在于 `phases` |
| `due_after_min` | 是 | 相对 `t0` 的分钟数，到点才算 due |
| `kind` | 是 | `script`（可被计划任务跑）或 `agent`（需要 DSH 会话，夜里只能记录状态） |
| `title` / `why` | 是 | 人读的标题与**为什么要有这一步**。`why` 不是装饰，写不出 why 的任务就不该存在 |
| `command` | `script` 必填 | 完整命令行，**相对仓库根目录**执行 |
| `success_pattern` | `script` 必填 | 输出里出现该字符串才算成功；找不到就是 `failed`，不是"看起来跑完了" |
| `status` | 是 | `pending` / `running` / `done` / `failed` / `timeout` / `blocked` / `skipped` / `dirty` |
| `max_attempts` | 是 | 尝试上限，达到后不再自动调度（**防止无限重试**） |
| `timeout_sec` | 是 | 墙钟超时，超时即杀进程树并记 `timeout` |
| `lock_key` | 是 | 互斥键。同键任务永不并发；`build-fw` 用于所有 ESP-IDF 编译 |
| `artifacts` | 否 | 预期产物路径，运行结束后逐条检查存在性并记进日志 |
| `last_run` | 否 | `{started, ended, result, note, attempts, dirty_before, dirty_after}` |

## 状态语义

- `running` 不是终态。若进程已死而状态仍是 `running`，`resume_nightly.ps1` 会把它标回 `pending`
  并把尝试计数保留——这就是"断电/崩溃后可恢复"的实现。
- `dirty` 表示**任务前后 git 工作树不一致**。它不会被自动修复，只会在汇总里被点名，
  等我来判断（第 6 条约束：异常退出不得破坏工作树，也不许脚本私自清理）。
- `blocked` 表示命令命中黑名单（刷机、串口、`git reset --hard` 等），永远不会被执行。

## 不允许出现的东西

- 任何"运行时由任务自己新增任务"的字段。任务列表是静态的，防止递归创建无穷任务。
- 任何指向 `flash_v3.ps1` / `serial_capture.ps1` / `esptool` / `idf.py flash` 的 `command`。
