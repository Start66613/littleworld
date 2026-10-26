# Little World V3.2 — 夜班任务状态

这份文件是给早上接手的人（人或者另一个 agent）看的第一屏。它只说"现在在哪、
哪些能信、哪些不能信"，不做结论。

```json
{
  "task": "OS V3.2 night offline triage",
  "branch": "fix/os-v3.2-display-wifi-offline",
  "base": "release/os-v3.1-stable @ d95b806",
  "worktrees": {
    "primary": "D:\\LittleWorld-V32",
    "wifi_experiment": "D:\\LittleWorld-V32-Wifi (branch exp/v3.2-wifi-hosted)"
  },
  "device_touched": false,
  "device_notes": [
    "no power on",
    "no serial port opened",
    "no flash, no erase, no partition change",
    "no C6 firmware update, no eFuse write"
  ],
  "paid_api_calls": 0,
  "verification": {
    "host_backend_tests": "store 29 + system 22 + net 188 + stress 2035 checks, 0 failures",
    "windows_regression": "System 22 / Duo 82 / RPG 85 / Storage 33 / OS V2 340, 0 failures",
    "firmware_builds": "os-offline, diag-a, diag-b, diag-c, os-wifi-experimental (see reports/v3.2/build_matrix.json)"
  },
  "claims_not_made": [
    "the flicker is NOT fixed - it is only made measurable",
    "Wi-Fi is NOT working - it is only compiled and honestly reported",
    "SNTP is NOT verified - the adapter still only checks the system clock",
    "audio has no PCM source"
  ]
}
```

## 未验证清单（明早必须逐条确认）

| 项目 | 现状 | 谁来验 |
| --- | --- | --- |
| 屏幕频闪的真实来源 | 已给出三个候选和判定矩阵，**没有结论** | 人手对比 A/B/C |
| 背光 PWM 是否真的在闪 | 代码路径清楚，硬件行为未知 | 明早第一步 |
| esp_hosted 能否起来 | 从未在真 C6 上跑过 | 明早第二步 |
| SDIO 引脚是否正确 | 用的是 esp_hosted 默认值，**BSP 没有导出这组引脚** | 需对照原理图 |
| SNTP | 占位实现，`LW_NET_HAVE_SNTP` 未编译 | 未验证 |
| 亮度手感 | 10..100 限幅，低亮是否可见未测 | 明早 |
| 音频输出 | 无 PCM 源 | 不适用 |

## 镜像来源（防止拿错 bin）

- os-offline: `firmware/build/os/little_world.bin`, 2,248,992 B,
  sha256 `b60158420a85d001c9170599afc0e0e554d65721b36982497711db0a3461c7ea`
- diag-a: `firmware/build/diag-a/little_world.bin`, 2,248,992 B,
  sha256 `8af55911024b9adf459d8e72e4ebb56f4726cf7f0f8a087d3715543ebbf99d69`
- diag-b / diag-c / os-wifi-experimental: 见 `build_matrix.json`

> **注意**：本轮早先为了保住证据，把 `D:\LittleWorld-V32-Wifi` 的 Wi-Fi 镜像复制到了
> `build/os`，随后已删除该目录并从头重建 os-offline。现在 `build/os` 里的 `sdkconfig`
> 明确写着 `# CONFIG_LW_NET_WIFI_ENABLE is not set`，可用它自证不是 Wi-Fi 镜像。

## 本轮做的代码改动（全部可回退）

1. **背光三档诊断开关**（`firmware/main/Kconfig.projbuild`,
   `firmware/components/lw_backend/src/lw_hw.c`）：`LW_BACKLIGHT_RUNTIME`（默认，
   行为与 V3.1 完全一致）/ `FIXED` / `MINIMAL`。运行时不写占空比时，
   `lw_hw_brightness_set()` 直接返回 false，UI 会如实显示"不可调"，而不是假装调了。
2. **静态界面诊断开关**（`firmware/components/lw_ui/src/lw_os.c`, `lw_sprite.c`）：
   `CONFIG_LW_DIAG_STATIC_UI=y` 时冻结演员动画、暂停帧定时器、只渲染一次。
3. **Wi-Fi 可用性诚实化**（`firmware/components/lw_net/src/lw_net_esp.c`,
   `lw_services.c`）：`lw_net_esp_wifi_available()` 不再一到初始化就返回 true，
   而是等 `esp_wifi_start()` 真的成功之后才为 true。编译进去但起不来的情况，
   UI 显示"不可用"而不是"可用但一直转圈"。
4. **热启动关联探测**：`lw_net_esp_wifi_probe_connected()` 在 net 起来时问一次
   C6 是否已经带着凭据关联着某个 AP（从机重启不会清凭据），有就如实上报，
   没有就什么都不做。
5. **离线压力/故障注入测试**（`tests/host/test_lw_stress.c`，新增）：
   事件风暴、陈旧 generation、时间回跳与 uint32 回绕、音频奇数字节分块与静音、
   Wi-Fi 开关抖动、两小时加速长跑。2,035 项断言全过。
6. **构建/测试脚本**：`tools/build_v3_2_matrix.ps1` 增量写结果、失败不丢已成功的
   配置；`tools/run_backend_tests.ps1` 增加 stress 套件。

## 本轮踩到的坑（留给下一个人）

- **不要跨 worktree 复制 IDF 构建目录**：`CMakeCache.txt` 里写死了源路径，
  CMake 会直接拒绝。只复制 `little_world.bin`。
- **`idf_component_require()` 不存在**：ESP-IDF 5.5.5 只有
  `idf_component_register` / `idf_component_optional_requires` / `idf_component_add_link_dependency`。
  日志里的 "not a CMake function" 让整个构建中止。
- **esp_hosted 的 Kconfig 依赖组件顺序**：manifest 叠加后必须重新 configure，
  否则 `CONFIG_ESP_WIFI_*` 那一批缓冲参数不会被生成，`esp_wifi.h` 会在 319-339 行
  报一串 undeclared。
- **`.NET WriteAllText` 用的是进程 CWD，不是 PowerShell 的 Location**：
  在 `Set-Location` 之后用相对路径写文件会写到别处。永远用绝对路径。
- **PowerShell 的 `-replace` 静默失败**：模式没匹配上时文件原样写回，还打印"已添加"。
  改完必须用 `Select-String` 复核。

## 明早的第一步

打开 `reports/v3.2/TOMORROW_BOARD_TEST.md`，照着第 0 节探一次口，然后
按 A → B → C 做频闪对比。**先别碰 Wi-Fi 镜像**，那是第二步。

---

## 2026-10-26 白天追加：现场观测推翻了频闪分析的排序

白天用户现场报告"调高亮度也没有频闪"，且屏幕确实是桌面 + 双角色动画在跑。
只读串口取证（未烧写、未改 NVS）确认动画时钟正常推进，画面持续变化。

**结论：昨晚"频闪 = 背光 PWM 调制"的隐含前提不成立，§1-§4 降级为待验证假设清单。**
详见 `FIELD_OBSERVATION_20261026.md`，`DISPLAY_FLICKER_ANALYSIS.md` 已加 §0 修订说明。

同时补充一条实测事实：运行期**完全没有** brightness/backlight/ledc 日志输出
（`reports/logs/backlight-20261026-120304.log`），所以"滑块每帧写 duty"这条
目前只有代码依据，没有运行时证据。这一点昨晚标注得不够重。

diag-a/b/c 三份镜像仍然有效，但**只在下次真的复现频闪时才刷**——
现在刷没有可比对的现场。
