# OS V3.2 真实 Wi-Fi（ESP32-C6 Hosted）接入评估

> 状态：**离线评估 + 一份"实验性"构建**。板子当晚断电，**没有一次真实连接**，
> 没有改 C6 固件、没有动 eFuse、没有把任何真实 SSID/密码写进代码或配置。
> 本文件的目的不是宣称"Wi-Fi 已经能用"，而是把"到底缺什么、缺到什么程度"
> 说清楚，并把骗人的路径从代码里删掉。

- 工程：`D:\LittleWorld-V32`，分支 `fix/os-v3.2-display-wifi-offline`，基线 `d95b806`
- 参考：`D:\Espressif\LittleWorld\sources\waveshare-7b\examples\esp-idf\05_wifistation`
- IDF：ESP-IDF 5.5.5（`D:\Espressif\LittleWorld\esp-idf-v5.5.5`）

---

## 1. 结论先行

1. **P4 本身没有 Wi-Fi 射频。** 板上 Wi-Fi 完全依赖 ESP32-C6 协处理器，P4 通过
   **SDIO（esp_hosted）** 访问它；`esp_wifi_remote` 把 `esp_wifi_*` 调用转发到 C6。
   因此"打开一个 Kconfig 就行"是不可能的，必须同时具备：
   `esp_hosted` + `esp_wifi_remote` + 正确的 SDIO 引脚 + **C6 侧固件支持 hosted**。
2. 官方示例的做法与我们一致：`main/idf_component.yml` 只加两条依赖
   （`esp_wifi_remote == 1.2.5`、`esp_hosted 1.4.*`），代码层面**没有任何 SDIO 引脚初始化**，
   全部由 `esp_hosted` 的 Kconfig 默认值 + C6 侧固件完成。这一点很重要：
   **"照抄别的 P4 板子的 SDIO 引脚"是错的方向**，正确做法是核对本板 BSP 与 esp_hosted 默认值。
3. 本轮把 **availability 的谎话修掉了**：以前只要编译进了栈就报告 "Wi-Fi 可用"，
   现在只有 `esp_wifi_start()` 真正成功才返回 true；失败原因写进启动日志。
4. **实验性构建（os-wifi-experimental）已经能编译通过并产出镜像**，但
   **它从未在真实硬件上跑过一次**，因此状态标记为 `compiled-only`,不是 "working"。

---

## 2. 依赖与版本（实测）

| 组件 | 版本 | 来源 | 备注 |
|---|---|---|---|
| ESP-IDF | v5.5.5 | 本机 | 官方示例声明 v5.5.5 / v6.0.2 双版本 CI |
| esp_hosted | 1.4.7 | 组件管理器拉取 | commit `d24d1dc3f709965fa61e3a1304c0896a9f3d497c`，要求 idf>=5.3 |
| esp_wifi_remote | 1.2.5 | 组件管理器拉取 | 官方示例注释：面向 IDF 6 的最后一个 1.2.x pin，同时保留 esp_hosted 1.4.* |
| esp_wifi_remote_over_eppp | 随 1.2.5 | 传递依赖 | |
| esp_serial_slave_link | 随 hosted | 传递依赖 | |
| eppp_link | 随 remote | 传递依赖 | |

> 重要教训：这套依赖**不能进默认 manifest**。已经改成
> `firmware/idf_component_wifi.yml` 覆盖文件，由 `tools/setup_wifi_components.ps1`
> 在 wifi 构建期间临时叠加、`finally` 里一定还原。默认 `idf_component.yml` 仍只有
> Waveshare BSP 3.0.1 + LVGL 9.5.0。

---

## 3. SDIO 引脚：来自哪里、如何核对

`esp_hosted` 在 `CONFIG_ESP_HOSTED_ENABLED=y` 时的默认值（构建产物 `build/wifi/sdkconfig` 实测）：

| 信号 | GPIO |
|---|---|
| CLK | 18 |
| CMD | 19 |
| D0 | 14 |
| D1 | 15 |
| D2 | 16 |
| D3 | 17 |
| RESET（C6 复位） | 54 |
| 位宽 | 4-bit (`CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y`) |
| 时钟 | 40000 kHz |
| TX/RX 队列 | 20 |

**这些是 esp_hosted 的通用默认值，不是从本板 BSP 里推导出来的。** 本板 BSP
（`waveshare__esp32_p4_wifi6_touch_lcd_7b`）**没有**导出任何 C6/SDIO 引脚宏，
官方 `05_wifistation` 示例同样不含引脚初始化代码 —— 也就是说这条链路的引脚
**只能从 Waveshare 的原理图/文档确认**，不能从代码里"读出来"。

第二天上板的第一件事（见 `TOMORROW_BOARD_TEST.md`）就是：启动日志里打印
`esp_hosted` 的引脚与 `esp_wifi_init()` 的返回码。若 `esp_wifi_init` 返回
`ESP_ERR_TIMEOUT` 或 `ESP_FAIL`，优先怀疑 SDIO 引脚与 C6 未就绪，
**而不是**继续改应用层代码。

---

## 4. C6 侧固件：必须承认的未知

- 本板出厂的 C6 固件**是否带 hosted slave**，代码里无法判断。
- 本轮**明确禁止**更新 C6 固件（避免把设备搞成半砖）。
- 因此实验性镜像即便启动成功，也可能停在 "`esp_wifi_init failed`"，
  这属于**预期内的失败**，需要人工决定是否升级 C6 固件。
- 判断方法（第二天，只读、不改）：看启动日志中 `esp_hosted` 的握手输出；
  若出现版本协商失败或 `esp_hosted_init` 超时，即可确认是 C6 侧的问题。

---

## 5. 代码层面的"不许骗人"改造

### 5.1 真实可用性来自 `esp_wifi_start()`

`firmware/components/lw_net/src/lw_net_esp.c`

```c
static bool s_started;                       /* 只有真正 start 成功才为 true */

static void lw_net_esp_wifi_attach(struct lw_services *services)
{
    ...
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed (no ESP32-C6 responder on SDIO?)");
        return;                              /* s_started 保持 false */
    }
    ...
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "station start failed");
        return;
    }
    s_started = true;
}

bool lw_net_esp_wifi_available(void) { return s_started; }
```

服务层在 `lw_services_start()` 里**再问一次**适配器，并据此设置
`services->caps.wifi_backend` 与 `lw_wifi_set_supported()`：

```c
services->caps.wifi_backend = lw_net_esp_wifi_available();
lw_wifi_set_supported(&services->wifi, services->caps.wifi_backend);
if (services->caps.wifi_backend) {
    lw_wifi_enable(&services->wifi, true, now_ms);
    lw_net_esp_wifi_probe_connected();       /* C6 可能已带着上次的凭据 */
}
```

### 5.2 开机即上报的"已在网"状态

新增 `lw_net_esp_wifi_probe_connected()`：若 `esp_wifi_sta_get_ap_info()` 返回有效 SSID，
就作为"非请求的链路事件"上报给服务层，UI 第一帧就能显示真实网络名，
而不是一直停在 "未连接"。没有链路时它是纯 no-op，状态自然保持 IDLE。

### 5.3 没有 Wi-Fi 栈时的行为

`#else /* no Wi-Fi stack */` 分支里：

- `lw_net_esp_wifi_available()` 返回 `false`
- `lw_net_esp_wifi_describe()` 返回 `"CONFIG_LW_NET_WIFI_ENABLE is off in this build"`
- `lw_net_esp_wifi_probe_connected()` 是空实现
- 所有 `start_scan/start_connect` 直接返回 false，模型停留在 `LW_WIFI_ST_UNSUPPORTED`

也就是说：**没编进栈 = 明确报告 Unsupported，而不是假装扫描**。

---

## 6. 两个构建配置

| 配置 | Kconfig | 状态 |
|---|---|---|
| **os-offline**（默认，可发布） | 无 wifi 片段 → `CONFIG_LW_NET_WIFI_ENABLE` 未设置 | 编译通过，镜像见 `BUILD_MATRIX.md` |
| **os-wifi-experimental** | `firmware/sdkconfig.extra.wifi`：`CONFIG_LW_NET_WIFI_ENABLE=y`、`CONFIG_LW_NET_SNTP_ENABLE=y` + hosted manifest 覆盖 | 编译通过，**未经硬件验证** |

构建命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools/build_v3_2_matrix.ps1 -Configs os-offline,os-wifi-experimental
```

---

## 7. 未验证清单（必须原样带进报告）

- [ ] `esp_wifi_init()` 在本板真实返回成功（需要上板）
- [ ] SDIO 引脚与 Waveshare 原理图一致（需要原理图，代码里没有）
- [ ] 出厂 C6 固件带 hosted slave
- [ ] `esp_wifi_scan_start()` 能扫到 AP
- [ ] `esp_wifi_connect()` 能拿到 IP
- [ ] SNTP 能拿到真实时间（当前实现是占位：只看系统时间是否 > 1.7e9）
- [ ] 断电重启后 C6 是否保留凭据

---

## 8. 本轮明确没有做的事

- 没有上电/串口/刷机/擦除/改分区表
- 没有更新 C6 固件、没有动 eFuse
- 没有连接任何真实 Wi-Fi，没有保存任何真实 SSID/密码
- **没有宣称 Wi-Fi 可用**
