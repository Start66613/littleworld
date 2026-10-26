# V4.1 联合集成交付报告

分支 `integration/os-v4-p4-unified`，仓库 <https://github.com/Start66613/littleworld>。

**本轮全程离线。** 没有上电、没有刷机、没有打开串口、没有改分区表、没有动 C6、没有擦 NVS。
所有"能跑"的结论都只到"主机测试通过 + ESP-IDF 编译通过 + 符号真的进了镜像"为止；
屏幕上的效果必须等用户授权刷机后按 `BOARD_ACCEPTANCE.md` 验收。

---

## 1. 合并了哪两个分支，冲突怎么解决的

| 角色 | 分支 | commit |
| --- | --- | --- |
| 底层基线（DSH） | `fix/os-v3.2-display-wifi-offline` | `518e2f5db3b9de4fecc752b23a6ef95ee92b19bb` |
| V4 视觉（Codex） | `agent/visual-v4-meowa-motion` | `94efbf590e5b7b2a51827edc46c03cb3c3a5237c` |
| 合并结果 | `integration/os-v4-p4-unified` | `f522aeb674dd1e70d53586566a3815241c753673` |

`release/os-v3.1-stable`（`d95b806`）保持不动，作为刷机回滚基线；`main` 未改动。

### 三个必然冲突的文件，逐个手改

| 文件 | DSH 侧改了什么 | Codex 侧改了什么 | 合并做法 |
| --- | --- | --- | --- |
| `.gitignore` | `/firmware/build/`、`/logs/nightly/`、`/reports/nightly/`、`/build_*.log` | `/assets/visual-v4/generated/`、`/reports/ui-v2/v4-*/` | 两边全部保留，按"构建产物 / 日志 / 视觉产物 / 私料"四段重排，没有删任何一条 |
| `firmware/components/lw_ui/CMakeLists.txt` | `REQUIRES` 加 `log`（诊断构建用 ESP_LOGI） | 加 `lw_visual_assets.c`、`lw_motion.c` 源文件 | `REQUIRES` 取并集（`lvgl__lvgl lw_core arcade_core lw_duo lw_system lw_rpg log`），源文件两边都留 |
| `firmware/components/lw_ui/src/lw_os.c` | 诊断静态 UI（`CONFIG_LW_DIAG_STATIC_UI` 早退）、Wi-Fi 能力位 | V4 场景背景、`lw_os_motion_*` 动画、控制中心手势、锁屏角色 | **不是二选一**：`#ifdef CONFIG_LW_DIAG_STATIC_UI` 的早退分支保留在 `update_actors()` 与 `tick()` 里，Codex 的场景/动画代码在外层；诊断构建打开时 V4 代码仍在镜像里但被冻结，正好是 C 组要的"最小显示负载" |

`lw_os.c` 的合并是本轮唯一需要逐行判断的地方。判断标准写在代码注释里：
**诊断开关必须能在不删 V4 代码的前提下冻结显示**，否则 A/B/C 对比就变成了"换了个 UI"，
不再是"同一个 UI 换显示负载"。

---

## 2. 板端真的怎么把 V4 美术装上去

这是本轮的核心，也是"美术文件在仓库里"和"固件真的按名字找得到、按帧读得出"的区别。

### 2.1 装载链

```
lw_boot.c: lw_boot_start()
   └─ lw_art_boot(characters_lwfa_start, size)      ← firmware/main/lw_art.c（本轮新增）
        ├─ lw_resource_mount()                       ← 挂 TF 卡 + 校验 littleworld-v4.json
        │    ├─ 成功 → lw_visual_assets_configure(root)      ← 场景目录（本轮补上的关键一行）
        │    │         lw_file_cache_open(root, 786432)      ← PSRAM 预读 worker
        │    │         lw_pack_registry_add(root,"male.lwp","male",cache)
        │    │         lw_pack_registry_add(root,"qianqian-wave.lwp","qianqian",cache)
        │    └─ 失败 → 记下确切原因（无卡/无目录/清单缺失/清单损坏/版本不兼容），继续
        ├─ 逐角色检查有没有 "idle"，没有就 lw_pack_registry_add_embedded(characters.lwfa)
        └─ lw_pack_registry_register(4 × 196608 B)   ← 一次注册，发布 lw_actor_art_t[2]
   └─ lw_os_start(&s_system, art)                    ← 角色/场景都从这里进 UI
```

### 2.2 三个"看起来能跑其实没接"的地方，本轮修掉了

| 缺陷 | 后果 | 修法 |
| --- | --- | --- |
| `lw_visual_assets_configure()` 从来没有被调用 | `directory[0]==0` → `available()` 永远 false → **即使卡里有三张 1024×600 场景，板子也只画纯色背景**。这是"美术在仓库里"最典型的一种假集成 | `lw_art.c` 在挂载校验通过后立刻 `lw_visual_assets_configure(lw_resource_root())`，失败时把原因写进报告行 |
| `lw_pack_registry_add()` 传的是 `NULL` cache | `lw_file_cache_open` / `lw_file_cache_read` 被 gc-section 掉，每次换帧都在 LVGL 线程里做阻塞式卡读，正是 100~125 ms 一帧要卡的地方 | `lw_art.c` 打开 `LW_ART_CACHE_BUDGET = 4 × 196608 = 786,432 B` 的 cache 并传给两个 `add`；打开失败时报告行追加 `(no cache: blocking reads)`，不假装没事 |
| `lw_art_release()` 没人调用 | 走到 legacy 回退路径后，注册表和预读 worker 仍然活着 | `lw_boot.c` 在 `lw_os_start()` 失败分支里调用 `lw_art_release()`（关 cache → 拆注册表 → 释放场景）；符号审计确认它现在真的在镜像里 |

### 2.3 场景：三套 .lwr，不整包常驻

- 格式：16 字节头 `LWSR0001` + 宽高小端 uint16，后接小端 RGB565。整图 1024×600（1,228,800 B 像素 / 1,228,816 B 文件），缩略图 256×150（76,800 B / 76,816 B）。
- 缓存槽位：**1 个 full + 3 个 thumb**（`entries[4]`），锁屏与桌面同主题共享 full 槽；不同主题的 full 槽仍被引用时**拒绝覆盖**，返回 NULL 让 UI 走绘制回退，而不是把正在显示的图释放掉。
- 主题顺序按 `assets/visual-v4/SCENES.json`：0 = 樱花清晨、1 = 月光梦、2 = 草莓日落。**不是**按文件名数字直觉排。
- 校验：magic、宽高、长度、EOF 全查；任何一项不过返回 NULL，UI 用代码绘制的几何房间兜底，不黑屏不花屏。
- 明确不做：三套全场景常驻 PSRAM（3 × 1.17 MiB = 3.5 MiB，加上角色和 LVGL 会挤爆）。

### 2.4 角色：两套容器，一张 spec 表

| 容器 | 位置 | 内容 | 读帧方式 |
| --- | --- | --- | --- |
| LWPX 1.0 | TF 卡 | `male.lwp` 5,505,364 B（idle 8f / blink 4f / wave 16f，256×256，锚点 128,225）；`qianqian-wave.lwp` 3,145,852 B（wave 16f，锚点 128,226） | 经 `lw_file_cache` 读 + 下一帧预取 |
| LWFA v1 | 应用镜像内嵌 572,960 B | male 1f 108×212 锚点 54,209；qianqian 8f 104×202 锚点 51,199 | 直接 memcpy（已在 flash 映射里） |

**关键约束**：`lw_assets_register()` 是**整表替换**且只存指针不拷贝。所以两种容器必须写进
`lw_pack.c` 里同一张文件级静态 spec 表；分两次注册会静默注销前一次，第二次注册后
第一次的 clip 全部消失。这也是 V3.1 板端 panic 的同一个根因（栈上 spec 表悬垂），
本轮在 `lw_pack.h` 里把这条写成契约注释。

LWPX 与 LWFA **不是**改个扩展名就行：记录长度（108 vs 32 字节）、几何（256×256 vs 每角色不同）、
payload 位置（文件偏移 vs flash 映射）全不一样，所以是两个解析器、一张表。

---

## 3. 本轮实际能跑起来的是什么

### 3.1 八份镜像（全部 PASS `tools/check_v3_images.ps1`，esp32p4 rev v3.0(300)..v3.99(399)）

| 配置 | 用途 | 字节 | SHA-256 |
| --- | --- | --- | --- |
| `os` | V4 离线默认（**这是要上板的那一份**） | 2,334,896 | `4192a903c9682b201ae5977f4ae73d17deb522e81a993db26c9611d6fc9e2563` |
| `legacy` | 旧版 shell 回退 | 1,420,864 | `3307e9c3dacccfa88bf63299a68d3b5644b05a63141419e8a48af4537e79d559` |
| `noaudio` | 不探 ES8311 | 2,334,896 | `b647bb46f3d3bd1b24ddaf51539e4915870b6ea276a6cfb7659802fe5883990a` |
| `nocard` | **无 TF 卡降级验收**（编译期强制走无卡分支） | 2,266,016 | `67b8827480a49ab3637ca1092c1410f0d530434345fe8f29e30f2ae1e6ec2226` |
| `diag-a` | 频闪对照 A（= 出货行为） | 2,334,896 | `0354e774cfab4bf7cf3a7032eaa0c3ca0e8f416649dadc5dcfc29afe7b5ea129` |
| `diag-b` | 频闪对照 B（背光固定，不写 duty） | 2,334,976 | `41cb091b78d85f8225f36a5bb5e72621fad7585421da47391b4e740701d19371` |
| `diag-c` | 频闪对照 C（30% 固定 + 静态 UI） | 2,331,152 | `50a466a93274bdab93ae858f62c1cb2dcf683bb0b4555f85119f1ece69f8a7b0` |
| `wifi` | V4 + Hosted Wi-Fi 实验（**未在板上跑过**） | 2,620,800 | `464384d9f7fc2b187c7a36209d63e190ac3a04270db846f970f8860054b7e18f` |

分区余量：factory 6 MiB = 6,291,456 B，`os` 占 **37.1%**，`wifi` 占 **41.7%**，都还有大量余量。
`partitions.csv` 本轮**未改动**（nvs 0x9000/0x6000、phy 0xf000/0x1000、factory 0x10000/6M、storage 4M）。

### 3.2 静态内存（`tools/map_footprint.py`，因为 `idf.py size` 在这个 P4 target 上直接报错）

`esp_idf_size` 对 P4 报 `warning: cannot assign memory region sram_seg to chip memory type`，
所以本报告改用 section header 统计（`tools/map_footprint.py`，按 P4 链接脚本的段名前缀归桶）：

| 配置 | 内部 RAM（静态） | PSRAM（静态） | flash 段合计 |
| --- | --- | --- | --- |
| `os` / `diag-a` / `diag-b` / `diag-c` | 142,016 B（138.7 KiB） | 0 B | ~2.12 MiB |
| `nocard` | 141,856 B（138.5 KiB） | 0 B | 2.05 MiB |
| `legacy` | 120,312 B（117.5 KiB） | 0 B | 1.25 MiB |

内部 RAM 主要构成（`os`）：`.iram0.text` 96,108 B、`.dram0.bss` 28,464 B、`.dram0.data` 17,264 B。
**PSRAM 静态占用是 0**，所有 PSRAM 都是运行时分配（场景缓存、角色帧、LVGL 缓冲），
所以"PSRAM 峰值"必须实测，不能从镜像读出来。

运行时预算（来自 Codex 的 `reports/visual-v4/MEOWA_BUDGET.md`，Windows 实测，**不是板端**）：

| 项 | 峰值 |
| --- | --- |
| 场景像素缓存（1 full + 1 thumb） | 1,459,200 B |
| 角色文件缓存（4 帧）+ 两角色 LVGL lease | 1,572,864 B |
| LVGL 角色图 lease | 983,040 B |
| 三者相加（不含 cache metadata、显示缓冲、字体、LVGL 对象、线程栈、后端） | **4,015,104 B ≈ 3.83 MiB** |
| Full 档 944×400 容器缩放可能额外需要的瞬时 ARGB 层 | ~1,510,400 B ≈ 1.44 MiB |

板端 PSRAM 共 32 MB，静态只占 0，所以 3.83 MiB + 1.44 MiB 是宽裕的；
但**最大连续块**和 SD 读吞吐仍是未验证项。

### 3.3 测试

| 套件 | 结果 |
| --- | --- |
| `tools/run_v4_resource_tests.ps1` | **136 checks, 0 failure(s)** — LWPX 解析/拒绝路径、.lwr 校验、清单版本门、无卡降级、**内嵌 LWFA 装载**、**卡包与内嵌包共存于一张表** |
| `tools/run_backend_tests.ps1` | Store 29 + System 22 + net 188 + stress 2035，0 failures |
| `tools/run_windows_regression.ps1` | System 22 / Duo 82 / RPG 85 / Storage 33（handles 58→58）/ OS V2 **489**，0 failures，67 张真实 LVGL 截图，`WINDOWS REGRESSION OK` |

OS V2 从 340 涨到 489，是因为 V4 的场景/动画代码进了 Windows 回归；截图从 31 涨到 67，
多出来的正是 V4 的打招呼、信封、锁屏、主题、控制中心下拉/收起。

### 3.4 符号审计（`riscv32-esp-elf-nm --defined-only`）

| 符号 | os | legacy | noaudio | nocard | diag-a | diag-b | diag-c | wifi |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `lw_art_boot` | Y | – | Y | Y | Y | Y | Y | Y |
| `lw_pack_registry_add_embedded` | Y | – | Y | Y | Y | Y | Y | Y |
| `lw_file_cache_open` | Y | – | Y | Y | Y | Y | Y | Y |
| `lw_visual_assets_configure` | Y | – | Y | Y | Y | Y | Y | Y |
| `lw_visual_scene_image` | Y | – | Y | Y | Y | Y | Y | Y |
| `lw_resource_mount` | Y | – | Y | Y | Y | Y | Y | Y |
| `lw_os_start` | Y | – | Y | Y | Y | Y | Y | Y |
| `lw_sprite_play` | Y | – | Y | Y | Y | Y | Y | Y |
| `esp_wifi_init` | – | – | – | – | – | – | – | **Y** |
| `esp_hosted_init` | – | – | – | – | – | – | – | **Y** |

`legacy` 全是 `–` 是**正确**的：它走 `lw_ui_start()`，OS 和资源层被 gc-section 掉，
镜像因此小 914 KB。Wi-Fi 符号只在 `wifi` 镜像里出现，证明"Wi-Fi 只在实验配置里"不是靠嘴说的。

---

## 4. Wi-Fi 的边界（说清楚，不含糊）

- `os`（要上板的那份）**没有** Wi-Fi 栈。`CONFIG_LW_NET_WIFI_ENABLE` 未设置，
  `lw_net_esp_wifi_available()` 返回 false，UI 显示"不支持"，不会假装扫描。
- `wifi` 配置编进了 esp_hosted 1.4.7 + esp_wifi_remote 1.2.5（Waveshare 05_wifistation 用的同一对版本），
  SDIO 4-bit，CLK 18 / CMD 19 / D0 14 / D1 15 / D2 16 / D3 17 / RESET 54，40 MHz，
  slave target `esp32c6`。这些是 **esp_hosted 自己的 P4 默认值**，不是从别的 P4 板抄的，
  但**也没有用这块板的原理图核对过**——Waveshare BSP 不导出任何 C6/SDIO 宏。
- **C6 固件版本未知，本轮没有更新，也不应该由本轮更新。** 若板上 C6 的 hosted slave 固件与
  1.4.7 不兼容，`esp_wifi_init` 会超时，这是预期失败，需要人判断。
- SNTP 只有在 `LW_NET_SNTP_ENABLE` 打开时才编译，且**"时间戳看起来对"不算同步成功**：
  现在的判断是系统时间 > 1.7e9，这在没有真实 NTP 响应时也可能成立，属于已知弱点。

**不能说"Wi-Fi 编译通过所以 Wi-Fi 解决了"。** 真实扫描 / 连接 / 拿到 IP 的日志一条都没有。

---

## 5. 没做 / 未验证（必须逐条读）

| 项 | 状态 |
| --- | --- |
| 上电、刷机、串口 | **全程未做**（本轮指令明确不授权） |
| V4 场景 `.lwr` 在 MIPI-DSI 上的实际显示 | 未验证（Windows 侧已验证三套切换） |
| TF 卡挂载 `bsp_sdcard_mount()` 在这块板上 | 未验证 |
| `lw_file_cache` 在真实卡上的读吞吐 | 未验证 |
| 8/4/16 帧动画在板上的观感与帧率 | 未验证 |
| 无卡降级在硬件上的表现 | 未验证（`nocard` 镜像已备好，等着刷） |
| 频闪是否与 V4 美术负载相关 | 未验证（`diag-a/b/c` 已备好，等着刷） |
| 拔卡重启、坏卡、空卡 | 未验证 |
| 存档跨重启持久化 | 未验证（NVS 格式本轮未改） |
| 锁屏/桌面共享 full 场景槽在真机上的内存曲线 | 未验证 |
| Wi-Fi 真实扫描/连接/IP | **未验证，且 C6 固件版本未知** |
| 60 FPS | 不作承诺。Windows 流畅不代表板上流畅 |

另外三条**刻意没做**的事：

1. **没有把三套全场景塞进 flash**。3 × 1.17 MiB + 角色 8.6 MB 远超 6 MiB 分区，
   硬塞只会得到"编译通过、启动 OOM"。
2. **没有改分区表**。真要塞进 flash 需要"必要性 / 影响 / 迁移方案 + 用户许可"，
   本轮一条都不满足。
3. **没有为了"更流畅"去调 DSI 时钟、背光 GPIO 或 LCD 初始化参数**。用户已经报告过高亮频闪，
   在根因没定位前动这些参数只会增加变量。

---

## 6. 下一步（等用户授权后才动）

1. 先刷 `nocard`（最保守：不碰卡，验证内嵌角色 + 绘制背景 + 不 panic）。
2. 再刷 `os`（有卡插卡、无卡自动降级），按 `BOARD_ACCEPTANCE.md` 的 8 条逐条打勾。
3. 频闪单独一轮：`diag-a → diag-b → diag-c`，每份之间**完全断电**，每次观察 30 秒。
4. Wi-Fi 最后单独一轮，且先确认 C6 固件版本。

回滚路径：`release/os-v3.1-stable` 镜像 sha256 `ef5202c9…`，全片备份
`D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`。
刷机时只写 0x2000 / 0x8000 / 0x10000，**绝不写 0x9000**（存档）。

---

## 7. 本轮代码改动清单

| 文件 | 类型 | 说明 |
| --- | --- | --- |
| `firmware/main/lw_art.c` | 新增 | 卡优先 + 内嵌补缺的装载器；场景目录配置；cache 打开；`lw_art_release()` |
| `firmware/main/lw_art.h` | 新增 | `lw_art_boot` / `lw_art_report` / `lw_art_release` |
| `firmware/components/lw_ui/src/lw_pack.c` | 改 | 新增 `lw_pack_registry_add_embedded()`；`read_frame()` 增加 `memory` 分支与每 clip `frame_bytes`；cache 绑定到 source |
| `firmware/components/lw_ui/include/lw_pack.h` | 改 | 声明内嵌装载；把"注册表只存指针"写成契约注释 |
| `firmware/components/lw_storage/src/lw_resource_mount.c` | 改 | 新增 `CONFIG_LW_DIAG_NO_MEDIUM` 分支（与真实挂载失败走同一段代码） |
| `firmware/main/lw_boot.c` | 改 | 用 `lw_art_boot()` 取代 `lw_char_pack_register()`；失败分支调 `lw_art_release()` |
| `firmware/main/Kconfig.projbuild` | 改 | 新增 `LW_DIAG_NO_MEDIUM` |
| `firmware/sdkconfig.extra.nocard` | 新增 | 无卡验收构建片段 |
| `firmware/main/CMakeLists.txt` | 改 | 加 `lw_art.c`、`REQUIRES lw_storage` |
| `tests/host/test_lw_v4_resources.c` | 改 | 96 → 136 checks：内嵌 LWFA 装载、拒绝路径、卡包+内嵌包共存 |
| `tools/build_v4_matrix.ps1` | 新增 | 八配置矩阵，每配置独立 build dir，逐配置落盘 JSON |
| `tools/map_footprint.py` | 新增 | 绕过 `idf.py size` 的 P4 报错，按段统计 RAM/flash |
| `reports/integration-v4/build_matrix.json` | 新增 | 八份镜像的字节数与 SHA-256 |
| `reports/integration-v4/BOARD_ACCEPTANCE.md` | 新增 | 上电后要打勾的 8 条 |

`lw_char_pack.c` 保留未动：V3.1 的路径仍在，回归测试还在用它，但启动路径不再经过它。
