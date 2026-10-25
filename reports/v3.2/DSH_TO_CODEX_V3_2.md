# Little World V3.2 — 夜间任务交付说明（给 Codex）

> 这一份是给 Codex 看的接口级说明：本轮改了哪些文件、为什么改、哪些行为变了、
> 哪些**故意没改**。所有改动都在分支 `fix/os-v3.2-display-wifi-offline` 上，
> 基于 `release/os-v3.1-stable` 的 `d95b806`，**没有碰 `main`，没有碰你的
> `agent/os-v2-experience-v3`，也没有碰 `agent/littleworld-os-v2`**。

## 1. 分支与产物

| 项 | 值 |
| --- | --- |
| 分支 | `fix/os-v3.2-display-wifi-offline` |
| 基线 | `d95b806`（V3.1 稳定版交付） |
| 主 worktree | `D:\LittleWorld-V32` |
| 实验 worktree | `D:\LittleWorld-V32-Wifi`（分支 `exp/v3.2-wifi-hosted`，只用于把 hosted 栈编通） |
| 板子 | 全程断电，零烧写、零串口、零擦除 |

## 2. 新增的编译期开关（默认全关，不影响你的构建）

`firmware/main/Kconfig.projbuild` 新增 `menu "Little World diagnostics (build-time only)"`：

```
choice LW_BACKLIGHT_MODE
    default LW_BACKLIGHT_RUNTIME
    - LW_BACKLIGHT_RUNTIME  (默认) 行为与 V3.1 完全一致
    - LW_BACKLIGHT_FIXED            开机写一次 duty，之后拒绝运行期写
    - LW_BACKLIGHT_MINIMAL          开机写一次 30%，之后拒绝运行期写
config LW_DIAG_STATIC_UI
    default n                       冻结角色动画、暂停帧定时器、只渲染一次
```

**关键约束**：不选任何诊断项时，`lw_hw_begin()` 走的仍然是原来的
`bsp_display_brightness_set()` 路径，`lw_hw_brightness_set()` 仍然返回 true。
也就是说 **os-offline 的行为与 V3.1 逐字节等价（除本节之外没有别的差异）**。

## 3. 对外行为变化（会影响 UI 显示，请注意）

### 3.1 Wi-Fi 可用性不再"编译进去就等于可用"

`lw_net_esp_wifi_available()` 以前是"初始化完成就返回 true"，现在改为
**只有在 `esp_wifi_start()` 真正成功之后才返回 true**。

后果：如果 hosted 栈编译进去了但 C6 起不来，`lw_services_start()` 会在启动后
重新问一次，`caps.wifi` 变成 false，UI 会显示"不可用"。这是**故意的**：
上一版会显示"可用"然后让你一直等待。你的 `lw_services_start()` 里已经有
"Ask the adapter again" 的注释，就是配合这个改动。

### 3.2 新增 `lw_net_esp_wifi_probe_connected()`

```c
void lw_net_esp_wifi_probe_connected(void);
```

在 `lw_services_start()` 网络后端就绪后调用一次。若 C6 从机重启后仍然带着
上次的凭据处于已关联状态，就把它如实上报为 CONNECTED；否则什么都不做。
**没有 Wi-Fi 栈的构建里是空函数**，所以离线镜像的链接行为不变。

## 4. 新增的测试与工具

| 文件 | 作用 |
| --- | --- |
| `tests/host/test_lw_stress.c` | 离线压力/故障注入，2,035 项断言，纯模型层，不需要硬件 |
| `tools/build_v3_2_matrix.ps1` | 一次跑完全部 7 个配置，**每编译完一个就增量写 JSON** |
| `tools/setup_wifi_components.ps1` | 临时给 manifest 叠加 hosted 依赖，`-Restore` 恢复；用 try/finally 保证一定恢复 |
| `firmware/idf_component_wifi.yml` | hosted 依赖的"叠加层"，**不是**默认 manifest |

`tools/run_backend_tests.ps1` 的套件列表里增加了 `stress`。总检查数
29 + 22 + 188 + 2035。

## 5. 故意没有改的东西（避免你被我的改动绊住）

- **没有修任何显示层缺陷**。审计发现 6 个（见
  `DISPLAY_FLICKER_ANALYSIS.md` 第 8 节），但任何一处改动都会让 A/B/C 三份
  对照失去"只差一个变量"的意义。等板端对照做完再动。
- **没有改 tear-avoid 模式**（仍是 `TRIPLE_PARTIAL`），没有改 `buffer_height`，
  没有改 DSI lane/时钟，没有改 GPIO。
- **没有改分区表**，没有改 `sdkconfig.os` 默认值。
- **没有改 `reports/os-v2/*`**（回归测试会写 `runtime.json`，我已经把它 revert 掉，
  不会混进本次提交）。

## 6. 需要你决定的两件事

1. `lw_net_esp_wifi_probe_connected()` 的调用位置我放在 `lw_services_start()`。
   如果你希望它由 boot 层在更晚（比如第一个 LVGL 帧之后）触发，告诉我，改动很小。
2. 你 `lw_services_start()` 里 `services->caps.wifi_backend` 的语义是否就是
   "后端已就绪"？我按这个语义实现了诚实化，如果它的原本意思只是"编译进去了"，
   我们需要把它拆成两个字段。

## 7. 复核命令

```powershell
# 主机端全套
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\LittleWorld-V32\tools\run_backend_tests.ps1
# 全矩阵（会跑很久）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\LittleWorld-V32\tools\build_v3_2_matrix.ps1
# 只跑诊断对照
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\LittleWorld-V32\tools\build_v3_2_matrix.ps1 -Configs diag-a,diag-b,diag-c
```
