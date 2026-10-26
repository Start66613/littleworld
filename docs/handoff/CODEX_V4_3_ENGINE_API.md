# Codex 交接：V4.3 引擎接口

给 Codex（视觉）用的接口说明。**你不需要读 C 源码**。所有坐标、尺寸、时间的
约定都在这里；每一节末尾标了对应的头文件，需要看细节时再看。

分支：`agent/dsh-v4-3-engine`（DSH 技术侧）。视觉分支：
`agent/visual-v4-meowa-motion` / `agent/visual-v4-2-polish`。

---

## 1. 资源接口一览

| 你要交付的东西 | 文件格式 | 放在哪 | 谁读它 |
| --- | --- | --- | --- |
| 角色动画（站/走/坐/睡…） | `.lwp` | TF 卡 `littleworld/` | `lw_pack` → `lw_assets` |
| 场景整图 + 缩略图 | `.lwr` | TF 卡 `littleworld/` | `lw_visual_assets` |
| UI 组件（面板/按钮/图标） | `.lwu` | TF 卡 `littleworld/ui/` | `lw_ui_asset_view` |
| 剧情/相册/信件 | `.lwcx` | TF 卡 `littleworld/content.lwcx` | `lw_content` |
| Studio 工程包 | `.lwpack` | 不给设备，走 PC 转换 | `tools/studio_to_device.py` |

**命名规则**（设备按名字找，改名等于删除）：

- 角色帧包：`<角色名>.lwp`，例如 `male.lwp`、`qianqian.lwp`；
- 场景：`scene-<theme>.lwr`，缩略图 `scene-<theme>-thumb.lwr`，
  theme = 0 清晨/白天、1 夜晚、2 黄昏；
- UI 组件：`ui/<任意名>.lwu`，名字里带 `icon-` / `panel-` / `button-` 前缀
  决定种类（打包脚本据此校验）。

---

## 2. UI 素材导出（`.lwu`）

### 2.1 怎么生成

```powershell
python tools\build_ui_assets.py --source <你的PNG目录> --out dist\littleworld-v4-sd\littleworld\ui
```

脚本按**文件名**判断种类，按**同名 `.json`** 读九宫格参数：

```json
{ "inset_left": 6, "inset_top": 6, "inset_right": 6, "inset_bottom": 6,
  "pixel_art": true }
```

### 2.2 文件命名

| 前缀 | 种类 | 状态数 | 说明 |
| --- | --- | --- | --- |
| `icon-` | 图标 | 1 | 不允许九宫格（图标就是要拉伸） |
| `panel-` | 面板 | 1 | 必须给 insets |
| `button-` | 按钮 | 3（竖排） | 一张图从上到下 普通/按下/禁用，高度必须能被 3 整除 |

例：`button-primary.png`（32×72，三段各 24 行）+ `button-primary.json`。

### 2.3 会被拒绝的情况（都是真实的素材错误）

- 边框宽度 ≥ 图片宽度/高度的一半；
- 按钮高度不能被 3 整除；
- 任一边 > 1024 px；
- 文件名没有上述前缀；
- 图标带了 insets；
- 整张图全透明；
- 面板/按钮没有 sidecar `.json`。

### 2.4 设备端怎么用

```c
/* 启动时（一次） */
lw_ui_asset_add_file("panel.card", "ui/panel-card.lwu");
lw_ui_asset_add_file("button.primary", "ui/button-primary.lwu");

/* 建控件时 */
lv_obj_t *p = lw_ui_asset_panel(parent, "panel.card", x, y, 300, 180, LW_UI9_STATE_NORMAL);
lv_obj_t *b = lw_ui_asset_panel(parent, "button.primary", x, y, 120, 36, LW_UI9_STATE_PRESSED);
lv_obj_t *i = lw_ui_asset_icon(parent, "icon.heart", x, y, 24, 24);
```

**九宫格保证**：四角永远是原尺寸不缩放，边只沿一个轴拉伸，中心两轴拉伸。
这是 `lv_image_set_scale` 做不到的（它缩放整张图，角会变形、边框会变粗）。

**像素风**：`pixel_art: true` 时设备自动关掉抗锯齿（最近邻），不会糊。

**最小尺寸**：`lw_ui_asset_minimum_size("panel.card", &w, &h)`，
小于这个尺寸会返回 NULL（不画半个边框）。

**内存**：每个 `.lwu` 只加载一次，常驻。上限 16 个、单个 ≤ 1 MiB。
不需要释放；关机前 `lw_ui_asset_reset()` 统一回收（有控件还活着时会拒绝）。

---

## 3. 场景锚点与图层

### 3.1 角色锚点

每帧图的"脚底接触点"用像素坐标表示，存在 `.lwp` 的帧头里（已有字段，
不用改格式）。**导出的关键要求**：同一个角色的所有帧，脚底必须在同一条
水平线上，锚点 x 在角色的垂直中线上。

设备算位置：

```c
lw_stage_projection_t proj = { .origin_x = 0, .origin_y = 380, .scale_256 = 256 };
lw_stage_actor_t actor = {
    .ground = { .x = 512, .y = 0 },
    .anchor_x = 54, .anchor_y = 209,
    .visible_h = 212, .native_h = 212,
    .scale_256 = 256,
    .facing = LW_STAGE_FACE_RIGHT,
};
lw_stage_placement_t place;
lw_stage_place_actor(&proj, &actor, &place);
```

**保证**：换姿势时 `baseline_y` 不变（哪怕 `anchor_y` 不同、`visible_h` 不同）。
这条有测试锁住（`test_stage_two_poses_match`），所以**你只要把锚点标对，
不会出现"换动作人跳一下"**。

### 3.2 站立/坐姿的缩放

V4.3 规格里站姿 `scale_256 = 288`、坐姿 `282`。用 `visible_h` 表达更稳：
给 `visible_h` 你希望角色在屏幕上多高，引擎反算缩放，锚点自动贴地。

### 3.3 图层

```
0 背景        1 远景家具    2 地面        3 接触阴影
4 角色        5 中景遮挡    6 前景        7 UI
```

设备按这个顺序建容器，**不重排**。你要做的是告诉我：

- 哪些家具属于第 5 层（会挡住角色）：沙发扶手、狗窝内沿、桌子前沿；
- 它们的矩形与"接触线 y"（家具站在地上的那条线）。

有了这两个数，`lw_stage_occlude()` 就能算出"坐沙发时腿被挡住多少"。

### 3.4 狗窝四态

```
0 空窝    1 睡觉    2 抬头    3 轻微活动
```

导出成**竖排四段**的一张图（和按钮三态一样的做法），设备按段裁。
`lw_stage_house_shows_pet()` 只有"空窝"返回 false。
狗在窝里时按 `lw_stage_house_opening()` 给的矩形裁剪，只从门口露出。

---

## 4. 时间系统怎么调

### 4.1 你只管画，不要自己判断时间

```c
#include "lw_ui_clock.h"

lw_ui_clock_t clock;
if (lw_ui_clock_read(&clock)) {
    /* clock.hour, clock.minute      —— 有效时间 */
    /* clock.source                  —— DEMO/MANUAL/REAL */
    /* clock.trusted / stale / manual */
    /* clock.phase                   —— 五时段 */
    /* clock.theme                   —— 0 清晨/白天, 1 夜晚, 2 黄昏 */
    /* clock.until_next_s            —— 距下一个边界多少秒 */
    /* clock.needs_qualifier         —— 是否必须加后缀 */
}
```

`read()` 返回 false 表示**没有任何时钟**——这时显示 `--:--`，
**不要**自己画 00:00。

### 4.2 必须加后缀的情况

`clock.needs_qualifier == true` 时，时间后面必须跟：

| source | 后缀 |
| --- | --- |
| DEMO | ` · 演示` |
| MANUAL | ` · 手动` |
| REAL + stale | ` · 时间未校时` |

`REAL + trusted` 不加后缀。这不是排版偏好，是"不得把模拟时钟当真实时间"
的硬要求。

### 4.3 五时段 → 三张场景

| 时段 | 时间 | theme |
| --- | --- | --- |
| 清晨 | 05:00–07:59 | 0 清晨/白天 |
| 白天 | 08:00–16:59 | 0 |
| 黄昏 | 17:00–19:59 | 2 黄昏 |
| 夜晚 | 20:00–22:59 | 1 夜晚 |
| 深夜 | 23:00–04:59 | 1 |

所以**只需要三张全屏场景**，不是五张。切换有 3 秒迟滞（边界上不会来回闪），
但用户手动改时间会立刻生效。

---

## 5. 运行时资源预算

| 项 | 上限 | 说明 |
| --- | --- | --- |
| 角色帧租约 | 786,432 B | 4 帧 × 196,608 B |
| 文件缓存 | 786,432 B | 4 帧 |
| 场景整图 | 1,228,800 B | 1024×600 RGB565 |
| 场景缩略图 | 76,800 B × 3 | 256×150 |
| UI 素材 | 16 个 × ≤1 MiB | 实际用几个就几个 |
| 全屏帧缓冲 | 3 × 1,228,800 B | DPI 三缓冲，不可调（BSP） |

**给你的约束**：单个 `.lwu` 不超过 1 MiB；角色帧保持 256×256
（`LW_PACK_FRAME_BYTES` 写死）；场景必须 1024×600。

超了会怎样：`.lwu` 直接拒绝加载（日志一行），角色帧包解析失败，
场景读不出来——**不会**悄悄降级成模糊图。

---

## 6. 我这轮**没有**做的事（避免你误以为可用）

- 没有把 `lw_stage` 接进 `lw_os.c` 的现有布局——那是你的文件，我不动；
- 没有把 `.lwu` 用在任何真实页面上，只做了加载 API 与测试；
- 没有做昼夜主题的自动换图（`theme` 字段有了，接线没做）；
- 没有上板验证任何一条（本轮零硬件操作）。

需要我接哪一条，告诉我，我按上面的接口接。

---

## 7. 需要你回给我的东西

1. UI 组件 PNG + sidecar JSON（按第 2 节的命名）；
2. 家具的"第 5 层矩形 + 接触线 y"清单（第 3.3 节）；
3. 狗窝四态竖排图（第 3.4 节）；
4. 三张场景图（theme 0/1/2）的最终尺寸确认（必须 1024×600）；
5. 角色帧的锚点表（每帧 anchor_x/anchor_y，或者告诉我"全角色统一"）。

拿到 1 和 3，我就能把 `.lwu` 接进真实页面；拿到 2，遮挡就能生效。
