# P4 · 开机动画优化

真机 `boot` 读数是 **5 帧 / 2047 ms（409 ms/帧）**，`boot phases` 里
`ui = 1542 ms`（占总上电时间 **89.9%**）。本文档把这两块拆开，说明哪部分本轮
已经改掉、哪部分需要真机视觉验收后才能改。

---

## 0. 结论

| 项 | 改前 | 本轮 |
|---|---|---|
| 开机期间同屏精灵数 | 4（桌面 2 + 开机 2） | **2**（遮罩下桌面不重绘） |
| 视觉 | — | **156/156 整页截图逐像素一致** |
| `ui = 1542 ms` 拆分 | 未拆 | **已定位为「整树同步构建」，机制 4 未做** |
| 字体是否成因 | 曾怀疑 | **排除**（静态位图、无运行时解析） |

---

## 1. 上电到首帧的分段计时（已落地）

### 1.1 插桩位置

`firmware/main/lw_boot.c` 的 `start_os()`，整段都在 LVGL 锁内、首帧之前：

```c
const int64_t t_start = esp_timer_get_time();
int64_t t_mark = t_start;
lw_hw_begin(want_audio);              const int64_t t_hw      = esp_timer_get_time();
...
lw_art_boot(characters_lwfa_start, ...); const int64_t t_art  = esp_timer_get_time();
(void)lw_content_boot();               const int64_t t_content = esp_timer_get_time();
...
lw_os_start(&s_system, art);           const int64_t t_ui      = esp_timer_get_time();
lw_content_boot_apply();               const int64_t t_apply  = esp_timer_get_time();
ESP_LOGI(TAG, "boot phases: hw=%lld art=%lld content=%lld ui=%lld apply=%lld total=%lld ms", ...);
```

### 1.2 真机读数（V4.4，`v4.3-baseline-1-g04ad8e5-dirty`）

```
boot phases: hw=31 art=141 content=2 ui=1542 apply=0 total=1717 ms
```

| 段 | ms | 占比 | 内容 |
|---|---|---|---|
| `hw` | 31 | 1.8% | codec / LEDC / SD 上电 |
| `art` | 141 | 8.2% | 挂卡 + 六个包头 + 读前缓存打开 |
| `content` | 2 | 0.1% | 用户内容文件 |
| **`ui`** | **1542** | **89.9%** | **整个首屏构建** |
| `apply` | 0 | 0% | 纪念日安装 |

### 1.3 `ui` 里到底是什么

`lw_os_start()`（`firmware/components/lw_ui/src/lw_os.c:925-942`）在**一次同步调用**里：

```c
lw_app_state_init();
s = lv_malloc_zeroed(sizeof(*s));
s->previous = lv_screen_active();
lw_duo_init(&s->duo, 0x10252026);  s->duo.autonomous = false;
s->root = lv_obj_create(NULL);  lv_obj_set_size(s->root, 1024, 600);
lv_screen_load(s->root);
render();                                  /* 整棵树 */
s->timer = lv_timer_create(tick, 20, s);
lw_os_boot();                              /* 遮罩 + 壁纸 + logo + 8 叶子 + 2 精灵 */
```

`render()`（`lw_os.c:412-436`）依次建：壁纸（1024×600 全屏 `lv_image`）→
ambience 层 → content 容器 → `build_desktop()`（纪念卡、信封、家具热区、`actors()`）
→ `build_status()`（含 `lv_os_skin_button`）→ `build_dock()`（512×75 皮肤面板 +
6 个 item，每个 item 9 个九宫格 piece）→ `refresh()` → `update_actors(0)`。

**⇒ `ui = 1542 ms` 是 `render()` + `lw_os_boot()` 之和。**

### 1.4 已排除的候选

| 候选 | 排除依据 |
|---|---|
| **字体** | `firmware/components/lw_ui/fonts/` 五个 `.c` 共 **3,718,114 B 编译进 flash**；`CONFIG_LV_USE_FONT_COMPRESSED=y`、`CONFIG_LV_USE_FONT_PLACEHOLDER=y`、`# CONFIG_LV_USE_FREETYPE is not set`、`# CONFIG_LV_USE_TINY_TTF is not set`、`# CONFIG_LV_USE_FONT_MANAGER is not set` ⇒ 静态位图字体，**无运行时字体解析** |
| 素材读取 | `art = 141 ms` 已单独计时，六包 + 缓存打开都在里面 |
| NVS | `apply = 0 ms` |
| 音频 | `hw = 31 ms` 含 codec |

---

## 2. 开机动画的构成与名义时长

`lw_os_boot()`（`lw_os.c:848-867`）：

```c
if (!s || lw_os_motion_get() == LW_MOTION_OFF) return;   /* 动画 OFF 时整段跳过 */
if (s->boot) lv_obj_delete(s->boot);
s->boot_ms = 0;
s->boot = lw_os_box(s->root, 0, 0, 1024, 600, palette()->bg, 0, 255);  /* 完全不透明 */
lw_os_wallpaper(s->boot, s->painted_wallpaper, 0, 0, 1024, 600);
lv_obj_t *light = lw_os_box(s->boot, 0, 0, 1024, 600, 0x282438, 0, 195);
lw_os_motion_dim(light, 195, 0, 1450);                    /* 压暗 195 -> 0，1450 ms */
/* logo 容器 1024x136 @ (0,84)：心形图标 + "Little World" + 副标题 */
/* 8 片叶子：46+i*130, 180+(i%3)*90，各带 lw_os_motion_vector(-22,-72,...) */
/* 最多 2 个 boot 精灵：lw_sprite_create(s->boot, x, y, 288) + 外部时钟 */
/* "轻触跳过" 标签 */
lv_anim_set_delay(&a, LW_MOTION_FULL ? 1740 : 80);
lv_anim_set_duration(&a, LW_MOTION_FULL ? 260 : 90);
```

⇒ **FULL 模式名义总时长 = 1740 + 260 = 2000 ms。**

真机实测 `elapsed = 2047 ms`，与名义值吻合 —— **动画时长本身没问题，问题是帧数**。

---

## 3. 本轮改动：遮罩完全不透明期间不重绘桌面

### 3.1 为什么这是纯浪费

遮罩是 `lw_os_box(s->root, 0, 0, 1024, 600, palette()->bg, 0, 255)` ——
**alpha 255 的全屏盒子**。桌面两个 288-scale 精灵在它下面，用户一个像素也看不见，
但 `tick()` 每 20 ms 仍在跑 `update_actors(dt)`，每帧付两份定点重采样 + 二次 blend。

真机 5 帧 / 2047 ms ⇒ 每一帧都付了这两份看不见的成本。

### 3.2 实现

`update_actors()` → `update_actors_paint(uint32_t dt, bool paint)`：

```c
static void update_actors_paint(uint32_t dt, bool paint) {
    if (!s->art || !s->sprites[0] || !s->sprites[1]) return;
    if (!s->system->locked) lw_duo_tick(&s->duo, dt);       /* 状态机照常推进 */
    for (unsigned i = 0; i < 2; i++) {
        ... 推导 clip 名（纯计算，不碰 widget）...
        if (!paint) continue;                                /* ← 唯一的分支 */
        if (clip && clip != s->clips[i] && lw_sprite_play(...)) s->clips[i] = clip;
        lw_sprite_seek(...);  lw_sprite_set_anchor(...);
        lv_obj_set_pos(s->shadows[i], ...);  lv_obj_set_width(s->shadows[i], ...);
        lv_obj_set_pos(hit, ...);  lv_obj_set_size(hit, ...);
    }
    unsigned near = ...;
    if (!paint) return;
    if (s->front_actor != near) { lv_obj_move_to_index(...); s->front_actor = near; }
    if (s->foreground) lv_obj_move_foreground(s->foreground);
}
static void update_actors(uint32_t dt) { update_actors_paint(dt, true); }
```

`tick()` 里的调用点：

```c
update_actors_paint(dt, s->boot == NULL || s->boot_visible_ms != 0);
```

### 3.3 门控条件为什么是这两个

- `s->boot == NULL`：没有遮罩，当然要画。
- `s->boot_visible_ms != 0`：`boot_fade_start()` 记下这一刻，说明淡出已经开始，
  桌面会透过半透明遮罩显露出来 ⇒ 必须恢复绘制。

**两次失败教训（关键）**：

| 版本 | 门控 | 截图差异 | 原因 |
|---|---|---|---|
| 1 | 整段 `else` 跳过 `update_actors` | **35/156** | 连 `lw_duo_tick()` 也跳过，duo 状态机停摆（`v43-home-dusk` diff 19,873） |
| 2 | `s->boot == NULL` | **2/156** | 淡出期间桌面已透出，精灵停在旧帧，遮罩消失时可见地「跳一下」（`v4-boot-10` diff 9,237 / worst 255） |
| **3** | **`s->boot == NULL \|\| s->boot_visible_ms != 0`** | **0/156** | ✅ |

第 1 版是最容易犯的错：**「跳过整段」看起来等价，实际上把仿真也停了**。
第 2 版说明「不可见」不是二值状态——淡出期间是部分可见的。

### 3.4 验证

```
compared 156 files: 156 identical, 0 differ, 0 missing on the right
```

（`build\v45-assets\shot-bootskip3` vs `shot-head`，`tools/shot_diff.py`）

宿主测试矩阵全绿（142 / 82+336+33 / 261 / 752-1 既有）。

---

## 4. 遮罩统计：`boot` 命令

`firmware/components/lw_ui/include/lw_os.h`：

```c
typedef struct {
    uint32_t frames;      /* 遮罩存在期间跑过的 OS tick 数 */
    uint32_t elapsed_ms;  /* lw_os_boot() 到遮罩删除（或到现在）的墙钟 */
    uint32_t visible_ms;  /* 到淡出开始为止的完全可见时长 */
    bool active, skipped;
} lw_os_boot_stats_t;
void lw_os_boot_stats(lw_os_boot_stats_t *out);
```

`lw_capture.c` 的 `boot` 命令输出：

```
boot: frames=5 elapsed=2047 ms visible=1877 ms fps=2 active=0 skipped=0
```

**怎么读这个数**：`frames / elapsed` 就是遮罩的实际动画速率。名义动画
2000 ms、tick 周期 20 ms ⇒ **应有 100 帧**。实测 5 帧 ⇒ **观感由调度决定，
不是由动画设计决定**。`visible = 1877 ms` 说明淡出从 1877 ms 才开始，
与名义 1740 ms 差 137 ms，是 LVGL 定时器被拖长的直接证据。

---

## 5. 未做项：`ui = 1542 ms` 的拆分

### 5.1 为什么本轮不做

拆分改变**首帧的可见顺序**（先出壳、后补内容），这需要真机视觉验收，
而板子当前未连接（`Get-PnpDevice -Class Ports -PresentOnly` 只有两条蓝牙串口）。
在没有真机确认的情况下改首帧顺序，风险是「用户看到半成品界面」。

### 5.2 可做的拆分（留给下一轮，机制 4）

| 阶段 | 内容 | 何时可做 | 预期收益 |
|---|---|---|---|
| **A（必须同步）** | 壁纸 + 遮罩 + logo | 首帧必须可出 | — |
| **B（延后一帧）** | dock + 6 个 item + 状态栏 | 用户已在看开机动画，看不见 dock | 估 300–600 ms |
| **C（延后到遮罩淡出后）** | 桌面家具热区 + 角色 hit box | 完全不可见 | 估 200–400 ms |

依据：`build_dock()` 建 512×75 皮肤面板 + 6 个 item，每个 item 经
`lw_skin_button()` → `lw_skin_panel()` 建 **3×3 = 9 个 `lv_image`** piece，
共 **54 个 `lv_image` 对象 + 54 次 `set_scale`**，全部在遮罩下面，一帧也看不见。

### 5.3 与 Brookesia 的关系

esp-brookesia 的分阶段 init 是这一机制的参照（`OPEN_SOURCE_RESEARCH.md` §5）。
它把 init 拆成多个 stage，每阶段之间让出 CPU 让首帧先出去。我们的 `ui` 段
目前是「一次性同步」，改成阶段化即可，不需要引入它的框架。

---

## 6. 真机待确认项

| 项 | 怎么测 | 预期 |
|---|---|---|
| 开机帧数 | 烧后 `boot` | 从 5 帧上升（遮罩期少画两个精灵） |
| `boot phases` 的 `ui` | 烧后启动日志 | 不变（本轮未动 `render()`） |
| 桌面首帧是否正确 | 肉眼看 + `shot` 命令 | 与烧前一致 |
| 动画 OFF 时开机是否仍跳过 | `anim off` 后重启 | 是（`lw_os_boot()` 的 `LW_MOTION_OFF` 早退未改） |
