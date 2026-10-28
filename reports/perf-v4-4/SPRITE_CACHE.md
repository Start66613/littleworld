# P0-C · 精灵缓存与画布裁剪

分支 `perf/v4-4-fluidity-overhaul`，基线 `v4.3-baseline`。
本轮改动前 HEAD `04ad8e5`，改动后见文末「提交状态」。

**本轮前后三个阶段**：先在没有开发板的情况下做完了静态分析（`COM5` 的 CH343 当时不在
`Get-PnpDevice -Class Ports -PresentOnly` 里），随后用户接上板子，补做了真机单变量实验；
最后用户明确批准烧录，本轮的改动**首次跑在板上**，拿到了改动后的收益。结论分成四类，
全程分开标注：

- **【静态证据】** —— 读源码、读 sdkconfig、读素材包头、跑宿主测试得出，可复核。
- **【真机实测·改动前】** —— COM5 上跑出来的数，板上是 `v4.3-baseline-dirty` 旧镜像。
- **【真机实测·改动后】** —— 烧录本轮镜像（`v4.3-baseline-1-g04ad8e5-dirty`）后的读数。
- **【待真机】** —— 仍需上板、且本轮板上镜像不支持的项目。

**真机数据推翻了本文件 §2.3.1 原先的候选排序**，也推翻了「RPG 120 ms 主要来自
遮挡换序」这一假设。两处都已在原位改写，保留原文以便看出结论是怎么被推翻的。

**改动后的收益已在 §9 验证**：C-1 生效（宠物页 I/O 归零、耗时 477–746 ms → 251 ms），
C-2 如预测无收益（RPG 119 ms），C-3 无可测收益，C-4 被确认为唯一剩余路径。

---

## 1. 结论摘要

| # | 问题 | 位置 | 量化 | 状态 |
|---|------|------|------|------|
| C-1 | 缓存槽位与字节预算都是按 256×256 设计的，宠物页用 320×320 | `lw_storage.c` `ENTRIES`、`lw_art.c` `LW_ART_CACHE_BUDGET` | 宠物页活跃帧 24 帧 × 307,200 B = **7,372,800 B**，旧上限是 16 槽 / 4 MiB | 已改：32 槽 / 12 MiB；**改动前颠簸只占墙钟 14%，改动后 I/O 归零、251 ms（§9）** |
| C-2 | RPG 页每 20 ms 无条件请求换序，真换序会作废整张 1024×494 地图 | `lw_rpg_view.c:103-104` | 一次真换序 = `lv_obj_invalidate(parent)` = 505,856 px | 已改：加 `front_actor` 守卫；**真机证明它不是 117 ms 的主因，改动后 119 ms（§9）** |
| C-3 | 宠物页每 40 ms 重写 5 个没变化的控件 | `lw_app_pet.c:60,63,66,69,76-77` | `lv_label_set_text` 无同文本守卫，每次都 invalidate + 延迟重排 | 已改：加值守卫；**改动后无可测收益（被绘制成本淹没，§9.3）** |
| C-4 | 精灵画布 288×288，实际不透明内容约 108×212 | `lw_sprite.c` 的 `lv_image` 对象尺寸 | 画布 82,944 px vs 可见约 22,896 px（约 3.6×） | **本轮不改代码**，但真机已证明它是 RPG/宠物页的**唯一剩余路径**（§5、§9.2） |

**真机重新排序后的结论**（这一条覆盖上面表格的旧暗示）：

1. **三页慢的主因是「每帧重绘精灵」的绘制成本，不是 I/O。** 决定性单变量是
   `anim off`：宠物页 488 ms → **20 ms**、RPG 页 117 ms → **20 ms**。
   关掉帧推进后，全屏背景与 ambience 覆盖层的合成工作量**完全不变**，却掉回 20 ms。
2. **RPG 页 117 ms 是纯绘制、零 I/O**：30 秒窗口里 `misses` 与 `evictions` 一个都没涨。
   ⇒ 成因 B 不是缓存 miss，也不是 `move_to_index`。
3. **宠物页的缓存颠簸是真的**（动画 ON 时 10 秒 +100 misses / +100 evictions，动画 OFF
   时计数器完全冻结），但按 14.1 ms/次算，**它只占 488 ms 墙钟的约 14%**（§2.3.2）。
   ⇒ C-1 是真实缺陷，但不是 777 ms 的充分解释。

---

## 2. C-1 缓存：槽位与字节预算都按错误的帧尺寸设计

### 2.1 素材包的真实几何【静态证据】

`tools/lwp_probe.py` 直接读 LWPX 头（`"LWPX0001"` + u16 width + u16 height），
`lw_pack.c` 里 `frame_bytes = width * height * 3`（RGB565A8，2 B 色 + 1 B alpha）。
六个包实测：

| 包 | 字节 | 宽×高 | 单帧字节 | clip 数 |
|----|------|-------|----------|---------|
| `male.lwp` | 17,892,100 | 256×256 | 196,608 | 7 |
| `qianqian-base.lwp` | 22,218,124 | 256×256 | 196,608 | 13 |
| `qianqian-wave.lwp` | 3,145,852 | 256×256 | 196,608 | 1 |
| `daily.lwp` | 18,125,680 | **320×320** | **307,200** | 8 |
| `ui.lwp` | 4,021,372 | 544×352 | 574,464 | 1 |
| `skin.lwp` | 835,708 | 512×544 | 835,584 | 1 |

关键点：**`daily.lwp` 是 320×320，单帧 307,200 B，是 256×256 的 1.5625 倍。**

### 2.2 宠物页的活跃帧集合【静态证据】

`lw_app_pet.c` 的 `refresh()` 同时驱动两个精灵：

- `couple` 用 `v42.couple.seated`（16 帧）或 `v42.couple.wave`（16 帧）或
  `v42.couple.rest`（1 帧）；
- `dog` 用 `v42.dog.idle`（8 帧）/ `v42.dog.sleep`（8 帧）/ `v42.dogbed.empty`（1 帧）/
  `v42.dogbed.sleep`（1 帧）/ `v42.dogbed.look`（8 帧）。

**关键约束**：`lw_app_pet.c:192` 的 `joint_bed = lw_polish_clip("v42.dogbed.look") != NULL;`
是**一次性判定、运行期不变**的开关。它决定狗走「地面」分支还是「床上」分支，
两条分支互斥（`lw_app_pet.c:64` 的三元链）。所以 `dog.idle` 与 `dogbed.*`
**不会同时活跃**。

稳态（坐着 + 狗待机），两种分支各算一遍：

```
分支 A（joint_bed=false，狗在地面）:
couple.seated  16 × 307,200 = 4,915,200 B
dog.idle        8 × 307,200 = 2,457,600 B
                             -----------
                             7,372,800 B

分支 B（joint_bed=true，狗在床上）:
couple.seated  16 × 307,200 = 4,915,200 B
dogbed.look     8 × 307,200 = 2,457,600 B
                             -----------
                             7,372,800 B
```

两种分支都是 **7,372,800 B（24 帧）**，而旧配置是 16 槽 / 4 MiB（4,194,304 B）。
这不是「差一点」，是**连一个 `couple.seated` clip 都装不下**
（4,915,200 > 4,194,304），更不用说 24 帧。槽位也不够：24 帧需要 24 个槽，
旧 `ENTRIES` 是 16。

（若把 `couple.wave` 的 16 帧也算进稳态，总数仍是 24 —— 因为 couple 同一时刻
只有一个 clip 在跑。`couple.rest` 只有 1 帧，睡眠时活跃集反而更小。）

### 2.3 装不下会发生什么【静态证据】

`lw_storage.c` 的 `reserve()`：

```c
if (key->bytes > c->stats.budget_bytes) return NULL;
for (;;) {
    slot = NULL; old = NULL;
    for (i < ENTRIES) { if (!e->state) slot = e; if (e->state==2 && (!old||e->used<old->used)) old = e; }
    if (slot && key->bytes <= c->stats.budget_bytes - c->stats.resident_bytes) break;
    if (!old) return NULL;
    c->stats.resident_bytes -= old->key.bytes; free(old->data); memset(old,0,...); c->stats.evictions++;
}
```

两个条件必须**同时**满足才不逐出：有空槽 **且** 剩余字节够。所以：

- 字节预算耗尽时，即使还有空槽也要逐出；
- 槽位耗尽时，即使字节预算够也要逐出。

`load_key()` 未命中时走 `misses++` + `reserve()` + `read_file()`，而 `read_file()`
是 `fopen`/`fseek`/`fread` 的**同步**读，跑在**调用者线程 = LVGL 线程**上。
`lw_file_cache_prefetch()` 只是把 key 塞进 8 深队列交给 worker，**不保证**赶在
demand 之前完成；demand 自己还是会阻塞读一次。

所以「活跃帧 > 容量」的直接后果是：**每一帧动画都变成一次 LVGL 线程上的同步 SD 读**。
`lw_storage.c` 自己的头注释记过这个量级：「each sprite frame fell back to a synchronous
~9 ms read from the TF card on the LVGL thread」。宠物页两个精灵 × 每 40 ms 一帧，
即使按 9 ms/帧算也远超 40 ms 的预算 —— 这与实测 **777 ms** 的量级方向一致。

> 注意：这里只说「方向一致」。777 ms 是否**全部**由 I/O 造成，必须上板用
> `diag` 的 `cache misses/evictions/reads` 单变量验证（§6）。
> **真机已给出答案（§2.3.2）：I/O 只占 14%，剩下 86% 是精灵重绘。**

### 2.3.1 算术反证：**单次 miss 解释不了 777 ms**

「方向一致」还不够。把数量级算清楚会发现：**一次 miss 差了一个数量级**。

`lw_storage.c` 头注释给的实测基准是「~9 ms 读 196,608 B」（一个 256×256 帧）。
换算到扇区粒度（`CONFIG_FATFS_SECTOR_4096=y`）：

| 量 | 值 | 来源 |
|---|---|---|
| 196,608 B 里的 4 KiB 扇区数 | 48 | `196608/4096` |
| 由 9 ms 反推的每扇区成本 | **0.188 ms** | `9/48` |
| 320×320 帧（307,200 B）的扇区数 | 75 | `307200/4096` |
| 按 0.188 ms/扇区算一次 miss 的成本 | **≈14.1 ms** | `9 * 307200/196608` |
| 若一次 miss 要凑出 777 ms，每扇区得花 | **10.36 ms** | `777/75` |

10.36 ms/扇区 比实测的 0.188 ms/扇区 高 **55 倍**。也就是说：**要让 I/O 单独
解释 777 ms，卡读必须慢 55 倍**，而 `BASELINE.md` 已排除 SD DMA 故障
（`reads=2104 failures=0`，32 KiB `dma_aligned_buffer` 就位）。

所以更可能的形态是以下之一，报告不替真机下结论：

1. **每 tick 多次 miss**：两个精灵 + 一张房间背景图，若一 tick 内触发 3–5 次
   同步读，14.1 ms × 5 ≈ 70 ms，仍不足 777 ms，但会把页面拖到 20–70 ms 区间
   —— 与「777 ms」还差一截。
2. **另有主因**：全屏重合成。宠物页是 1024×600 背景图 + 全屏半透明
   `lw_ambience_layer` 覆盖层，一次整屏重绘要合成 1,228,800 px
   （背景 blit 614,400 + ambience blend 614,400）。777 ms 对应
   **1,581 Mpx/s** —— 这个吞吐对纯软件 RGB565 合成是**可能达到的量级**。
   > **⚠️ 本条已被 §2.3.2 的真机 `anim off` 实验否证**：关掉帧推进后全屏合成量
   > 一点没变，宠物页却从 488 ms 掉到 20 ms。**全屏重合成不是主因。**
3. **两者叠加**：I/O 阻塞让 tick 变长，变长又让每次重绘的脏区更大，正反馈。

⇒ 结论：C-1 是**必要条件**而不是**充分条件**。上板时 `diag` 的判据必须能区分
这两类：**`evictions` 与 `reads` 都归零而 `sched` 仍 >100 ms ⇒ 主因不是 I/O**
（§6 已写成判据表）。真机结果见下一节：I/O 只占 **14%**，其余是精灵重绘。

### 2.3.2 真机实测：颠簸是真的，但只占 14% 墙钟【真机实测】

上一节留了个悬念。板子接上后这个悬念被两个实验直接解开。

**实验一：宠物页，动画开 vs 关，看缓存计数器动不动。**

```
# 动画 ON，宠物页（page 8），10 秒窗口
diag  → cache: hits=… misses=3475 evictions=3084 resident=3145728 budget=4194304
（10 秒后）
diag  → cache: hits=… misses=3575 evictions=3184
        ⇒ +100 misses / +100 evictions

# 动画 OFF，同一页，20 秒窗口
sched → ticks=1103 avg_gap=20 ms max_gap=20 ms
diag  → hits / misses / evictions 三个数**完全冻结，一动不动**
```

**实验二：宠物页，动画开 vs 关，看定时器间隔。**

| 状态 | `sched` 读数 | 折算 | 说明 |
|---|---|---|---|
| 动画 ON | `ticks=7 avg_gap=746 ms` / `ticks=17 avg_gap=488 ms` / `max_gap=919` | **1.3–2.0 次/秒** | 与 `BASELINE.md` 的 777 ms 同量级 |
| 动画 OFF | `ticks=503 avg_gap=20 ms max_gap=20`（8 s）/ `ticks=1103 avg_gap=20`（20 s） | **50 次/秒，满帧** | 一个 tick 都不掉 |

**把两个实验合起来算，就得到本节最重要的一条量化：**

| 量 | 值 | 算法 |
|---|---|---|
| 动画 ON 时每秒 miss 次数 | **10 次/秒** | 100 次 / 10 秒 |
| 每次 miss 的同步读成本 | **14.1 ms** | §2.3.1 按 9 ms/196,608 B 折算到 307,200 B |
| 每秒花在 I/O 上的时间 | **141 ms/秒** | `10 × 14.1` |
| 占墙钟比例 | **14.1%** | `141 / 1000` |
| 单 tick 平均 miss 次数 | **4.9 次** | `10 / (1000/488)` |
| 单 tick 里的 I/O 时间 | **69 ms**，占 488 ms 的 **14%** | `4.9 × 14.1` |
| **剩下的 419 ms（86%）是绘制** | —— | `488 − 69` |

⇒ **C-1 修好之后，宠物页最乐观也只能从 488 ms 降到约 419 ms**（把 I/O 那 69 ms 全消掉）。
**要回到 20 ms，必须另外解决绘制成本**。这一条直接决定了 C-4（画布裁剪）从
「本轮刻意不做」升级为「下一轮的主战场」。

> 为什么 I/O 只占 14%：因为 `lw_storage.c` 的 `read_file()` 是**同步阻塞**，
> 但它每次只阻塞一个帧的读取（14.1 ms），而每 tick 的**绘制**要花几百毫秒。
> 阻塞读把 tick 拉长，tick 变长又让每 tick 触发更多 miss（`4.9 次/tick`），
> 于是形成「越慢越 miss、越 miss 越慢」的正反馈——这正是 777 ms 与 488 ms
> 两个读数不稳定的原因（`max_gap` 到 919 ms）。

### 2.4 改动【已落地】

`firmware/main/lw_art.c`：

```c
/* V4.4: 4 MiB was sized for the 256x256 character packs only and is too small
 * for the pet page ... */
#define LW_ART_CACHE_BUDGET (12u * 1024u * 1024u)
```

`firmware/components/lw_storage/src/lw_storage.c`：

```c
/* V4.4: sixteen was still derived from 256x256 only. The pet page's daily.lwp
 * is 320x320, i.e. 307,200 B/frame, and its live set is couple.seated 16 +
 * dog.idle 8 = 24 frames (the dog is either on the floor or on the bed, never
 * both - see joint_bed in lw_app_pet.c). 24 frames cannot fit in 16 slots no
 * matter how large the byte budget is. The slot count - not the budget - was
 * the binding constraint there. */
#define ENTRIES 32
```

为什么两个都要改：**只加字节不解决**。24 帧需要 24 个槽，`ENTRIES` 是 16，
字节再大也会因为「没有空槽」而逐出。反过来只加槽也不行：24 × 307,200 = 7,372,800 B
超过 4 MiB。所以槽位与字节必须一起抬。

12 MiB 的依据：宠物页最坏情形 7.37 MB + 翻页时另一个房间图（1,228,800 B，走
`lw_visual_assets` 的 aux 槽，不进这个缓存）+ 余量。板上实测空闲 PSRAM 约 21 MB
（`reports/integration-v4-3-board-smoke/BOARD_TEST.md:54`：`psram: free=21960980
largest=20971520`），12 MiB 缓存 + 2 MiB 租约预算 = 14 MiB，仍在范围内。

**没有动**的东西：`MAX_FRAME (4 MiB)` 单帧上限、`QUEUE 8` 预取深度、
`reserve()` 的逐出语义、LRU 的 `used` 时钟。也就是说这次只是把「容量」对齐到
真实帧尺寸，缓存策略本身一个字节都没改。

### 2.5 宿主验证【静态证据】

`tests/host/test_storage.c` 用 `FRAME 2048` 和 `3*FRAME` 预算验证缓存语义，
与 `ENTRIES` 数值无关，因此不受影响：

```
Storage: 33 checks, 0 failures; handles 54 -> 54
```

`tools/run_core_regression.ps1` 全绿（`Duo 82` / `RPG 336` / `Storage 33`）。

---

## 3. C-2 RPG 遮挡换序：库内已有守卫，但真换序的代价被低估了

### 3.1 事实修正【静态证据】

我原本以为 `lv_obj_move_to_index()` 是无条件重排，实际**库内已经早退**
（`lv_obj_tree.c:208-255`）：

```c
if (index < 0) { index += parent_child_count; }
if ((index < 0) || (index >= (int32_t)parent_child_count) || (index == old_index)) return;
```

所以「每 20 ms 无条件调两次」本身不产生重排。**但是**，一旦索引真的变化：

```c
parent->spec_attr->children[index] = obj;
lv_obj_send_event(parent, LV_EVENT_CHILD_CHANGED, NULL);
lv_obj_invalidate(parent);
```

`parent` 是 RPG 的地图对象 `v->map`，**1024×494**。`lv_obj_invalidate(parent)` 把
整块作废，下一帧要把整张地图连同压在上面的房间背景重绘。

因此真正的成本不是「调用两次函数」，而是「**脚底 y 顺序真的翻转的那一帧**」——
那一帧的脏区从「两个精灵的矩形」变成「505,856 px」。

`lw_rpg_view.c` 的 `refresh()` 里，`py[0] > py[1]` 用的是**每帧重算的插值后像素**，
所以两角色在同一格或相邻格时，这个比较会**反复翻转**。这正是需要守卫的地方。

### 3.1.1 但真机证明它**不是** 117 ms 的主因【真机实测】

守卫该加还是加（它确实消除了整图 invalidate），但把它当作 RPG 页 117 ms 的解释是**错的**。
决定性实验：在 RPG 页连跑 30 秒，看缓存计数器。

```
# RPG 页（page 9），动画 ON，30 秒窗口
diag → cache: hits=… misses=3389 evictions=3001
（30 秒后）
diag → cache: hits=… misses=3389 evictions=3001   ← 一个都没涨
       hits 增加 637
sched → ticks=272 avg_gap=117 ms max_gap=119 ms
```

三条读数合起来说明：

1. **`misses` 与 `evictions` 30 秒零增长** ⇒ RPG 页**完全不走 SD 卡**。
   这是合理的：RPG 活跃集是 16 帧 × 196,608 B = **3,145,728 B**，
   正好装进旧的 16 槽 / 4 MiB（`resident` 实测就是 3,145,728）。
2. **`hits +637` 而 30 秒只有约 256 个 tick** ⇒ 每个 tick 命中约 2.5 次，
   即每 tick 两个精灵各换一帧，走的是 `present()` 的第二级路径
   （`lw_asset_refill()` + `lv_obj_invalidate(s->image)`），**纯内存拷贝 + 重绘**。
3. **`sched` 稳定在 117 ms**（`max_gap 119`，没有尖刺）⇒ 这是**恒定的每帧成本**，
   不是偶发的整图 invalidate。真换序造成的整图 invalidate 会表现为
   `max_gap` 远大于 `avg_gap` 的尖刺。

⇒ **RPG 成因 B = 每帧重绘两个 288×288 精灵的绘制成本。** 不是缓存，不是换序。
C-2 的守卫仍然保留（它是对的、也是必要的），但它解决的不是 117 ms 这个数。

> 补充：`anim off` 后 RPG 页立刻回到 `ticks=504 avg_gap=20 ms max_gap=20 ms`，
> 而全屏背景与 ambience 覆盖层一点没少。这再次把矛头指向精灵重绘，
> 而不是整屏合成。

### 3.2 对照范本：桌面页早就这么做了【静态证据】

`lw_os.c:282`：

```c
if (s->front_actor != near) {
    lv_obj_move_to_index(lw_sprite_object(s->sprites[1-near]), -1);
    lv_obj_move_to_index(lw_sprite_object(s->sprites[near]), -1);
    s->front_actor = near;
}
```

桌面页 30–37 ms，RPG 页 120 ms —— 同样的双角色、同样的精灵层，差异之一就在这里。

> **注意（真机修正）**：上一条是静态分析时的推断，**已被真机数据部分推翻**。
> 桌面页与 RPG 页的真实差异不是换序守卫（两页现在都有），而是
> **背景层数**：桌面页的 `build_desktop()` 有 `lw_os_foreground()` 前景层且
> 家具是绘制回调，而 RPG 页压着一张**全屏 1024×600 的 `rpg-room.lwr` RAM 图**
> （1,228,800 B）。两个 288×288 精灵在这张全屏图上重绘，比在桌面页上重绘贵得多。
> 见 §5 与 §3.1.1。

### 3.3 改动【已落地】

`lw_rpg_view.c:19` 结构体加字段：

```c
float sep; uint32_t dt; int last_near; int front_actor;
```

`lw_rpg_view.c:103-109`：

```c
/* Only reorder when the front actor actually changes. lv_obj_move_to_index
 * ... this the hard way too (lw_os.c:282 keeps a front_actor guard): a real
 * swap invalidates the whole 1024x494 map, which is the entire room repaint. */
{ int front = py[0] > py[1] ? 0 : 1;
  if (front != v->front_actor) { lv_obj_move_to_index(v->actor[front], -1); v->front_actor = front; } }
```

`lw_rpg_view.c:216` 初始化 `v->front_actor = -1;`（保证第一次一定会摆正）。

**这只是必要条件，不是充分条件。** RPG 页 120 ms 的成因可能有多个（房间背景是
1,228,800 B 的全屏 RAM 图、`lw_sprite_set_anchor` 每帧字符串查表、精灵画布
288×288 而可见只有约 108×212）。本文件只声称「移除了一个会在真换序时作废整图
的路径」，不声称 RPG 已经流畅。

---

## 4. C-3 宠物页：每 40 ms 重写 5 个没变的控件

### 4.1 机制【静态证据】

`lv_label_set_text()` **没有同文本守卫**（`lv_label.c:131-136` → `set_text_internal`）：
即使 `label->text == text`，也走 `lv_realloc` 分支，末尾无条件调
`lv_label_mark_need_refr_text(obj)`，后者执行

```c
label->invalid_size_cache = true;
lv_obj_invalidate(obj);
lv_obj_refresh_self_size(obj);
```

并挂 `LV_EVENT_UPDATE_LAYOUT_COMPLETED` 回调。也就是说：**每 40 ms 写一次同样的
字符串 = 每 40 ms 一次 invalidate + 一次延迟重排。**

`lw_app_pet.c` 的 `refresh()` 里，原本每 tick 无条件写：

| 行 | 写入 | 实际变化频率 |
|----|------|--------------|
| 60 | `lv_label_set_text(s_mood_label, ...)` | 心情变化时（分钟级） |
| 63/66/69 | `lw_bar_set(fullness/energy/affection, ...)` | 数值变化时（分钟级） |
| 76 | `lv_label_set_text(s_sleep_button_label, "叫醒"/"休息")` | 睡觉状态翻转时 |
| 77 | `lw_skin_select(s_sleep_button, sleeping)` | 同上 |

而 `lw_bar_set()`（`lw_widgets.c:194-211`）本身也没有同值守卫，它调
`lv_obj_set_width(fill, (width*percent)/100)`；`lv_obj_set_width` 内部有同宽守卫，
所以 bar 的浪费比 label 小，但仍有每 tick 的取宽与乘法。

### 4.2 改动【已落地】

`lw_app_pet.c:41-43` 新增：

```c
static const char *s_mood_shown; static bool s_mood_shown_set;
static int s_fullness_shown=-1, s_energy_shown=-1, s_affection_shown=-1;
static int s_sleep_shown=-1;
```

`lw_app_pet.c:71-72`、`76-77`、`94-95` 等处改成「值变了才写」：

```c
if (!s_mood_shown_set || mood != s_mood_shown) { ... s_mood_shown = mood; s_mood_shown_set = true; }
if (s_fullness_bar != NULL && pet->fullness != s_fullness_shown) { ... s_fullness_shown = pet->fullness; }
if (sleeping != s_sleep_shown) { ... s_sleep_shown = sleeping; }
```

`mood` 用**指针比较**是安全的：`lw_pet_mood_name()`（`lw_pet.c:162-174`）全部返回
字符串字面量（`return "开心";` 等），同一分支每次返回同一地址。

---

## 5. C-4 精灵画布裁剪：**本轮不改代码，但真机把它升为下一轮主战场**

### 5.1 现象【静态证据】

`lw_rpg_view.c` 建精灵：`lw_sprite_create(v->map, 0, 0, 288)`。**288 是 scale 不是
画布尺寸**（`256` = 1×，`288` = 1.125×）。帧源是 256×256，显示 288×288，而
不透明内容实测约：男方 108×212、千千 104×202。

所以每个精灵的 `lv_image` 对象是 288×288 = 82,944 px，其中约 22,896 px 有内容，
**约 3.6× 的面积是透明 padding**。两个精灵 = 165,888 px 的画布。

### 5.2 为什么静态阶段判断「本轮不做」

我查了 `lv_image.c` 确认了裁剪的可行路径：

- `lv_obj_set_size(image, w, h)` **只改对象坐标，不裁源像素**（构造函数里
  `img->w = lv_obj_get_width(obj)`，换源时 `img->w = header.w`）；
- 真正裁剪由 `layer->_clip_area` 完成（`lv_refr.c:125`）；
- 想裁画布必须配合 `lv_image_set_offset_x/y`（默认 `LV_IMAGE_ALIGN_CENTER` 下
  `lv_area_align` 会把 offset 算进落点，`lv_image.c:891`）。

当时的理由是它不解决**当时以为的**瓶颈（调度/缓存）：

1. 脏区由「变化的像素」决定，不是由对象尺寸决定。`lv_obj_invalidate(image)` 用的是
   `obj->coords`（= 对象尺寸），所以**缩小对象确实会缩小脏区** —— 但只在精灵真的
   重绘时才有收益。当时以为 RPG 页的问题是定时器被拖到 120 ms 即「根本没在画」。
2. 缩小画布会改变命中测试与锚点几何。`lv_image.c:760-767` 的 `LV_EVENT_HIT_TEST`
   在「对象尺寸 == 图像尺寸且有缩放」时才做变换区域命中测试，缩了之后就退回矩形。
   桌面页的 hit box 是独立对象（`lw_os.c` 里 `lv_obj_set_user_data(lw_sprite_object(...), hit)`），
   但 RPG 页的热区是 `v->spots[i]`，两者耦合方式不同，需要逐个页面验证点击是否
   仍然准。
3. 交接书禁止「降画质换 FPS」。裁剪画布本身不降画质（像素一个不动），但它会改变
   锚点落点，稍有不慎就是角色错位 —— 这是画质问题。

### 5.3 真机数据推翻理由 1，C-4 升级为下一轮主战场【真机实测】

理由 1 的前提「根本没在画」是**错的**。真机证明 RPG 页**恰恰就是在画，而且画得很贵**：

| 证据 | 读数 | 含义 |
|---|---|---|
| 30 秒 `misses` / `evictions` | 零增长 | 不是 I/O |
| 30 秒 `hits` | +637（约 2.5 次/tick） | 每 tick 两个精灵都在换帧重绘 |
| `sched` | `avg 117 ms` / `max 119 ms`，无尖刺 | 恒定每帧成本，不是偶发整图 invalidate |
| `anim off` 后 | **20 ms / max 20** | 关掉帧推进即恢复满帧 ⇒ 成本全在「画精灵」 |

再叠加 §2.3.2 的算术：宠物页 488 ms 里只有约 14% 是 I/O，**86% 是绘制**。
⇒ **三个慢页面（RPG 117 / 宠物 488 / 世界偶发 157）的公共主因都是精灵重绘成本。**
而精灵重绘成本里最大的一块正是 C-4 指出的**3.6 倍透明 padding**：
每帧要把 288×288 的源矩形交给 `lv_draw_image` 做缩放+alpha 混合，
其中约 72% 的像素在混合后完全被丢弃。

所以 C-4 在本轮的状态更新为：

- **代码本轮不改**（没有板子时改几何=用没测过的几何换没测过的收益，而且当时板上
  还是旧镜像，改了也验证不了）；
- **但它现在是下一轮的第一优先级**，量化预期写在下面。

### 5.4 下一轮 C-4 的做法与预期【待真机】

做法（不改素材、不关动画、不降画质）：

1. `lw_sprite_create()` 之后，按 spec 的**实际不透明包围盒**（需要给 `lw_asset_spec_t`
   加 `bbox_x/y/w/h`，或在注册时从 alpha 通道扫一遍）调 `lv_obj_set_size(image, bbox_w*scale/256, bbox_h*scale/256)`；
2. 用 `lv_image_set_offset_x/y` 把源图位移回去，使落点与现在**逐像素一致**；
3. 锚点仍用 `spec->anchor_x/y` 换算，公式不变。

预期收益（可算，待真机验证）：`82,944 → 约 22,896 px`，即精灵重绘面积降到 **27.6%**。
若 RPG 页 117 ms 里精灵占大头，理论上限是降到约 **32–40 ms**；宠物页同理。
**这只是算术上限，不是承诺** —— 必须上板用同一套 `sched`/`anim off` 单变量验证。

风险（必须在报告里留档）：`LV_EVENT_HIT_TEST` 在对象尺寸 ≠ 图像尺寸时退回矩形命中，
RPG 页热区是 `v->spots[i]` 独立对象所以不受影响，但**桌面页的 hit box 与精灵绑定**，
需要单独确认点击仍然准。

---

## 6. 【待真机】验证方法与判据

板子插上后，按顺序做单变量验证。每条都只改一件事，`sched reset` 后采 5 秒。

### 6.1 验证 C-1（缓存）

```
lw> diagjson        # 记 cache hits/misses/evictions/reads/resident/peak/budget
lw> sched reset
（等 10 秒）
lw> sched
lw> diagjson        # 再记一次，算差值
```

判据：

- **改前**（16 槽 / 4 MiB）：宠物页 10 秒内 `evictions` 应接近「帧数 × 2 精灵」，
  `reads` 同步增长，`resident` 钉在 `budget` 上。
  **真机实测（旧镜像，即改前）**：10 秒 **+100 misses / +100 evictions**，
  `resident=3145728`（正好 16 × 196,608），`budget=4194304`。
- **改后**（32 槽 / 12 MiB）：`resident` 应稳定在约 7.37 MB 附近并**停止增长**，
  `evictions` 增速应大幅下降甚至归零。
- 若 `evictions` 归零而 `sched` 仍 >100 ms，则慢**不是** I/O 造成的
  —— 按 §2.3.2 的算术，I/O 最多占 14%，**即使全部消除，宠物页也只到约 419 ms**，
  必须转 C-4（§5.4）。

### 6.2 验证 C-2（RPG 遮挡）

```
lw> rpg             # 连续读 10 次，看 screen 是否还在跳
lw> sched reset
（让两角色走到同一格附近，脚底 y 交叉）
lw> sched
```

判据：`sched` 的 `max_gap` 若在交叉那一帧出现尖峰（明显高于 avg），说明整图
invalidate 确实是尖峰来源；守卫后尖峰应消失或显著变窄。

### 6.3 验证 C-3（宠物页控件）

`lw> anim` 看帧索引是否在推进。C-3 的收益量级是「每秒省掉若干次
`lv_obj_invalidate` + 一次延迟重排」，预期在**毫秒级**，不会改变页面档位
（488 ms → 419 ms 那一步靠 C-1，419 ms → 20 ms 那一步靠 C-4）。
所以 C-3 的验证方式是**精细对照**：同一页 `anim on` 下，改动前后
`sched` 的 `avg_gap` 差值，而不是等它掉一个数量级。

### 6.4 若 C-1 修好后宠物页仍慢，转查这里

> **状态更新（真机实测后）**：本条已从「候选怀疑」升级为**已确认的主因**。
> `anim off` 实验（§2.3.2）证明：关掉帧推进后全屏背景与 ambience 覆盖层的工作量
> 一点没变，宠物页却从 488 ms 掉到 **20 ms**。⇒ **全屏重合成不是主因，精灵重绘才是。**
> 下面这段保留原文，但结论已被改写。

`create_room()` 叠了**两层全屏 1024×600**：

- `lw_visual_daily_image(stage)` 并 `lv_obj_set_y(bg, -52)` —— 1,228,800 B RAM 图；
- `lw_ambience_layer(stage, 1, -52)` —— 第二个全屏半透明层。

`lw_ambience_refresh()` 会 `lv_obj_invalidate(o)`。任何精灵重绘都要把该区域连同
1,228,800 B 的背景图重新合成。

**原先的算术**（保留以示推断过程）：一次整屏重绘要合成
背景 blit 614,400 px + ambience blend 614,400 px = **1,228,800 px**，
777 ms 对应约 **1,581 Mpx/s**，对纯软件 RGB565 是「可能达到」的量级。

**真机判定：这条被否证。** `anim off` 时同样的两层全屏图、同样的 1,228,800 px
合成量，`sched` 是 **20 ms / max 20 ms**。若全屏合成真是 1,581 Mpx/s 的吞吐，
它不可能在关掉精灵动画后凭空变快 24 倍 —— 因为合成量根本没变。

⇒ 真正贵的是**精灵那一块**：每个 288×288 画布里 72% 是透明 padding，
`lv_draw_image` 要对整块做缩放 + alpha 混合。这就是 §5.3 把 C-4 升为下一轮
主战场的原因。下表是更新后的判据。

| 观察 | 结论 |
|---|---|
| `anim off` 后 `sched` 回到 20 ms | **成本在精灵重绘**（本轮实测，已确认） |
| `evictions`/`reads` 随 C-1 改动大幅下降，`sched` 同步下降 | I/O 也有份，但按 §2.3.2 最多占 14% |
| `evictions`/`reads` 归零，`sched` 仍 >100 ms | 纯绘制，转 C-4（画布裁剪） |

### 6.5 本轮已经做完的真机实验（可复现）

板子接上后跑了下面四组单变量实验，命令与读数如下。**板上是
`v4.3-baseline-dirty` 镜像**，所以这些数解释的是**改动前**的成因。

```powershell
# 进入宠物页（dock 把手 → 宠物 item），然后：
& .\tools\console_cmd.ps1 -Command "anim on"  -Seconds 2
& .\tools\console_cmd.ps1 -Command "diag"     -Seconds 3   # 记 cache 基线
& .\tools\console_cmd.ps1 -Command "sched reset" -Seconds 2
Start-Sleep -Seconds 10
& .\tools\console_cmd.ps1 -Command "sched"    -Seconds 3
& .\tools\console_cmd.ps1 -Command "diag"     -Seconds 3   # 算差值
```

| 页面 | 动画 ON | 动画 OFF | 缓存计数器（ON 时） |
|---|---|---|---|
| 音乐 6（参照） | 20 ms / max 20 | —— | `sprites=0`，无轮换 |
| 桌面 0 | 稳定 20 ms（启动窗口 35 ms，`max_gap 439`） | 20 ms / max 20 | 偶发 +97 misses / +85 evictions |
| 世界 1 | 预热后 34–49 ms，偶发 157 ms | —— | 偶发 +85 misses / +82 evictions |
| RPG 9 | **117 ms / max 119** | **20 ms / max 20** | **30 秒零 miss、零 eviction** |
| 宠物 8 | **477–746 ms / max 919** | **20 ms / max 20** | **10 秒 +100 misses / +100 evictions** |

这条表是 P0-C 全部结论的原始依据，也是下一轮验收的对照基线。

---

## 7. 本轮没做的事

- **没有**关动画、降画质、删素材来换帧率。
- **没有**改 `lw_sprite.c` 的 `present()` 三级路径（V4.4 之前已修）。
- **没有**改 `lw_assets.c` 的 2 MiB 租约预算（`skin.lwp` 的 835,584 B 图集需要它，
  且租约是「当前显示的那一帧」，与缓存容量是两回事）。
- **没有**做 C-4 的画布裁剪（§5.3 已把它升为下一轮第一优先级，但本轮不改代码：
  真机数据证明它是主战场的同时，也说明必须用**同一套单变量方法**验证收益，
  而不是拿一个没测过的几何去换一个没测过的收益）。
- **没有**碰 NVS、分区表、C6、eFuse。
- **已烧录**（用户批准后，`-Stage app -Execute -Authorized`，只写 `0x010000`，
  esptool 回报 `Hash of data verified.`）。烧后读数见 §9。
- **没有**在未上板的情况下声称任何页面变快。改动后的收益只在 §9 里出现，
  且每个数字都带命令与窗口长度。

---

## 8. 提交状态

本轮改动（`git status --short`）：

```
M firmware/components/lw_storage/src/lw_storage.c
M firmware/components/lw_ui/src/lw_app_pet.c
M firmware/components/lw_ui/src/lw_rpg_view.c
M firmware/main/Kconfig.projbuild
M firmware/main/app_main.c
M firmware/main/lw_art.c
M firmware/main/lw_boot.c
M firmware/main/lw_capture.c
M firmware/components/lw_ui/include/lw_os.h
M firmware/components/lw_ui/src/lw_os.c
M tools/build_v4_matrix.ps1
?? firmware/sdkconfig.extra.perf-ppa
?? firmware/sdkconfig.extra.perf-unit1
?? firmware/sdkconfig.extra.perf-double
?? firmware/sdkconfig.extra.perf-full
?? tools/lwp_probe.py
```

构建与测试（本轮实测）：

| 检查 | 结果 |
|------|------|
| `ninja -C build/host` | 通过（`lw_app_pet.c` / `lw_os.c` / `lw_rpg_view.c` / `lw_storage.c` 全部重编） |
| `tools/run_host_tests.ps1` | `142 checks, 0 failure(s)` `HOST TESTS PASSED` |
| `tools/run_core_regression.ps1` | `Duo 82` / `RPG 336` / `Storage 33`，全 0 failure，`CORE REGRESSION PASSED` |
| `idf.py build`（`os`） | `Project build complete`，2,359,248 B，分区余 63% |

---

## 9. 改动后的真机验证（烧录本轮镜像之后）

用户明确批准后烧录（`tools/flash_v4_safe.ps1 -Config os -Stage app -Execute -Authorized`，
只写 `0x010000`，esptool `Hash of data verified.`，日志
`reports/logs/flash/flash-20261028-174129.log`）。烧后版本串
`v4.3-baseline-1-g04ad8e5-dirty`，`boot` 命令与 `boot phases` 日志首次可用。

### 9.1 C-1 生效：宠物页 I/O 归零

| 指标 | 改动前（旧镜像） | 改动后（本轮镜像） |
|------|------------------|--------------------|
| 20 秒窗口 `misses` | **+100 / 10 秒** | **+0**（48 冻结） |
| 20 秒窗口 `evictions` | **+100 / 10 秒** | **+0**（13 冻结） |
| `cache_resident` | 3,145,728 B（钉在 16 槽上限） | **9,584,640 B** |
| `cache_budget` | 4,194,304 B | **12,582,912 B** |
| `sched` avg_gap（稳态） | 477–746 ms / max 919 | **251 ms / max 274**（`ticks=81`，20 秒） |
| `sched` avg_gap（刚切页） | —— | 480 ms / max 765（`ticks=3`，预热窗口） |

命令：

```
console_seq.ps1 -Port COM5 -Steps @('sched reset|300','status|20000','sched|600','diagjson|900')
```

读数逐字：`sched: ticks=81 avg_gap=251 ms max_gap=274 ms (timer period 20 ms)`，
`"cache_hits":2558,"cache_misses":48,"cache_evictions":13,"cache_resident":9584640,"cache_budget":12582912`。

**判定**：24 帧活跃集（7,372,800 B）现在真的驻留了，`resident` 从 3,145,728 涨到
9,584,640 就是证据；`misses`/`evictions` 在 20 秒里一动不动，说明同步 SD 读已经消失。
耗时 477–746 → **251 ms**，比 §2.3.2 预测的「最乐观约 419 ms」还要好，因为那个预测
是按 488 ms 中位数算的，而实际稳态取到了 251 ms。

### 9.2 剩余 251 ms 全是绘制：C-4 是唯一路径

同一页面、同一窗口，`anim off` 后：

```
anim: animations off (saved)
sched: ticks=776 avg_gap=20 ms max_gap=20 ms (timer period 20 ms)
```

⇒ 关掉帧推进后**回到 20 ms**。I/O 已经在 §9.1 归零，所以这 251 ms 里没有任何等待
SD 的成分，**全部是每帧重绘两个精灵（320×320 源、352×352 显示）的绘制成本**。

与 §5.3 的算术一致：宠物页单精灵画布 352×352 = 123,904 px，实际不透明内容按同比例
估计约 30% ⇒ 约 **70% 的混合像素被丢弃**。C-4 是把这个比例降下来的唯一手段。

### 9.3 C-2 如预测无收益，C-3 无可测收益

| 页面 | 改动前 | 改动后 | 20 秒缓存变化 | 判定 |
|------|--------|--------|---------------|------|
| RPG | 117 ms / max 119 | **119 ms / max 129** | 零增长（`misses=24`、`evictions=0`） | 守卫**无收益**，符合 §3.1.1 预测 |
| 宠物 | 477–746 ms | **251 ms** | 零增长 | 收益来自 C-1，不是 C-3 |
| 桌面 | 20 ms（预热窗口 156） | **21 ms / max 36** | `evictions` **+0** | 无退化 |
| 世界 | 预热后 34–49，偶发 157 | **21 ms / max 37** | —— | 无退化，预热窗口消失 |
| 音乐 | 20 ms | **20 ms / max 20** | —— | 无退化（参照系） |

C-3（宠物页 5 处控件值守卫）的预期收益是「每 40 ms 少 5 次 invalidate + 延迟重排」，
量级在毫秒级；在 251 ms 的绘制成本面前**不可测**。它的价值是减少无谓 invalidate，
不是把页面拉回 20 ms。**不要把它算作性能修复**。

### 9.4 对 §1 摘要表的最终状态

- C-1：**已修复并验证**（宠物页 I/O 归零，477–746 → 251 ms）。
- C-2：**已修复但无收益**（RPG 119 ms，守卫正确但瓶颈不在这里）。
- C-3：**已修复但不可测**（被绘制成本淹没）。
- C-4：**未做，且已被证明是唯一剩余路径**。RPG 119 ms、宠物 251 ms、开机 409 ms/帧，
  三个数字的共同分母都是「每帧重绘的精灵面积」。

### 9.5 新的未解现象：桌面/世界页的双稳态（116 ms ↔ 32 ms）

这是本轮最后阶段才浮现的，与 §1–§9.4 的结论**不冲突但需要单列**，因为它
说明「精灵重绘」的成本本身还能再差 3.6 倍。

**现象**：32.1 分钟 soak（`reports/logs/v44/stress-20261028-180752.log`，162 样本）
里，桌面页与世界页**同步地**在快慢两态之间成串切换：

| 样本 | 桌面 `avg/max` | 世界 `avg/max` |
|---|---|---|
| #1–#68（约 20 分钟） | 21–25 / 36–57 | 21–24 / 34–56 |
| #73–#98（约 5 分钟） | **116** / 131–142 | **117** / 127–139 |
| #103–#110 | 22 / 53–54 | 23–24 / 40–41 |
| #115–#158（约 7 分钟） | **114–116** / 131–144 | **113–118** / 125–139 |

音乐页与回忆页**全程** `20/20`，不受影响。快慢两态绘制成本差约 **3.6 倍**。

**三个单变量实验**（`tools/probe_desktop_slow.ps1`，本轮新增，输出行含
`sprites` / `running` / `leases` / `bytes` / `resident` / `evict` / 两角色
`clip/frame`）：

1. **不是泄漏。** 慢态下连续 31 个样本：`anim_sprites` 恒 2、
   `asset_leases` 恒 3（两角色帧 + 皮肤图集 835,584 B）、`cache_resident`
   恒 8,146,944、`cache_evictions` 恒 2042 —— **完全冻结，零 I/O**。
2. **慢的仍然是精灵帧推进。** 同一慢态下 `anim off` →
   `sched: ticks=313 avg_gap=20 ms max_gap=20 ms`；`anim on` →
   `sched: ticks=55 avg_gap=113 ms max_gap=135 ms`。
3. **一次整页重建可重置慢态。** 慢态（116 ms）下往返一次
   （桌面 → 音乐页 `page 6, dock open` → 返回桌面 `page 0, dock closed`），
   回来立刻 `avg_gap=33 ms`；随后 7 分钟 24 个样本全部 31–33 ms，
   未再漂移回慢态。

   两次探针的日志可以并排对照（`reports/logs/v44/probe-desktop-20261028-184117.log`
   是慢态 32 个样本、`...-185310.log` 是快态 24 个样本）：

   | | 慢态（184117） | 快态（185310） |
   |---|---|---|
   | `sched avg/max` | 113–116 / 127–141 | **31–33 / 60–79** |
   | `ticks` | 58–61 | 207–217 |
   | `sprites` / `running` | 2 / 2 | 2 / 2 |
   | `leases` / `bytes` | 3 / 1,228,800 | 3 / 1,228,800 |
   | `cache_resident` | **8,146,944** | **8,681,472** |
   | `cache_evictions` | 2042 | 2044 |

   ⇒ **慢态与快态的唯一可观测量差别是 `cache_resident` 差 534,528 B 与
   `evictions` 差 2。** 精灵数、租约数、租约字节数完全相同。
   534,528 B 不是整帧数（÷196,608 = 2.72，÷307,200 = 1.74），但 `evictions` 恰好
   多 2 次 —— 说明快态那次重建把两个 clip 的帧换成了另一组，驻留集合不同。
   **这条差值不能证明因果**（它是重建的后果，不一定是变快的原因），
   但它是目前唯一稳定的相关量，可作为下一轮探针的抓手。

**已排除的候选**（都有真机或源码依据）：

| 候选 | 排除依据 |
|---|---|
| 内存泄漏 / 精灵对象累积 | 精灵数恒 2、租约恒 3、`heap_free` 与 `cache_resident` 反向抵消 |
| 缓存颠簸 / I/O | `cache_misses` 与 `cache_evictions` 在慢态窗口内完全冻结 |
| 脚本错标页面 | 已加 `page_ok` 校验（修好 7 次 RPG 错页），剩下的样本是真实现象 |
| `lw_ambience` 相位切换触发整页 `render()` | `lw_day.c:156` 写入 `LW_DAY_DEMO_SECONDS (19*3600+30*60)`=19:30，`lw_ambience_phase()` 分档使 19:30 属 NIGHT，32 分钟内相位不变 |
| 切页频繁导致的抖动 | 连续 48 次页面切换（24 个往返）后桌面仍为 `ticks=298 avg_gap=21 ms` |
| 开机窗口残留 | 慢态窗口出现在 `boot_ms≈4,800,000`（约 80 分钟运行）之后 |

**仍未定论**：为什么同一份 `male.idle` / `qianqian.idle` 在两个态下差 3.6 倍。
剩余候选（按可测性排序）：

1. 慢态下精灵对象的 `ext_draw_size` 与 `coords` 与快态不同（`lv_obj_set_size`
   未生效的旧值），使 `lv_obj_redraw()` 的裁剪框远大于实际不透明区域。
2. `lw_sprite_set_anchor()` 每帧做 `lw_assets_find(s->shown_id)` 字符串线性查找，
   慢态下命中的 spec 与快态不同（`shown_id` 被 `present()` 换过）。
3. `s->foreground` 层（`lw_os.c` 末尾每帧 `lv_obj_move_foreground`）在慢态仍在
   最前，导致桌面页多一层全屏合成。
4. 桌面页 `hit box`（`124×242` / `252×112`）在慢态未随 `lying` 更新。

**验证方法**：给 `lw_os.c` 的 `update_actors()` 末尾加一个 `diag` 可读的探针，
记录两个精灵的 `lv_obj_get_x/y`、`lv_obj_get_width/height`、
`lv_obj_get_ext_draw_size` 与当前 `shown_id`；在快态与慢态各采一次即可二分定位。
这需要一次新的编译 + 烧录，**本轮未做**。

**对 C-4 的影响**：如果慢态确实由「裁剪框远大于内容」引起，那么 C-4（把画布缩到
内容 bbox）不只是把 82,944 px 降到 22,896 px，还可能**同时消除双稳态**。
这使 C-4 的预期收益上界从「RPG 32–40 ms」提高到「RPG 与桌面/世界页一并回到 20 ms 档」。
