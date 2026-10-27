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

## 4. 定位到 (b) 的下一步

同一块板、同一个固件，`sched` 逐页实测：

| 页面 | avg_gap |
| --- | --- |
| 音乐 | 21 ms |
| 桌面双角色 | 30–37 ms |
| RPG | 120 ms |
| 世界 | 139–151 ms |
| 宠物 | **777 ms** |

音乐页 21 ms 证明显示链路和 LVGL 任务本身没问题，所以不是「硬件不行」。
RPG/世界/宠物这三页有每帧固定开销。RPG 页已读到的可疑点：

- `lw_rpg_view.c:103-104` 每 20 ms 无条件 `lv_obj_move_to_index(v->actor[i],-1)`
  两次，不论前后遮挡关系是否真的变了。
- 精灵控件是 288×288 画布，而实际像素只有男 108×212、千千 104×202，
  重绘面积大 3.6 倍。
- 房间背景是 1024×600 的整张 RAM 图（`lw_visual_assets.c` 的 `aux_image()`），
  角色移动区域的背景每次都要重新拷一遍。

这三项都要按任务书要求做单变量对照，不能一次全改再猜哪个有效。宠物页 777 ms
的量级不属于绘制，单独排查同步 I/O 与整页重排。

## 5. 本轮没有做的事

- 没有为了「看起来顺」去延长过渡时间掩盖跳帧。
- 没有关闭装饰动画或降低画质来换帧率。
- 没有改角色母版、没有缩放素材。
- 没有在未实测的情况下声称 RPG 已经流畅。
