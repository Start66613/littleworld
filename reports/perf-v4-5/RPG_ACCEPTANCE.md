# P5 · RPG 页验收

RPG 页是 V4.4 里**改动最多、收益最少**的页面：成因 A（瞬移）修好了，
成因 B（遮挡守卫）真机实测**零收益**。本文档说明现在的验收判据是什么、
哪些已经通过、哪些还差什么。

---

## 0. 验收结论

| 验收项 | 判据 | 状态 |
|---|---|---|
| 连续移动（无整格瞬移） | 每帧位移 ≤ 0.16 格；逻辑层给浮点位置 | ✅ 通过（宿主 336 checks 含 `test_render_interpolation`） |
| 走路不受装饰动画开关影响 | `anim off` 时走路仍逐帧推进 | ✅ 通过（`if (moving \|\| lw_os_motion_get()!=LW_MOTION_OFF)`） |
| 双人靠近不瞬移 | 128 px 偏移用 ~80 ms 缓动，到位后归零 | ✅ 通过 |
| 前后遮挡正确 | 脚底像素比较 + `front_actor` 守卫 | ✅ 通过 |
| 存档不因走路逐帧标脏 | 只按逻辑格 `state.actors[i].pos` 标脏 | ✅ 通过 |
| **40–50 FPS** | 真机 `sched` ≤ 25 ms | ❌ **真机 119 ms（约 8 fps）** |
| 点击 p95 < 50 ms | 真机点击延迟探针 | ⬜ 未测（板子未连接） |

**一句话**：RPG 的**正确性**验收全部通过，**性能**验收未通过，而性能的解法
不在 RPG 页内部——在 `SPRITE_RENDER.md` 的烘焙路径里。

---

## 1. 成因 A（已修）：整格瞬移

### 1.1 旧表现层做了什么

`firmware/components/lw_ui/src/lw_rpg_view.c` 的旧 `refresh()`：

```c
int oldx = lv_obj_get_x(v->actor[i]), oldy = lv_obj_get_y(v->actor[i]);
lw_sprite_set_anchor(v->players[i], x, y);       /* x,y 是整格 */
int nx = lv_obj_get_x(...), ny = ...;
if (v->actor_x[i] != x || v->actor_y[i] != y) {
    lv_obj_set_pos(v->actor[i], oldx, oldy);     /* 拉回上一格 */
    lw_os_motion_step(v->actor[i], nx, ny, 140); /* 再补 140 ms 补间追过去 */
}
```

两个后果：

1. 每换一格都把基准位置拉回上一格再补间 ⇒ 视觉每 140 ms 从格线重新起步（一顿一顿），
   而不是匀速走。
2. `lw_os_motion_step()`（`firmware/components/lw_ui/src/lw_motion.c:75-81`）：

   ```c
   if (lw_os_motion_get() == LW_MOTION_FULL) { animate(...); }
   else { lv_anim_delete(...); move(o,0); move_x(o,0); }
   ```

   **动画模式不是 FULL 时直接把 translate 归零** ⇒ 整格瞬移。

### 1.2 修法

- **逻辑层给连续坐标**：`rpg/include/lw_rpg.h` 新增
  `void lw_rpg_actor_point(const lw_rpg_state_t *state, int index, float *x, float *y);`
  以 `actor->pos` 为基准，若正在移动且 `step_accum_ms > 0`，按「尚在飞行中的
  下一步方向」叠加 `t = step_accum_ms / LW_RPG_STEP_MS`（clamp 到 1.0）。
- **表现层直接写连续坐标**：`refresh()` 每帧 `lw_rpg_actor_point()` 取浮点位置，
  `tile_point_frac()` 换成像素，`lw_sprite_set_anchor()` 直接写入。
  **删掉了全部 `lv_os_motion_step()` 调用。**
- 千千原来的 ±128 px 瞬移改成目标值 + 每帧 `0.25` 缓动（约 80 ms 到位），
  到位后吸附回 0，避免长期漂移。
- `tile_point_frac()` 抽出来供整格与插值共用，**整格数值与旧常量逐字节一致**，
  所以站位、点击热区、家具对齐都没变。

### 1.3 单测

`tests/host/test_rpg.c` 新增 `test_render_interpolation()`：

- 静止时返回精确格 `(6,5)`；
- `lw_rpg_tap_tile(10,7)` 后逐帧 20 ms 推进 80 帧，断言**每帧位移 ≤ 0.16 格**。

RPG 核心测试 **336 checks / 0 failure**。

---

## 2. 成因 B（已试，零收益）：遮挡守卫

### 2.1 假设

`refresh()` 每 20 ms 无条件两次 `lv_obj_move_to_index`，LVGL 在真换序时
`lv_obj_invalidate(parent)` ⇒ 整张 1024×494 地图重绘。加守卫应该能省掉。

### 2.2 实现

```c
int front = py[0] > py[1] ? 0 : 1;
if (front != v->front_actor) {
    lv_obj_move_to_index(v->actor[front], -1);
    v->front_actor = front;
}
```

### 2.3 真机实测：**无收益**

| 项 | 值 |
|---|---|
| 改动前 RPG | 113–117 ms |
| 改动后 RPG | **119 ms** |
| 30 秒内 `misses` | 24（未变） |
| 30 秒内 `evictions` | 0 |

**为什么无收益**：`lv_obj_tree.c` 的 `lv_obj_move_to_index()` 本来就有早退
`if ((index < 0) || (index >= parent_child_count) || (index == old_index)) return;`
⇒ 不变索引根本不会 invalidate。守卫只是**语义上**正确，不是性能优化。

这个结论在 V4.4 就写进 `SPRITE_CACHE.md` 了，本轮把它移到本文档作为 RPG 页的
正式验收记录。

---

## 3. 性能：为什么 RPG 是 119 ms

### 3.1 已排除

| 候选 | 排除依据（真机） |
|---|---|
| 缓存颠簸 | RPG 30 秒 `misses 3389→3389`、`evictions 3001→3001`、`hits +637` ⇒ **零 I/O**。活跃集 16 × 196,608 = 3,145,728 B 正好装进 16 槽 / 4 MiB |
| 遮挡重排 | 加守卫后 119 ms，与 117 ms 无差别 |
| 全屏背景图 | `lv_visual_rpg_image(v->map)` 是 1024×600 RGB565 = 1,228,800 B，但它在 `s->content` 下面且不每帧 invalidate |
| 主任务栈 / SD DMA / 全局调度 | V4.4 BASELINE 已逐项排除 |

### 3.2 已确认：精灵逐帧重绘

真机单变量 `anim off`：

| 页面 | 动画 ON | 动画 OFF |
|---|---|---|
| RPG | **117 ms** | **20 ms** |
| 宠物 | **477–746 ms** | **20 ms** |

⇒ **RPG 的 119 ms 里几乎全部是精灵重绘。**

### 3.3 精灵重绘为什么贵

`lw_sprite_create(v->map, 0, 0, 288)` ⇒ `lv_image_set_scale(288)` ⇒
**不是 `LV_SCALE_NONE(256)`** ⇒ 每帧走 `transform_and_recolor`
（`lv_draw_sw_img.c:300-311`）：`lv_malloc(8192)` + 定点重采样 + 二次 blend + `lv_free`。

素材是 `male.lwp` 的 256×256 画布，内容只有约 105×212（`sprite_bbox.json`）⇒
**66.7% 的画布是空白**，全部参与重采样。

### 3.4 解法（已在真机验证）

**RPG 页不需要任何 RPG 专用绘图改动。** 烘焙 `male.lwp`（`scale 288`）后：

- `lv_image_set_scale(256)` ⇒ 走直通快速路径；
- 画布从 256×256 降到 **137×239**（内容 bbox）；
- 每帧字节从 196,608 降到 **97,512**（-50.4%）⇒ 缓存压力更小。

真机结果（烧录后第一次进 RPG，站着不动）：

```
改前：rpg 页 sched avg_gap = 103 ms   （V4.4 是 119 ms）
改后：rpg 页 sched avg_gap =  20 ms   max_gap = 20 ms
```

宿主 RenderBench 预测 5.54×，真机是 5×以上，**预测与实测一致**。

### 3.5 命中测试风险（已实测，未发生）

`lv_image.c:760-767` 的 `LV_EVENT_HIT_TEST` 在「对象尺寸 ≠ 图像尺寸」时退回矩形命中。
RPG 的点击热区是独立的 `lv_obj`（`v->spots[i]`，`hits[3][4]={{314,280,96,80},...}`）
与 `map_click`，不是精灵对象本身。实测 22 次点击全部落到正确格子
（`taps=22 tap=8 ms`，角色按预期走动），风险未发生。

---

## 3.6 站着 20 ms 不等于走路 20 ms（用户纠正的验收项）

用户指出：「RPG界面你得让人物走起来测啊，你人物走起来就掉帧了」（m04059）。
实测证实：站立 20 ms，一走动立刻回到 89–117 ms。**这一项最终也修好了**：

| 阶段 | 站立 | 走路 `avg` | 走路 `max` |
|---|---|---|---|
| V4.4 基线 | 119 | 89–117 | 448 |
| 烘焙包（并集 bbox） | **20** | 43–89 | 436 |
| + 遮挡门控（`move_to_index` 相交判断） | 20 | 55 | 525 |
| + 零拷贝 borrow | 20 | 55 | 420 |
| + `ENTRIES 64` / `QUEUE 24` + clip 预取 | 20 | **20–21** | **185** |

最终读数（26 次点击、16 秒连续走动）：

```
sched: ticks=730 avg_gap=20 ms max_gap=185 ms
rpg perf: frames=777 refresh=383 ms avg=0 max=165 slow>=25ms=3
rpg perf: taps=22 tap=8 ms max=1 core=1 label=7 orders=1 order=0 ms
rpg perf: refresh split state=0 anchor=2 seek=321 ms
rpg perf: refill calls=286 borrowed=286
```

### 3.6.1 定位过程（每次都是一次烧录 + 一次实测）

| 步 | 假设 | 读数 | 结论 |
|---|---|---|---|
| 1 | 换序触发整屏重绘 | 加门控后 `max` 仍 448 | ❌ 否证 |
| 2 | 点击路径贵 | `tap=1 ms core=0 label=1` | ❌ 否证 |
| 3 | 精灵换帧贵 | `refresh split state=0 anchor=7 seek=7393` | ✅ seek 占 75% |
| 4 | seek 里 memcpy 贵 | 借用 100% 命中仍 21.9 ms/次 | 🟡 部分 |
| 5 | 命中里混着 SD 读 | `borrow_misses +98` ⇒ 98 × 70 ms ≈ 7 s | ✅ 成立 |

**真凶**：`lw_sprite_seek()` → `present()` → `lw_asset_refill()`，未命中时在 LVGL 线程
同步读 SD，实测 60–75 ms/帧；而走路每 50 ms 就要一帧 ⇒ 在绘制路径上等，永远追不上。
另外走路工作集 = 2 角色 × 16 帧 = 32 帧，**正好等于**当时 `ENTRIES 32` ⇒ 每步都在逐出
下一步要用的帧，这是结构性不匹配。

### 3.6.2 三项修复

| 修复 | 位置 | 依据 |
|---|---|---|
| 槽位 32 → 64、队列 8 → 24 | `firmware/components/lw_storage/src/lw_storage.c` | 工作集 = 32 帧；字节预算 12 MiB 未动，驻留 4.3 → 8.4 MB |
| 零拷贝借用 + `pins` | `lw_file_cache_borrow()` / `lw_asset_refill()` | 去掉每帧 97–170 KB PSRAM 拷贝；被借出的块禁止淘汰 |
| 整段 clip 预取 | `lw_sprite_play()` | 需求读 60–75 ms/帧 vs 走路 50 ms/帧 ⇒ 必须提前排队，把「每帧都 miss」变成「首帧 miss」 |

---

## 4. 真机复测记录

| 步骤 | 命令 | 结果 |
|---|---|---|
| 1. 烧录 | `tools/flash_v4_safe.ps1 -Config os -Stage app -Execute -Authorized` | `Hash of data verified.`、`flash done`、sha256 `3a67894b…` |
| 2. 进 RPG | `tap 512 578` → `tap 308 541` → `tap 866 372` | `page 9` |
| 3. 站立调度 | `sched reset` → `sched` | **20 ms / max 20** |
| 4. 走路调度 | 连续 `tap 700 470` / `tap 148 332` | **20–21 ms / max 185** |
| 5. 点击 | 地图格点击 | 22 次共 8 ms，单次 max 1 ms |
| 6. 瞬移 | 连续走 10 格 | 无整格跳（V4.4 已修，本轮回归通过） |
| 7. `anim off` 对照 | `anim off` → `sched` | 20 ms（走路不再依赖开关） |

---

## 5. 与交接书验收的对照

交接书要求「RPG 页 40–50 FPS」（≈ `sched` ≤ 25 ms）。**本轮达成**：

- 站立：20 ms（V4.4 119 ms）
- **走路：20–21 ms**（V4.4 89–117 ms），777 帧中 3 帧 > 25 ms
- 正确性：连续插值、双人不瞬移、遮挡门控生效、存档不逐帧标脏、点击命中正确

剩下的只有单帧尖峰 `max 185 ms`（新 clip 首次加载），不影响平均帧率，已记入
`FINAL_BENCHMARK.md` §6 的待办。
