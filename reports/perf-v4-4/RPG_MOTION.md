# V4.4 P0-B：RPG 连续移动

## 1. 原来的问题（已读源码确认，不是推测）

`rpg/src/lw_rpg.c` 的逻辑层本来是「按格推进」的：

```c
#define LW_RPG_STEP_MS 140u
...
actor->step_accum_ms += dt_ms;
while (actor->step_accum_ms >= LW_RPG_STEP_MS && actor->moving) {
    actor->step_accum_ms -= LW_RPG_STEP_MS;
    step_actor(state, i);
}
```

`step_actor()` 里 `uint8_t dir = actor->path[actor->path_index++];` 先取值后自增，
所以走完这一步之后 `path[path_index]` 就是「正在飞行中的下一步方向」，而
`step_accum_ms` 正好是这一步已经走过的比例。**这个「半步信息」本来就有，只是表现
层完全没用。**

旧的表现层（`firmware/components/lw_ui/src/lw_rpg_view.c` 的 `refresh()`）是这样
做的：

```c
int oldx=lv_obj_get_x(v->actor[i]),oldy=lv_obj_get_y(v->actor[i]);
lw_sprite_set_anchor(v->players[i],x,y);          /* x,y 是整格 */
int nx=lv_obj_get_x(...),ny=...;
if (v->actor_x[i]!=x || v->actor_y[i]!=y) {
    lv_obj_set_pos(v->actor[i],oldx,oldy);        /* 拉回上一格 */
    lw_os_motion_step(v->actor[i],nx,ny,140);     /* 再用 140ms 补间追过去 */
}
```

两个后果：

1. **每换一格都把基准位置拉回上一格再补间**，所以视觉每 140 ms 从格线重新起步
   （一顿一顿），而不是匀速走。
2. `lw_os_motion_step()` 在 `firmware/components/lw_ui/src/lw_motion.c:75-81` 是

   ```c
   if (lw_os_motion_get()==LW_MOTION_FULL) { animate(...); }
   else { lv_anim_delete(...); move(o,0); move_x(o,0); }
   ```

   **动画模式不是 FULL 时直接把 translate 归零**，于是整格瞬移。这正是任务书里
   那句「RPG 基本运动是玩法功能，不应该因为关闭装饰动画就变成瞬移」。

## 2. 改法

### 2.1 逻辑层给出连续坐标

`rpg/include/lw_rpg.h` 新增：

```c
/* Continuous render position in tile units. ... */
void lw_rpg_actor_point(const lw_rpg_state_t *state, int index, float *x, float *y);
```

`rpg/src/lw_rpg.c` 实现：以 `actor->pos` 为基准，若正在移动且 `step_accum_ms>0`，
按「尚在飞行中的下一步方向」叠加 `t = step_accum_ms / LW_RPG_STEP_MS`（clamp 到
1.0）。逻辑层仍然只管路径/碰撞/目标格，表现层拿到的是可以插值的浮点位置。

### 2.2 表现层直接写连续坐标

`lw_rpg_view.c` 的 `refresh()` 重写：

- 每帧 `lw_rpg_actor_point()` 取浮点位置，`tile_point_frac()` 换成像素，
  `lw_sprite_set_anchor()` 直接写入。**删掉了全部 `lv_os_motion_step()` 调用。**
- 千千原来在两人靠近时瞬间横移 ±128 px，改成目标值 + 每帧 `0.25` 缓动（约 80 ms
  到位），并且到位后吸附回 0，避免长期漂移。
- 走路帧不再受装饰动画开关门控：`if (moving || lw_os_motion_get()!=LW_MOTION_OFF)`。
- 存档脏标记只看逻辑格 `state.actors[i].pos`，走路本身不会每帧标脏。
- 前后遮挡改用脚底像素 `py[0]>py[1]`。

`tile_point_frac()` 抽出来供整格与插值共用，整格数值与旧常量逐字节一致，所以
站位、点击热区、家具对齐都没有变。

### 2.3 单测

`tests/host/test_rpg.c` 新增 `test_render_interpolation()`：

- 静止时返回精确格 (6,5)。
- `lw_rpg_tap_tile(10,7)` 后逐帧 20 ms 推进 80 帧，断言**每帧位移 ≤0.16 格**
  （一帧最多跨一次逻辑步的 1/7），且位移不越界、终点精确落在 (10,7)。
- 再 tap 后取 140+70 ms，断言方向取的是「还没走的那一步」而不是滞后一格。

`tools/run_core_regression.ps1` → RPG 核心 336 checks, 0 failure。

## 3. 真机结果（COM5，本轮实测）

新增 `rpg` 控制台命令，一次打印三层：

```
rpg 0: tile=4,6 point=4.89,6.00 screen=222,188 moving=1
```

`point` 是插值位置，`screen` 是控件真实 x/y。实测：

- **插值确实生效**：`point` 连续出现 `4.00 → 4.89 → 6.31 → 7.74`，
  千千一侧也出现 `9.43`、`6.17` 这种明显的小数格。
- **屏幕仍然跳**：`screen` 每 50–80 px 跳一次，而 `sched` 在该页读数是
  **120 ms**。

所以现象改善了一半、没有解决。诚实结论：

> **「瞬移」有两个成因。(a) 旧的整格吸附 + 140 ms 补间 —— 本轮已从代码删除；
> (b) 该页面 LVGL 定时器被拖到 120 ms 一次 —— 仍然存在，插值再细也画不出来。**
> (b) 不解决，肉眼看到的还是一次跳一格。

## 4. 成因 (b) 已在真机上定位：不是缓存，不是换序，是精灵重绘

板子接上后补做了单变量实验，成因 (b) 的猜测被逐条否证，真凶找到了。

### 4.1 否证「缓存 miss」

RPG 页连跑 30 秒，看 `diag` 的缓存计数器：

```
diag → cache: hits=… misses=3389 evictions=3001
（30 秒后）
diag → cache: hits=… misses=3389 evictions=3001   ← 一个都没涨
       hits 增加 637
sched → ticks=272 avg_gap=117 ms max_gap=119 ms
```

**`misses` 与 `evictions` 30 秒零增长** ⇒ RPG 页完全不碰 SD 卡。这符合算术：
RPG 活跃集 = 16 帧 × 196,608 B = **3,145,728 B**，正好装进旧的 16 槽 / 4 MiB
（`resident` 实测就是 3,145,728）。⇒ **缓存不是 RPG 的瓶颈**，C-1 的改动
（32 槽 / 12 MiB）对 RPG 页没有收益。

### 4.2 否证「`move_to_index` 整图 invalidate」

`max_gap` 是 **119 ms**，只比 `avg_gap` 的 117 ms 高 2 ms —— **没有尖刺**。
真换序造成的整图 invalidate（505,856 px）会表现为 `max_gap` 远大于 `avg_gap`
的偶发尖峰。读数是平的 ⇒ 这不是偶发的整图重绘，而是**恒定的每帧成本**。

（另外查过 LVGL 库：`lv_obj_tree.c:208-255` 的 `lv_obj_move_to_index()` 已有
`index == old_index` 早退，所以「每 20 ms 无条件调两次」本身不产生重排。
守卫仍然加了、也是对的，但它解决的不是这个数。）

### 4.3 决定性单变量：`anim off`

```
anim on  → sched: ticks=272 avg_gap=117 ms max_gap=119 ms
anim off → sched: ticks=504 avg_gap=20 ms max_gap=20 ms   ← 满帧
```

**关掉帧推进后，全屏房间背景图与 ambience 覆盖层的合成量一点没变，却从 117 ms
掉回 20 ms。** ⇒ 成本全在「每帧重绘两个精灵」。

`hits +637 / 30 秒 ÷ 约 256 个 tick ≈ 2.5 次/tick`，即每 tick 两个精灵各换一帧，
走 `present()` 的第二级路径（`lw_asset_refill()` + `lv_obj_invalidate(s->image)`）：
**纯内存拷贝 + 重绘，零 I/O**。

### 4.4 修正后的结论

> **「瞬移」有两个成因。(a) 旧的整格吸附 + 140 ms 补间 —— 本轮已从代码删除；
> (b) RPG 页每帧要重绘两个 288×288 精灵，单帧成本 117 ms —— 插值再细也画不出来。**
> (b) 的真凶是**精灵绘制面积**，不是缓存、不是遮挡换序、不是全屏合成。

修复方向是 C-4 画布裁剪：精灵画布 288×288 里约 72% 是透明 padding
（实际内容男 108×212、千千 104×202），`lv_draw_image` 要对整块做缩放 + alpha
混合后丢弃大部分像素。量化与方案见 `SPRITE_CACHE.md` §5.3–§5.4。

### 4.5 逐页实测（改动前，动画 ON）

| 页面 | avg_gap | max_gap | 备注 |
| --- | --- | --- | --- |
| 音乐 | 20 ms | 20 ms | 参照系，`sprites=0` 无轮换 |
| 桌面双角色 | 稳定 20 ms | 20 ms | 启动窗口 35 ms（`max_gap 439` 仅首次） |
| RPG | **117 ms** | 119 ms | 30 秒零 miss，纯绘制 |
| 世界 | 预热后 34–49 ms | 偶发 157 | 切页后需预热，`+85 misses/+82 evictions` |
| 宠物 | **477–746 ms** | 919 ms | 10 秒 +100 misses/+100 evictions，I/O 占约 14% |

音乐页 20 ms 证明显示链路和 LVGL 任务本身没问题，所以不是「硬件不行」。

### 4.6 烧录本轮镜像后的复测（用户批准烧录之后）

板上版本串变为 `v4.3-baseline-1-g04ad8e5-dirty`（本轮 HEAD），C-2 的遮挡守卫
首次生效。同页、同窗口（8 秒）复测：

```
sched: ticks=12 avg_gap=119 ms max_gap=129 ms (timer period 20 ms)
cache: misses=24 evictions=0        ← 20 秒窗口零增长
```

| 指标 | 改动前 | 改动后 | 判定 |
|------|--------|--------|------|
| `avg_gap` | 117 ms | **119 ms** | 无变化 |
| `max_gap` | 119 ms | **129 ms** | 仍无尖刺（只高 10 ms） |
| `cache misses` | 30 秒零增长 | **20 秒零增长** | 仍不碰卡 |
| `cache evictions` | 30 秒零增长 | **0** | 仍不碰卡 |

**结论：C-2 守卫如预测没有收益。** 这本身是有价值的确认——它排除了「换序整图
invalidate」这条最后的分支，把成因 (b) 完全锁定在**精灵绘制面积**上。继续在
缓存或换序上找原因已经没有意义，必须做 C-4（`SPRITE_CACHE.md` §5.4 / §9.2）。

其余页面同期复测（同一镜像）：音乐 20 ms、桌面 21 ms、世界 21 ms、宠物 251 ms
（宠物从 477–746 降到 251，收益来自 C-1 而不是 C-2）。

## 5. 本轮没有做的事

- 没有为了「看起来顺」去延长过渡时间掩盖跳帧。
- 没有关闭装饰动画或降低画质来换帧率（`anim off` 只用于**测量**，是单变量实验
  手段，交付配置仍是动画 ON）。
- 没有改角色母版、没有缩放素材。
- 没有在未实测的情况下声称 RPG 已经流畅。
- **没有修成因 (b)**：本轮只把它定位到「精灵重绘面积」，没有改 `lw_sprite.c`
  的画布尺寸。修复方案见 `SPRITE_CACHE.md` §5.4，属于下一轮。
- **已验证改动后的收益**：烧录后复测确认 C-2 无收益（119 ms）、C-1 有收益
  （宠物 251 ms）。RPG 页仍是 119 ms，正是「必须做 C-4」的证据。
