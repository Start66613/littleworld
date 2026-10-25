# OS V3 + DSH 后端合并说明

本文记录 `integration/os-v3-p4-combined` 分支做了什么、为什么这样改、哪些问题
是真缺陷、哪些结论尚未被硬件验证。英文摘要见 `docs/OS_V3_INTEGRATION.md`。

## 1. 起点与合并

| 侧 | 分支 | 提交 |
| --- | --- | --- |
| 前端（Codex OS V3） | `agent/os-v2-experience-v3` | `7f1a170`（`17c1545` + 仅文档/截图的一提交） |
| 后端（DSH 连接服务） | `integration/p4-connectivity-services` | `904d7bc` |

第一次合并用的是 `17c1545`，对应的合并提交是 `337f7f1`。之后 Codex 又推了
`7f1a170`，`git diff 17c1545..7f1a170 -- firmware host tools tests` 为空——它只
改了 `AGENTS.md`、`TASKS.md`、`docs/STATUS.md` 和 `reports/os-v3/*`，并把这轮
用户授权的 31 张 LVGL 截图放进了仓库。因此第二次合并（`e7cd6af`）不改变任何
镜像内容，只把证据纳入进来。

`git merge --no-ff` 的合并提交是 `337f7f1`，**零冲突**（第二次 `e7cd6af` 同样
零冲突）。原因是两侧改动的文件集合几乎不相交：Codex 侧只动
`firmware/components/lw_ui/*`、`host/os_host.c`、`reports/os-v3/*` 与
`tools/{export_os_evidence.py,run_os_v2.ps1}`；DSH 侧只动
`firmware/components/lw_net/*`、`firmware/main/*`、`tests/host/*`、`tools/*.ps1`。
唯一需要人工判断的是 `firmware/main/CMakeLists.txt`：合并后 `REQUIRES` 同时保留
`lw_ui lw_system lw_backend lw_net`，`SRCS` 保留两侧的源文件。

`firmware/sdkconfig.os` 在合并后成为**被跟踪**文件（`.gitignore` 里也列了它）。
这是有意的：OS V3 的默认配置必须能复现，否则 `build/os` 与 `build/noaudio`
的差异只存在于本机。

## 2. 四个真缺陷

### 2.1 断线语义错误（`WIFI_EVENT_STA_DISCONNECTED`）

合并前的代码把任何 `WIFI_EVENT_STA_DISCONNECTED` 都送进
`lw_services_wifi_report_failed()`，于是"连上之后再掉线"会被记成"连接失败"：
时间因此不会降级、重连预算不会启动、UI 也看到错误的状态迁移。

修复分三层，每层只做自己该做的事：

| 层 | 文件 | 行为 |
| --- | --- | --- |
| 适配器 | `firmware/components/lw_net/src/lw_net_esp.c` | 记住本端是否真的拿到过 IP；拿到过就发 `LW_NET_EV_LINK_LOST`，否则发 `LW_NET_EV_CONNECT_FAILED` |
| 服务层 | `firmware/components/lw_net/src/lw_services.c` | 收到 `LINK_LOST` 时调用 `lw_time_service_on_network_lost()`（时间转 `STALE`），并让 Wi-Fi 模型按重连策略处理；`CONNECT_FAILED` 才走失败路径 |
| 模型 | `firmware/components/lw_net/src/lw_wifi_model.c` | `lw_wifi_on_disconnected()` 在 `IDLE` 上排一次退避重连，或在预算耗尽时转 `FAILED` |

### 2.2 ESP 回调与定时器并发

`lw_services_poll()` 跑在 `esp_timer` 任务里，而 `esp_event` 回调跑在 event
任务里，两者都会改 `lw_services_t` 与两个模型。合并前没有任何同步，属于数据竞争，
只是平时不容易撞上。

修复方式是**单写者**模型：

- 适配器回调只做一件事：把一个 `lw_net_event_t` 推进 SPSC 环
  `lw_net_event_ring_t`（8 项，满则丢最旧并计数 `dropped`）。
- 只有 `lw_services_poll()` 从环里取事件、改模型、改状态快照。
- 入队/出队用一个 `volatile int lock` 做临界区（`lw_net_lock/unlock`），
  因为 ESP-IDF 的 event 任务可能抢占 poll，纯无锁实现需要 C11 atomics 与
  目标编译器支持，这里选择更小、更容易在两边编译的临界区。
- UI 线程绝不直接读模型：`lw_services_snapshot()` 拷贝一份
  `lw_net_status_t`，模型只由 poll 写，因此 UI 永远看到"事件全部应用完"的状态。

`lw_services_t` 里的 `snapshot/snapshot_valid` 就是这份拷贝，源码注释写明了
"Written only by lw_services_poll()"。

### 2.3 陈旧事件隔离

合并前的 `report_*()` 传的是 `services->wifi.generation`——当前代号。适配器
晚到的回调因此与刚发出的请求不可区分，会把旧结果当成新结果应用。

修复：

- `lw_wifi_generation()` / `lw_wifi_scan_generation()` 暴露当前代号；
- 适配器在 `start_scan()` / `start_connect()` 时**捕获**代号并放进事件；
- 模型侧 `lw_wifi_on_*()` 只接受与当前代号相同的事件，其他一律丢弃。

测试覆盖：`wifi/connect` 用 `gen-1` 调用 `lw_wifi_on_connected()`，断言 IP 不变；
服务层测试再覆盖"旧扫描结果晚于新扫描到达"。

### 2.4 SNTP 曾用系统时钟自证成功

`lw_net_esp_time_request()` 原来的实现是"读一下系统时间，大于 1700000000 就
返回成功"。这不是同步，只是把 RTC 的旧值当成网络时间，一旦 NTP 不通就会把
错误时间写进 `lw_time_service` 并显示成 `SYNCED`。

修复后：

- 没有 `CONFIG_LW_NET_SNTP_ENABLE` 时，`lw_net_esp_time_request()` 直接返回
  `LW_TIME_SYNC_NOT_SUPPORTED`，服务层把状态置为 `FAILED` 且 `error` 保留，
  UI 显示"时间服务尚未接入"。
- 打开该选项时走真实的 `esp_netif_sntp_start()` + 异步等待
  `esp_netif_sntp_sync_wait()`，成功后才 `lw_time_service_on_synced()`。
  这条路径**尚未在硬件上跑过**（没有 C6 链路），因此不计入已验证结论。

## 3. 重连策略

模型层实现，可测、不依赖 radio：

- 断线且 `enabled && credentials_held` 时，`reconnects++` 并写入
  `retry_at_ms = now + LW_WIFI_BACKOFF_MS * reconnects`（2s、4s、6s）。
- `lw_wifi_tick()` 在 `IDLE` 上看到到期就**自己发起一次 `CONNECT` 动作**
  （`generation++`，复用 RAM 里的密码，不需要 UI 再点一次）。
- 成功重连清零预算；连续 `LW_WIFI_MAX_RECONNECTS`（3）次失败后转 `FAILED`、
  报 `LW_WIFI_ERR_RETRIES_EXHAUSTED` 并擦除密码，绝不无限重试。
- 关闭无线电时断线不排任何重连。

对应的 21 条断言在 `wifi/reconnect-policy` 套件里。

## 4. 能力上报接到 LVGL

`lw_os_capabilities_t` 由 `firmware/main/lw_boot.c` 构造：亮度取自
`lw_hw_caps_t.brightness`（GPIO32 争用 ledc，实际为 `SIMULATED`），音量/音频
取自 codec 句柄，Wi-Fi 取自 `lw_services_supported()`，存储固定
`UNAVAILABLE`（本轮不接 TF 卡）。

关键约束来自 Codex 的契约（`docs/handoff/DSH_UI_CAPABILITIES_V3.md`）：能力只能
在 **LVGL 任务**里应用。`lw_boot.c` 因此不在 `app_main` 直接调用
`lw_os_capabilities()`，而是登记到 UI 的启动回调里执行；服务状态变化也通过
`lv_async_call` 在 LVGL 边界上重发一次快照。

`lw_os.c` 已经按能力屏蔽入口：`capabilities.wifi == LW_SERVICE_UNAVAILABLE` 时
Wi-Fi 图标、扫描、连接按钮一并隐藏，不会出现"点了没反应"的假按钮。截图
`reports/ui-v2/os-v2/23-services-unavailable.*` 就是这条路径的现场。

## 5. 测试矩阵

HOST（`tools/run_backend_tests.ps1`，MSVC `/W4 /WX`）：188 checks, 0 failures
（store 29 + system 22 + services 137）。服务套件新增：断线时的时间降级、
陈旧代号、事件顺序、重连预算耗尽。

WINDOWS（`tools/run_windows_regression.ps1`）：`WINDOWS REGRESSION OK`，其中
System 22、Duo 82、RPG 85、Storage 33、OS V2 340 checks，全部 0 失败，并抓了
31 张真实 LVGL 截图。

固件（三套配置全部 `Project build complete`，`image_check.py --expect-rev 302` PASS）：

| 配置 | 大小 | SHA-256 |
| --- | --- | --- |
| os | 1,598,656 B | `0750cf158fa6f09f312a61300a1dab23ef0de496de14af1ac0dbc5017a674993` |
| legacy | 1,349,008 B | `7e4d2bc5a4beee907e62ee328f1435646d0618edfa2eb1c7cd3cbfc5df1c8adb` |
| noaudio | 1,598,656 B | `c78aa70adc5884199c7dd6706e96b2398a7c8fc0606af46b32bb2e98acdbdfdf` |

分区：factory app 位于 `0x10000`、6M（6,291,456 B），os 镜像占 25.4%，
`partitions.csv` 与合并基线逐字节一致。

链接证据（`tools/v3_evidence.ps1` 用 `riscv32-esp-elf-nm` 查
`little_world.elf`）：os 与 noaudio 同时含 `lw_os_start` / `lw_os_capabilities` /
`lw_ui_start`（OS V3）和 `lw_services_poll` / `lw_wifi_init` /
`lw_time_service_init` / `lw_audio_init` / `lw_net_esp_wifi_attach`（DSH 后端），
不是"两个互不相连的树"。legacy 只保留 `lw_ui_start`、`lw_boot_start`、
`lw_pet_tick`：它走 pre-OS shell，没有任何代码引用服务层，`--gc-sections`
把服务层整段删掉，所以比 os 小 249,648 B——这是预期，不是后端坏了。

## 6. 仍然没有接线的东西

- **C6 链路**：`esp_hosted` / `esp_wifi_remote` 在本机 IDF 里不存在，Wi-Fi 适配器
  从未对真实 C6 跑过；`CONFIG_LW_NET_WIFI_ENABLE` 默认 `n`。
- **SNTP**：代码存在，未编译进默认镜像，未实测。
- **音频**：ES8311 只做了探测与音量设置，没有 PCM 源，所以"能出声"未被证明。
- **存储**：TF 卡按要求不接，`storage` 能力恒为 `UNAVAILABLE`。
- **时间戳**：`now_ms` 仍是 `uint32_t`，约 49.7 天回绕；回绕时的行为未测。

## 7. 刷机前置条件

本轮**不刷机**。真正上板前还需要：

1. 独立的 C6 固件与 `esp_hosted` 组件到位，且 `idf.py menuconfig` 里
   `LW_NET_WIFI_ENABLE=y` 能在目标板上完成一次真实连接。
2. 一块已知可用的 NTP 服务器地址（或明确用 `pool.ntp.org`）与一次成功的
   `SYNCED` 日志。
3. 确认分区表未变（`partitions.csv` 与合并前逐字节一致），NVS 不清空，
   旧存档（prefs 24B / pet 72B / RPG 40B）可读。
4. 刷机前用 `tools/image_check.py --expect-rev 302` 校验待刷镜像。
