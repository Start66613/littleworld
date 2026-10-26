# Wi-Fi Hosted 离线完善（P1）

分支 `agent/dsh-v4-3-engine`。**本轮没有上电、没有串口、没有刷机、没有更新 C6。**
所有内容都是离线代码与配置层面的工作，硬件验证一律标注为未做。

## 1. 现状：编译通过，从未与真实 C6 对话

`os-wifi-experimental` 配置能编译出带 hosted 栈的镜像（2.6 MB 级），
`esp_wifi_init` / `esp_wifi_scan_start` / `esp_hosted_init` 都在符号表里。
但**从来没有**在真实板子上跑过——V3.2 那一轮实验镜像一次都没烧。

这意味着下面每一条都属于"设计上正确、未经验证"：

| 项 | 状态 |
| --- | --- |
| esp_hosted 1.4.7 与本板 C6 固件兼容 | **未知** |
| SDIO 引脚与实物连线一致 | **未知**（见第 3 节） |
| C6 上是否已烧 hosted slave 固件 | **未知** |
| 扫描能返回真实 AP | 未测 |
| 连接 + DHCP + IP | 未测 |
| SNTP 真同步 | 未测 |
| 断电后凭据保留 | 未测 |

## 2. 组件与版本（本轮实测的解析结果）

`firmware/main/idf_component.yml` 保持**离线**，只声明：

```yaml
dependencies:
  idf: ">=5.5"
  waveshare/esp32_p4_wifi6_touch_lcd_7b: { version: "==3.0.1" }
  lvgl/lvgl: { version: "==9.5.0", public: true }
```

`firmware/idf_component_wifi.yml` 是**叠加清单**，只在 wifi 构建时由
`tools/setup_wifi_components.ps1` 追加进去：

```yaml
  espressif/esp_wifi_remote:
    version: "==1.2.5"
  espressif/esp_hosted:
    version: "==1.4.*"
```

解析结果（`firmware/build/wifi/sdkconfig` 与 `dependencies.lock`）：

| 组件 | 版本 |
| --- | --- |
| ESP-IDF | v5.5.5 |
| esp_hosted | 1.4.7（commit `d24d1dc3f709965fa61e3a1304c0896a9f3d497c`） |
| esp_wifi_remote | 1.2.5 |
| esp_wifi_remote_over_eppp | 传递依赖 |
| esp_serial_slave_link | 传递依赖 |
| eppp_link | 传递依赖 |

**为什么要钉版本**：`esp_wifi_remote` 的**每一个**版本（含 1.2.5）都依赖
`esp_hosted >=0.0.6` 与 `wifi_remote_over_eppp >=0.1`，所以不显式钉就会拉到
最新组合。1.2.5 是最后一个依赖 `esp_hosted 1.4.*` 的 1.2.x，也正是
Waveshare `05_wifistation` 示例用的组合。

## 3. SDIO 引脚：这是 esp_hosted 的默认值，不是从别处抄的

`firmware/build/wifi/sdkconfig`：

```
CONFIG_ESP_HOSTED_ENABLED=y
CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="esp32c6"
CONFIG_ESP_HOSTED_SDIO_PIN_CLK=18
CONFIG_ESP_HOSTED_SDIO_PIN_CMD=19
CONFIG_ESP_HOSTED_SDIO_PIN_D0=14
CONFIG_ESP_HOSTED_SDIO_PIN_D1=15
CONFIG_ESP_HOSTED_SDIO_PIN_D2=16
CONFIG_ESP_HOSTED_SDIO_PIN_D3=17
CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54
CONFIG_ESP_HOSTED_SDIO_4_BIT_BUS=y
CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=40000
```

这些数字来自 `managed_components/espressif__esp_hosted/Kconfig` 里
`if IDF_TARGET_ESP32P4` 的 default，**不是**从另一块 P4 板抄的。

**但也不能说它们经过本板验证**：仓库里没有原理图
（`sources/waveshare-7b/README.md` 明确写没有本地副本），BSP
`waveshare__esp32_p4_wifi6_touch_lcd_7b` 也没有导出任何 C6/SDIO 宏。
Waveshare README 只说 C6-MINI-1 通过 SDIO 连接。

所以本轮做的是**把它打印出来**，让错误的引脚在日志里可见，而不是表现为
"卡住不动"：

```
hosted esp32c6 sdio clk=18 cmd=19 d0=14 d1=15 d2=16 d3=17 rst=54 40MHz
```

这行在 `lw_net_esp_wifi_attach()` 开头（`log_transport()`）打印，
也在 `lw_net_esp_wifi_describe()` 里供诊断读取。

## 4. 本轮真正改掉的四个缺陷

### 4.1 SNTP 从未启动

`esp_netif_sntp_init()` 用 `ESP_NETIF_SNTP_DEFAULT_CONFIG()` 时默认
`start = true`，但代码里传了 `config.start = false`（为了不在没网时启动），
**然后就没有任何地方再启动它**。结果 `esp_netif_sntp_sync_wait()` 永远等到超时。

修法：`lw_net_esp_time_request()` 里显式调用 `esp_netif_sntp_start()`：

```c
esp_err_t start_err = esp_netif_sntp_start();
if (start_err != ESP_OK) { ... report failure ... return; }
```

注释写明了 `esp_netif_sntp_start()` 内部是 `sntp_stop() + sntp_init()`，
在 lwIP 线程里发起 NTP 交换。**没有这行，时钟永远停在 DEMO。**

### 4.2 开机时已关联的网络被丢弃

hosted slave 可能在**上一次开机**就连上了某个 AP，这次上电后直接带着连接起来，
但不会触发本机的 `WIFI_EVENT_STA_CONNECTED`（那不是本机发起的）。于是界面显示
"未连接"，而实际链路是通的。

修法：新增事件 `LW_NET_EV_LINK_UP` 与 `lw_wifi_on_link_up()`，它**不校验
generation**（因为这次连接不属于任何一次本机请求），只更新 SSID/IP/状态。
`lw_net_esp_wifi_probe_connected()` 用 `esp_wifi_sta_get_ap_info()` 探测，
并且**要求拿到 IP 才算 up**——DHCP 还没完成的链路不能报"已连接"。

### 4.3 探测只做一次

初版只在 `lw_services_start()` 里调一次 `lw_net_esp_wifi_probe_connected()`。
如果 C6 是在启动后 3 秒才关联上，就永远发现不了。

修法：`lw_services_poll()` 在"radio 开着 + 状态是 IDLE"时每
`TIME_PROBE_INTERVAL_MS`（1000 ms）探测一次：

```c
if (services->caps.wifi_backend && services->wifi.enabled) {
    lw_wifi_state_t ws = lw_wifi_state(&services->wifi);
    if (ws == LW_WIFI_ST_IDLE && (services->probe_at_ms == 0 || now_ms >= services->probe_at_ms)) {
        want_probe = true;
        services->probe_at_ms = now_ms + TIME_PROBE_INTERVAL_MS;
    }
} else {
    services->probe_at_ms = 0;
}
```

探测在**锁外**执行（`run_action` 之后），因为它要调 esp_wifi 的阻塞 API。

### 4.4 时间失败后不重试

`lw_services_poll()` 原来只在 `LW_TIME_DEMO` 时请求同步。一次失败后状态变成
`LW_TIME_FAILED`，就再也不试了。

修法：条件加上 `LW_TIME_FAILED || LW_TIME_STALE`，并按
`TIME_SYNC_RETRY_MS` 节流。

## 5. 状态机（`lw_wifi` 模型）

```
UNSUPPORTED ──(编译进来且 attach 成功)──> IDLE
IDLE ──enable──> CONNECTING ──on_connected──> CONNECTED
                     │                          │
                     ├──on_connect_failed──> FAILED
                     │                          │
                     └──on_disconnected───────> IDLE ──(重连预算)──> CONNECTING
```

重连策略（V3.2 已有，本轮沿用）：链路丢失后按
`LW_WIFI_BACKOFF_MS * reconnects`（2/4/6 秒）退避重试，最多
`LW_WIFI_MAX_RECONNECTS`（3）次；预算耗尽 → `FAILED` +
`LW_WIFI_ERR_RETRIES_EXHAUSTED`，并**擦掉内存中的密码**。

关键约束：`lw_wifi_on_connected()` / `on_connect_failed()` **要求状态是
CONNECTING 且 generation 完全匹配**。这条在压力测试里踩过坑——
`lw_wifi_scan()` 会把状态从 CONNECTING 挪走，之后到达的 `on_connected`
就应用不上。这是**正确**行为（过期的连接结果不该覆盖新状态），
但写测试时必须知道。

## 6. 可用性诚实性

`lw_net_esp_wifi_available()` 返回 `s_started`，而 `s_started` 只在
`esp_wifi_start()` 成功后才置 true。`lw_services_start()` 在
`lw_net_esp_wifi_attach()` 之后**再问一次**：

```c
services->caps.wifi_backend = lw_net_esp_wifi_available();
lw_wifi_set_supported(&services->wifi, services->caps.wifi_backend);
```

这样"编译进来了但起不来"不会被报成"就绪"。没有 Wi-Fi 栈的构建里，
`lw_net_esp_wifi_describe()` 输出 `CONFIG_LW_NET_WIFI_ENABLE is off in this build`。

## 7. 一个硬边界：C6 不应答会在 app_main 之前 abort

`esp_hosted` 用 `constructor` 属性在启动时初始化，`esp_wifi_remote_init()`
里是 `ESP_ERROR_CHECK(esp_hosted_reconfigure())`。所以：

- **如果 C6 没有 hosted slave 固件、或 SDIO 引脚不对**，
  `esp_hosted_reconfigure()` 会失败，`ESP_ERROR_CHECK` 直接 abort；
- 这发生在 `app_main()` 之前，也就是说**没有任何应用层代码能优雅降级**；
- 表现是上电即重启循环，而不是"Wi-Fi 不可用"。

这不是本分支能修的（属于 esp_hosted 的设计）。**刷这个镜像之前必须知道这一点**，
并且必须能刷回 `release/os-v3.1-stable` 的镜像
（sha256 `ef5202c9…`，见 `BUILD_AND_TEST.md`）。

## 8. 授权后可执行的测试步骤（**本轮未执行**）

前提：用户明确授权、C6 固件版本已确认、备份已就位。

1. 刷 `os-wifi-experimental`（`tools/flash_v4_safe.ps1`，需要 `-Authorized`）；
2. 串口 115200 抓启动日志，**第一件事**看有没有
   `hosted esp32c6 sdio clk=18 cmd=19 ...` 与 `station ready`；
3. 若出现 `esp_wifi_init failed (no ESP32-C6 responder on SDIO?)` →
   SDIO 引脚或 slave 固件问题，**停止**，不要尝试更新 C6；
4. 控制台 `wifi scan`，看是否返回真实 AP 列表（不需要密码）；
5. 连接测试**只能用手机会热点**，不得使用真实家庭网络的密码；
6. `clock` 命令看 SNTP 是否把 `demo` 变成 `real`；
7. 断电重启，看 `probe_connected` 是否报出上一次的连接（凭据保留验证）。

## 9. 严禁事项（本轮全部遵守）

- 不更新 C6 固件、不写 eFuse、不改分区表；
- 不连接真实 Wi-Fi、不保存真实密码；
- 不在没有真实连接的情况下让界面显示 READY；
- 不把"编译通过"说成"Wi-Fi 可用"。
