# P0-F · 综合验证

分支 `perf/v4-4-fluidity-overhaul`，基线 `v4.3-baseline`。

---

## 0. 先说最重要的一件事

**本轮分三个阶段：前半段没有开发板，中段用户把板子接上了，末段用户批准烧录。**

第一阶段：`Get-PnpDevice -Class Ports -PresentOnly` 只有两条蓝牙串口（COM7 / COM8），
CH343 的 `COM5`（`USB-VID_1A86&PID_55D3`）状态是 `Unknown`，复核了六次都是这个结果。

第二阶段（用户说「我接板子了」之后）：`COM5` 变成 `Status=OK`，于是补测了六页的
`sched`、`diag` 缓存计数器，并做了**决定性的 `anim off` 单变量实验**。但当时板上跑的
仍是 `v4.3-baseline-dirty` 旧镜像，所以那一批数据只解释了**改动前**的成因。

第三阶段（用户说「烧录」之后）：`tools/flash_v4_safe.ps1 -Stage app -Execute -Authorized`
写入本轮镜像（2,359,248 B，只写 `0x010000`，esptool `Hash of data verified.`），
版本串变为 `v4.3-baseline-1-g04ad8e5-dirty`，**本轮改动首次跑在板上**。

因此本轮拿到了三类数据：

- **改动前的成因**（第二阶段，硬数据）；
- **改动后的收益**（第三阶段，硬数据）——这是本文件 §1 与 §4 的主要更新来源；
- **仍未测得**：点击延迟 <50 ms、五变体显示矩阵的真机对照、p50/p95/p99 直方图。

---

## 1. 验收标准逐条对照

| 标准 | 要求 | 本轮状态 | 依据 |
|------|------|----------|------|
| 真机 40–50 FPS | 六页实测 | **未达成**：音乐 20 ms ✅、桌面 21 ms ✅、世界 21 ms ✅、**RPG 119 ms ❌**、**宠物 251 ms ❌** | `BASELINE.md` §8 |
| 点击响应 < 50 ms | 量化 | **未测** | 需新增时序探针（§5） |
| 连续 30 分钟无异常 | 压力 | **已测**（见 §4.6，32.1 分钟 / 162 样本） | `last_error` 全 `none`、`safe_mode` 全 `false`、`heap_free` 无单调下降 |
| P0-A 基线 | 六组 | ✅ 完成 + 两轮复测 + 烧后复测 | `BASELINE.md` §1/§7/§8 |
| P0-B RPG 连续移动 | 插值 | ✅ 插值已落地，成因 B 已定位为精灵绘制面积 | `RPG_MOTION.md` §4 |
| P0-C 精灵缓存/画布 | 分析+改动 | ✅ 改动落地**并已验证收益**（宠物 477–746 → 251 ms，I/O 归零） | `SPRITE_CACHE.md` §9 |
| P0-D 开机动画 | 分析+改动 | ✅ 插桩落地**并已拿到真机读数**（5 帧 / 2047 ms，`ui=1542 ms`） | `BOOT_ANIMATION.md` §7 |
| P0-E 显示对照 | 矩阵 | 🟡 五个镜像全部构建完成，**真机对照未做** | `DISPLAY_BENCHMARK.md` §5 |

**结论：本轮仍不能签收验收标准。** 五页里三页已达 20 ms 档（音乐/桌面/世界），
RPG 与宠物仍是 119 / 251 ms。能签收的是「真机成因定位 + 改动收益 + 测量能力 + 构建产物」。

三个验收项里，**只有「30 分钟无异常」这一条本轮真正测了**（32.1 分钟，162 样本，
无错误、无安全模式、无泄漏趋势，但发现桌面/世界页双稳态这一未解现象，见 §4.6.3）；
40–50 FPS 明确未达成；点击延迟 <50 ms 仍无探针。

---

## 2. 本轮实际改了什么

### 2.1 代码（14 个文件，+383 / −46）

| 文件 | 改动 | 属于 |
|------|------|------|
| `firmware/main/lw_art.c` | `LW_ART_CACHE_BUDGET` 4 MiB → **12 MiB** | P0-C |
| `firmware/components/lw_storage/src/lw_storage.c` | `ENTRIES` 16 → **32** | P0-C |
| `firmware/components/lw_ui/src/lw_rpg_view.c` | 遮挡换序加 `front_actor` 守卫 | P0-C |
| `firmware/components/lw_ui/src/lw_app_pet.c` | 5 处控件写入加值守卫 | P0-C |
| `firmware/main/lw_boot.c` | 五段启动计时 + `boot phases` 日志 | P0-D |
| `firmware/components/lw_ui/include/lw_os.h` | `lw_os_boot_stats_t` + 声明 | P0-D |
| `firmware/components/lw_ui/src/lw_os.c` | 开机遮罩统计实现 | P0-D |
| `firmware/main/lw_capture.c` | `boot` 控制台命令 | P0-D |
| `firmware/main/Kconfig.projbuild` | `choice LW_TEAR_AVOID` | P0-E |
| `firmware/main/app_main.c` | tear-avoid 编译期映射 | P0-E |
| `tools/build_v4_matrix.ps1` | 4 个 perf 变体 + `-Fresh` + `effective` 回读 | P0-E |
| `.gitignore` | 忽略 `reports/logs/v44/`（矩阵日志 0.64 MB） | 工具 |
| `reports/perf-v4-4/BASELINE.md` | 新增 §7 第二轮真机复测 + 推翻原文标注 | 报告 |
| `reports/perf-v4-4/RPG_MOTION.md` | §4 整节按真机数据重写 | 报告 |

新增文件：

- `firmware/sdkconfig.extra.perf-ppa`
- `firmware/sdkconfig.extra.perf-unit1`
- `firmware/sdkconfig.extra.perf-double`
- `firmware/sdkconfig.extra.perf-full`
- `tools/lwp_probe.py`（只读 LWPX 头解析，用于帧几何量化）
- `reports/perf-v4-4/` 下 5 份新报告 + `build_matrix.json`

### 2.2 量化依据（这三条是 P0-C 的核心）

**帧尺寸实测**（`tools/lwp_probe.py` 读包头）：

| 包 | 宽×高 | 单帧字节 |
|----|-------|----------|
| `male` / `qianqian-base` / `qianqian-wave` | 256×256 | 196,608 |
| **`daily`** | **320×320** | **307,200** |
| `ui` | 544×352 | 574,464 |
| `skin` | 512×544 | 835,584 |

**宠物页活跃帧**（`joint_bed` 决定狗在地面还是床上，两条分支互斥，
`dog.idle` 与 `dogbed.*` 不会同时活跃；两种分支同为 24 帧）：

```
couple.seated  16 × 307,200 = 4,915,200
dog.idle        8 × 307,200 = 2,457,600   （或 dogbed.look 8 帧，同为 2,457,600）
                             ---------
                             7,372,800 B = 24 帧
```

旧配置 16 槽 / 4 MiB。**连一个 `couple.seated` 都装不下**（4,915,200 > 4,194,304）。
真机实测确认了颠簸：宠物页动画 ON 时 10 秒 **+100 misses / +100 evictions**
（约 5 次 miss / tick），动画 OFF 时计数器**完全冻结**。

**但算术上必须记一笔：单次 miss 解释不了 777 ms。** 按 `lw_storage.c` 头注释
记录的实测「~9 ms 读 196,608 B」折算（`CONFIG_FATFS_SECTOR_4096=y`，
196,608 B = 48 扇区 ⇒ 0.188 ms/扇区），一个 320×320 帧（307,200 B = 75 扇区）
一次 miss 只要 **≈14.1 ms**。要凑出 777 ms 得每扇区 10.36 ms，比实测慢 **55 倍**。

> ⚠️ **原文在此处把「全屏重合成」列为另一条同量级候选（背景 blit + 全屏
> ambience 半透明层 = 1,228,800 px，777 ms 对应 1,581 Mpx/s）。**
> **这条已被真机 `anim off` 实验否证**：关掉帧推进后全屏合成量一点没变，
> 宠物页却从 488 ms 掉到 **20 ms**。⇒ 主因是**精灵重绘的绘制成本**，
> 不是 I/O 也不是全屏重合成。

真机实测的 I/O 占比：动画 ON 时 10 秒 **+100 misses / +100 evictions**
（≈10 miss/s × 14.1 ms = 141 ms/s），而宠物页一个 tick 是 488 ms
⇒ I/O 约占墙钟 **14%**，剩下 **86% 是绘制**。
所以 C-1 修好后最乐观也只能降到约 **419 ms**，要回 20 ms 必须再做 C-4。
详见 `SPRITE_CACHE.md` §2.3.2 与 §5.3。

**两个约束必须一起抬**：24 帧需要 24 个槽（`ENTRIES` 16 → 32），
24 × 307,200 = 7,372,800 B 又超过 4 MiB（预算 → 12 MiB）。只改一个都不生效。

---

## 3. 构建与测试（本轮实测，可复核）

### 3.1 宿主

```
ninja -C build/host
  [1/5] lw_app_pet.c.obj   [2/5] lw_os.c.obj   [3/5] lw_rpg_view.c.obj   [4/5] LittleWorld.exe

tools/run_host_tests.ps1        142 checks, 0 failure(s)      HOST TESTS PASSED
tools/run_core_regression.ps1   Duo 82 / RPG 336 / Storage 33  CORE REGRESSION PASSED
```

### 3.2 固件（五个镜像全部 `Project build complete`）

| 镜像 | 字节 | 关键配置 |
|------|------|----------|
| `build/os/little_world.bin` | 2,359,248 | 基线，`TRIPLE_PARTIAL`，`UNIT_CNT=2`，`ALIGN=4` |
| `build/perf-unit1/...` | 2,358,896 | `UNIT_CNT=1` |
| `build/perf-double/...` | 2,359,248 | `DOUBLE_PARTIAL=y` |
| `build/perf-full/...` | 2,359,248 | `TRIPLE_FULL=y` |
| `build/perf-ppa/...` | 2,360,784 | `LV_USE_PPA=y`、`PPA_IMG=y`、`ALIGN=128` |

分区余量 63%（`0x3c0030` / `0x600000`）。

**每个变体都从生成的 sdkconfig 回读了生效符号**，确认不是静默退化
（`DISPLAY_BENCHMARK.md` §4 记录了那个会让变体静默退化成基线的坑）。

### 3.3 与 V4.4 前一轮的对比

| 项 | 上一轮（`04ad8e5`） | 本轮 |
|----|---------------------|------|
| `os` 镜像 | 2,358,000 B | 2,359,248 B（+1,248） |
| 构建变体 | 9 个 | 13 个 |
| 报告 | 2 份 | 7 份 + `build_matrix.json` |

镜像只涨 1,248 B（约 0.05%），换来的全是测量能力与三处缓存/重绘改动。

---

## 4. 验证流程与实测结果

### 4.0 前置：烧新镜像 —— **已完成**

用户批准后执行（`User said (m01715): "烧录"`）：

```
tools/flash_v4_safe.ps1 -Config os -Stage app -Execute -Authorized
```

- 镜像：`firmware/build/os/little_world.bin`，**2,359,248 B**，
  sha256 `D89774A5AE322FE367681E139F5EE55C7C9D721F0709C09E4130903054666904`；
- 只写 `0x010000` 应用分区（`app fits: 2359248 / 6291456 B (37.5% of factory)`）；
- 不碰 nvs / phy_init / storage / C6 / eFuse，无 `erase_flash`、无失败重试；
- esptool：`Wrote 2359248 bytes (1105319 compressed) at 0x00010000 in 30.3 seconds`、
  **`Hash of data verified.`**、`Hard resetting via RTS pin...`；
- 日志：`reports/logs/flash/flash-20261028-174129.log`。

烧后串口确认三件事全部命中：`App version: v4.3-baseline-1-g04ad8e5-dirty`
（不再是 `v4.3-baseline-dirty`）、出现 `boot phases:` 一行、`boot` 命令可用。

### 4.1 第一件事：确认基线没有退化 —— **已测，无退化**

```
lw> sched reset → 8~20 s → sched
```

| 页面 | 改动前 | 改动后 | 判定 |
|------|--------|--------|------|
| 音乐 | 20 ms | **20 ms / max 20** | 参照系，无退化 |
| 桌面 | 20 ms（预热窗口 156） | **21 ms / max 36** | 无退化 |
| 世界 | 预热后 34–49，偶发 157 | **21 ms / max 37** | 无退化，预热窗口消失 |

判据：**音乐 20 ms、桌面 20 ms 应当保持**。实测保持 ⇒ 本轮的缓存/守卫改动
没有副作用。

### 4.2 第二件事：宠物页 —— **已测，收益确认**

```
lw> diagjson        # 记 cache hits/misses/evictions/reads/resident/budget
lw> sched reset
（20 秒）
lw> sched
lw> diagjson        # 算差值
```

**实测结果（本轮镜像）**：

- `sched: ticks=81 avg_gap=251 ms max_gap=274 ms`（20 秒窗口）；
- `cache_misses=48`、`cache_evictions=13` 在 20 秒里**完全不动**；
- `cache_resident=9,584,640`（旧配置钉在 3,145,728）、`cache_budget=12,582,912`。

判据全部命中：`resident` 从 3.15 MB 涨到 9.58 MB 并稳定、`evictions` 归零、
耗时 477–746 ms → **251 ms**（比 §2.3.2 预测的 419 ms 更好）。

**剩余 251 ms 的归属已用单变量确认**：同一页面 `anim off` 后
`sched: ticks=776 avg_gap=20 ms max_gap=20 ms` ⇒ 剩下的全是绘制成本，
转 C-4（`SPRITE_CACHE.md` §9.2）。

### 4.3 第三件事：开机 —— **已测**

```
boot: frames=5 elapsed=2047 ms visible=1877 ms fps=2 active=0 skipped=0
boot phases: hw=31 art=141 content=2 ui=1542 apply=0 total=1717 ms
```

判据（`BOOT_ANIMATION.md` §3.2）：`frames≈100 / elapsed≈2000` 是按设计跑满；
`frames≈20` 是 tick 被拉到 100 ms。**实测 5 帧，比最差一档还差 4 倍**（409 ms/帧）。
`elapsed=2047` 精确等于设计值 ⇒ 时间轴没问题，是绘制跟不上。
`ui=1542 ms` 占首屏总时间 89.9% ⇒ 开机时间几乎全是首屏构建，不是 I/O。

### 4.4 第四件事：RPG —— **已测，如预测无收益**

```
lw> rpg             # 连读 10 次，看 screen 是否还跳
lw> sched reset
（8 秒）
lw> sched
```

**实测结果（本轮镜像）**：`sched: ticks=12 avg_gap=119 ms max_gap=129 ms`，
`cache_misses=24`、`cache_evictions=0`（20 秒零增长）。

判据命中：改动前是 117 ms / max 119，改动后 119 ms / max 129 ⇒ **守卫改动没有
带来收益**，与预测一致（`max_gap` 只比 `avg` 高 10 ms，本来就没有尖刺）。
⇒ 确认必须做 C-4 画布裁剪，而不是继续找缓存/换序原因。

### 4.5 第五件事：显示对照矩阵（P0-E）—— **仍未测**

逐个刷 `perf-unit1` / `perf-double` / `perf-full` / `perf-ppa`，每个都测同一组
四个页面。**音乐页 20 ms 是全局参照系** —— 任何变体让音乐页变差，说明改的是
全局路径而不是目标页面。五个镜像已构建完成（`build_matrix.json`），**一个都没烧**。

### 4.6 第六件事：30 分钟压力 —— **已测**

`tools/stress_v44.ps1`（本轮新增）按墙钟跑 30 分钟，六页轮转，每分钟采样
`diagjson` 的 `heap_free` / `psram_free` / `cache_resident` / `cache_evictions` /
`last_error` / `safe_mode`，并用 `sched reset` + 6 秒窗口取**当前页**的 `avg_gap`
（而不是开机以来的累计平均 —— 早期版本犯过这个错，累计平均会随每次切页重建
整屏而单调上升，看着像退化其实是统计口径错）。

日志：`reports/logs/v44/stress-<timestamp>.log`。

判据：30 分钟内 `heap_free` 与 `psram_free` 不应出现**不可回收的单调下降**；
`last_error` 必须全程 `none`；`safe_mode` 必须全程 `false`。

#### 4.6.1 实测结果（32.1 分钟，162 样本）

逐字汇总：

```
# soak finished after 32.1 minutes, 162 samples
heap_free first=18062779 last=17193963 delta=-868816 min=12942911
psram_free first=18018564 last=17150212 delta=-868352
cache_evictions first=828 last=2042
samples with non-none last_error: 0
samples with safe_mode true: 0
samples that did not reach the intended page: 7 of 161
misrouted: rpg->id 0, rpg->id 0, rpg->id 0, rpg->id 0, rpg->id 0, rpg->id 0, rpg->id 0
```

三条判据全部通过：`last_error` 全程 `none`，`safe_mode` 全程 `false`，
`heap_free` 没有不可回收的单调下降。

**`heap_free` 的 −868,816 B 不是泄漏。** 同一窗口里 `cache_resident` 从
6,930,432 涨到 8,146,944（**+1,216,512 B**），`cache_evictions` 从 828 涨到 2042。
两者量级一致、方向相反：PSRAM 里的帧缓存多驻留了约 1.2 MB，堆就少了约 0.87 MB。
`heap_free` 的谷值 12,942,911 出现在宠物页样本上（该页 `resident=9,584,640`，
是六页里驻留最多的），也符合「驻留多的页面堆余量小」而不是「随时间下滑」。

#### 4.6.2 顺带发现：脚本的页面标签曾经不可信

`stress_v44.ps1` 原版只按「打算去哪一页」给行打标签，从不校验板子实际停在哪页。
加上校验后立刻兑现：**7 次 RPG 导航全部落在桌面页（`id 0`）**，因为 RPG 不是
dock 项，只能从世界页的「小冒险」按钮进入，而脚本的 RPG 步骤有时先撞上别的东西。

这条直接影响上一轮 soak 的解读：**「桌面页 116 ms」里有一部分其实是 RPG 页读数
被贴到了桌面行上**。修好之后剩下的现象才是真的（见 4.6.3）。

#### 4.6.3 未解现象：桌面/世界页在 21 ms 与 116 ms 之间成串跳变

即使排除错页样本，桌面页与世界页**仍然**在快慢两态之间切换，且两页同步：

| 样本 | 桌面 `avg/max` | 世界 `avg/max` |
|---|---|---|
| #1–#68（约 20 分钟） | 21–25 / 36–57 | 21–24 / 34–56 |
| #73–#98（约 5 分钟） | **116** / 131–142 | **117** / 127–139 |
| #103–#110 | 22 / 53–54 | 23–24 / 40–41 |
| #115–#158（约 7 分钟） | **114–116** / 131–144 | **113–118** / 125–139 |

音乐页与回忆页**全程** `20/20`，不受影响。

三个单变量实验（`tools/probe_desktop_slow.ps1`，本轮新增）：

1. **不是泄漏。** 慢态下连续 31 个样本，`anim_sprites` 恒为 2、
   `asset_leases` 恒为 3（两角色帧 + 皮肤图集 835,584 B）、`cache_resident`
   恒 8,146,944、`cache_evictions` 恒 2042（完全冻结，零 I/O）。
2. **慢的仍然是精灵帧推进。** 同一慢态下 `anim off` →
   `sched: ticks=313 avg_gap=20 ms max_gap=20 ms`；`anim on` →
   `sched: ticks=55 avg_gap=113 ms max_gap=135 ms`。
3. **一次整页重建能把慢态重置为快态。** 慢态（116 ms）下做一次往返
   （桌面 → 音乐页 `page 6, dock open` → 返回桌面 `page 0, dock closed`），
   回来立刻测得 `avg_gap=33 ms`；随后 7 分钟 22 个样本全部 31–33 ms，
   没有再次漂移回慢态。

**结论：桌面页与世界页存在两个态，绘制成本差约 3.6 倍（116 / 32），
差别不在精灵数量、不在缓存驻留、不在 I/O，而在「这一次渲染之后精灵是怎么被
设置的」。为什么同一份 `male.idle` / `qianqian.idle` 在两个态下差 3.6 倍，
本轮没有定论。** 已排除的候选见 `SPRITE_CACHE.md` §9.5。

### 4.7 第七件事：点击延迟 < 50 ms

**当前没有现成探针。** `tap <x> <y>` 是合成指针，它只注入事件，不测响应。
要量化需要新增：在 `lw_capture.c` 的 `cmd_tap` 里记录注入时刻，在页面切换或
控件回调里记录完成时刻，取差值。**本轮没做**，因为需要真机才能验证探针本身
是否正确 —— 加一个没测过的探针只会让报告多一个不可信的数字。

---

## 5. 未测项清单（诚实标注）

| 项 | 状态 |
|----|-----------|
| 六页改动前 `sched` | ✅ **已测**（`BASELINE.md` §7：音乐 20 / 桌面 20 / 世界 34–49 / RPG 117 / 宠物 477–746） |
| 六页改动后 `sched` | ✅ **已测**（`BASELINE.md` §8：音乐 20 / 桌面 21 / 世界 21 / RPG 119 / 宠物 251） |
| `anim off` 单变量 | ✅ **已测**（改动前 RPG 117→20、宠物 488→20；改动后 RPG 119→20、宠物 251→20） |
| 宠物页改动后 `cache` 差值 | ✅ **已测**（20 秒零 miss/零 eviction，`resident` 9,584,640） |
| `boot phases` 五段耗时 | ✅ **已测**（`hw=31 art=141 content=2 ui=1542 apply=0 total=1717 ms`） |
| `boot` 的 frames/elapsed/visible | ✅ **已测**（`frames=5 elapsed=2047 visible=1877 fps=2`） |
| 五个变体的对照帧率 | ⬜ 无（镜像已构建，**未烧**） |
| 点击延迟 p50/p95 | ⬜ 无（且探针未实现） |
| 30 分钟压力与泄漏趋势 | ✅ **已测**（`tools/stress_v44.ps1`，`reports/logs/v44/stress-*.log`） |
| p50/p95/p99 直方图 | ⬜ 无（只有 `avg_gap` / `max_gap`） |
| `fps` 读数 | ✅ 已测但**基本无效**（RPG `fps: 1`、reset 后 `fps: 0`） |

---

## 6. 风险与已知副作用

1. **缓存从 4 MiB 抬到 12 MiB**，租约预算 2 MiB 不变，合计 14 MiB。烧后实测
   `psram_free=20,336,952 B`（约 19.4 MB 空闲）、`psram_largest=19,922,944 B`，
   在范围内。**但注意：`largest` 从改动前的 27,262,976 降到 19,922,944**，
   因为 12 MiB 缓存是从 PSRAM 里实占的。若后续再加 PSRAM 消费者（比如给 C-4
   做预乘 bbox 表），要先看 `largest` 还剩多少。
2. **`ENTRIES` 16 → 32** 会让 `lw_file_cache_stats_t` 之外的结构体变大
   （`entries[ENTRIES]` 是静态数组，每个 `entry_t` 含 `cache_key_t` 的
   `char file[128]`）。约 +16 × 152 B ≈ 2.4 KB，可忽略。
3. **`perf-ppa` 强制 `LV_DRAW_BUF_ALIGN=128`**，会改变所有绘制缓冲的分配行为。
   它是测量构建，**不应出厂**。
4. **`boot` 命令与 `boot phases` 日志已在真机验证**（§4.3），不再是编译层验证。
5. **C-1 的收益已确认但有限**：宠物页 477–746 → 251 ms，`evictions` 归零。
   **「缓存修好了」≠「宠物页流畅了」**：剩下的 251 ms 全是绘制，`anim off`
   后回到 20 ms 就是证据。**不要把 C-1 记成宠物页已解决。**
6. **C-2 与 C-3 没有可测收益**（RPG 119 ms 无变化；C-3 被绘制成本淹没）。
   它们是正确性/整洁性改动，不是性能修复。报告与提交信息都不应把它们算作
   「本轮提速」的一部分。

---

## 7. 下一步建议（按性价比排序）

1. **C-4 精灵画布裁剪**（`SPRITE_CACHE.md` §5.3–§5.4、§9.2）。**现在是第一位。**
   真机三轮数据把它从「可选项」钉成了唯一路径：RPG 119 ms、宠物 251 ms、
   开机 409 ms/帧，三个数字的共同分母都是「每帧要重绘的精灵面积」。
   288×288 画布只有约 22,896 / 82,944 px（27.6%）有内容，约 72% 的像素混合后
   被丢弃。若按比例见效，RPG 理论上限 32–40 ms。
   **注意风险**：`LV_EVENT_HIT_TEST` 在对象尺寸≠图像尺寸时退回矩形命中，
   桌面页的 hit box 与精灵绑定需要单独确认。
2. **开机期间暂停 `update_actors(dt)`**（`BOOT_ANIMATION.md` §7.3）。
   开机遮罩完全盖住桌面角色，画了也看不见；同屏精灵从 4 个降到 2 个。
   改动极小，收益直接（当前 409 ms/帧）。
3. **首屏构建 `ui=1542 ms`**（`BOOT_ANIMATION.md` §7.1）。占开机总时间 89.9%。
   这一段是 `lv_obj_clean` + 全屏重建 + 6 个九宫格按钮（每个 ≈9 个 `lv_image`）
   + 2 个精灵 + 开机遮罩。**这是「开机慢」的真正主体**，比开机动画本身重要。
4. **宠物页剩余 251 ms**：已定位为绘制，与第 1 项同源，C-4 一并解决。
5. **显示矩阵**（§4.5）。四个变体 × 四页，工作量大，且真机已把它的优先级
   下调为「20 ms 里还能再省多少」，放在 1–4 之后。
6. **点击延迟探针**（§4.7）。需要先写探针再上板验证，建议单独一轮。
7. **p50/p95/p99 直方图**。当前只有 `avg_gap`/`max_gap`，看不到分布形态；
   做 C-4 前后对比时，直方图比均值更能说明「卡顿是否消失」。

**已不再是待办的项**：烧录（已完成）、宠物页缓存差值（已测，收益确认）、
RPG `sched` 复测（已测，确认无收益、转向 C-4）、`boot` 读数（已测）、
30 分钟压力（已测）。
