# 场景空间与遮挡引擎（P0）

分支 `agent/dsh-v4-3-engine`。纯 C，不依赖 LVGL，可在 Host 上完整测试
（`firmware/components/lw_engine/include/lw_stage.h` + `src/lw_stage.c`）。

## 1. 要解决的问题

角色与场景的坐标此前分散在 `lw_os.c`、`lw_duo_view.c`、`lw_rpg_view.c` 三处，
每处各自算"人站在哪"。同一只角色在桌面、双人、RPG 里用不同公式，换动作时
锚点变化导致脚底位置跳一像素、坐姿比站姿矮半格。遮挡靠 LVGL 的对象创建顺序
隐式决定，加一件家具就可能把角色盖住。

## 2. 坐标系（唯一一套）

```c
typedef struct { int32_t x, y; } lw_stage_point_t;      /* 世界坐标，像素 */

typedef struct {
    int32_t origin_x, origin_y;  /* 世界 (0,0) 在屏幕上的位置 */
    int32_t scale_256;           /* 场景缩放，256 = 1:1 */
} lw_stage_projection_t;

typedef struct {
    lw_stage_point_t ground;     /* 角色脚底接触点（世界） */
    int16_t anchor_x, anchor_y;  /* 精灵内脚底锚点（源图像素） */
    int16_t visible_h;           /* 可见高度，用于 UI 布局与命中盒 */
    int16_t native_h;            /* 源图高度 */
    int32_t scale_256;           /* 角色自身缩放，256 = 1:1 */
    int8_t facing;               /* +1 右 / -1 左 */
    int16_t offset_x, offset_y;  /* 视觉微调，不参与锚点计算 */
} lw_stage_actor_t;
```

`lw_stage_place_actor()` 的输出：

```c
typedef struct {
    int32_t screen_x, screen_y;  /* 精灵左上角（屏幕） */
    int32_t baseline_y;          /* 脚底在屏幕上的 y */
    int32_t scale_256;           /* 最终缩放（场景 × 角色） */
    bool mirrored;
} lw_stage_placement_t;
```

**关键公式**（这里是修过的缺陷）：

```c
int64_t scaled_anchor_x = ((int64_t)actor->anchor_x * scale + 128) / 256;
int64_t scaled_anchor_y = ((int64_t)actor->anchor_y * scale + 128) / 256;
left = (int64_t)ground_x - scaled_anchor_x + actor->offset_x;
top  = (int64_t)ground_y - scaled_anchor_y + actor->offset_y;
```

旧实现用的是"差值法"（先算 `anchor × (scale-256)/256` 再从 `ground` 里减），
在非整数缩放时会**截断而不是四舍五入**，脚底差半像素——表现就是换动作时人
抖一像素。现在先乘后除并加 `+128` 做四舍五入，`test_stage_actor_anchor`
用 209×128/256 → 105 的期望值锁住这个行为。

`test_stage_two_poses_match()` 断言：同一个 `ground` 点、两个不同
`anchor_y` 的姿势（站/坐）算出的 `baseline_y` 完全相等——这就是"动作切换
不跳不缩"的可验证形式。

## 3. 图层（数值即 z 序）

```c
typedef enum {
    LW_STAGE_LAYER_BACKGROUND = 0,
    LW_STAGE_LAYER_FAR_FURNITURE,
    LW_STAGE_LAYER_FLOOR,
    LW_STAGE_LAYER_CONTACT_SHADOW,
    LW_STAGE_LAYER_ACTOR,
    LW_STAGE_LAYER_NEAR_FURNITURE,   /* 沙发扶手、狗窝内沿 */
    LW_STAGE_LAYER_FOREGROUND,       /* 前景桌角 */
    LW_STAGE_LAYER_UI,
    LW_STAGE_LAYER_COUNT
} lw_stage_layer_t;
```

`lw_stage_behind(a, b)` 用 `(layer, baseline_y)` 排序：同层按脚底 y 决定前后，
这正是"坐在沙发后面/前面"的判据。

## 4. 遮挡

```c
typedef struct {
    int32_t x, y, w, h;      /* 屏幕矩形 */
    int32_t baseline_y;      /* 家具的接触线 */
    lw_stage_layer_t layer;
} lw_stage_occluder_t;

typedef struct {
    bool occluded;           /* 角色被这件家具挡住 */
    bool overlaps;           /* 矩形相交（用于命中盒） */
    int32_t top, bottom;     /* 实际可见的上下边界 */
} lw_stage_occlusion_t;

lw_stage_occlusion_t lw_stage_occlude(const lw_stage_occlusion_t *actor_box,
                                      const lw_stage_occluder_t *furniture);
```

规则：家具在 `NEAR_FURNITURE` 层且 `baseline_y` 大于角色脚底时，遮挡角色下半身，
可见 `bottom = min(actor_bottom, furniture.y)`；否则不遮挡。测试覆盖
"坐沙发遮腿"（`bottom = 155+212`，不是 420）与"狗窝内沿遮狗"。

## 5. 狗窝四态

```c
typedef enum {
    LW_STAGE_HOUSE_EMPTY = 0,   /* 空窝 */
    LW_STAGE_HOUSE_SLEEPING,    /* 狗在窝里睡觉 */
    LW_STAGE_HOUSE_LOOKING,     /* 抬头看 */
    LW_STAGE_HOUSE_STIRRING,    /* 轻微活动 */
    LW_STAGE_HOUSE_STATE_COUNT
} lw_stage_house_state_t;

lw_stage_rect_t lw_stage_house_rect(void);
bool lw_stage_house_shows_pet(lw_stage_house_state_t state);
lw_stage_rect_t lw_stage_house_opening(void);   /* 内沿开口，用于裁剪 */
```

`shows_pet()` 只有 `EMPTY` 返回 false。四态与组合动画的映射交给 Codex 的美术包
（`dogbed.empty / dogbed.sleep / dogbed.look`），引擎只提供几何与开关。

## 6. 测试

`tests/host/test_lw_engine.c` 的 stage 部分：

| 测试 | 覆盖 |
| --- | --- |
| `test_stage_projection` | 世界↔屏幕双向换算、缩放、负坐标、int32 溢出拒绝（世界 x = 2,000,000,000） |
| `test_stage_actor_anchor` | 锚点四舍五入、facing 镜像、offset 叠加、`baseline_y` 与 ground 一致 |
| `test_stage_two_poses_match` | 两个姿势同 baseline（动作切换不跳） |
| `test_stage_occlusion` | 前后判定、可见上下边界、非遮挡层不受影响 |
| `test_stage_dog_house` | 四态 `shows_pet`、开口矩形、内沿裁剪 |

## 7. 未做

- **没有**把 `lw_os.c` 现有的坐标计算替换成 `lw_stage`：那会改动 Codex 正在
  迭代的 UI 布局，属于跨人冲突。本轮提供的是**契约 + 测试**，接线由后续
  单独一轮做（接口见 `docs/handoff/CODEX_V4_3_ENGINE_API.md`）。
- RPG 的家具前后遮挡目前仍由 `lw_rpg_view.c` 的对象顺序决定，未接 `lw_stage_occlude()`。
- 未在硬件上验证：真实面板上的 1 像素接缝、遮挡边缘的撕裂。
