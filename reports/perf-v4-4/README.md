# reports/perf-v4-4 · V4.4 流畅度重构

分支 `perf/v4-4-fluidity-overhaul`（从 tag `v4.3-baseline` 开出）。
目标：把六页的真机帧率从当前的 1.3–48 fps 区间推到 40–50 fps，点击响应 < 50 ms。

## 这个目录里有什么

| 文件 | 编号 | 内容 | 真机数据 |
|------|------|------|----------|
| `BASELINE.md` | P0-A | 六组页面级真机基线（`sched` / `fps` / `rpg`） | ✅ 有（三轮：改动前 / 改动前复测 / 烧后） |
| `RPG_MOTION.md` | P0-B | RPG 连续移动：固定步长 + 时间插值 | ✅ 有（成因 B 已定位，烧后复测无收益） |
| `SPRITE_CACHE.md` | P0-C | 精灵缓存容量与画布裁剪 | ✅ 有（改动前成因 + **改动后收益**） |
| `BOOT_ANIMATION.md` | P0-D | 开机动画：分段计时与遮罩统计 | ✅ 有（`boot phases` 与 `boot` 首次读数） |
| `DISPLAY_BENCHMARK.md` | P0-E | LCD / LVGL / PPA 单变量对照矩阵 | ⬜ 无（五个镜像已构建，未烧） |
| `FINAL_PERFORMANCE.md` | P0-F | 综合验证与验收标准对照 | 🟡 部分（三页达标，两页未达） |
| `build_matrix.json` | — | 五个构建变体的机器可读结果 | — |

## 最重要的一句话

**三页已回到 20 ms 档，RPG 与宠物仍未达标。**

用户中途接上板子、随后批准烧录，因此本轮拿到了**改动前**与**改动后**两套真机数据：

| 页面 | 改动前 | 改动后 | 判定 |
|------|--------|--------|------|
| 音乐 | 20 ms | **20 ms** | 参照系，无退化 |
| 桌面 | 20 ms（预热窗口 156） | **21 ms** | 无退化 |
| 世界 | 预热后 34–49，偶发 157 | **21 ms** | 无退化，预热窗口消失 |
| RPG | 117 ms | **119 ms** | 遮挡守卫**无收益**，如预测 |
| 宠物 | 477–746 ms | **251 ms** | C-1 生效，I/O 归零 |
| 开机 | —— | **5 帧 / 2047 ms** | 409 ms/帧 |

**没有任何一条结论声称「全部页面已经变快」。** 交接书要求 40–50 FPS，
现在达标的是音乐/桌面/世界三页；RPG 119 ms（约 8 fps）与宠物 251 ms（约 4 fps）
仍远未达标，原因已在真机上定位为**精灵重绘面积**（`SPRITE_CACHE.md` §9.2）。

## 真机测出来的三件事（本轮最有价值的部分）

用 `anim on` / `anim off` 做单变量，三页的真凶被分离出来：

| 页面 | 动画 ON | 动画 OFF | 缓存计数器 |
|---|---|---|---|
| RPG | **117 ms** | **20 ms** | 30 秒零 miss、零 eviction |
| 宠物 | **477–746 ms** | **20 ms** | 10 秒 +100 misses / +100 evictions |

1. **`anim off` 是决定性的**：关掉帧推进后，全屏背景与半透明覆盖层的合成量
   **一点没变**，两页却都掉回 20 ms。⇒ 主因是**精灵重绘**，不是全屏合成。
2. **RPG 页是纯绘制、零 I/O**：30 秒里 `misses`/`evictions` 一个都没涨，只有
   `hits +637`（约 2.5 次/tick，两个精灵各换一帧）。⇒ 成因 B **不是**缓存，
   **也不是** `move_to_index`（库内早有 `index==old_index` 早退）。
3. **宠物页的缓存颠簸是真的，但只占墙钟约 14%**：10 miss/秒 × 14.1 ms = 141 ms/秒；
   488 ms 的 tick 里约 69 ms 是 I/O，**剩下 86% 是绘制**。⇒ 缓存修好最多降到约 419 ms，
   **要回 20 ms 必须解决绘制成本**（见 `SPRITE_CACHE.md` §5.3）。

烧录本轮镜像后，这三条全部得到验证：宠物页 477–746 → **251 ms**（I/O 归零），
RPG 仍是 **119 ms**（守卫如预测无收益），`anim off` 一律回 20 ms。

**但还有一个没解决的新现象**：桌面页与世界页会在 **21 ms 与 116 ms** 之间成串跳变，
两页同步、音乐与回忆页不受影响。慢态下精灵数、租约数、缓存计数器全部冻结（不是泄漏、
不是 I/O），`anim off` 仍回 20 ms，而**一次整页重建能把 116 ms 重置回 32 ms**。
已排除泄漏 / 缓存 / 错页 / 相位切换 / 切页频繁 / 开机残留，**机制未定论**，
详见 `SPRITE_CACHE.md` §9.5 与 `FINAL_PERFORMANCE.md` §4.6.3。

## 基线参照系（`BASELINE.md` 上一轮 + 本轮真机复测）

那个 20 ms 的 LVGL 定时器的实际间隔：

| 页面 | 上一轮 | 本轮复测（动画 ON，改动前） | 本轮（动画 OFF） | 烧后（改动后） |
|------|--------|---------------------------|------------------|----------------|
| 音乐 | **21 ms** | 20 ms / max 20 | —— | **20 ms / max 20** |
| 桌面双角色 | 30–37 ms | 稳定 20 ms（启动/预热窗口 35–156） | 20 ms | **21 ms / max 36** |
| RPG | 120 ms | **117 ms / max 119** | **20 ms** | **119 ms / max 129** |
| 世界 | 139–151 ms | 预热后 **34–49 ms**，偶发 157 | —— | **21 ms / max 37** |
| 宠物 | **777 ms** | **477–746 ms / max 919** | **20 ms** | **251 ms / max 274** |

**音乐页 20–21 ms 是全局参照系**：它证明显示链路与 LVGL 任务本身能跑满，
所以「硬件就这水平」这个解释不成立。

**动画开/关是决定性的单变量**：关掉帧推进后，宠物页 488 → **20 ms**、RPG 117 → **20 ms**，
而全屏背景与 ambience 覆盖层的合成量一点没变。⇒ 三页慢的主因是**精灵重绘**，
不是全屏合成，也不是 I/O。

## 本轮的核心量化发现

`daily.lwp`（宠物页用）是 **320×320**，单帧 **307,200 B**；而缓存配置
`ENTRIES 16` / `LW_ART_CACHE_BUDGET 4 MiB` 是**按 256×256 设计的**。

宠物页稳态活跃帧（狗在地面 / 狗在床上是**互斥**的两条分支，见 `lw_app_pet.c:192`
的 `joint_bed`，所以 `dog.idle` 与 `dogbed.*` 不会同时活跃）：

```
couple.seated 16 × 307,200 = 4,915,200
dog.idle       8 × 307,200 = 2,457,600   （或 dogbed.look 8 帧，同为 2,457,600）
                            ---------
                            7,372,800 B = 24 帧
```

**连一个 `couple.seated` clip 都装不下**（4,915,200 > 4,194,304）。装不下的后果是
每一帧都退化成 LVGL 线程上的同步 SD 读。真机确认颠簸存在：动画 ON 时
**10 秒 +100 misses / +100 evictions**，动画 OFF 时计数器完全冻结。

**I/O 只占墙钟的约 14%**：100 miss / 10 秒 = 10 次/秒，每次 ≈14.1 ms，
即 141 ms/秒；宠物页 488 ms 的 tick 里约 69 ms 是 I/O，**剩下 86% 是绘制**。
⇒ 缓存不足是**真实缺陷但不是充分解释**。

改动：`ENTRIES` 16 → 32、`LW_ART_CACHE_BUDGET` 4 MiB → 12 MiB。
两个必须一起改：24 帧需要 24 个槽，而 24 × 307,200 = 7.37 MB 又超过 4 MiB。

**烧后验证（新增）**：20 秒窗口里 `misses`/`evictions` **完全不动**（48/13 冻结），
`resident` 从 3,145,728 涨到 **9,584,640**，耗时 477–746 → **251 ms**。
⇒ 缓存缺陷已修好，且**只剩 251 ms 的绘制成本**（`anim off` 回到 20 ms）。

**下一轮主战场已明确**：精灵画布 288×288 里约 72% 是透明 padding（实际内容
约 108×212），`lv_draw_image` 要对整块做缩放 + alpha 混合。见
`SPRITE_CACHE.md` §5.3–§5.4 与 §9.2。RPG 119 ms、宠物 251 ms、开机 409 ms/帧
三个数字的共同分母都是这一项。

## 怎么复现本轮的所有检查

```powershell
# 宿主构建
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. .\tools\activate_env.ps1
# 需要先导入 MSVC 环境（vcvars64.bat），见 SPRITE_CACHE.md §8
ninja -j 2 -C build/host

# 宿主测试
.\tools\run_host_tests.ps1        # 142 checks
.\tools\run_core_regression.ps1   # Duo 82 / RPG 336 / Storage 33

# 五个构建变体（-Fresh 用于片段新增符号时强制重生成 sdkconfig）
.\tools\build_v4_matrix.ps1 -Configs @('os','perf-unit1','perf-double','perf-full','perf-ppa') `
    -Report 'reports\perf-v4-4\build_matrix.json' -LogDir 'reports\logs\v44' -Fresh -KeepGoing
```

## 上板后要跑的命令

```
lw> sched reset   → 5 s → sched      # 定时器间隔
lw> fps reset     → 5 s → fps        # 真帧率（注意：只统计有 flush 的秒）
lw> diagjson                         # 缓存 hits/misses/evictions/resident
lw> boot                             # 开机遮罩 frames/elapsed/visible
lw> rpg                              # RPG 逻辑点 vs 屏幕点
lw> anim on / anim off               # 本轮最有效的单变量
```

`boot` 是本轮新增的（P0-D）。其余在 V4.4 之前就有。

**上板后第一件事必须是烧新镜像**：`firmware/build/os/little_world.bin`（2,359,248 B，
sha256 `D89774A5…`），用 `tools/flash_v4_safe.ps1 -Stage app -Execute -Authorized`
（只写 `0x10000` 应用分区，不碰 NVS/分区表/storage/C6/eFuse）。**烧录需要用户明确批准。**

**`fps` 命令在本轮场景下基本无效**：它只统计「至少提交一次 flush 的秒」，RPG 页实测
`fps: 1`、`fps reset` 后 `fps: 0`。判页面对不对要看 `sched` 的 `avg_gap`。

## 硬约束（本轮遵守情况）

| 约束 | 遵守 |
|------|------|
| 不关动画、不降画质、不删素材换 FPS | ✅ 一处未做（`anim off` 只用于**测量**，是单变量实验手段，不是交付配置） |
| 不碰 NVS / 分区表 / C6 / eFuse | ✅（烧录只写 `0x010000` 应用分区，脚本内有 `protected` 断言） |
| 烧录需用户明确批准 | ✅ 用户说「烧录」后才执行，`-Execute -Authorized` 双开关 |
| 结论要有真机数据 | ✅ 改动前成因 + 改动后收益都有真机数据；未测项已逐条标注 |
| 新增文件 / 构建 / 缓存放 D 盘 | ✅ 全部在 `D:\LittleWorld-V43-Board` |
| 不清理用户其他文件 | ✅ |

## 已知未完成

- **P0-B 成因 B**：真机已定位为「每帧重绘两个 288×288 精灵」的绘制成本
  （30 秒零 miss、`anim off` 后 20 ms；烧后复测 119 ms 确认守卫无效）。
  **修复方案是 C-4 画布裁剪**，本轮未改代码。
- **点击延迟 < 50 ms**：仍无探针。`tap <x> <y>` 只注入事件，不测响应时间。
- **画布裁剪**（`SPRITE_CACHE.md` C-4）：真机已把它升为**下一轮第一优先级**
  （精灵画布 3.6 倍透明 padding），方案与预期收益写在 §5.4，本轮不改代码。
- **P0-E 显示矩阵**：五个变体全部构建完成，**一个都没烧**（用户只批准了 `os`）。
- **p50/p95/p99 直方图**：只有 `avg_gap` / `max_gap`，看不到分布形态。
- **开机首屏 `ui=1542 ms`**：占开机总时间 89.9%，是本轮新发现、未优化。
- **桌面/世界页双稳态（116 ms ↔ 32 ms）**：现象已复现、五条候选已排除，
  **机制未定论**。需要一次新的编译 + 烧录来加 `ext_draw_size` / `shown_id` 探针
  才能二分定位（`SPRITE_CACHE.md` §9.5）。若它与 C-4 同源，C-4 的收益上界
  会从「RPG 32–40 ms」提高到「RPG 与桌面/世界页一并回到 20 ms 档」。
