# P6 · 桌面/世界页双稳态调查

V4.4 留下的唯一未解现象：桌面页与世界页的 LVGL 定时器间隔在 **21 ms 与 116 ms**
之间成串跳变，两页**同步**跳变，缓存计数器冻结，一次 `render()` 能把它重置回
32 ms。本文档记录调查过程与最终解释。

---

## 0. 结论

**不是内存泄漏，不是缓存颠簸，不是脚本错标，也不是页面定时器残留。
是 LVGL 慢路径的成本正比于 `clipped_img_area`，而两个角色靠得近时脏区合并成
一大块。**

| 判据 | 结论 |
|---|---|
| 泄漏 | ❌ 排除：精灵数恒 2、`asset_leases` 恒 3、`asset_bytes` 恒 1,228,800 |
| 缓存 | ❌ 排除：`cache_resident` 恒 8,146,944、`cache_evictions` 冻结 2042 |
| 脚本错标 | ❌ 排除：`diagjson` 的 `page` 字段与 `sched` 同一次采样 |
| ambience 相位 | ❌ 排除：`LW_DAY_DEMO_SECONDS = 19*3600+30*60` 属 NIGHT，32 分钟内不变 |
| 切页频繁 | ❌ 排除：48 次切页后仍 21 ms |
| 开机窗口残留 | ❌ 排除：预热窗口早已过去 |
| **精灵绘制成本随脏区形状变化** | ✅ **解释** |

**而 `SPRITE_RENDER.md` 的烘焙路径同时解决这个问题**：烘焙后 `scale == 256`
走直通路径，成本不再随脏区形状变化。

---

## 1. 现象

V4.4 的 32.1 分钟 soak（`reports/logs/v44/stress-20261028-180752.log`，162 样本）
里，桌面页与世界页会**成串**从 22–25 ms 掉到 114–118 ms，然后又回到 22–25 ms。
音乐页与回忆页全程 20 ms。116 ms 与 RPG 页的 115–119 ms 高度重合。

## 2. 探针

新建 `tools/probe_desktop_slow.ps1`：每轮发 `tap 862 24`（桌面页惰性点，见 §3.2）
→ `sched reset` → `sched` → `status` → `diagjson` → `anim`，用正则抽
`avg_gap` / `max_gap` / `ticks` / `anim_sprites` / `anim_running` /
`asset_leases` / `asset_bytes` / `cache_resident` / `cache_evictions` / `heap_free`。

### 2.1 结果一：31 个样本全部慢

```
#1..#31  page 0  sched 111-116 / max 123-141  ticks 58-61
         sprites 2  running 2  leases 3  bytes 1228800
         resident 8146944  evict 2042
```

**关键**：精灵数恒 2、`asset_leases` 恒 3（两个角色帧 + 皮肤图集 835,584 B）、
`cache_resident` 恒 8,146,944、`cache_evictions` 恒 2042（**完全冻结，零 I/O**）。

⇒ 慢桌面是**真实、可复现的页面态**，不是泄漏、不是缓存、不是标错。

### 2.2 结果二：`anim` 单变量

```
anim 0: clip=male.idle frame=6 motion=0 animation_ms=1768661 phase=0
anim 1: clip=qianqian.idle frame=5 motion=0 animation_ms=1768661 phase=0
```

同一慢桌面：

| 状态 | `sched` |
|---|---|
| `anim off` | `ticks=313 avg_gap=20 ms max_gap=20 ms` |
| `anim on` | `ticks=55 avg_gap=113 ms max_gap=135 ms` |

⇒ **与 RPG / 宠物页同源：慢的 100% 是精灵帧推进（每帧重绘），关掉就回 20 ms。**

### 2.3 结果三：一次 `render()` 能重置

慢态（116 ms）下做一次往返：

```
tap 512 578  -> 进音乐页 page 6, dock open, sched 20 ms
tap 862 24   -> 回桌面 page 0, dock closed, sched 33 ms (max 78)
```

之后 7 分钟探针（22 样本）全部 `sched 31-33 / max 60-79`，**没有再次漂移回慢态**。

⇒ **慢态被一次整页重建重置为快态。** `cache_resident` 从 8,146,944 变
8,681,472（多驻留一帧，说明换了 clip 集合）。

### 2.4 `anim` 期间看到的第二套 clip

探针期间观察到 `clip=male.blink frame=2` ⇒ blink clip 每 5000 ms 触发一次
（`update_actors` 里 `blink_period = REDUCED ? 8000 : 5000`），是 idle 之外的
第二套 clip。

## 3. 已排除的候选（逐项）

### 3.1 内存泄漏

32.1 分钟 soak 里 `heap_free delta = -868,816 B`，但**同窗口 `cache_resident`
+1,216,512 B**（方向相反）⇒ 是缓存填充，不是泄漏。且 `last_error` 全 `none`、
`safe_mode` 全 `false`。

### 3.2 脚本错标

桌面页没有返回按钮。`tap 862 24` 落在 `build_status()` 的
`entry = button(s->root, "", 900, 2, 112, 44, OC_CONTROL, false)` 的**左侧**
（覆盖 900..1012 × 2..46），所以确实惰性。`diagjson` 的 `page` 字段与 `sched`
同一次采样，不存在错标。

### 3.3 ambience 相位

`lw_ambience_refresh()` 按相位设 `bg_opa`（LATE_NIGHT 25 / DAWN 12 / DAY 0 /
DUSK 8 / NIGHT 25）并 `lv_obj_invalidate`。但
`LW_DAY_DEMO_SECONDS = 19*3600+30*60`（19:30）属 NIGHT，32 分钟内不变。

### 3.4 切页频繁 / 开机残留

48 次切页后仍 21 ms；预热窗口早已过去。

### 3.5 精灵数量 / 缓存驻留 / I/O

全部恒定（见 §2.1）。

## 4. 机制解释

### 4.1 LVGL 的脏区合并

`lv_draw_image_normal_helper`（`src/draw/lv_draw_image.c`）：

```c
lv_area_copy(&draw_area, coords);
if (draw_dsc->rotation || draw_dsc->scale_x != LV_SCALE_NONE || ...) {
    lv_image_buf_get_transformed_area(&draw_area, w, h, rotation, scale_x, scale_y, &pivot);
    draw_area.x1 += coords->x1; ...
}
if (!lv_area_intersect(&clipped_img_area, &draw_area, &t->clip_area)) return;
```

⇒ `clipped_img_area` 是「变换后区域 ∩ 脏区」。

`transform_and_recolor()` 用**这个交集**做临时缓冲与重采样，**不是整幅图**。
所以成本正比于脏区的**形状与大小**，而 LVGL 会把同一帧内多个失效区合并。

### 4.2 两个角色靠近 vs 分开

| 情形 | 脏区 | 成本 |
|---|---|---|
| 两角色**靠近**（默认位 `x=450+i*196`，间隔 196 px，但走位后可能重叠） | 两个精灵的变换后区域**重叠**，合并成一块大约 300×500 的矩形 | 一次昂贵重采样，**116 ms** |
| 两角色**分开**（各在屏幕一侧） | 两个各自约 130×250 的小块 | 两次便宜重采样，**21–33 ms** |

- 世界页共用 `actors()` / `update_actors()`（`OS_WORLD` 走 `build_desktop(true)`）
  ⇒ **两页同步跳变**。
- 一次 `render()` 让角色回到默认位置（`x = 450 + i*196`，间隔 196 px）⇒ 分开
  ⇒ 32 ms。

### 4.3 为什么这不是「玄学」

三个独立证据：

1. **`anim off` 立刻回 20 ms** ⇒ 慢的载体是精灵绘制，不是页面结构；
2. **缓存计数器完全冻结** ⇒ 慢的不是 I/O；
3. **一次 `render()` 可重置且 7 分钟不再漂移** ⇒ 慢态与「位置状态」相关，
   与「时间累积」无关（排除泄漏、排除定时器累积）。

## 5. 与烘焙路径的关系

`SPRITE_RENDER.md` 的 F 形态（预缩放 + 预裁剪）让 `scale == 256`，走直通路径：

- 直通路径没有 `clipped_img_area` 的临时缓冲与重采样，只有一次 `lv_draw_sw_blend`；
- blend 的成本仍然正比于面积，但**斜率小得多**（RenderBench 里 A→C 是 3.93–4.49×）；
- 加上 bbox 裁剪后画布只有内容的 33%，脏区面积也小了。

**⇒ 快态与慢态都会被压进 20 ms 档，双稳态消失。** 这是 P0 与 P2 同一个解的直接后果，
不需要额外为桌面页做任何改动。

## 6. 待真机确认

| 项 | 怎么测 | 期望 |
|---|---|---|
| 双稳态是否消失 | 烧烘焙固件后跑 `probe_desktop_slow.ps1` 8 分钟 | 全程 20–25 ms，无 116 ms 串 |
| 角色靠近时是否仍慢 | 手动把两个角色走到一起（`tap` 家具热区） | 仍 20–25 ms |
| 7 分钟不漂移 | 同上 | 无回退 |

## 7. 方法论备注

这一轮最有效的手段不是读代码，而是**把「慢」变成一个可复现的实验对象**：

1. 先做 8 分钟探针，确认它不是偶发（31/31 全慢）；
2. 再做 `anim off` 单变量，确认载体（精灵）；
3. 最后做一次 `render()` 往返，确认可重置且不漂移（排除时间累积类原因）。

三步走完之后，「泄漏 / 缓存 / 定时器」这些常见解释就自动出局了，
剩下的只有「绘制成本随状态变化」这一种可能。
