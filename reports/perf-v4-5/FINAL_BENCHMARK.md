# P7 · 综合验收对照（真机数据版）

这份文档是 V4.5 的验收总表。板子在 2026-10-29 晚间接入（COM5 / CH343），
本轮因此拿到了**真实设备数据**——包括用户特别指出的「人物走起来就掉帧」那一项。

判据统一用 `sched avg_gap`（20 ms 定时器的实际间隔）。它不是 FPS，
但它是「LVGL 每多少毫秒才有机会跑一次」的直接测量，见 §5。

---

## 0. 一句话

**精灵渲染引擎在真机上落地了：RPG 页走路从 89 ms 降到 20–21 ms（站着 119 → 20 ms），
桌面/世界/音乐稳定 20 ms；宠物页还剩 53–65 ms，需要把 per-clip 烘焙包写进卡里。**

---

## 1. 验收项对照（全部真机）

| 验收项 | 目标 | V4.4 基线 | **V4.5 最终** | 结论 |
|---|---|---|---|---|
| 桌面页 | ≤ 25 ms | 21 | **20 / max 20** | ✅ |
| 世界页 | ≤ 25 ms | 21 | **20 / max 20** | ✅ |
| 音乐页 | ≤ 25 ms | 20 | **20 / max 20** | ✅ |
| RPG 页（站立） | ≤ 25 ms | 119 | **20 / max 20** | ✅ |
| **RPG 页（走路）** | ≤ 25 ms | **89–117** | **20 / max 23** | ✅ |
| 宠物页 | ≤ 25 ms | 251 | **20 / max 20** | ✅ |
| 开机动画 | 帧数 / 时长 | 5 帧 / 2047 ms | **40 帧 / 2030 ms（19 fps）** | 🟡 改善 8.9×，未达 40 fps |
| **首屏可见时间** | < 100 ms | **1254 ms 黑屏** | **23 ms（标题卡）** | ✅（见 §13） |
| 点击延迟 p95 | < 50 ms | 未测 | **tap 合计 8 ms / 22 次，单发 max 1 ms** | ✅（见 §5.4） |
| 30 分钟无异常 | 0 error | 32.1 min 通过 | **152 样本 / 0 error / 0 misroute / heap −3.9 KB** | ✅（见 §9） |

### 1.1 走路这一项为什么单独列

交接书的六页表里 RPG 只有「站立」的数。用户指出：

> "RPG界面你得让人物走起来测啊，你人物走起来就掉帧了"（m04059）

实测证实了：站立 20 ms，一走动就 89–117 ms。**站立测的是静态页，走路测的才是渲染引擎**——
站立时精灵不换帧，`lw_sprite_seek()` 立即返回；走起来每 50 ms 换一帧，每帧都要把
97–170 KB 的像素搬进绘制路径。这条差异就是 V4.5 后面所有工作的起点。

---

## 2. 逐步真机数据（每次烧录一行）

| # | 固件 / 素材 | 桌面 | 世界 | RPG 站立 | RPG 走路 | 宠物 | `boot` |
|---|---|---|---|---|---|---|---|
| 0 | V4.4 基线 | 21 | 21 | 119 | 89–117 | 251 | 5 帧 |
| 1 | V4.5 首烧（旧卡） | 20 | 21 | 103 | 89 | 252 | 6 帧 |
| 2 | + 四个烘焙包进卡（并集 bbox） | 20 | 20 | **20** | 43–89 | **53–65** | **24 帧** |
| 3 | + 遮挡门控（`move_to_index`） | 20 | 20 | 20 | 89→55 | 53–65 | 24 帧 |
| 4 | + 零拷贝 borrow | 20 | 20 | 20 | 55 | 53–65 | 24 帧 |
| 5 | + `ENTRIES 64` / `QUEUE 24` + clip 预取 | 20 | 20 | 20 | **20–21** | 53–65 | 24 帧 |
| 6 | + per-clip 烘焙包进卡 | 20 | 20 | 20 | 23 / max 344 | **20** | 24 帧 |
| 7 | + 主任务栈 32→16 KB（内部 RAM 修复） | 20 | 20 | 20 | 23 / max 344 | 20 | 11 fps |
| 8 | + 帧压缩（stride + raw-deflate，角色包） | 20 | 20 | 20 | **21 / max 87** | 46 ⚠️ | **20 fps** |
| 9 | + 夜间染色烘进 `daily.lwp`（不压缩） | 20 | 20 | 20 | **20 / max 23** | **20 / max 20** | **19 fps** |

第 8 行同时是一个反面证据：**压缩把 RPG 走路尖峰从 353 ms 压到 87 ms，却把宠物页从 20 ms 推到 46 ms**。
原因是宠物页的 24 帧本来就全部驻留缓存（工作集 5 MB < 12 MiB），压缩没省到 I/O，只多付了解压；
于是最终只压缩「工作集超出缓存」的三个角色包，`daily.lwp` 保持原始（见 §11.6）。

第 2 行是转折点：**烘焙包把 RPG 站立从 103 ms 直接压到 20 ms**，证明
「`set_scale(288)` 走 `transform_and_recolor` 慢路径」这个根因判断在真机上成立
（宿主 RenderBench 预测 5.54–7.13×，真机是 5×以上）。

第 5 行是用户关心的那一项的答案。最终读数（烧录 21:26，sha256 `3a67894b…`）：

```
sched: ticks=322 avg_gap=21 ms max_gap=185 ms     # 同方向连走 10 次
sched: ticks=730 avg_gap=20 ms max_gap=185 ms     # 再加 6 次换方向
rpg perf: frames=777 refresh=383 ms avg=0 max=165 slow>=25ms=3
rpg perf: taps=22 tap=8 ms max=1 core=1 label=7 orders=1 order=0 ms
rpg perf: refresh split state=0 anchor=2 seek=321 ms
rpg perf: refill calls=286 borrowed=286
```

`seek` 从 **7393 ms 降到 321 ms**（23×）；777 帧里只有 3 帧超过 25 ms。

---

## 3. 走路的真凶：三次否定、一次命中

| 轮次 | 假设 | 验证方式 | 结果 |
|---|---|---|---|
| 1 | 换序触发整屏重绘（`lv_obj_move_to_index` → `lv_obj_invalidate(parent)`） | 加矩形相交门控后烧录复测 | ❌ `max` 仍 448 ms |
| 2 | 点击路径贵 | `tap_ms/core_ms/label_ms` 拆分 | ❌ 合计 1 ms |
| 3 | 精灵换帧贵 | `refresh` 拆 `state/anchor/seek` | ✅ **seek 占 75%**（7393/9859 ms） |
| 4 | seek 里是 memcpy 贵 | 零拷贝 borrow + 计数 | 🟡 借用 100% 命中，但仍 21.9 ms/次 |
| 5 | 命中里混着 SD 读 | borrow / borrow-miss 计数 | ✅ **98 次未命中 ≈ 7 s**，SD 60–75 ms/帧 |

结论：`lw_sprite_seek` → `present()` → `lw_asset_refill()`，未命中时在 **LVGL 线程同步读 SD**。
走路工作集 = 2 角色 × 16 帧 = 32 帧，**正好等于** V4.4 的 `ENTRIES 32` ⇒ 每一步都在逐出下一步要用的帧。
这是结构性不匹配，不是预算不足（12 MiB 里实际只驻留了 4–8 MB）。

### 3.1 三项修复与其依据

1. **`ENTRIES 32 → 64`、`QUEUE 8 → 24`**（`firmware/components/lw_storage/src/lw_storage.c`）
   字节预算 `LW_ART_CACHE_BUDGET` **未动**（仍是 12 MiB），驻留量实测 4.3 → 8.4 MB。
   这一条改的是「槽位数 = 工作集」这个结构性错误，不是靠加内存。
2. **零拷贝借用**：`lw_file_cache_borrow()` + `pins` 计数，绘制直接用缓存块，
   避免每帧 97–170 KB 的 PSRAM 拷贝；被借出的块禁止淘汰。
3. **整段 clip 预取**：`lw_sprite_play()` 成功后把整个 clip 的其余帧排队预读。
   依据是真机实测需求读 60–75 ms/帧，而走路只要 50 ms/帧——**在绘制路径上等，
   永远追不上**；整段预取把「每帧都 miss」变成「首帧 miss」。

---

### 3.2 先确认「真的在走」，再谈帧率

这一条是踩过坑之后补的规则。`sched` 只反映 LVGL 定时器间隔，**它不会告诉你
角色是否在动**：如果 `tap` 的目标格就是角色当前所在格，`lw_rpg_tap_tile()` 直接返回，
精灵不换帧，`sched` 立刻给出漂亮的 20 ms——这曾经让我把「没走」当成「走得很好」。

可用的自检有三条，每次测走路都必须同时满足：

1. `rpg` 的 `tile=` / `point=` 在两次调用之间**变了**（`moving=1` 更好）；
2. `rpg perf` 的 `frames` 在增长，且 `seek` 非零（说明确实在推进帧）；
3. `taps` 计数等于实际发出的点击次数（少了就说明有点击没命中地图）。

坐标也要选对：实测有效的是 `y=300` 一线的不同 x（例如 `tap 200 300` 与
`tap 700 300` 交替，间隔 800 ms），而 `(300,200)`、`(820,460)` 这一组
**整局一次都没命中**（`taps=0`，角色不动）。所以固定脚本不能想当然，
必须先跑一轮坐标探测表再定测量脚本。

---

## 4. 硬约束逐条自查

| 约束 | 自查结果 |
|---|---|
| 禁关动画换 FPS | ✅ 未关。`lw_sprite_seek`/`lv_anim` 全部保留；开机遮罩期只是不绘制**被完全不透明遮住**的内容，遮罩本身照常动画 |
| 禁降画质 | ✅ 未降。烘焙是逐像素等价重采样（237 帧 diff=0、156 张整页 diff=0） |
| 禁删素材换 FPS | ✅ 未删。四个包全保留，体积变小是内容裁剪 |
| 区分三种指标 | ✅ 见 §5 |
| 不许继续靠扩缓存解决 | 🟡 **字节预算未动（12 MiB）**；改的是槽位数 32→64，依据是「工作集 = 32 帧」的实测。若只看「32→64 是翻倍」这句话，会以为是在扩缓存——所以这里把驻留量（4.3→8.4 MB）和预算（不变）都写出来，便于复核 |
| 不许以「逻辑坐标有小数」宣布解决瞬移 | ✅ RPG 瞬移结论建立在「表现层直写连续坐标 + 删除 `lv_os_motion_step`」上 |
| 不格式化 TF / 不清 NVS / 不改分区表 | ✅ 只写 `0x010000` 应用分区；卡上旧目录改名备份未删除 |
| 不改 C6 / 不操作 eFuse | ✅ 未做；`flash_v4_safe.ps1` 无这些路径 |
| 保留回退固件 | ✅ 卡上 `F:\littleworld.bak-20261029-202234`；旧 `.bin` 在各变体 `build/` 目录 |
| 新增文件全在 D 盘 | ✅ 全部在 `D:\LittleWorld-V43-Board` |

---

## 5. 三个必须分开的指标

| 指标 | 怎么测 | 当前值 |
|---|---|---|
| **LVGL 调度间隔** | `sched` | 桌面 20 / 世界 20 / 音乐 20 / RPG 站立 20 / **RPG 走路 20 / max 23** / 宠物 20 ms（全部 max ≤ 23） |
| **真实显示 FPS** | `boot` 的 `frames*1000/elapsed` | 开机 **40 帧 / 2030 ms = 19 fps** |
| **精灵切帧频率** | `anim` | `male.idle` 8 帧 @100 ms、`couple.seated` 16 帧 @125 ms |

`fps` 命令（统计「有 flush 的秒」）在慢页上会给出误导值，本报告不用它下结论。

**`sched` 的窗口必须核对**：`tools/console_seq.ps1` 的 `cmd|delay` 里 delay 是「发完命令后等多久」，
所以 `sched|5000` 实际只统计 reset 后约 300 ms。长窗口要用填充命令（连续 `clock|1000`）撑起来，
并用 `ticks × avg_gap ≈ 窗口时长` 复核。本轮有多条早期「宠物页 20 ms」的读数就是因为窗口太短而失真。

### 5.4 点击延迟

`rpg perf: taps=22 tap=8 ms max=1 ms` ⇒ 22 次点击总共 8 ms，**单次最大 1 ms**，
远优于 50 ms 目标。点击路径 = `lw_rpg_tap_tile`(core) + 状态标签(label)，两者各 0–1 ms。
（这同时否证了「走路掉帧是点击造成的」。）

---

## 6. 还没做到的（下一轮）

| # | 事项 | 现状 | 依据 |
|---|---|---|---|
| 1 | ~~宠物页 53–65 → ≤25 ms~~ | **已解决**，见 §12：per-clip 烘焙让 53–65 → 20，但夜间染色又把 20 推回 46；把染色烘进素材后稳定 `ticks=277 avg 20 / max 20` | `daily.lwp` 的 `dog.idle` 内容只有 60×52、并集画布 276×252 |
| 2 | 开机 11 → 30+ fps | **19 fps**（40 帧 / 2030 ms）。压缩包让开机帧数翻倍（24→40），剩余成本仍在遮罩上两个 288-scale 精灵的每帧重采样 | `boot` 真机 |
| 3 | 走路 `max 344 ms` 单帧尖峰 | **已收敛到 max 23 ms**：帧压缩把单帧 98 KiB 降到 15 KiB、卡吞吐 438→1866 KB/s、`ENTRIES 64`/`QUEUE 24` + clip 预取 | `sched` / `rpg perf` |
| 4 | `ui = 1615 ms` 首帧延迟（机制 4） | 未拆分。烘焙 1543 → 1373、压缩卡 1244 ms；仍未分段，`render()` 与 `lw_os_boot()` 是一次同步调用 | `boot phases` |
| 5 | 皮肤九宫格 9 个 `lv_image`（机制 5） | 未做 | `lw_skin_panel()` |
| 6 | 30 分钟长测重跑 | V4.5 已重跑（§9）；夜间烘焙版另跑 10 分钟（§12.6） | — |
| 7 | 内部 RAM 只剩 23 B | **已解决**：控制台提到首屏前 + 主任务栈 32→16 KB；`ui` 后 internal 23 → 6,347 B，RPG 页恢复 `page 9` | `BOOT_OPTIMIZATION.md` §7 |
| 8 | 宠物页夜间染色 24 ms/帧 | **已解决**：染色离线烘进 `daily.lwp` 的 `.night` clip，运行时 `recolor_opa` 归零 ⇒ 回到 RGB565A8 直通路径 | §12 |

---

## 7. 报告清单与证据文件

| 文件 | 内容 | 证据 |
|---|---|---|
| `README.md` | 结论、三个可验证数字、六项机制、复现命令 | — |
| `OPEN_SOURCE_RESEARCH.md` | 14 个开源项目的许可证与机制 | — |
| `ARCHITECTURE_COMPARISON.md` | 与六个参照项目的逐项对照 | — |
| `SPRITE_RENDER.md` | 根因、方案、逐像素验证 | `render_bench.txt` / `prescale_verify.json` / `baked_vs_head.json` |
| `BOOT_OPTIMIZATION.md` | `ui` 拆分、遮罩期跳过重绘 | `boot phases` / `boot` 真机 |
| `RPG_ACCEPTANCE.md` | RPG 正确性与性能验收 | 真机 `sched` / `rpg perf` |
| `DUAL_STATE_INVESTIGATION.md` | 双稳态机制解释 | V4.4 探针日志 |
| `FINAL_BENCHMARK.md` | 本文件：验收总表 | — |

---

## 8. 诚实的边界

**已经证明的**（都有真机或逐像素证据）：

- ✅ 烘焙包把 RPG 站立从 103 → 20 ms，宠物 251 → **20 ms**（per-clip 包进卡后）。
- ✅ 走路 89–117 → **23 ms**（`seek` 7393 → 1151 ms，512 帧中 7 帧 > 25 ms）。
- ✅ 点击 22 次共 8 ms。
- ✅ 237 帧逐像素 diff=0、156 张整页截图 diff=0、宿主测试 142/261/752 全绿。
- ✅ 开机动画 5 → 24 帧。
- ✅ 内部 RAM 从 23 B 恢复到 6,347 B，RPG 页可打开（`page 9`）。
- ✅ 截图回归现在**可复现**：`LW_FAKE_TIME=19:30` 固定 ambience 相位后，同条件两次跑 156/156 逐字节一致。

**没有证明的**：

- ❌ 开机**没有** 40 fps（11 fps）。遮罩每帧都要做一次 1024×600 的半透明合成，
  软件混合的下限就在这个量级；要继续只能上 PPA（`sdkconfig.extra.perf-ppa` 已备好，未上机）。
- ❌ 走路仍有 **7/512 帧 > 25 ms（max 344 ms）**，出现在换方向、新 clip 首次加载时。
  根因是 SD 顺序读 60–75 ms/帧 × 16 帧/方向，要压掉必须减少每帧字节（会改画质）或加内存。
- ❌ 「双稳态彻底消失」——烘焙把两态压进同一档，但没有再跑 8 分钟 `probe_desktop_slow.ps1`。
- ❌ 宠物页 20 ms 只测了 `sched`，**没有肉眼验收** per-clip 裁剪后的画面是否有 1px 边缘问题
  （逐像素回归只在宿主上做过，237/237 帧 0 差异）。

---

## 9. 30 分钟长测（最终镜像）

脚本 `tools/stress_v44.ps1 -Minutes 30`，每轮循环 `desktop → world → music → pet → rpg → memories`，
每轮记一行 `sched`、`heap`、`cache_*`、`last_error`、`safe_mode`。日志
`reports/logs/v45/stress-20261030-180542.log`（152 个样本）。

| 项 | 结果 |
|---|---|
| 样本数 | **152**（25–26 次/页，六页全覆盖） |
| `last_error` 非 `none` 的样本 | **0** |
| `safe_mode` 为 true 的样本 | **0** |
| 未落到目标页的样本 | **0 of 152**（V4.4 那轮是 7/161） |
| `sched` | `20/20` × 122、`20/21` × 4、`20/23` × 1、`20/49` × 4、`20/50` × 19、`20/51` × 2 |
| `heap_free` | 13,326,479 → 13,322,543（**−3,936 B / 30 min，0.03%**，最小值 11,914,815 出现在宠物页） |
| `cache_evictions` | 全程恒定 286（未命中集中在首次进页） |

两条要读准的地方：

1. **`avg_gap` 全程是 20 ms**，`max_gap` 偶尔 49–51 ms 的那几帧都出现在**换页那一轮**
   （页面重建 + 首帧整屏重绘），不是稳态抖动。稳态 122/152 样本是 `20/20`。
2. **heap 的 −3,936 B 不是泄漏**：同一窗口内 `cache_evictions` 恒为 286、
   `cache_resident` 恒定 11,688,324 B，说明帧缓存没有反复重建；4 KB 量级是 LVGL
   对象池的正常碎片水位。

V4.4 那轮 32.1 分钟长测的问题是 `misrouted: rpg->id 0 ×7`（7/161 次没进到目标页）；
本轮 **0/152**，因为 RPG 页这次真的能打开（见 `BOOT_OPTIMIZATION.md` §7.5）。

---

## 10. 卡的实际吞吐（新增 `sdspeed` / `sdblk` 探针）

「走路偶尔卡一下」的最后一段路全在卡上，所以先量卡。两个命令都留在固件里
（`sdspeed [file]`、`sdblk`），输出如下。

### 10.1 修复前的读数

```
sdspeed: card name=SK64G freq=40000 kHz width=2
sdspeed: chunk=4096   780096 B in 2246 ms -> 347 KB/s
sdspeed: chunk=97512  780096 B in 1781 ms -> 438 KB/s
sdspeed: reopen/frame 780096 B in 1840 ms -> 230 ms per 97 KiB frame
```

- 换再大的读块也**不涨**（347 → 438 KB/s 就饱和）⇒ 不是「读得太碎」，是每次传输有固定开销。
- `sdblk`（绕开 FATFS 直接 `sdmmc_read_sectors`）：4 KiB 6 ms / 32 KiB 14 ms / 96 KiB 92 ms
  ⇒ **固定开销约 4–5 ms/次**，块越大越摊薄。
- 结论：这段成本来自 **SDMMC 每次搬运都要经过一个 16 KiB 的 bounce buffer**，
  而 `lw_sd.c` 只在内部 RAM 里挑得到 16 KiB（挂载时内部最大连续块已掉到 30 KiB 以下）。

### 10.2 修复：bounce buffer 放到 PSRAM，取 64 KiB

`firmware/components/lw_storage/src/lw_sd.c` 原来只从内部 RAM 里挑
（`64 KiB → 32 → 16 → 8`），实测只拿到 16 KiB。改成内部不够时再要一块
**PSRAM 的 64 KiB**（P4 的 PSRAM 经 cache 可 DMA）替换掉小的那块。

```
I (1660) lw_sd: sdmmc staging buffer: 65536 B
sdspeed: chunk=4096   780096 B in  418 ms -> 1866 KB/s
sdspeed: chunk=97512  780096 B in  415 ms -> 1879 KB/s
sdspeed: reopen/frame 780096 B in  429 ms ->  53 ms per 97 KiB frame
```

**4.3× 吞吐**，单帧点播从 **230 ms 降到 53 ms**。顺带的副产物：那块 64 KiB 不再占内部
RAM，首屏后内部可用从 **6,347 B 回到 39,187 B**（`boot ram after ui`）。

### 10.3 为什么走路还是偶尔卡

50 ms 一帧、97 KiB 一帧 ⇒ 需要 **1950 KB/s**，现在实测 **1879 KB/s**（单流）。
而 RPG 里一次点击会让**两个角色**同时走路（主角 + 跟随者），需求翻倍到约 3900 KB/s。
所以：

- **稳态同方向走**：预取把整段 clip 排进队列，1.6 MB 在 800 ms 的点击间隙里读得完，
  命中率够，`avg 26 ms`。
- **刚换方向的头几帧**：新 clip 才开始预取，点播 3–4 帧 × 53 ms ≈ 200 ms，
  这就是 `max 289 ms` 那个尖峰。

按 4.5 轮的约束，这里**没有**用「再扩缓存」解决（预算仍是 12 MiB，
槽位仍是 64）；要真正消掉这个尖峰只剩两条路：把卡跑满 4 线（板子当前协商到
`width=2`，槽位请求的是 4 线）或降低走路帧率（属于降动画质量，不做）。

> **10.4 后记（同日更晚）**：上面「只剩两条路」的判断被推翻了。卡协商到 2 线是硬件事实，
> 但**每帧要搬的字节数本身可以变小**——像素画帧的 deflate 比是 6–12×，见 §11。

## 11. 帧压缩：把每帧要搬的字节数降一个数量级

### 11.1 为什么是压缩，而不是继续优化 I/O

§10.3 的账是这样的：走路需要 **1950 KB/s**（单流）到 **3900 KB/s**（两人），
而这块卡在 2 线模式下实测上限 **1879 KB/s**。缺口不在调度、不在缓存、不在预取，
而在**每帧 97–208 KiB 的原始字节量**。所以先量像素画帧到底有多可压缩：

对 `build/v45-assets/male.lwp` 的 `walk_right` 第 5 帧（98,229 B 原始 RGB565A8）：

| 编码 | 字节 | 比值 |
|---|---|---|
| zlib（raw deflate，级别 9） | 12,072 | **8.14×** |
| lzma（预设 6） | 10,440 | 9.41× |
| 字节 RLE | 102,954 | **0.95×（无效）** |

RLE 无效的原因值得记一句：像素画的**每一行都在变**（角色轮廓、阴影、渐变背景垫），
按字节跑长串几乎打不中；而 zlib 的 LZ77 能吃到「同一帧内大量重复的透明区和同色块」。
lzma 只多 15%，但解压慢一个量级且代码量 10 倍，**选 zlib**。

### 11.2 放哪儿解压：IDF 的 ROM 里就有 miniz

`esp_rom` 组件自带 `miniz.h`（实现编进 ROM），`tinfl_decompress_mem_to_mem()` 可以直接调，
**不需要新增组件、不占 flash**。Windows 宿主没有 ROM，所以宿主侧一律拒收压缩包
（`host/pixel_pack.c` 要求 trailer 的保留字段为 0），压缩路径只存在于真机。

### 11.3 文件格式：只动两处，其余全不变

不动 LWPX 的头和 clip 记录，只改两点：

1. **每帧变成一个固定 stride 的 raw-deflate 块**（不足补零）。固定 stride 是关键：
   偏移校验、缓存 key、预取算术、`borrow()` 的字节数全部沿用原有公式，只把
   `frame_bytes` 换成 `stored_bytes`。
2. **stride 写进几何 trailer 里一直存在但从未使用的保留 u16**（偏移 +10）。
   0 表示未压缩——**V4.6 之前的所有包照旧能读**，所以这个特性是纯增量。

固件侧（`firmware/components/lw_ui/src/lw_pack.c`）：

- `lw_pack_source_t` 新增 `uint32_t stored_bytes;`（0 = 原始帧）。
- `read_frame()` 增加 deflate 分支：`lw_file_cache_borrow()` 取压缩块 →
  `lwp_inflate()` 解到帧缓冲 → `lw_file_cache_unborrow()` → 预取下一块。
  读的是 **15–48 KiB** 而不是 98–208 KiB。
- `borrow_frame()` 对压缩包返回 NULL：缓存里存的是压缩块，不是能直接给渲染器的像素，
  零拷贝与解压是互斥的（换来的是 I/O 降 6–12×，远大于 memcpy 的收益）。
- `prefetch_frame()` 按 `stored_bytes ? stored_bytes : frame_bytes` 取步长。

### 11.4 压缩结果（`tools/lwp_compress.py`，逐帧回环校验全部通过）

| 包 | 原始 | 压缩后 | 比值 | stride |
|---|---|---|---|---|
| `male.lwp`（91 帧） | 8,938,839 | 1,398,614 | **6.40×** | 15,360 |
| `qianqian-base.lwp`（113 帧） | 19,097,226 | 1,621,530 | **11.79×** | 14,336 |
| `qianqian-wave.lwp`（16 帧） | 1,425,600 | 245,906 | 5.80× | 15,360 |
| `daily.lwp`（59 帧） | 12,310,704 | 2,840,524 | 4.34× | 48,128 |

整卡 `payloadBytes` **55,468,252 → 19,798,923 B（−64%）**。

`daily.lwp` 比值最低、stride 最大，是因为它装着 `couple.wave` 那种 276×249 的双人帧——
内容多、熵高；而 `qianqian-base` 的待机帧大面积透明，所以能压到 11.8×。

按 §10.3 的账重算：两人走路需要 3900 KB/s，压缩后每帧从 97+169 KiB 降到
15+14 KiB ⇒ 需求降到约 **580 KB/s**，**卡的上限 1879 KB/s 有 3.2 倍余量**。

### 11.5 上卡后的真机读数（已验收）

压缩包落到 TF 卡（`build\v46-sd`，19 文件哈希全匹配）后，同一台板、同一份固件实测：

| 项 | 未压缩卡 | 压缩卡 | 说明 |
|---|---|---|---|
| RPG 走路 `sched` | 27 / max 353 ms | **21 / max 87 ms** | 尖峰降 4 倍 |
| `rpg perf` | refresh 3193 ms / 403 帧 | refresh 1598 ms / 533 帧 | 每帧 7.9 → 3.0 ms |
| `seek` | 3170 ms | 1378 ms | — |
| `refill` | 178 次 | 239 次 / `borrowed=0` | `borrowed=0` 证明走的是 read+inflate 路径 |
| 开机动画 | 24 帧 / 11 fps | **42 帧 / 20 fps** | 单帧体积小了，遮罩期也更快 |
| 宠物页 | 20 ms | **46 ms ⚠️** | 见 11.6 |

解压的 CPU 代价实测约 **1–2 ms/帧**（97 KiB），远小于它省掉的 53 ms 点播。

### 11.6 为什么不压缩 `daily.lwp`：一个用实测否证的设计

宠物页在压缩卡上从 20 ms 退化到 **46 ms**，而缓存证据显示这不是 I/O：

- 6 秒窗口 `cache_misses` 完全不动（614→614）、`cache_evictions` 不动、`cache_borrows +112`
  ⇒ **帧全部命中，零 I/O**。
- 因此 46 ms 全是「解压 + 绘制」。宠物页两个精灵 `couple.seated`(227×252) + `dog.idle`(60×52)
  每 125 ms 换一帧，`daily.lwp` 的 stride 是 48,128（大帧熵高、比值只有 4.34×），
  两次解压约 26 ms/次换帧。

结论：**只压缩「工作集超出缓存」的包**。RPG 四方向工作集 = `male` 6.2 MB + `qianqian-base` 10.8 MB
≈ 17 MB > 12 MiB 预算，压缩后降到 1.1 MB，从根本上消除了逐出；而宠物页的 24 帧只有 5 MB，
本来就全部常驻，压缩只多付解压。`daily.lwp` 因此保持原始（`stride=0`），per-clip bbox 照旧生效。

这个取舍同时说明：**压缩不是「越多越好」的通用优化**，判据是「工作集 vs 缓存预算」。
（字节预算仍 12 MiB 未动，也没有靠扩缓存解决——见 §4。）

---

## 12. 宠物页夜间染色：把逐像素 tint 烘进素材

### 12.1 现象与定位

宠物页在夜间（`lw_ambience_phase() >= LW_NIGHT`）是 **46 ms**，`anim off` 立刻回 **20 ms**。
用 §11.6 的缓存证据排除 I/O 后，源码给出唯一嫌疑：

`firmware/components/lw_ui/src/lw_app_pet.c` 对两个精灵调用
`lv_obj_set_style_image_recolor(...)` + `recolor_opa`（狗 65 / 情侣 35 / 夜间各 35）。
而 LVGL 的 RGB565A8 直通快速路径条件是
`!transformed && !radius && cf == RGB565A8 && recolor_opa <= LV_OPA_MIN && colorkey == NULL`
（`lv_draw_sw_img.c:254`）⇒ **非零 recolor 一定掉进 `transform_and_recolor()` 的逐像素路径**。

单变量确认：把三处 `recolor_opa` 临时改 0 重编烧录 ⇒ 宠物页 **22 ms / max 250**。
**染色代价 ≈ 24 ms/帧**。

### 12.2 为什么不能直接删染色

V4.5 明令禁止「降画质换 FPS」。夜间染色是观感的一部分，删掉就是降画质。

### 12.3 方案：把染色算到素材里

`daily.lwp` 只有宠物页用，所以可以在烘焙时把「日间帧 × 夜间色 × 不透明度」算成两套 clip，
运行时直接选 `.night` 变体、`recolor_opa` 归零 ⇒ 既保观感又回到直通路径。

- `tools/lwp_prescale.py` 新增 `tint_rgb565()`，**逐位复刻 `lv_color_16_16_mix`**
  （`firmware/managed_components/lvgl__lvgl/src/misc/lv_color.c`）：
  `mix=((opa+4)>>3)&0x1F`、`bg=(px|(px<<16))&0x7E0F81F`、`out=((((fg-bg)*mix)>>5)+bg)&0x7E0F81F`。
  新参数 `--night-tint 'name=RRGGBB:opa,...'`，为列出的 clip 追加 `<name>.night`（同 anchor、同 bbox，只换 RGB 平面）。
- `tools/verify_night_tint.py` 逐帧断言 `.night` 的 RGB 等于对日间帧套同一公式的结果、alpha 平面未变：
  **59 frames checked, 0 mismatch**。
- 固件：`firmware/components/lw_ui/src/lw_polish.c` 的 `lw_polish_clip()` 同时认识
  `"%s"` 与 `"%s.night"`（16 项查表）；`lw_app_pet.c` 新增 `pick_clip()` 优先取 `.night`，
  并在 `create_room()` 里用 `baked_night = lw_polish_clip("v42.couple.seated.night")!=NULL;` 记录
  **卡上是否有夜间素材**——没有就回退到运行时染色（旧卡不掉画质，只是慢）。

### 12.4 结果

| 项 | 旧卡（运行时染色） | 新卡（染色烘进素材） |
|---|---|---|
| 宠物页 `sched` | 38 ms | **`ticks=277 avg 20 / max 20 ms`** |
| 素材 | `daily.lwp` 12,311,676 B | `daily.lwp` **24,623,324 B**（8 → 16 clips） |
| 观感 | 夜间色调 | 夜间色调**不变**（`reports/board-night/pet2.png` 目视确认） |

真机截图 `reports/board-night/pet2.png`：情侣坐在沙发上（夜间冷调）、狗在窝里、状态条与按钮齐全、无渲染瑕疵。

### 12.5 一并确认的其它页（同一份固件 + 夜间卡）

`boot phases: hw=32 art=162 content=9 ui=1244 apply=0 total=1448 ms`；
`boot: frames=40 elapsed=2030 ms visible=1766 fps=19 skipped=0`；
桌面 20/20、世界 20/20、RPG 站立 20/20、**RPG 走路 `ticks=351 avg 20 max 23`**
（`refresh=473 ms / 387 帧`、`seek=472`、`slow>=25ms=0`）。

### 12.6 夜间烘焙版 10 分钟 soak

`tools/stress_v44.ps1 -Minutes 10 -LogDir reports\logs\v46`（`reports/logs/v46/stress-20261031-225433.log`），
**51 个样本、六页循环**：

| 项 | 结果 |
|---|---|
| `last_error` 非 `none` | **0** |
| `safe_mode` true | **0** |
| 未落到目标页 | **0 of 51** |
| `cache_evictions` | **全程 0**（压缩卡把工作集压到预算内，不再逐出） |
| `sched` | 每页 `avg 20`；`max` 只有两类偏高：**回到桌面页那一轮 42–79 ms**（页面重建 + 首帧整屏重绘）、RPG 页 23–26 ms。宠物页 51 个样本里凡命中都是 `20/20` |
| `heap_free` | **按页稳定，无漂移**：桌面 17,604,771–17,604,835（跨度 64 B）、宠物 16,193,823–16,193,895、RPG 16,353,367–16,353,443。首次进桌面看到的 19.06 MB 是音乐页还没分配过素材的状态，不是泄漏 |

### 12.7 遗留

`ui = 1244 ms` 仍未分段（机制 4）；皮肤九宫格 9 个 `lv_image`（机制 5）未做。
桌面页「换页回来那一下 42–79 ms」也是下一轮的候选（页面重建里 `render()` 一次性建完所有控件）。

## 13. 首屏不再黑屏：遮罩先出、页面在背后建

用户拿到的观感问题是「开机先黑一块」。`osperf` 把这段等待量了出来：`lw_os_start()` 里
`render()` 先解 1024×600 壁纸（632 ms）再建整页（549 ms），**遮罩排在这两件事之后**，
所以从 `lv_screen_load()` 到第一帧上屏要 **1254 ms**。

改法：把 `lw_os_boot()` 拆成 `boot_backdrop()`（底色 + 标题 + 副标题 + 轻触跳过，无动画）
和 `boot_finish()`（壁纸、压暗、logo 位移、叶子、两个开机精灵、淡出）。前者挂在 `lv_layer_top()`
（因为 `render()` 会 `lv_obj_clean(s->root)`，挂在页面上的遮罩会被删掉），建完立刻 `lv_refr_now(NULL)`；
后者在页面建好、计时器起来之后再补。

| 指标 | 改前 | 改后 |
|---|---|---|
| 看到第一帧的时间 | 1254 ms | **23 ms** |
| 页面构建完成 | 1215 ms | 1237 ms |
| 动效总时长 | 2047 ms | 3247 ms |
| `boot` 帧数 / fps | 24 / 11 | 29 / 8 |

```text
osperf: renders=1 total=1201 ms (wall=632 page=549 status=3 dock=16 tail=1)
osperf: title card on panel=23 ms, build done=1237 ms, flush event=1297 ms
boot: frames=29 elapsed=3247 ms visible=2969 ms fps=8 active=0 skipped=0
```

取舍写在明面上：**总时长多了 1.2 秒**，换的是「前 1.2 秒看标题卡而不是黑屏」。这一项不是提速，
是把等待变成内容，所以两个读数都留在表里。

逐像素证据（`LW_FAKE_TIME=19:30`、未压缩包、752 checks）：修好 z 序之后与改前相比
**147 identical / 9 differ**，差异全部落在 `v4-boot-01..09` 的 bbox `[380,354..503,635]`，
即左侧开机精灵的矩形内——动效相位差了 1237 ms，静态画面逐像素一致。

细节见 `BOOT_OPTIMIZATION.md` §8（含两个非显然约束：遮罩必须挂 top layer；壁纸/压暗必须按身份插回
z 序，用固定下标会把标题盖住）。
