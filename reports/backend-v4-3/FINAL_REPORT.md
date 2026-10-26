# V4.3 引擎交付最终报告

分支：`agent/dsh-v4-3-engine`（基线 `d168426`）
仓库：https://github.com/Start66613/littleworld
本轮：**零硬件操作**（没有上电、串口、刷机、擦除、C6 更新、分区改动）

---

## 1. 一句话结论

完成了内容路径修复、UI 资产容器与转换工具、场景锚点/遮挡/九宫格/时间的
纯 C 引擎与测试、Studio 转换桥、诊断扩展、发布打包框架，共 **3,443 项宿主
断言全绿**、固件离线构建通过。**没有**把新引擎接进真实页面，**没有**上板验证
任何一条——这两条是本轮明确划出的边界，见第 7 节。

## 2. 按任务清单对照

| 任务 | 状态 | 证据 |
| --- | --- | --- |
| P0 内容路径 Bug | **完成** | `CONTENT_PATH_FIX.md`，69 项端到端测试 + 变异验证 16 项失败 |
| P0 Meowa UI 资产管线 | **完成（引擎+工具）** | `UI_ASSET_PIPELINE.md`，264 引擎断言 + 34 工具断言 |
| P0 场景空间与遮挡引擎 | **完成（契约+测试）** | `SCENE_LAYER_ENGINE.md`，未接入 UI |
| P0 统一时间与昼夜 | **完成** | `TIME_SERVICE.md`，已接入状态栏/锁屏/诊断 |
| P1 性能 | **部分完成** | `RESOURCE_PERFORMANCE.md`，只做低风险改动，帧率未测 |
| P1 Studio 桥 | **完成** | `STUDIO_BRIDGE.md`，26 项测试 |
| P1 Wi-Fi Hosted | **部分完成** | `WIFI_PROGRESS.md`，四个缺陷修好，**镜像本轮未重建** |
| P1 发布打包器 | **完成（框架）** | `tools/package_v4_3_release.ps1`，4 条 gaps 如实记录 |
| P2 诊断 | **完成** | 缓存/时钟/UI 资产/动画四类新增字段，diag 132 断言 |
| 协作文档 | **完成** | `docs/handoff/CODEX_V4_3_ENGINE_API.md` |
| 十份报告 | **完成** | `reports/backend-v4-3/` |

## 3. 真实代码变更（按文件）

### 3.1 新增组件 `lw_engine`（纯 C，无 LVGL/IDF 依赖）

| 文件 | 内容 |
| --- | --- |
| `include/lw_ui9.h` + `src/lw_ui9.c` | 九宫格几何、按钮三态、最小尺寸 |
| `include/lw_ui_asset.h` + `src/lw_ui_asset.c` | `.lwu` 容器解析 + 切片 repack |
| `include/lw_stage.h` + `src/lw_stage.c` | 场景投影、角色锚点、遮挡、狗窝四态 |
| `include/lw_day.h` + `src/lw_day.c` | 四态时钟、五时段、三主题、迟滞 |

### 3.2 新增路径层 `lw_core`

`include/lw_path.h` + `src/lw_path.c`：`lw_path_is_safe_relative`、
`lw_path_join`。

### 3.3 新增 UI 侧

| 文件 | 内容 |
| --- | --- |
| `firmware/components/lw_ui/include/lw_ui_asset_view.h` + `src/lw_ui_asset_view.c` | `.lwu` 设备端加载与九宫格控件（仅 ESP_PLATFORM） |
| `firmware/components/lw_ui/include/lw_ui_clock.h` + `src/lw_ui_clock.c` | 页面读时钟的唯一入口 |
| `firmware/main/lw_time_boot.c/.h` | 时间启动接线与诊断行 |

### 3.4 改动的既有文件

| 文件 | 改动 |
| --- | --- |
| `firmware/components/lw_storage/src/lw_resource.c` | `join()` 走 `lw_path_join`，新增 `lw_resource_path()` |
| `firmware/components/lw_storage/src/lw_storage.c` | `read_file()` 走 `lw_path_join` |
| `firmware/components/lw_ui/src/lw_visual_assets.c` | 新增 `scene_path()`，场景路径走 `lw_path_join` |
| `firmware/components/lw_ui/src/lw_pack.c` | 路径拼接走 `lw_path_join` |
| `firmware/components/lw_ui/src/lw_os.c` | 三处取时间收敛为 `clock_fields()` |
| `firmware/components/lw_ui/src/lw_sprite.c` | 双向链表 + `lw_sprite_stats()` + tick 早退 |
| `firmware/main/lw_content_boot.c` | 改用 `lw_content_resolve_path()` |
| `firmware/main/lw_boot.c` | `lw_time_boot_init()` / `lw_ui_clock_attach()` / `lw_content_boot_apply()` |
| `firmware/main/lw_capture.c` | 新增 `content`、`clock` 控制台命令 |
| `firmware/main/lw_diag_board.c` | 四个新 provider |
| `firmware/components/lw_backend/include/lw_diag_report.h` + `src/lw_diag_report.c` | 缓存/时钟/UI 资产/动画字段 |
| `firmware/components/lw_net/src/lw_net_esp.c` | SNTP 启动、LINK_UP 探测、协处理器版本 |
| `firmware/components/lw_net/src/lw_services.c` | 探测轮询、时间重试 |
| `firmware/components/lw_net/include/lw_services.h` | 快照加 `utc_seconds` |
| `host/CMakeLists.txt` | 加入 lw_engine 源与 include |

### 3.5 新增工具

`tools/build_ui_assets.py`、`tools/make_ui_fixtures.py`、`tools/studio_to_device.py`、
`tools/package_v4_3_release.ps1`、`tools/run_engine_tests.ps1`、
`tools/run_card_layout_tests.ps1`、`tools/test_ui_assets.ps1`、
`tools/test_studio_bridge.ps1`。

### 3.6 新增测试

`tests/host/test_lw_engine.c`（264 项）、`tests/host/test_lw_card_layout.c`（69 项）、
`tests/fixtures/ui/*`。

## 4. 测试证据

```
backend:  Store 29 / System 22 / net 216 / stress 2035 / diag 132 / recovery 150 / faults 157 / content 124  = 2,865
engine:   264
card:     69
ui asset: 34
studio:   26
content:  31
resource: 154
--------------------------------------------
total     3,443   0 failures
```

固件：`firmware/build/os/little_world.bin` **2,356,928 B**，
sha256 `AB057114666CFD63A788CDAFCE20E50A4AB1A6AB0CE95C161DB5E13F56A978F7`，
`Project build complete`，分区占用 37.5%（factory 6 M）。

七配置矩阵全部构建成功并通过镜像校验（`6 config(s), 0 failure(s)`，加 `os`
共 7 张）：

| 配置 | 大小 | sha256 |
| --- | --- | --- |
| `os` | 2,356,928 B | `AB057114666CFD63A788CDAFCE20E50A4AB1A6AB0CE95C161DB5E13F56A978F7` |
| `legacy` | 1,429,808 B | `edf2fc82c6097c3f30a4b9b5415661d7f21891d653dc518a7d9991005ae04d10` |
| `noaudio` | 2,356,928 B | `d1d4b371935661a8258a42fb113e9e680c0ca921d6cae4024028881c4282530f` |
| `nocard` | 2,287,872 B | `55473e96414272e94e050f344b01955b22562fd9aff3f6f20abc181d746041ea` |
| `diag-a` | 2,356,928 B | `cea273fd8e8743a74e26ed7dde5c4d602b3b052ceda39a888be5415daa9958a7` |
| `diag-b` | 2,357,024 B | `bf48e885dfd5d6ac4a943edc4bf27dc7e699108dde8f0131b2e6b3b201376dd6` |
| `diag-c` | 2,353,184 B | `3fd8abaf1f105883fb135f7b615307b082dea883dd1af0368af30ad52d1782c2` |

`wifi` 不在本轮矩阵内（旧镜像，见 `BUILD_AND_TEST.md` 3.2）。

## 5. 四个真实缺陷（不是"顺手优化"）

1. **内容路径双层拼接**——所有内容文件加载失败，且报错像是卡的问题。
2. **SNTP 从未启动**——`esp_netif_sntp_init(start=false)` 之后没人调
   `esp_netif_sntp_start()`，同步永远超时，时钟永远停在 DEMO。
3. **显式设置时间不生效**——相位变更全部走 3 秒迟滞，用户改到 18:00
   界面仍是白天。新增 `commit_now()` 让显式事件立即提交。
4. **`lw_day_needs_qualifier` 漏了 STALE**——链路掉线后时间仍是 REAL
   但已不可信，界面却不再标注。现在 `stale` 也要求加后缀。

另外三个在写测试时发现的实现缺陷：
- `lw_stage_place_actor` 的锚点修正用差值法**截断**而非四舍五入，
  非整数缩放下脚底差半像素（换姿势时"抖一下"）；
- `lw_ui_asset_copy_piece` 必须逐行拷 alpha 平面（RGB565A8 的布局决定了
  不能指针偏移），这条写成了测试；
- `.lwu` 的 reserved 字段必须为 0（否则是格式版本混淆）。

## 6. 本轮明确没做的

| 项 | 原因 |
| --- | --- |
| 把 `lw_stage` 接进 `lw_os.c` 现有布局 | 那是 Codex 正在迭代的文件，改了会冲突 |
| 把 `.lwu` 用在真实页面 | 需要 Codex 的 PNG 素材；接口已就绪 |
| 昼夜主题自动换图 | `theme` 字段有了，换图属 UI 层 |
| 帧率与真实内存峰值 | 没有面板测不出来，不编数字 |
| Wi-Fi 真机验证 | 本轮禁止上电 |
| `os-wifi-experimental` 重建 | 矩阵任务持有清单文件，避免并发改写 |

Windows 回归**本轮已重跑并全绿**（EXIT 0）：System 22 / Duo 82 / RPG 85 /
Storage 33（句柄 67→67）/ OS V2 489，0 失败，67 张截图。但它覆盖的是模型层与
既有 UI 路径，**不覆盖**新增的 `lw_engine` / `lw_ui_clock` / `lw_ui_asset_view`
——那三块由专用套件覆盖。

## 7. 需要用户授权才能继续的

1. **上板验证**：需要明确授权、C6 固件版本确认、备份就位。
   顺序建议：`nocard` → `os` → `diag-a/b/c` → `wifi` 最后。
2. **Wi-Fi 镜像重建**：需要独占 `firmware/main/idf_component.yml`。
3. **Codex 素材到位后接线**：`.lwu` 页面接入、场景遮挡、昼夜换图。

## 8. 风险与回退

- 所有改动都在 `agent/dsh-v4-3-engine` 分支，**未碰** main、Codex 分支、
  `release/os-v3.1-stable`。
- V3.1 可恢复固件：sha256 `ef5202c9…`，全片备份
  `D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`。
- 刷机脚本默认 Dry Run，必须 `-Authorized`，只写 0x2000/0x8000/0x10000，
  **从不**碰 nvs(0x9000)、不 erase、不更新 C6。
