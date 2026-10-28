# P0-E · LCD / LVGL / PPA 对照

分支 `perf/v4-4-fluidity-overhaul`，基线 `v4.3-baseline`。

**本文件没有任何一组对照帧率数字**：五个变体镜像都已构建完成并回读 `effective`
配置确认不是静默退化（§5），但**一个都没烧**。用户批准烧录的只是默认的 `os`
镜像（`v4.3-baseline-1-g04ad8e5-dirty`），不是这四个对照变体。

这一轮完成的是**让对照成为可能**：把原先硬编码在 `app_main.c` 的 tear-avoid 模式
变成编译期选项，新增四个单变量变体，并且**真的把它们都编出来了**（不是「计划要编」）。
每个变体都回读生成的 `sdkconfig` 确认改动生效 —— 这一点比编译通过更重要，因为
过程中发现了一个会让变体静默退化成基线的坑（§4）。

**真机复测已经改变了本文件最重要的一条待验证问题的形态。** 原先的问题是
「桌面页 30–37 ms 里那 10–17 ms 固定开销来自 tear-avoid 还是绘制单元」，
隐含假设是「绘制链路是瓶颈」。烧后的真机数据证明：**慢页的主因是精灵重绘的
绘制成本，不是 flush/显示链路**——`anim off` 后宠物页 251 → 20 ms、RPG 119 → 20 ms，
而全屏合成量不变（§7.1）。所以本矩阵的**优先级下调**：它回答的是「20 ms 里还能
再省多少」，而不是「为什么是 119/251 ms」。真机结论见 §7。

---

## 1. 当前显示链路与配置【静态证据】

### 1.1 硬件与驱动

| 项 | 值 | 来源 |
|----|-----|------|
| 面板 | EK79007，1024×600，MIPI-DSI | `reports/v3.2/DISPLAY_FLICKER_ANALYSIS.md` |
| DSI | 2 lane @ 1 Gbps | 同上 |
| 带宽占用 | 60 Hz × 1024×600 × RGB565 ≈ 884 Mbps，占 2 Gbps 的 44% | 同上 |
| 背光 | GPIO32，LEDC 低速组 ch1 / timer1 / 10 bit / 5 kHz / `output_invert=1` | 同上 |
| 旋转 | `ESP_LV_ADAPTER_ROTATE_180`（adapter/驱动层能力，非 LVGL 软旋转） | `app_main.c` |
| SD | 4-bit @ 40 MHz（`SDMMC_FREQ_HIGHSPEED`） | BSP `esp32_p4_wifi6_touch_lcd_7b.c:112-148` |
| CPU | 双核 400 MHz（`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=400`） | `sdkconfig.os` |
| L2 cache | 256 KB，line 128 B（`CONFIG_CACHE_L2_CACHE_LINE_SIZE=128`） | `sdkconfig.os:1575` |

### 1.2 LVGL 与渲染配置（`firmware/build/os/sdkconfig` 实测）

```
CONFIG_LV_COLOR_DEPTH_16=y                # RGB565
CONFIG_LV_DEF_REFR_PERIOD=15              # LVGL 刷新周期 15 ms
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2         # 两个软件绘制单元
CONFIG_LV_DRAW_SW_ASM_NONE=y              # 未启用任何汇编加速
CONFIG_LV_USE_DRAW_SW_ASM=0
# CONFIG_LV_USE_PPA is not set            # PPA 关闭
# CONFIG_LV_USE_PXP is not set
# CONFIG_LV_USE_DRAW_DMA2D is not set
CONFIG_LV_DRAW_BUF_ALIGN=4                # 与 L2 line 128 不一致（见 §3.1）
CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=24576
CONFIG_LV_DRAW_LAYER_MAX_MEMORY=0
CONFIG_LV_DRAW_THREAD_STACK_SIZE=8192
CONFIG_LV_DRAW_THREAD_PRIO=3
# CONFIG_LV_USE_PERF_MONITOR is not set   # 无法用 LVGL 自带性能面板
# CONFIG_LV_USE_PROFILER is not set
CONFIG_LV_USE_SNAPSHOT=y                  # shot 命令依赖它
CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS=y
# CONFIG_ESP_LVGL_ADAPTER_PARTIAL_AUX_IMG_CACHE is not set
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3          # 三个 DPI framebuffer
CONFIG_BSP_LCD_COLOR_FORMAT_RGB565=y
CONFIG_SOC_PPA_SUPPORTED=y                # 硬件有 PPA
CONFIG_SOC_SIMD_INSTRUCTION_SUPPORTED=y   # 硬件有 SIMD，但 LVGL 侧未用
```

两条关键静态结论：

1. **当前是纯软件渲染**：`LV_USE_DRAW_SW=y` + `ASM_NONE` + 无 PPA/PXP/DMA2D。
   P4 的 PPA 与 SIMD 都存在，但一个都没接。
2. **`LV_DRAW_BUF_ALIGN=4` 与 `CACHE_L2_CACHE_LINE_SIZE=128` 不一致**。当前能跑
   是因为没接 PPA；一旦接 PPA 这个不一致会直接变成编译错误（§3.1）。

### 1.3 tear-avoid 模式枚举（`esp_lv_adapter_display.h:63-75`）

| 枚举 | 值 | 含义 |
|------|-----|------|
| `NONE` | 0 | 不做防撕裂处理 |
| `DOUBLE_FULL` | 1 | 双缓冲全帧刷新 |
| `TRIPLE_FULL` | 2 | 三缓冲全帧刷新 |
| `DOUBLE_DIRECT` | 3 | 双缓冲直接送屏 |
| `TRIPLE_PARTIAL` | 4 | 三缓冲局部刷新 |
| `TE_SYNC` | 5 | 同步 TE 信号 |
| `DOUBLE_PARTIAL` | 6 | 双缓冲局部刷新 |

默认值：`..._DEFAULT_MIPI_DSI = TRIPLE_PARTIAL`、`..._DEFAULT_RGB = TRIPLE_PARTIAL`、
`..._DEFAULT = NONE`。本工程是 MIPI-DSI，所以默认走 `TRIPLE_PARTIAL`。

这个模式决定**帧缓冲轮转数与 flush 交给 DPI 的方式**，是 P0-E 要对照的第一变量。
它原先硬编码在 `app_main.c`，意味着不改代码就无法回答「瓶颈在刷新路径还是绘制路径」。

---

## 2. 本轮改动：把 tear-avoid 变成编译期选项

### 2.1 `firmware/main/Kconfig.projbuild`

在 `menu "Little World diagnostics (build-time only)"` 下新增：

```
choice LW_TEAR_AVOID
    prompt "Display tear-avoid mode (measurement builds only)"
    default LW_TEAR_AVOID_TRIPLE_PARTIAL
config LW_TEAR_AVOID_TRIPLE_PARTIAL   # 出厂默认
config LW_TEAR_AVOID_DOUBLE_PARTIAL   # 省约 2.4 MB PSRAM
config LW_TEAR_AVOID_TRIPLE_FULL      # flush 成本上界
endchoice
```

### 2.2 `firmware/main/app_main.c`

```c
#if CONFIG_LW_TEAR_AVOID_DOUBLE_PARTIAL
#define LW_TEAR_AVOID_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_PARTIAL
#elif CONFIG_LW_TEAR_AVOID_TRIPLE_FULL
#define LW_TEAR_AVOID_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL
#else
#define LW_TEAR_AVOID_MODE ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL
#endif
```

`bsp_display_cfg_t cfg` 的 `.tear_avoid_mode` 从硬编码改为 `LW_TEAR_AVOID_MODE`。
**出厂默认行为不变**（仍是 `TRIPLE_PARTIAL`）。

### 2.3 四个变体片段（`firmware/sdkconfig.extra.perf-*`）

| 片段 | 内容 | 对照什么 |
|------|------|----------|
| `perf-ppa` | `LV_USE_PPA=y`、`LV_USE_PPA_IMG=y`、`LV_DRAW_BUF_ALIGN=128` | 硬件 2D 加速值不值得接 |
| `perf-unit1` | `LV_DRAW_SW_DRAW_UNIT_CNT=1` | 两个绘制单元是不是在互相抢 |
| `perf-double` | `LW_TEAR_AVOID_DOUBLE_PARTIAL=y` | 少一个缓冲、轮转更短有没有用 |
| `perf-full` | `LW_TEAR_AVOID_TRIPLE_FULL=y` | flush 成本的上界 |

每个文件都写明「measurement build only，出厂仍是 TRIPLE_PARTIAL」。

### 2.4 `tools/build_v4_matrix.ps1`

新增三样东西：

- `-Fresh` 开关：只删 `build/<dir>/sdkconfig` 与 `sdkconfig.old`，不碰源码与已编对象
  （为什么必须要有它，见 §4）。
- `Get-VariantConfig`：构建后读回 `build/<dir>/sdkconfig`，把
  `SYMBOL=y` 记为 `'y'`、`SYMBOL=value` 记为值。
- 结果 JSON 每行新增 `effective` 字段 —— **这是唯一能区分「真变体」与「被静默
  忽略的变体」的手段**。

---

## 3. PPA 不是开关，是硬门槛【静态证据 + 本轮实测】

### 3.1 第一次构建直接失败

```
lv_draw_ppa_private.h:41:2: error: #error "CONFIG_LV_DRAW_BUF_ALIGN must be
equal to CONFIG_CACHE_L2_CACHE_LINE_SIZE!"
```

源码位置 `firmware/managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa_private.h:39-42`。
本工程 `L2_CACHE_LINE_SIZE=128` 而 `DRAW_BUF_ALIGN=4`，所以必须把对齐抬到 **128**。

这就是 P0-E 的实质结论：**PPA 不是「打开一个开关」，它强制所有 LVGL 绘制缓冲
对齐到 cache line（4 → 128）**。对齐变化会连带改变内存布局与 cache 行命中行为，
所以 PPA 变体的收益不能全部归给「硬件加速」（§6.3 判读陷阱）。

### 3.2 同一个 `#error` 里还有两条别的硬要求

`lv_draw_ppa_private.h` 里另外两条：

- `#if LV_PPA_NONBLOCKING_OPS` → `#error "PPA draw in nonblocking is experimental and
  not supported yet, please make it to 0!"`
- `#ifndef CONFIG_SOC_PPA_SUPPORTED` → `#error "This SoC does not support PPA"`

后一条在 P4 上不触发（`CONFIG_SOC_PPA_SUPPORTED=y`），前一条提醒：PPA 绘制路径
目前只支持阻塞模式。

### 3.3 PPA 的接入面

`firmware/managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa.c`：

- `#define DRAW_UNIT_ID_PPA 80`
- `#define DRAW_UNIT_PPA_PREF_SCORE 70`
- `lv_draw_ppa_init(void)` 注册三个 client：`PPA_OPERATION_SRM`、`PPA_OPERATION_FILL`、
  `PPA_OPERATION_BLEND`，每次 `LV_ASSERT(res == ESP_OK)`
- `ppa_evaluate()` 只在 `t->preference_score > DRAW_UNIT_PPA_PREF_SCORE` 时把任务抢过来

⇒ **轻量填充仍会走软件路径**，PPA 只接管「值得」的任务。所以 PPA 变体在纯色填充
为主的页面上收益有限，在图像/混合为主的页面（宠物页、RPG 页）才有意义。

依赖侧：`firmware/managed_components/lvgl__lvgl/env_support/cmake/esp.cmake:60-62`

```cmake
if(${target} STREQUAL "esp32p4")
    list(APPEND IDF_COMPONENTS esp_driver_ppa esp_mm)
endif()
```

⇒ P4 上 PPA 组件自动链接，打开 `CONFIG_LV_USE_PPA` 不需要手工加依赖（本轮构建已验证）。

---

## 4. 一个会让变体静默退化成基线的坑（本轮踩到）

### 4.1 现象

把 `CONFIG_LV_DRAW_BUF_ALIGN=128` 写进 `sdkconfig.extra.perf-ppa` 后**重建仍报同一条
`#error`**，而 `build/perf-ppa/sdkconfig` 里仍然是 `CONFIG_LV_DRAW_BUF_ALIGN=4`。

### 4.2 原因

`idf.py -DSDKCONFIG=build/<dir>/sdkconfig` 把该文件当成**用户自己的配置文件**。
文件一旦存在，CMake/IDF 只对它做**增量合并**，`SDKCONFIG_DEFAULTS` 链里的新值
**不会覆盖已存在的符号**。第一次构建（align=4）留下的 sdkconfig 吃掉了后来的 128。

### 4.3 解决

`Remove-Item build/perf-ppa/sdkconfig`（连 `sdkconfig.old`）后重建 ⇒ 成功。
删掉后实测 `build/perf-ppa/sdkconfig` 含 `CONFIG_LV_DRAW_BUF_ALIGN=128`、
`CONFIG_LV_USE_PPA=y`、`CONFIG_LV_USE_PPA_IMG=y`、`CONFIG_CACHE_L2_CACHE_LINE_SIZE=128`。

已把 `-Fresh` 开关写进 `tools/build_v4_matrix.ps1`，注释里记了这次的 `#error` 原文。

### 4.4 为什么写进报告

因为**这个坑不会报错、不会警告**：变体名、日志、JSON 全都正常，只有回读 sdkconfig
才能发现「你以为你编的是 perf-ppa，其实是 os」。以后任何人加新变体都会踩到，
所以 `effective` 字段是这套矩阵的必需品，不是装饰。

---

## 5. 五个变体全部构建完成（本轮实测）

`reports/perf-v4-4/build_matrix.json`（全部 `Project build complete`，全部 `-Fresh` 重建）：

| 变体 | 镜像字节 | `LV_USE_PPA` | `DRAW_BUF_ALIGN` | `DRAW_UNIT_CNT` | tear-avoid 生效项 |
|------|----------|--------------|------------------|-----------------|-------------------|
| `os`（基线） | 2,359,248 | — | 4 | 2 | `TRIPLE_PARTIAL=y` |
| `perf-unit1` | 2,358,896 | — | 4 | **1** | `TRIPLE_PARTIAL=y` |
| `perf-double` | 2,359,248 | — | 4 | 2 | **`DOUBLE_PARTIAL=y`** |
| `perf-full` | 2,359,248 | — | 4 | 2 | **`TRIPLE_FULL=y`** |
| `perf-ppa` | **2,360,784** | **y** | **128** | 2 | `TRIPLE_PARTIAL=y` |

读法：

- **每个变体都确认了 `effective` 里那一个符号真的变了**，不是静默退化：
  `perf-double` 的 sdkconfig 里是 `CONFIG_LW_TEAR_AVOID_DOUBLE_PARTIAL=y` 且
  `# CONFIG_LW_TEAR_AVOID_TRIPLE_PARTIAL is not set`；`perf-full` 同理。
- `perf-ppa` 比 `os` 大 **1,536 B**（2,360,784 − 2,359,248），这是 PPA 三个 client
  的代码量。
- `perf-double` / `perf-full` 与 `os` **字节数完全相同**（2,359,248）：tear-avoid
  只是运行期枚举值，代码量不变，符合预期。
- `perf-unit1` 比 `os` 小 **352 B**。
- `os` 相对 V4.3 基线 2,358,000 B 涨 **1,248 B**（约 0.05%，含本轮全部插桩）；
  分区余量 63%（`0x3c0030` / `0x600000`）。

---

## 6. 【待真机】对照矩阵怎么读

### 6.1 每变体的固定动作

```
lw> sched reset          # 清统计
（在该页连续操作 10 秒：走动、点按钮、切页）
lw> sched                # 记 avg_gap / max_gap
lw> fps reset
（再动 10 秒）
lw> fps                  # 记 fps
lw> diag                 # 记 heap / psram / cache
```

六个页面 × 五个变体 = 30 组。至少要做桌面（0）、音乐（6）、RPG（9）、宠物（8）四页。

### 6.2 各变体要回答的问题

| 变体 | 要回答 | 若显著变好说明 |
|------|--------|----------------|
| `perf-ppa` | 硬件 2D 值不值得接 | 绘制是瓶颈（不是 flush、不是 I/O） |
| `perf-unit1` | 两个绘制单元在不在互相抢 | 并行绘制有调度开销；单线程反而稳 |
| `perf-double` | 少一个缓冲、轮转更短有没有用 | 帧缓冲轮转/等待是瓶颈 |
| `perf-full` | 全帧 flush 的成本上界 | 局部刷新的收益有多大（这是反向对照） |

### 6.3 已知的判读陷阱

1. **`fps` 只统计「至少提交一次 flush 的秒」**，页面静止会读 0。判断动没动要看
   `anim` 的帧索引和截图 SHA-256，不能只看 fps（`BASELINE.md` §2 已记录）。
2. **`sched` 测的是那个 20 ms LVGL 定时器的实际间隔**，不是渲染耗时。它变长说明
   LVGL 任务被拖住，但拖住的原因可能是绘制、可能是 I/O、也可能是别人抢 CPU。
3. **`perf-double` 省约 2.4 MB PSRAM**（3 个 1024×600 RGB565 帧缓冲 → 2 个）。
   所以它变快不能全归给「轮转更短」—— 也可能是 PSRAM 压力下降带来的连带效果。
4. **`perf-ppa` 把 `DRAW_BUF_ALIGN` 从 4 改成 128**，这会改变所有绘制缓冲的布局与
   cache 行行为。PPA 的收益里混着对齐的收益，要分开只能再编一个「只改对齐不接 PPA」
   的变体（本轮没做）。

---

## 7. 真机复测改变了本矩阵的优先级

### 7.1 决定性的单变量：`anim off`

板子在写这份文件的后期被用户接上了（`COM5` 的 CH343 变为 `Status=OK`），
于是在 `v4.3-baseline-dirty` 旧镜像上做了一次**只改一件事**的实验：

```
lw> anim off     # 关掉精灵帧推进（不是关页面，不是关绘制）
```

结果（同一块板、同一份存档、同一个页面，只改这一个开关）：

| 页面 | 动画 ON | 动画 OFF | 全屏合成量 |
|------|---------|----------|------------|
| RPG | **117 ms / max 119** | **20 ms / max 20** | 不变 |
| 宠物 | **477–746 ms / max 919** | **20 ms / max 20** | 不变 |

烧录本轮镜像后再测一遍（`v4.3-baseline-1-g04ad8e5-dirty`），结论不变：

| 页面 | 动画 ON（改动后） | 动画 OFF | 说明 |
|------|-------------------|----------|------|
| RPG | **119 ms / max 129** | **20 ms / max 20** | 遮挡守卫未改变绘制成本 |
| 宠物 | **251 ms / max 274** | **20 ms / max 20** | 缓存修好后剩下的全是绘制 |

**这两页的差别只有「精灵每帧换不换帧」**。全屏背景（`rpg-room.lwr` /
`daily-room.lwr`，各 1,228,800 B RAM 图）、全屏 ambience 半透明层、
壁纸绘制回调 —— 这些在 OFF 时**一个都没少**，合成量完全相同。

⇒ **主因是精灵重绘的绘制成本，不是 flush、不是显示链路、不是全屏合成。**

### 7.2 对本矩阵的三个后果

1. **优先级下调。** 本矩阵回答的是「20 ms 里还能再省多少」，而不是
   「为什么是 119 / 251 ms」。前者是收益优化，后者是交接书的核心痛点。
   所以在 `FINAL_PERFORMANCE.md` §7 的建议排序里，矩阵从第 5 位挪到了第 6 位。
2. **判据要改。** 原先设想的判据是「某个变体让 RPG/宠物页的 `avg_gap` 显著下降」。
   现在知道这两页的成本在 `lv_draw_image` 的缩放+alpha 混合上，而 tear-avoid
   模式与绘制单元数**都不改变每帧要混合的像素数**。所以即使矩阵全部跑完，
   最可能的结局是「音乐/桌面页 20 ms 里省下几毫秒，RPG/宠物页几乎不动」——
   **这本身就是一个有价值的结论**（它进一步确认瓶颈不在显示链路）。
3. **`perf-double` 的 PSRAM 副作用值得单独看。** 它省下约 2.4 MB 帧缓冲，
   如果它让宠物页变快，可能不是「轮转更短」，而是**给 12 MiB 缓存腾出了空间**
   —— 这两者在 `perf-double` 上是混在一起的，§6.3 第 3 条已经记录了这个陷阱。

### 7.3 本轮没做的事

- **没有**在真机上跑任何一组对照矩阵（五个变体一个都没刷）。
- **没有**改出厂默认的 tear-avoid 模式、没有改 `LV_DRAW_SW_DRAW_UNIT_CNT` 的出厂值、
  没有把 PPA 接进正式镜像。
- **没有**为了好看去动 `LV_DEF_REFR_PERIOD`（15 ms）—— 它是全局刷新节拍，改它会
  影响所有页面的观感与功耗，不属于「不改画质」范围内的实验。
- **没有**做「只改对齐不接 PPA」的第五个变体（§6.3 第 4 条需要它，但本轮没编）。
- **没有**测 `TE_SYNC` / `DOUBLE_DIRECT` / `DOUBLE_FULL` 这三种模式 —— 本轮只做了
  默认值两侧各一个（更省的 double-partial、更贵的 triple-full）。
- **没有**解释清楚「桌面页 20 ms 里那 0 ms 固定开销」——因为实测桌面页**已经就是
  20 ms**（等于定时器周期），本轮没找到可再压缩的余量。

---

## 8. 构建与测试（本轮实测）

| 检查 | 结果 |
|------|------|
| `tools/build_v4_matrix.ps1 -Configs @('os','perf-ppa','perf-unit1','perf-double','perf-full') -Fresh` | 5/5 `Project build complete`，`V4 BUILD MATRIX OK` |
| `os` 镜像 | 2,359,248 B，分区余 63% |
| `perf-unit1` | 2,358,896 B |
| `perf-double` | 2,359,248 B |
| `perf-full` | 2,359,248 B |
| `perf-ppa` | 2,360,784 B |
| 镜像内字符串扫描 | 含 `boot phases`、`boot: frames=`、`sched: ticks=`、`rpg %d: tile=` |
| `tools/run_host_tests.ps1` | 142 checks, 0 failure(s) — `HOST TESTS PASSED` |
| `tools/run_core_regression.ps1` | Duo 82 / RPG 336 / Storage 33，0 failure — `CORE REGRESSION PASSED` |

**这些都是编译与静态验证，不是性能数据。** 性能结论必须等 §6 在真机上跑完。
