# Wi-Fi 后端（P4 ↔ C6）

这是 P5 阶段的核心：给 OS V2 一个**真实的** Wi-Fi 后端，而不是模拟器里的假扫描。

本文只描述**已经写进代码并通过主机测试**的行为。板级验证（真正的 C6 SDIO 链路、真实 AP）**尚未进行**，见文末"未验证项"。

---

## 1. 分层

```
LVGL 界面（lw_os / lw_os_design，Codex 负责）
        │  只读取快照，不直接调用 ESP-IDF
        ▼
lw_services.c        ← 胶水层：把模型的"动作请求"变成真实的 IDF 调用
        │
        ├── lw_wifi_model.c   纯 C 状态机（无 IDF / 无 LVGL）  ← 主机测试 131 项
        ├── lw_time_service.c 纯 C 时钟服务                    ← 同上
        └── lw_audio.c        纯 C 音频环形缓冲 + 状态机        ← 同上
        │
        ▼
lw_net_esp.c         ← 适配层：CONFIG_LW_NET_WIFI_ENABLE + <esp_wifi.h> 才编译真实实现
        │
        ▼
ESP-IDF esp_wifi / esp_netif / esp_event   （经 esp_hosted 走 SDIO 到 C6）
```

为什么要分三层：

- **纯 C 模型可以在 Windows 上编译并测试**。Wi-Fi 的超时、重连、掉线抖动，这些是最容易写错也最难在板子上复现的部分；放在可测层是唯一能"证明"的办法。
- **模型无副作用**。它不调用 `esp_wifi_scan_start()`，只是记下"我想扫描"，由胶水层执行。这样模型可以在没有射频的进程里跑完整个状态机。
- **适配层可裁剪**。没有 C6 栈时编译成 stub，UI 必须显示"未接线"，绝不假装成功。

---

## 2. 文件

| 文件 | 作用 |
| --- | --- |
| `firmware/components/lw_net/include/lw_wifi.h` | 模型 API：状态、错误码、AP 列表、动作、事件队列 |
| `firmware/components/lw_net/src/lw_wifi_model.c` | 状态机实现（14 KB） |
| `firmware/components/lw_net/include/lw_services.h` | 胶水层：能力探测、快照、请求、适配层回调 |
| `firmware/components/lw_net/src/lw_services.c` | 动作执行、事件分发、时钟/音频编排 |
| `firmware/components/lw_net/include/lw_net_esp.h` | 适配层边界（9 个函数） |
| `firmware/components/lw_net/src/lw_net_esp.c` | 真实 IDF 实现 + stub，二者由 `__has_include` 选择 |
| `tests/host/test_lw_net.c` | 131 项主机测试 |
| `tests/host/lw_net_esp_stub.c` | 主机测试用的适配层空实现 |

---

## 3. 状态机

```
        UNSUPPORTED ──(无后端)────────────────────────────► 永远 UNSUPPORTED
             │
          enable(true)
             ▼
            OFF ──enable(true)──► IDLE ──scan()──► SCANNING ──► IDLE
             ▲                     │                    │
             │                     │ connect()          │ 10s 超时
             │                     ▼                    ▼
             └──enable(false)── CONNECTING ──► CONNECTED ──► IDLE(掉线)
                                   │ 15s 超时 / 认证失败
                                   ▼
                                 FAILED ──(2s×n 退避后自动重试，最多 3 次)──► CONNECTING
```

| 迁移 | 触发 | 结果 |
| --- | --- | --- |
| IDLE → SCANNING | `lw_wifi_scan()` | 记录 `scan_generation`，动作 `SCAN` |
| SCANNING → IDLE | 扫描回调 / 10 s 超时 | 结果排入事件队列，`LW_WIFI_ERR_SCAN_FAILED` 表示失败 |
| IDLE → CONNECTING | `lw_wifi_connect()` | `generation++`，`attempts=1`，15 s 计时 |
| CONNECTING → CONNECTED | 拿到 IP | `ip` 写入快照，`attempts` 清零 |
| CONNECTING → FAILED | 认证失败 / 无 AP / 15 s 超时 | 密码立即清零 |
| FAILED → CONNECTING | `now_ms >= retry_at_ms` | `retry_at_ms = now + 2000 × attempts` |
| FAILED → FAILED | `attempts > 3` | `LW_WIFI_ERR_RETRIES_EXHAUSTED`，停止重试 |
| CONNECTED → IDLE | `WIFI_EVENT_STA_DISCONNECTED` | 密码清零，错误 `LINK_LOST` |

认证失败 (`AUTH_FAILED`) 与无 AP (`NO_AP`) **不重试**——重试只会让路由器封你。

常量（`lw_wifi.h`，全部可在编译期覆盖）：

```c
#define LW_WIFI_SSID_MAX           33     /* 32 字节 + NUL */
#define LW_WIFI_PASS_MAX           65     /* 64 字节 + NUL */
#define LW_WIFI_MAX_APS             8     /* 扫描结果上限 */
#define LW_WIFI_MAX_EVENTS          8     /* UI 事件队列深度 */
#define LW_WIFI_MAX_ATTEMPTS        3
#define LW_WIFI_SCAN_TIMEOUT_MS  10000u
#define LW_WIFI_CONNECT_TIMEOUT_MS 15000u
#define LW_WIFI_BACKOFF_MS        2000u
```

---

## 4. 三个容易写错的地方（都已在测试里覆盖）

**代数（generation）校验。** 每次连接尝试 `generation++`。适配层在发起请求时捕获当时的代数，回调带回；模型丢弃代数不匹配的事件。没有这条，一次"取消 → 重新连接"的迟到回调会把新状态覆盖成旧的 `CONNECTED`。测试 `test_late_scan_result_is_ignored` 与 `test_connect_generation_isolates`。

**密码只活在一次尝试里。** 连接失败、掉线、禁用、主动断开都会 `memset` 掉 `password`。`credentials_held` 只反映"当前这次尝试内存里有没有密码"，不落盘。测试 `test_password_is_wiped_on_failure`。

**扫描列表有界且有序。** 插入时按 RSSI 从强到弱，超过 8 个直接丢弃最弱的。测试 `test_scan_list_is_sorted_and_capped`。测试 `test_scan_overflows_safely` 用 100 个 AP 压过。

---

## 5. 胶水层

`lw_services_poll()` 由 `esp_timer` 每 500 ms 调一次（`lw_boot.c: net_poll`），它做三件事：

1. `run_actions()`：从模型取一个动作，调用适配层。`SCAN → lw_net_esp_wifi_start_scan()`，`CONNECT → lw_net_esp_wifi_start_connect(ssid, pass)`，`DISCONNECT → lw_net_esp_wifi_stop()`。
2. `lw_wifi_tick()` / `lw_time_service_tick()`：超时、退避、周期重同步（6 h）。
3. 连接成功后触发一次 SNTP 请求（如果时间后端存在）。

**为什么放定时器而不是 LVGL 线程**：扫描可能阻塞几百毫秒，放 LVGL 线程会掉帧。模型是无锁的（UI 只读快照），定时器回调里跑是安全的。

定时器周期 500 ms 是权衡：`CONNECT_TIMEOUT_MS=15000` 下最多晚 0.5 s 发现超时，但一次 poll 只有几次比较，CPU 开销可忽略。

---

## 6. 适配层（`lw_net_esp.c`）

真实实现只在**同时满足**下面两条时参与编译：

```c
#if CONFIG_LW_NET_WIFI_ENABLE
#if __has_include("esp_wifi.h")
#define LW_NET_HAVE_WIFI 1
```

这样做的原因：`CONFIG_LW_NET_WIFI_ENABLE=y` 但 managed components 没装时，编译结果是 stub 而不是一堆 `undefined reference to esp_wifi_init`。Kconfig 说谎时固件仍然能链接、能启动、能诚实地说"没有无线后端"。

事件映射：

| IDF 事件 | 适配层动作 |
| --- | --- |
| `WIFI_EVENT_SCAN_DONE` | 取前 8 条 → `lw_services_wifi_report_scan()` |
| `IP_EVENT_STA_GOT_IP` | `IPSTR` 格式化 → `lw_services_wifi_report_connected()` |
| `WIFI_EVENT_STA_DISCONNECTED` | `map_reason()` → `report_failed()` |

`map_reason()` 把 `NO_AP_FOUND → NO_AP`，`AUTH_FAIL/4WAY_HANDSHAKE_TIMEOUT/HANDSHAKE_TIMEOUT → AUTH_FAILED`，`ASSOC_FAIL/CONNECTION_FAIL → TIMEOUT`，其余 `INTERNAL`。分不清的失败一律 INTERNAL，不猜。

---

## 7. 打开它的条件

当前默认 **关闭**（`CONFIG_LW_NET_WIFI_ENABLE=n`）。默认关闭不是偷懒：这台 P4 的 Wi-Fi 在 C6 上，走 SDIO，需要 hosted 组件 + C6 侧固件，三样缺一不可。默认打开会让"没插 C6 的板子"编译失败。

要启用：

1. 在 `firmware/main/idf_component.yml` 加 `espressif/esp_wifi_remote` 与 `espressif/esp_hosted`（版本以 IDF 5.5.5 兼容表为准）。
2. `menuconfig` → *Little World connectivity* → 打开 `LW_NET_WIFI_ENABLE`，需要时间同步再打开 `LW_NET_SNTP_ENABLE`。
3. 确认 C6 侧跑的是与 esp_hosted 匹配的 slave 固件（**这一步本次没有做，也不会在没有你确认的情况下动 C6**）。

参考片段（**未验证**，仅示意）：

```
CONFIG_LW_NET_WIFI_ENABLE=y
CONFIG_LW_NET_SNTP_ENABLE=y
CONFIG_ESP_WIFI_REMOTE_ENABLED=y
```

---

## 8. UI 契约（给 Codex）

`lw_net_status_t` 是唯一的数据源，UI 不要自己去问 IDF：

| 字段 | 含义 | UI 应该怎么显示 |
| --- | --- | --- |
| `wifi_backend=false` | 没有适配层 | "Wi-Fi 未接线"，禁用开关 |
| `wifi_state=OFF` | 用户关了 | 灰掉，不显示列表 |
| `SCANNING` | 正在扫 | 骨架屏 / 转圈，10 s 上限 |
| `CONNECTING` | 正在连 | 显示 SSID，15 s 上限 |
| `CONNECTED` | 已连 | 显示 `ssid` + `ip` + `rssi` |
| `FAILED` + `AUTH_FAILED` | 密码错 | "密码不对"，**不要**自动重试 |
| `FAILED` + `NO_AP` | 找不到 | "没找到这个网络" |
| `FAILED` + `RETRIES_EXHAUSTED` | 重试用完 | 提供"重试"按钮，不要自己轮询 |

`clock` / `date_label` 在 `time_state` 为 `DEMO` 时是 `--:--` 和空串，UI 应显示"演示时间"，不要显示伪造的真实时间。

---

## 9. 已验证 / 未验证

**已验证（主机，2026-10-25）**

- `tests/host/test_lw_net.c`：131 项通过，0 失败（含超时、退避、代数、密码清零、列表溢出、掉线）。
- 三套固件配置编译通过：`os`（OS V2 + 音频）、`legacy`（旧壳）、`noaudio`（`LW_ENABLE_AUDIO=n`）。
- stub 路径：默认配置下 `wifi_backend=false`，UI 会显示"未接线"。

**未验证（需要板子 + C6，本次明确跳过）**

- 真实 SDIO 链路、真实扫描、真实连接、真实 DHCP。
- `esp_hosted` 与 C6 slave 固件的版本匹配。
- 长时间运行的稳定性（本次只做了模型级，没有做整机 24 h）。
- 射频相关：信道、发射功率、共存（Wi-Fi 与 RGB 屏的 SPI/PPA 干扰）。

**已知限制**

- 只支持 STA + WPA2/WPA3 个人版；企业级 802.1X 不支持。
- 只有一个目标 SSID，没有多配置记忆（`lwo2.net` 记录只存最近一次成功连接的 SSID，不存密码）。
- 没有 RSSI 平滑，快照里是最后一次扫描的原始值。
- 重连退避是线性的（2 s × 次数），不是指数退避；3 次后停在 FAILED 等用户。

---

## 10. 下一步（需要你批准才做）

1. 插上 C6（或确认板载 C6 已有匹配固件），打开 `LW_NET_WIFI_ENABLE` 编译，这是唯一能验证适配层的方法。
2. 用真实 AP 跑一遍：扫描 → 错误密码 → 正确密码 → 拔电路由器 → 恢复。
3. 如果 `esp_hosted` 版本与 IDF 5.5.5 不匹配，先解决组件，再谈联调。

以上三步都会写进下一轮报告，不会在没有确认的情况下执行。
