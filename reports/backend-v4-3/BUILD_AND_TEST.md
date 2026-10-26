# 构建与测试（V4.3）

分支 `agent/dsh-v4-3-engine`，基线 `d168426`。
本轮**没有上电、没有串口、没有刷机**，所有 ESP32 结果都是编译产物，不是运行结果。

## 1. 测试总览（本轮实跑）

| 套件 | 命令 | 结果 |
| --- | --- | --- |
| 后端（8 套） | `tools\run_backend_tests.ps1` | Store 29 / System 22 / net 216 / stress 2035 / diag 132 / recovery 150 / faults 157 / content 124，**0 失败**，`BACKEND TESTS PASSED` |
| 引擎 | `tools\run_engine_tests.ps1` | **264 checks，0 失败**，`ENGINE TESTS PASSED` |
| TF 卡目录端到端 | `tools\run_card_layout_tests.ps1` | **69 checks，0 失败**，`CARD LAYOUT TESTS PASSED` |
| UI 素材工具 | `tools\test_ui_assets.ps1` | **34 checks，0 失败**，`UI ASSET TESTS PASSED` |
| Studio 桥 | `tools\test_studio_bridge.ps1` | **26 通过，0 失败**，`STUDIO BRIDGE TESTS PASSED` |
| 内容工具 | `tools\test_v4_content.ps1` | **31 通过，0 失败**，`CONTENT TOOL TESTS PASSED` |
| V4 资源 | `tools\run_v4_resource_tests.ps1` | **154 checks，0 失败**，`V4 RESOURCE TESTS PASSED` |
| 宿主桌面构建 | `tools\build_demo.ps1` | `build/host/LittleWorld.exe` 1,387,008 B |

合计新增/覆盖的断言：后端 2,865 + 引擎 264 + 卡目录 69 + UI 34 + Studio 26 +
内容 31 + 资源 154 = **3,443 项**，全部 0 失败。

## 2. 本轮新增的测试套件

### 2.1 `tests/host/test_lw_card_layout.c`（69 项）

这是 P0 内容路径修复的**验收测试**，不是单元测试。它做的事：

1. 在 `LW_CARD_ROOT` 下搭出一棵**与真实 TF 卡完全相同**的目录树：
   ```
   littleworld/littleworld-v4.json
   littleworld/content.lwcx
   littleworld/male.lwp
   littleworld/qianqian-wave.lwp
   littleworld/scene-0.lwr  scene-1.lwr  scene-2.lwr
   littleworld/scene-0-thumb.lwr  scene-1-thumb.lwr  scene-2-thumb.lwr
   ```
2. `content.lwcx` **不是手写的**，是用真实的 `tools/package_v4_content.py`
   生成后拷进去的——这样"PC 工具产物能否被板端定位"才被真正验证。
3. 按设备启动顺序跑：`lw_resource_mount` → `lw_resource_verify` →
   `lw_content_resolve_path` → `lw_content_load`。
4. 断言解析出的路径**恰好**是 `<root>/content.lwcx`，且**不等于**旧的
   双层路径 `<root>/littleworld/content.lwcx`，且那个双层目录**不存在**。

**变异验证**（这是本轮最重要的一条证据）：
把 `lw_content_resolve_path()` 改回

```c
return lw_path_join(dst, cap, root, "littleworld/" LW_CONTENT_FILE_NAME);
```

后同一套件 **16 项失败**（content loads / status / version / memories /
photos / letters / dialogues / anniversary / memories borrowable /
memory count / slashed root / remove content / content restored …）。
恢复后重新 **69/0 通过**。这证明测试真的能抓住原 bug，而不是"改完就绿"。

### 2.2 `tests/host/test_lw_engine.c`（264 项）

纯 C 引擎的三块：九宫格（`lw_ui9`）、场景（`lw_stage`）、时间（`lw_day`），
外加 `.lwu` 容器解析与像素 repack。

其中 `--check-asset <file>` 是一个 CLI 模式：用**固件的**解析器读一个真实
`.lwu`，供 Python 工具调用。这样 `build_ui_assets.py` 的产物不是被 Python
自证，而是被固件代码验收。

### 2.3 `tests/host/test_lw_stress.c`（2035 项，V4.1 已有）

六族故障：事件风暴（2000 次上报 vs 8 槽环形队列）、过期 generation、
时间跳变 + uint32 回绕、奇数字节音频块、Wi-Fi 开关/扫描/连接抖动、
两小时 500 ms 长跑（14,400 次轮询）。

**注意**：这是**网络服务**的压力测试，不是渲染压力测试。把它当"性能已验证"
是错的。

## 3. 固件构建

### 3.1 os（离线，可发布）

```
firmware/build/os/little_world.bin   2,356,928 B
sha256  AB057114666CFD63A788CDAFCE20E50A4AB1A6AB0CE95C161DB5E13F56A978F7
```

`Project build complete`，`tools/check_v3_images.ps1` 判定 esp32p4 rev v3.0(300)
.. v3.99(399) 匹配。分区未改：factory 6 M = 6,291,456 B，占用 **37.5%**。

构建日志：`reports/logs/v43-build-os-clock2.log`。

### 3.2 全矩阵

`tools/build_v4_matrix.ps1 -Configs legacy,noaudio,nocard,diag-a,diag-b,diag-c -Report reports\backend-v4-3\build_matrix.json -KeepGoing`。

每配置独立 build 目录、独立日志、每次成功后增量写 JSON（后期失败不会抹掉
前面的结果）。最终结果 **6 config(s), 0 failure(s)**，`V4 BUILD MATRIX OK`，
JSON 在 `reports/backend-v4-3/build_matrix.json`：

| 配置 | 大小 | sha256 | 镜像校验 |
| --- | --- | --- | --- |
| `os` | 2,356,928 B | `AB057114666CFD63A788CDAFCE20E50A4AB1A6AB0CE95C161DB5E13F56A978F7` | PASS |
| `legacy` | 1,429,808 B | `edf2fc82c6097c3f30a4b9b5415661d7f21891d653dc518a7d9991005ae04d10` | PASS |
| `noaudio` | 2,356,928 B | `d1d4b371935661a8258a42fb113e9e680c0ca921d6cae4024028881c4282530f` | PASS |
| `nocard` | 2,287,872 B | `55473e96414272e94e050f344b01955b22562fd9aff3f6f20abc181d746041ea` | PASS |
| `diag-a` | 2,356,928 B | `cea273fd8e8743a74e26ed7dde5c4d602b3b052ceda39a888be5415daa9958a7` | PASS |
| `diag-b` | 2,357,024 B | `bf48e885dfd5d6ac4a943edc4bf27dc7e699108dde8f0131b2e6b3b201376dd6` | PASS |
| `diag-c` | 2,353,184 B | `3fd8abaf1f105883fb135f7b615307b082dea883dd1af0368af30ad52d1782c2` | PASS |

七张镜像全部通过 `tools/check_v3_images.ps1`（esp32p4 rev v3.0(300)..v3.99(399)，
32 MB flash，DIO），日志 `reports/logs/v43-image-check.log`。

尺寸差可以解释：`diag-b` 比 `os` 大 96 B（多了"fixed at %d%%"日志字符串），
`diag-c` 比 `os` 小 3,744 B（静态 UI 分支让部分动画代码被回收），`nocard` 小
69,056 B（无卡启动路径），`legacy` 小 927,120 B（整套 OS 服务被 gc-section 回收）。

**一个看起来可疑、实际正确的现象**：`os` 和 `noaudio` 的镜像大小**完全相同**
（都是 2,356,928 B），但字节内容不同。原因是 `CONFIG_LW_ENABLE_AUDIO` 只控制
`start_os()` 传给 `lw_hw_begin()` 的那个布尔量，不改变链接进去的代码量：

```
os:       li a0,1      ; want_audio = true
noaudio:  li a0,0      ; want_audio = false
```

`riscv32-esp-elf-objdump -d --disassemble=start_os` 实测上面两行，
`.flash.text` / `.flash.rodata` 段大小两配置完全一致。所以这不是"配置没生效"，
而是这个开关本来就是运行期探测开关：关掉它，ES8311 不被打开，音量退回"仅存
偏好"，UI 不再声称有真实音频输出。**大小相同不等于配置无效**，这条已用反汇编
而不是推断确认。

**注意**：本轮矩阵**没有**包含 `wifi` 配置。原因：`wifi` 需要先跑
`tools/setup_wifi_components.ps1` 装 hosted 组件，而这会改动
`firmware/main/idf_component.yml`；矩阵任务是串行的、耗时长，把 wifi 塞进同一
个任务里会让"清单被改写"的窗口跨过全部七个配置。所以 wifi 单独跑，本轮**未重建**。
`wifi` 的构建能力在 V4.1 已证明（2,620,800 B，符号表含 `esp_wifi_init`），
本轮的 Wi-Fi 代码改动（SNTP 启动、LINK_UP、探测轮询）**未**重新编译进 wifi 镜像。

这一条要如实记录：**`os-wifi-experimental` 镜像本轮是旧的**。

### 3.3 符号验证

用 `riscv32-esp-elf-nm --defined-only firmware/build/os/little_world.elf` 实测：

| 符号 | os 镜像 | 说明 |
| --- | --- | --- |
| `lw_os_start` | ✓ | |
| `lw_services_poll` | ✓ | |
| `lw_content_boot` | ✓ | |
| `lw_ui_clock_read` | ✓ | 已接入状态栏/锁屏 |
| `lw_day_tick` | ✓ | |
| `lw_path_join` | ✓ | 路径层已生效 |
| `lw_ui_asset_add_file` | **✗ 缺失** | 未被任何页面调用，被 gc-section 回收 |
| `lw_ui9_slice` | **✗ 缺失** | 同上 |
| `lw_ui_asset_copy_piece` | **✗ 缺失** | 同上 |
| `lw_stage_place_actor` | **✗ 缺失** | 同上 |
| `esp_wifi_init` | ✗（正确） | 离线镜像不应有 |
| `esp_hosted_init` | ✗（正确） | 离线镜像不应有 |

**这张表要按字面读**：`lw_engine` 里的九宫格、`.lwu` 加载、场景投影这几组
函数**本轮没有链进固件**，因为还没有任何页面调用它们，链接器把整段
`-ffunction-sections` 回收了。它们的正确性由 Host 套件（264 项）证明，
不是由固件符号表证明。

这一点很重要：如果只看"编译通过"就以为"功能在固件里"，是错的。
要让它们进镜像，必须先把调用点接进 UI（第 6 节的未做项）。

离线镜像里**没有**任何 hosted 符号，这是刻意的：默认固件不得依赖 C6。

## 4. 宿主回归

`tools\run_windows_regression.ps1`（本轮**已重跑**，EXIT 0，日志
`reports/logs/v43-windows-regression.log`）：

| 套件 | 检查数 |
| --- | --- |
| System model | 22，0 失败 |
| Duo model | 82，0 失败 |
| RPG core | 85，0 失败 |
| Storage | 33，0 失败，句柄 67 → 67（无泄漏） |
| OS V2 | 489，0 失败，100 次覆盖层循环，峰值像素 983,040 B |

文件缓存：峰值 1,572,864 / 1,572,864 B，命中 4,374，预取 4,723，驱逐 5,816。
67 张真实 LVGL 截图（本地，已 gitignore）。`WINDOWS REGRESSION OK`。

**注意**：Windows 回归覆盖的是模型层与既有 UI 路径，**不覆盖**新增的
`lw_engine`（九宫格/场景/时间）、`lw_ui_clock`、`lw_ui_asset_view`——那三块由
第 2 节的专用套件覆盖。别把"回归通过"读成"新代码被回归验证过"。

## 5. 可复现命令

```powershell
# 全部宿主测试
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_backend_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_engine_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_card_layout_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_ui_assets.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_studio_bridge.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test_v4_content.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_v4_resource_tests.ps1

# 固件
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\build_v4_matrix.ps1' -Configs os -Report 'reports\backend-v4-3\build_matrix.json'"

# 镜像校验
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '.\tools\check_v3_images.ps1' -Configs os"
```

## 6. 环境事实（复现必需）

- ESP-IDF **v5.5.5**，路径 `D:\Espressif\LittleWorld\esp-idf-v5.5.5`；
- 工具链 `riscv32-esp-elf` esp-14.2.0_20260121；
- Python（工具用）`D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe`
  （系统 python 是 WindowsApps 占位，直接调用会失败）；
- 本 worktree 的 `firmware/managed_components` 是从 `D:\LittleWorld-V41-Sprint`
  复制来的 22 个组件，**gitignored**；新克隆必须先跑一次 `idf.py reconfigure`
  让组件管理器装齐，否则 `lvgl.h` 找不到；
- PowerShell 只有 5.1（没有 pwsh），含中文的 `.ps1` 必须是 **UTF-8 带 BOM**，
  否则报 "Unexpected token"。

## 7. 未验证清单（必须随交付一起看）

| 项 | 状态 |
| --- | --- |
| 任何真机启动 | **未做**（本轮零硬件操作） |
| `.lwu` 在真实面板上的九宫格外观 | **未做** |
| 场景遮挡在真实布局中的效果 | **未做**（引擎未接入 UI） |
| 帧率 | **未做**（无面板无法测） |
| 真实内存峰值 | **未做**（只有账本一致性与静态预算） |
| Wi-Fi 扫描/连接/SNTP | **未做**（且 wifi 镜像本轮未重建） |
| C6 固件兼容性 | **未知** |
| Studio 从运行中的 UI 导出 | **未做**（只用仓库演示包） |
| 断电重启后的内容加载 | **未做** |

## 8. 回退

V3.1 可恢复固件仍在 `release/os-v3.1-stable`：

```
os 镜像 sha256 ef5202c965bc23db2cf2d870ed60f29abfa056f2b0f597c5e1aa94ab4cc66be2
全片备份 D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021
```

刷机脚本 `tools/flash_v4_safe.ps1` 默认 **Dry Run**，必须显式 `-Authorized`，
只写 0x2000 / 0x8000 / 0x10000，**从不**碰 nvs(0x9000)、不 erase、不更新 C6。
