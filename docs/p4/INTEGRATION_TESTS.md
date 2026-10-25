# 集成测试与构建矩阵

本文件记录**真实跑出来的结果**，不是计划。所有数字都能在 `reports/logs/` 里找到对应的原始输出。

---

## 1. 构建矩阵

三套配置，同一个 `firmware/`，只用 `SDKCONFIG_DEFAULTS` 与 `-D SDKCONFIG` 区分：

| 配置 | 用途 | SDKCONFIG_DEFAULTS | sdkconfig | 结果 |
| --- | --- | --- | --- | --- |
| `os` | OS V2 桌面（默认出货形态） | `sdkconfig.defaults;sdkconfig.defaults.rev3_x` | `sdkconfig.os` | **通过** |
| `legacy` | 旧的应用外壳（回退用） | 上面 + `sdkconfig.extra.legacy` | `sdkconfig.legacy` | **通过** |
| `noaudio` | 无 codec 的降级形态 | 上面 + `sdkconfig.extra.noaudio` | `sdkconfig.noaudio` | **通过** |

复现命令：

```powershell
. tools\activate_env.ps1
cd firmware
$env:SDKCONFIG_DEFAULTS = 'sdkconfig.defaults;sdkconfig.defaults.rev3_x'
python $env:IDF_PATH\tools\idf.py -B ..\build\os -D SDKCONFIG=$PWD\sdkconfig.os build
```

为什么必须是三套：`legacy` 证明 OS 层的改动没有污染旧壳；`noaudio` 证明 `CONFIG_LW_ENABLE_AUDIO` 这条编译分支真的成立（第一版这里就是错的，见下）。

### 构建过程中修掉的两个真实问题

1. **`CONFIG_LW_ENABLE_AUDIO` 未定义**。`noaudio` 配置下这个 Kconfig 布尔为 `n`，IDF 不生成宏，`lw_hw_begin(CONFIG_LW_ENABLE_AUDIO)` 直接编译失败。改成 `#ifdef` 取常量：
   ```c
   #ifdef CONFIG_LW_ENABLE_AUDIO
       const bool want_audio = true;
   #else
       const bool want_audio = false;
   #endif
   ```
2. **枚举重名**。`lw_system.h`（已有）与 `lw_wifi.h`（新增）都定义了 `LW_WIFI_OFF/IDLE/...`，`lw_boot.c` 同时包含两者 → `redeclaration of enumerator`。把新模型的枚举加前缀改为 `LW_WIFI_ST_*`，不去动 Codex 已经出货的头文件。

---

## 2. 主机测试

运行器：`tools/run_backend_tests.ps1`（MSVC `/W4 /WX /utf-8`，与产品固件同源的 .c 文件直接编译成 PC 可执行文件）。

```
store   exit=0  Store backend: 29 checks, 0 failures
system  exit=0  System model: 22 checks, 0 failures
net     exit=0  131 checks, 0 failures
```

合计 **182 项，0 失败**。

### 本轮新增：`net` 套件（131 项）

`tests/host/test_lw_net.c`，15 个用例函数：

| 用例 | 覆盖的内容 |
| --- | --- |
| `test_wifi_unsupported` | 没有后端时一切请求被拒，状态停在 `UNSUPPORTED`，错误码 `NOT_SUPPORTED` |
| `test_wifi_scan` | 扫描请求、10 s 超时、结果排序（RSSI 强→弱）、8 个上限 |
| `test_wifi_connect` | 连接成功、IP 写入、`attempts` 清零 |
| `test_wifi_failures` | 认证失败/无 AP 不重试、密码清零、错误码映射 |
| `test_wifi_timeouts` | 15 s 连接超时、2 s×n 退避、3 次后 `RETRIES_EXHAUSTED` |
| `test_wifi_events_overflow` | 事件队列 8 条上限，溢出计数 |
| `test_wifi_disable` | 关闭无线、断开、`OFF` 状态 |
| `test_time_demo` | 从未同步时 `clock()` 返回假、`hm()` 返回 `--:--` |
| `test_time_sync` | 同步成功、UTC 外推、6 h 重同步 |
| `test_time_zone` | 时区夹紧到 ±840、改变时区立即反映到本地时间 |
| `test_time_stale` | 断网后 STALE 但时间继续走 |
| `test_time_failure_and_restore` | 20 s 超时 → FAILED；RTC 恢复 → STALE 且 `rtc_restored=true` |
| `test_audio_unavailable` | 无 codec 时写入被拒、状态 `UNAVAILABLE` |
| `test_audio_pipeline` | 4096 字节环形缓冲写入/读回、字节序正确 |
| `test_audio_limits` | 静音丢弃、溢出计数、欠载回落、状态迁移 |

### 新增的测试桩

`tests/host/lw_net_esp_stub.c`：把 9 个适配层函数实现成"永远不可用"。没有它，`lw_services.c` 在主机上会因为缺符号链不过（第一版就是这样，`LNK2019` × 9）。

这个桩同时验证了一件事：**"没有射频"是一条真实路径**，不是异常路径。

---

## 3. 做了什么 / 没做什么

**做了**

- 三套固件配置各自完整编译（含 bootloader、partition table、app image）。
- 182 项主机断言，覆盖状态机、超时、退避、代数隔离、凭据清零、缓冲边界。
- LVGL 之外的整条服务链路（模型 → 胶水 → 适配层边界）都在这份代码里。

**没做（明确记录，不假装）**

- **没有刷机**。用户要求"不刷机、不擦除、不动 C6"，本轮一次都没有调用 `idf.py flash`。
- **没有板级运行**。三套镜像只是编译产物，没有上板。
- **没有 24 h 稳定性**。任务里的"稳定性测试"在模型层做了（超时/退避/溢出），整机长跑没做。
- **没有真实 Wi-Fi / SNTP / 音频播放**。原因见各自文档。
- **没有 TF 卡**。本板无可靠卡槽结论，未做挂载。

---

## 4. 可重复性

| 检查 | 命令 | 期望 |
| --- | --- | --- |
| 主机测试 | `tools\run_backend_tests.ps1` | `BACKEND TESTS PASSED`，182 项 |
| 三套构建 | 见第 1 节 | 三行 `Project build complete` |
| 默认配置的诚实性 | 打开 `os` 镜像串口日志 | `connectivity: wifi=no-backend time=demo audio=<codec|no-codec>` |

最后一条是这套设计的价值所在：即使什么都没接，固件也会在一行日志里说清楚自己有什么、没有什么。

---

## 5. 已知风险（按严重度）

1. **`now_ms` 是 uint32**，`esp_timer_get_time()/1000` 约 49.7 天回绕。回绕后所有超时与 `synced_ms` 比较都会错乱。修法明确（升 uint64），本轮未做。
2. **SNTP 适配层是占位**（读系统时间判断），真同步未实现。
3. **`esp_hosted` 版本未验证**。启用 Wi-Fi 时很可能撞组件版本，这是下一轮最大的不确定项。
4. **音频只有管道没有数据源**，"能出声"未证明。
