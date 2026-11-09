# Wi-Fi 落地说明（2026-11-09，分支 agent/muse-wifi-landing）

## 结论先行

Wi-Fi 的代码路径已经全线接通：面板 → 系统门面 → 桥 → 服务/模型 → ESP 适配器 → C6。
之前「有真实现却用不上」的原因不在驱动，而在三处接缝没焊上，本轮全部补齐。
**剩下的只有构建 wifi 变体并上板验证**——沙箱没有开发板，这一关必须在真机上过，
清单在文末。所有帧率/时序结论仍按老规矩标注 static（静态推断），本轮没有编造任何真机数字。

## 数据流（接通后）

```
控制中心/WiFi 面板 (lw_os.c)
  → lw_system 门面：wifi_scan/connect/disconnect/enable + scan_result/connection_result
  → lw_system_wifi_bridge（新增）：backend 回调转发请求；sync() 回灌结果与链路变化
  → lw_services / lw_wifi 模型：状态机、重试退避（2 s 起、最多 3 次重连）、代次防串扰
  → lw_net_esp 适配器：esp_wifi + esp_hosted（SDIO → ESP32-C6），事件只入队、poll 里应用
  → 连上后自动触发 SNTP 对时（lw_time_service），演示时钟被真实时间替换
```

线程模型不变：services 由 500 ms esp_timer 在 LVGL 线程外 poll；桥的 sync() 挂在
lw_boot 原有的 caps_timer（1 s，LVGL 线程）里，和 publish_capabilities/publish_time 并列，
门面只在 LVGL 线程被改。

## 本轮补的缺口（按提交）

1. **0a0233d 密码被截断**：services 动作队列把连接密码拷进 33 字节（SSID 长度）的缓冲，
   33–63 字符的合法 WPA 密码会被静默截断、永远连不上。已改为 LW_WIFI_PASS_MAX(65)，
   桥测试里有 40 字符密码直达适配器的回归。
2. **21e7a2c 桥本体**：门面的 wifi_scan/connect/disconnect 回调从来没人装，
   真实路径下 scan_result/connection_result 从来没人调用（tick 只给模拟后端造结果）。
   新增 lw_system_wifi_bridge（回调转发 + sync 对账）与门面侧
   lw_system_link_lost()/lw_system_link_up()（反映用户没发起的链路变化：
   掉线、自动重连成功、C6 跨 P4 重启保持的关联被 probe 发现）。
3. **1817929 boot 接线**：start_os 里 install 桥（context=&s_services，其余回调都忽略
   context，互不影响），caps_timer 里加 publish_wifi()。
4. **79ef7b2 可用性来源**：门面的 network_available 之前取自 lw_hw_caps——而 hw 层
   按设计永远报 wifi=false（「后端从不宣称有射频，可用性以服务层为准」），且服务在
   门面 init 之后才启动，init 时刻根本拿不到正确答案。结果是门面在所有构建里都生于
   UNSUPPORTED 并拒绝一切扫描/连接。新增 lw_system_set_network_available()，
   由桥 sync 从 services->caps.wifi_backend 持续对账（离线构建永远等不到后端，
   门面就诚实地停在 UNSUPPORTED）。
5. **fcef78c 文案**：密码面板不再对真实连接声称「本轮为模拟连接」；真实后端的文案
   保留了原句中为真的部分（密码只存内存、不入存档与日志）。
6. **deab15b 宿主测试**：新增 tests/host/test_wifi_bridge.c（67 项检查），
   已注册进 tools/run_backend_tests.ps1 的 bridge 套件。原有 net 216 项、system 22 项不回退。

## 保持不变的设计约束

- **密码只存 RAM**：连接成功后留在模型里供自动重连用，掉线/失败按模型既有策略擦除；
  不写 NVS、不进存档、不入日志。所以「忘记网络」与「断开」等价（没有落盘凭据可删）；
  板子断电重启后需要重新输一次密码——除非 C6 自己保持了关联（probe 能捡回来，面板会
  直接显示已连接）。要不要做凭据落盘是产品决策，不在本轮范围。
- **默认离线构建不变**：主 manifest 仍不声明 esp_wifi_remote/esp_hosted，
  靠 overlay 临时注入（见下）。没有动分区表、NVS 布局、存档格式、BSP 驱动。

## 构建 wifi 变体（Windows，ESP-IDF 5.5.5）

一条命令（脚本自动注入 overlay、构建、还原 manifest）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_v4_matrix.ps1 -Configs wifi -Fresh -LogDir reports\logs\wifi-landing
```

产物在 `firmware\build\wifi\`。等价的手工三步：

```powershell
powershell -File tools\setup_wifi_components.ps1            # 注入 hosted 组件 overlay
idf.py -B build\wifi -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.rev3_x;sdkconfig.os;sdkconfig.extra.wifi" build
powershell -File tools\setup_wifi_components.ps1 -Restore   # 还原离线 manifest
```

烧录（在 firmware 目录）：`idf.py -B build\wifi flash monitor`

## 上板验证清单（measured-by-user，逐项打勾）

串口先看三行：

- `connectivity: wifi=backend time=... audio=...`——wifi 必须是 **backend**；
  若是 no-backend，先查 C6 固件/ SDIO，不要查 UI。
- `network: ESP32-C6 hosted stack compiled in; ...`——wifi 构建的新文案；
  若仍是 "not integrated"，说明烧的不是 wifi 变体。
- `capabilities: ... wifi=2 ...`（2 = LW_SERVICE_READY）。

面板流程：

1. 控制中心 → Wi-Fi 面板：能扫出**真实** AP 列表（名称/RSSI 与手机看到的一致）。
2. 点自家加密网络 → 输密码 → 状态变「已连接」并显示信号强度；错密码 → 失败面板出现，
   可直接重试。
3. 用一个 **33 字符以上**的密码连一次（截断回归）：应能连上。
4. 连上后关掉路由器：面板应在数秒内回到未连接；重新打开路由器，约 2 秒退避后自动
   重连，面板自行恢复「已连接」（不需要任何点击）。
5. 连着的状态下按复位重启板子：开机后不做任何操作，面板应自己显示已连接
   （C6 保持关联 + probe 路径）；若 C6 关联已丢，则回到需输密码状态，也属正常。
6. 连上后观察时钟：SNTP 对时完成后，演示时钟应被真实日期时间替换
   （串口 time 服务状态 + 顶栏时钟对照手机时间）。
7. 开关 Wi-Fi 开关：关时面板显示已关闭、开时重新扫描出列表。
8. 性能旁证（static 预期，需用户实测确认）：以上流程中看串口 `frame: slow=/last=`，
   切页与面板打开不应比离线构建出现新的周期性抬升；如异常按 FINAL_BENCHMARK 的
   方法抓 sched 长窗口核对。

## 已知边界

- 沙箱无法编译 ESP 目标（无 ESP-IDF），wifi 变体的**编译本身**也是上板验证的一部分：
  若构建报错，先看 lw_system 的 REQUIRES 是否解析到 lw_net（本轮新增的组件依赖），
  以及 overlay 是否生效（sdkconfig 里应有 CONFIG_LW_NET_WIFI_ENABLE=y）。
- C6 侧固件/esp_hosted 版本配对（esp_wifi_remote 1.2.5 + esp_hosted 1.4.*）是
  idf_component_wifi.yml 里既定的组合，本轮未动；若 C6 固件版本不匹配，
  串口会在 services 启动阶段先报错，与 UI 无关。
