# V4 硬件诊断模块（任务 5）

分支：`agent/dsh-v4-1-backend-sprint`
日期：2026-10-26
状态：**已编译、已在宿主端测试；板端未验证**（本轮没有可用串口，见第 7 节）

---

## 1. 为什么要做这个模块

前几轮的板端问题有一个共同点：**失败时没有任何数字**。屏幕闪、角色不出现、Wi-Fi
"编译进去了但连不上"，这些都只能靠猜。诊断模块的目标不是做一个漂亮的日志系统，
而是让"现在到底是什么状态"变成一条可以采集、可以比对、可以贴进报告的结构化数据。

三条设计约束，都是从真实故障里换来的：

1. **不许编造数值。** LVGL 用 C 库分配器构建时 `lv_mem_monitor()` 是空实现，
   它把结构体清零。把那些零打印成"已用 0 字节"比不打印更糟——上一轮就是靠
   "0 B used" 浪费了一轮排查。所以 `prov_lv_mem()` 检查 `mon.total_size > 0`，
   为假就返回 false，报告里写 `lv_note = "monitor unavailable"`。
2. **不许在 LVGL 线程之外读 LVGL。** 控制台命令跑在 console 任务上，直接遍历
   控件树会和 UI 任务删控件撞车。`prov_lv_mem()` 用 `bsp_display_lock(2000)`
   取锁，取不到就返回"未取得测量值"而不是硬读。
3. **报告不能占堆。** 整份报告是定长结构体，文本/JSON 缓冲区由调用方提供。
   控制台命令用的是静态缓冲（`CONFIG_LW_DIAG_MAX_TEXT`），JSON 用 2 KB 静态缓冲。

---

## 2. 分层

```
firmware/components/lw_backend/include/lw_diag_report.h   平台无关的报告定义
firmware/components/lw_backend/src/lw_diag_report.c       组装 + 文本 + JSON
firmware/main/lw_diag_board.h                            板端接线声明
firmware/main/lw_diag_board.c                            取数（ESP32-P4 专用）
firmware/main/lw_capture.c                               diag / diagjson / diagerr 命令
tools/collect_v4_diagnostics.ps1                         PC 端采集与解析
```

`lw_diag_report.c` 不 include 任何驱动、不 include LVGL。所有子系统数值都通过
`lw_diag_providers_t` 的函数指针进来。这样做的直接好处是：它在 Windows 上能编、
能测（96 个断言），而板端只需要实现"取数"这一层。

**依赖方向**：`lw_backend` 不能依赖 `lw_ui` / `lw_storage` / `lw_net`（会成环），
所以取数代码放在 `main`，由启动层把指针传进去。

---

## 3. 报告字段

| 字段 | 来源 | 说明 |
| --- | --- | --- |
| `firmware` / `build_time` / `idf` | `esp_app_get_description()` | 用来分辨两块板跑的是不是同一个镜像 |
| `build_config` | 编译期宏推导 | `os` / `os-wifi` / `legacy` / `noaudio` / `diag-static` / `diag-fixed` / `diag-minimal` |
| `reset_reason` / `boot_ms` | `esp_reset_reason()` / `esp_timer_get_time()` | 掉电重启和 panic 重启在这里能分开 |
| `heap_free` / `heap_min` / `heap_largest` | `esp_get_free_heap_size()` 等 | `heap_largest` 是内部 RAM 最大连续块，碎片问题看这个 |
| `psram_free` / `psram_largest` | `heap_caps_get_info(SPIRAM)` | |
| `lv_*` | `lv_mem_monitor()`（持锁） | 不可用时 `lv_available=false` + `lv_note` |
| `pack` / `resource` / `resource_reads` / `resource_failures` | `lw_resource_report()` + `lw_file_cache_stats()` | 读失败次数是排查"角色不出现"的第一手数据 |
| `frame_failures` / `asset_leases` / `asset_bytes` | `lw_assets_stats()` | 租约泄漏会在这里累积 |
| `theme` | 实时偏好 | |
| `medium` / `medium_reason` | `lw_resource_medium_mounted()` + `report->root` | 卡没挂载时给出原因，不是只给一个 false |
| `wifi` / `wifi_reason` | `lw_services_status()` | 见下 |
| `last_error` / `last_error_ms` | `lw_diag_note_error()` | 单槽，记最后一次 |

### Wi-Fi 状态四态

这是本轮特意做的区分，因为之前"编译进去了"被当成了"能用"：

| 值 | 含义 |
| --- | --- |
| `0 LW_DIAG_WIFI_ABSENT` | 本构建没有射频协议栈 |
| `1 LW_DIAG_WIFI_COMPILED` | 协议栈已链接，station 未启动 |
| `2 LW_DIAG_WIFI_STARTED` | `esp_wifi_start()` 成功（已连接时也落在这里） |
| `3 LW_DIAG_WIFI_FAILED` | 协议栈拒绝启动 |

映射在 `prov_wifi()`：`!status.wifi_backend` → ABSENT；`LW_WIFI_ST_UNSUPPORTED`
→ FAILED；`LW_WIFI_ST_CONNECTED` → STARTED；其余 → COMPILED。

---

## 4. 串口命令（只读）

三个命令，全部在 `CONFIG_LW_DIAG_CONSOLE`（默认 y）下注册：

| 命令 | 作用 | 写操作 |
| --- | --- | --- |
| `diag` | 打印完整 key/value 块 | 无 |
| `diagjson` | 打印 `LWDJ {json}` 单行 | 无 |
| `diagerr <text>` | 把一段文字记进 `last_error` | 只写 RAM 的一个槽 |

**只读是构造上的**，不是承诺：这三个函数里没有任何 `nvs_*` / `esp_partition_*` /
`esp_wifi_*` / `esp_lcd_*` 写接口，`diagerr` 也只写 `lw_diag_report.c` 里的一个
静态槽。命令列表在 `lw_capture.c` 的 `capture_start()` 里注册。

`CONFIG_LW_DIAG_PERIODIC_LOG`（默认 n）会额外起一个 30 秒的 esp_timer，只在空闲
堆变化超过 8 KiB 时打一行。默认关闭的原因写在 Kconfig 的 help 里：这行本身够长，
足以干扰串口抓取，板测应该是"按需索取"而不是"被塞一条流"。

---

## 5. PC 端采集脚本

`tools/collect_v4_diagnostics.ps1`，三种模式：

```powershell
# 1) 不需要板子：用合成报告自检解析器和摘要生成
powershell -NoProfile -ExecutionPolicy Bypass -File tools/collect_v4_diagnostics.ps1 -SelfTest

# 2) 不需要板子：重放一份已经抓到的串口日志
powershell -NoProfile -ExecutionPolicy Bypass -File tools/collect_v4_diagnostics.ps1 -FromLog reports/logs/serial-xxx.log

# 3) 需要板子：实时采集
powershell -NoProfile -ExecutionPolicy Bypass -File tools/collect_v4_diagnostics.ps1 -Port COM5
```

输出：

```
reports/diagnostics/diag-<时间戳>.json     完整记录（schema = littleworld.diag/1）
reports/diagnostics/diag-<时间戳>.md       中文摘要
reports/diagnostics/diag-<时间戳>-console.log   原始串口文本（仅板端模式）
```

解析器的两条硬规则：

- **缺少任何一个必需键就报错**，不静默通过。报告和解析器一旦漂移，这里会立刻炸。
- **截断的 `LWDJ` 行抛异常**，不当作"这行不是报告"跳过。串口丢字节是真实故障，
  必须让人看见。

自检 `-SelfTest` 10 项，覆盖：正常行往返、日志行被拒绝、截图行被拒绝、截断 JSON
抛错、缺键抛错、摘要必须写明 LVGL 测量缺失、摘要必须解释 Wi-Fi 状态。

---

## 6. 实测结果（本轮真实执行）

### 6.1 宿主测试

```
Store backend: 29 checks, 0 failures
System model:  22 checks, 0 failures
net:           216 checks, 0 failures
stress:       2035 checks, 0 failure(s)
diag:          96 checks, 0 failure(s)
BACKEND TESTS PASSED
```

`tests/host/test_lw_diag.c` 的 96 项里包含：无 provider 时的默认值（必须全是
"n/a" / 0 / false，不能是未初始化内存）、全 provider 的逐字段往返（含
`heap_free=200000` / `psram_free=27000000` 这类具体值）、LVGL 不可用时的
`lv_note`、Wi-Fi 失败原因、文本块内容、截断语义、JSON 转义（内嵌引号、反斜杠、
制表符转 `\u0009`）、缓冲区不足时返回 0 且清空、错误槽覆盖、超长错误截断。

### 6.2 固件编译

| 配置 | 镜像 | 大小 | SHA-256 |
| --- | --- | --- | --- |
| os（默认） | `firmware/build/os/little_world.bin` | 2,342,176 B | `963c51e61eeb807786f2b989094f0a1df62b17fc2d93fb95db80fd1997b417a5` |
| diaglog（验证用） | `firmware/build/diaglog/little_world.bin` | 2,342,736 B | `8506aa1420635f6279c2c29d79ca7a711f239f003dc2c1f6fa25de3c3fd35dda` |

`diaglog` 是临时验证配置（`firmware/sdkconfig.extra.diaglog`，`CONFIG_LW_DIAG_PERIODIC_LOG=y`），
只为证明周期日志那段代码能编、能被链接进来，不是出货配置。

符号核验（`riscv32-esp-elf-nm --defined-only`，os 镜像）：

```
4800ff3e T lw_diag_board_bind
4800ff50 T lw_diag_board_init
4801010a T lw_diag_board_report
4802c39e T lw_diag_json
4802bd9c T lw_diag_note_error
4802bd12 T lw_diag_providers
4802bde8 T lw_diag_report
4802c206 T lw_diag_text
```

diaglog 镜像另有 `t diag_periodic_cb`、`t cmd_diag`、`t cmd_diagjson`、`t cmd_diagerr`。

镜像从 2,335,408 B（任务 3 之后）增长到 2,342,176 B，**+6,768 B**，就是整个诊断
模块的代价：报告结构体 + 文本/JSON 格式化 + 板端取数 + 三个控制台命令。分区余量
仍然充足（factory 6,291,456 B，占用 37.2%）。

### 6.3 采集脚本自检

```
10 checks, 0 failure(s)
DIAG COLLECTOR SELFTEST PASSED
```

---

## 7. 明确未验证的部分

**本轮没有板子。** `[System.IO.Ports.SerialPort]::GetPortNames()` 返回空，
`Win32_PnPEntity` 里没有任何 COM 设备。因此下面这些**全部未在硬件上跑过**，
不得在后续报告里写成"已验证"：

1. `diag` / `diagjson` / `diagerr` 在真实串口上的输出格式与可读性。
2. `bsp_display_lock(2000)` 在 console 任务上是否会真的取到锁（会不会超时）。
3. `lw_mem_monitor()` 在当前 `CONFIG_LV_USE_CLIB_MALLOC=y` 下返回 `total_size == 0`
   的推断——这是从 LVGL 源码读出来的（`lv_mem_core_clib.c` 的 `lv_mem_monitor_core()`
   是空实现），但没有在板上打印过。
4. `esp_app_get_description()` 拿到的 version 字符串内容（预期是 `337f7f1-dirty`
   这类，实际要看了才知道）。
5. `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)` 在 32 MB PSRAM 配置下的
   实际返回值。
6. `LWDJ` 单行是否会被其他任务的日志插进来（这行约 700 B，115200 波特率下约
   60 ms，理论上会被打断；解析器按整行匹配，被打断时会抛错而不是静默丢弃）。
7. 周期日志（`diaglog` 配置）在板上的实际节奏。

板端验证步骤写在 `reports/backend-sprint/TEST_RESULTS.md` 的"诊断"一节。

---

## 8. 已知限制

- **JSON 是手写的，不是 cJSON。** 值全部由本项目生成、全部 ASCII，转义只处理
  引号/反斜杠/控制字符。省一个依赖，代价是以后如果往里加非 ASCII 内容必须改
  `json_escape()`。
- **`last_error` 只有一个槽。** 设计如此：目的是知道"最后出了什么事"，不是
  在 RAM 里建日志。真要时间线，用串口日志。
- **`resource_reads` 是缓存计数器之和**（`reads + hits + misses`），不包含
  manifest 校验时读的几个头部。这一点写在代码注释里，避免以后有人拿它当
  "总读取次数"。
- **`build_config` 是推导出来的**，不是编译期注入的字符串。两个配置如果宏完全
  相同（例如 os 和 diag-a），这里都会显示 `os`——这是对的，因为它们确实是同一
  份代码路径。

---

## 9. 改动清单

| 文件 | 改动 |
| --- | --- |
| `firmware/components/lw_backend/include/lw_diag_report.h` | 新增：报告结构、provider 表、API |
| `firmware/components/lw_backend/src/lw_diag_report.c` | 新增：组装、文本、JSON |
| `firmware/components/lw_backend/CMakeLists.txt` | 加入 `src/lw_diag_report.c` |
| `tests/host/test_lw_diag.c` | 新增：96 项断言 |
| `tools/run_backend_tests.ps1` | 新增 `diag` 套件 |
| `firmware/main/lw_diag_board.h` / `.c` | 新增：板端取数、周期日志 |
| `firmware/main/lw_capture.c` | 新增 `diag` / `diagjson` / `diagerr` |
| `firmware/main/lw_boot.c` | 启动末尾 bind + init |
| `firmware/main/Kconfig.projbuild` | 新增 `LW_DIAG_CONSOLE` / `LW_DIAG_PERIODIC_LOG` / `LW_DIAG_MAX_TEXT` |
| `firmware/main/CMakeLists.txt` | 加入 `lw_diag_board.c`，REQUIRES 加 `esp_app_format` |
| `firmware/main/lw_art.h` / `.c` | 新增 `lw_art_cache()` 借用句柄 |
| `firmware/sdkconfig.extra.diaglog` | 新增：周期日志验证配置 |
| `tools/collect_v4_diagnostics.ps1` | 新增：采集脚本 + 自检 |

**过程中修掉的编译错误**（都是真实的，不是笔误）：

1. `lw_diag_board.h` 原本用 `struct lw_system *` 前置声明，但 `lw_system_t` 是
   **匿名 typedef**，`struct lw_system` 根本不存在 → `-Wincompatible-pointer-types`。
   改为直接 include 头文件并用 typedef 指针。
2. `esp_app_desc.h` 不在 `main` 的默认 include 路径里，属于 `esp_app_format`
   组件 → REQUIRES 加 `esp_app_format`。
3. `lw_diag_report.c` 没有加进 `lw_backend/CMakeLists.txt` 的 SRCS，链接期
   8 个 `undefined reference`。

`tools/collect_v4_diagnostics.ps1` 的 4 空格缩进 JSON 里含 `"4.1.0"` 这类引号，
PowerShell 5.1 **在没有 BOM 时按 ANSI 读脚本**，把中文和引号一起读错，报
`Unexpected token ':"4.1.0"'`。脚本必须存成 UTF-8 **带 BOM**。这条已经踩过两次
（V3.2 那次是 BOM 多余，这次是 BOM 缺失），以后所有含中文的 .ps1 统一带 BOM。
