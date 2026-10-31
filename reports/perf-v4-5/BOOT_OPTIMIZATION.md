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

---

## 7. 真正的绑定约束是内部 RAM，不是时间（2026-10-30 追加）

这一节记录的是一次**排错了三轮的定位**：前六轮都在找「谁慢了」，而板子
的崩溃来自「首帧之后内部 RAM 只剩几个 KB」。时序数据没问题，问题在内存。

### 7.1 现象

有卡启动（`art: card: 6/6 packs [card]`、`ui = 1728~2297 ms`）时，板子在
`lw_capture: synthetic pointer ready` 之后约 400 ms 崩溃，三种形态随镜像变化：

| 镜像 | 崩溃形态 | 位置 |
|---|---|---|
| 原始 | `Guru Meditation Error ... (Stack protection fault)` | `lock_acquire_generic`（`locks.c:138`） |
| 主任务栈 64 KB | `abort() was called at PC 0x4ff41519` | `xQueueCreateMutex` 失败（`locks.c:75-78`） |
| 加 `WATCHPOINT_END_OF_STACK` | `panic'ed (Breakpoint)`、`MTVAL 0x4ff825dc` | 对栈底的 store |

三者是同一个事实的不同表现：**内存分配失败**走在 `printf` 取 stdout 递归锁
的路径上。无卡（safe mode）启动从不复现，因为 `ui = 68 ms`、内部池充裕。

### 7.2 量化

`firmware/main/lw_boot.c` 新增 `boot_ram_report(phase)`，每次打印栈水位与
内部堆：

```c
ESP_LOGI(TAG, "boot ram after %s: stack %u B free, internal %u B free, largest %u B",
         phase, (unsigned)uxTaskGetStackHighWaterMark(NULL),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
```

真机读数（卡在场，控制台仍放在启动末尾的那版）：

| 阶段 | 栈空闲 | 内部 RAM 空闲 | 最大连续块 |
|---|---|---|---|
| `hw`（含 SD 挂载前） | 30,332 | 97,743 | 55,296 |
| `art`（SD 挂载 + 六包打开） | 28,984 | **44,223** | 30,720 |
| `content` | 28,984 | 44,223 | — |
| `ui`（`render()` + `lw_os_boot()`） | 28,984 | **6,179** | 5,888 |
| `apply` | 28,984 | 6,139 | — |

⇒ **SD 挂载 + 六包打开吃约 53 KB**（其中 `lw_sd: sdmmc staging buffer: 32768 B`
是单项最大），**首屏构建再吃约 38 KB**，内部池从 97 KB 压到 6 KB。
此时 `largest block` 只有 5,888 B，控制台 REPL 需要的 12 KB 任务栈
必然失败（实测 `W ... no stack for the console task; the REPL stays off`）。

**栈不是问题**：整段 2.1 s 的首屏构建只用了 3,784 B（32,768 − 28,984）。
崩溃时 SP 比栈下界低 24 B 而水位仍有 28,984 B ⇒ 是**帧指针被踩**，
不是栈耗尽。这条读数同时否掉了「把栈加大」和「把栈缩小」之外的两种误解。

### 7.3 修复一：控制台提到首屏之前

`firmware/main/app_main.c` 把控制台任务从启动末尾移到
`bsp_display_backlight_on()` 之后、`lw_boot_start()` 之前，并前置打印一行
内部堆实测值。真机读数变化：

| 时刻 | free | largest |
|---|---|---|
| 控制台之后、`lw_boot_start()` 之前 | 107,759 | 55,296 |
| `art` 之后（控制台已在跑） | 31,039 | 16,384 |
| `ui` 之后 | **23** | **0** |

板子由此**稳定**：`lw_capture: console ready on UART0`、`Type 'help' ...`
正常打印，`status` 可应答，20 秒观察无重启（`boots=0`），
`boot phases: hw=30 art=164 content=9 ui=1728 apply=1 total=1934 ms`。

### 7.4 修复二：主任务栈 32 KB → 16 KB（已上机验证）

`firmware/sdkconfig.os` 的 `CONFIG_ESP_MAIN_TASK_STACK_SIZE` 与
`CONFIG_MAIN_TASK_STACK_SIZE` 从 32768 改为 16384。依据是上面的水位：
实测只用 3,784 B，16 KB 仍有 4 倍余量；而主任务栈是静态占用的**内部 RAM**，
缩掉 16 KB 等于直接把 16 KB 还给内部堆。

镜像 `0x241140`（2,363,712 B）烧录 `Hash of data verified.`，真机读数：

| 时刻 | free | largest | 对比（32 KB 栈） |
|---|---|---|---|
| 控制台之后、`lw_boot_start()` 之前 | 125,295 | **73,728** | 107,759 / 55,296 |
| `hw` 之后 | 102,091 | 61,440 | 84,555 |
| `art` 之后 | 48,575 | 30,720 | 31,039 / 16,384 |
| `ui` 之后 | **6,347** | **6,144** | **23 / 0** |
| `boot phases` | `hw=31 art=145 content=11 ui=1615 apply=1 total=1804 ms` | | `ui=1728 total=1934` |

⇒ 内部池在首屏后从 **23 B 升到 6,347 B**，栈水位从 28,984 → 12,520 B（仍剩一半）。
顺带 `ui` 从 1728 降到 1615 ms（少了 SD 与堆竞争的抖动）。

### 7.5 副作用与它的修复：RPG 页曾经打不开

内部 RAM 只剩 23 B 的那版，进 RPG 页失败：世界页（`page 1`）正常，
但 `tap 866 372` 之后 `status` 仍是 `page 1`，`rpg` 命令回 `rpg: page not open`。
这是**同一个内存问题的用户可见后果**，不是 RPG 页自身缺陷。

16 KB 栈版上 RPG 页恢复：`tap 866 372` → `page 9`，`rpg` 回
`rpg 0: tile=11,7 ... rpg 1: tile=10,7 ...`，走 12 次地图点击后
`rpg perf: frames=512 refresh=1466 ms avg=2 max=332 slow>=25ms=7`、
`seek=1151 ms`、`sched: ticks=466 avg_gap=23 ms max_gap=344 ms`。

### 7.6 已排除的候选

每一项都单独出过一次固件并烧录，全部仍崩：

| 候选 | 怎么排除 | 结果 |
|---|---|---|
| 宠物页探针 | 两次 `git stash` 掉 `lw_os.h`/`lw_app_pet.c`/`lw_capture.c` 改动 | 仍崩 |
| per-clip 子窗口几何 | `spec_bbox()` 临时恒返回整画布 | 仍崩 |
| 零拷贝借用 | `borrow_frame()` 临时恒 `return NULL` | 仍崩 |
| 主任务栈太小 | 提到 64 KB | 换成 `xQueueCreateMutex` 失败 |
| 未持锁 attach | `bsp_display_lock(0)` → `-1` | 是真实 bug，但不是崩溃主因 |
| 整段 clip 预取 | `if (0 && clip->count > 1)` | `ui` 1728→1405 ms，但内部 RAM 仍 23 B |

### 7.7 已验证（16 KB 栈版，per-clip 烘焙卡）

| 项 | 怎么测 | 结果 |
|---|---|---|
| 16 KB 主任务栈后内部池 | 启动日志 `boot ram after ui` | `internal 6,347 B / largest 6,144 B`（原 23 / 0） |
| RPG 页能否打开 | `tap 866 372` + `status` | `page 9`，`rpg` 有读数 |
| 板子是否稳定 | 连续启动 + 30 分钟轮询 | 无 panic、无 safe mode |
| 桌面页 | `sched reset` → `sched` | `ticks=304 avg_gap=20 max_gap=20` |
| 宠物页（per-clip 生效） | 同上 | `ticks=456 avg_gap=20 max_gap=20`（并集卡时代 53–65 ms） |
| `asset_bytes` 佐证 per-clip | `diagjson` | 1,102,815 B = skin 835,708 + male 97,512 + qianqian-base 169,002 |

## 8. 首屏不再黑屏：遮罩先刷出、页面在背后建

### 8.1 问题：`ui=1244 ms` 期间面板是黑的

旧的 `lw_os_start()` 是纯同步的：`render()` 先解一张 1024×600 壁纸（632 ms）再建整页控件（549 ms），
`lw_os_boot()` 的遮罩**排在它们后面**。所以从 `lv_screen_load(s->root)` 到第一帧上屏，用户面对的是
一块只有背景色的黑屏。`osperf` 把这 1.2 秒变成了可读的数字：

| 项 | 改前（`osperf` 首读，真机） |
|---|---|
| `renders=1 total=1201 ms` | `wall=632 page=549 status=3 dock=16 tail=1` |
| `splash ready` | **1215 ms** |
| `first frame on panel` | **1254 ms** |

真正让用户等的是「壁纸解码 + 页面构建」，而这两件事**都不需要在遮罩之前**完成。

### 8.2 做法：把 `lw_os_boot()` 拆成两半

| 函数 | 内容 | 何时调用 |
|---|---|---|
| `boot_backdrop()` | 只有底色调的 1024×600 盒 + 心形图标 + "Little World"（44 号）+ 副标题（22 号）+ "轻触跳过"。**不含任何动画**，所以建完就能显示 | `render()` **之前** |
| `boot_finish()` | 壁纸、195→0 的压暗、logo 的 `LW_MOVE_BOOT`、8 片叶子、两个 288 scale 精灵、淡出 | `render()` + 计时器之后 |

`lw_os_start()` 的新顺序：

```c
lv_obj_add_event_cb(s->root,gesture,LV_EVENT_GESTURE,NULL); lv_screen_load(s->root);
const bool splash=lw_os_motion_get()!=LW_MOTION_OFF && boot_backdrop();
if (splash) lv_refr_now(NULL);
s_frames.splash_ms=lv_tick_get()-started;   /* this is when the panel shows something */
render();
s->timer=lv_timer_create(tick,20,s);
if (splash) boot_finish();
s_frames.build_ms=lv_tick_get()-started;    /* the intro timeline starts here */
```

两个非显然的约束：

1. **遮罩必须挂在 `lv_layer_top()`，不能挂在 `s->root` 上**。`render()` 第一件事是 `lv_obj_clean(s->root)`，
   挂在页面上的遮罩会被它自己盖住的那次构建删掉，于是「先画遮罩」变成「先画又立刻删掉」。
2. **壁纸和压暗必须用身份定位插回 z 序，不能用固定下标**。第一次实现写的是
   `lv_obj_move_to_index(lv_obj_get_child(s->boot,1),1)`，当时的子对象顺序是 `[底色盒(0), logo(1), 提示(2)]`，
   壁纸建在最后，所以 "child 1" 其实是 logo，等于没挪，压暗层反而盖住了标题。宿主截图立刻暴露：
   有屋子、有角色、有花瓣，**没标题也没压暗**。改成按身份取最后一个子对象：

```c
const uint32_t count=lv_obj_get_child_count(s->boot);
lv_obj_t *wall=count ? lv_obj_get_child(s->boot,count-1) : NULL;
if (wall) lv_obj_move_to_index(wall,1);
```

### 8.3 真机结果（板子，`0x241f90` = 2,367,888 B）

```
osperf: renders=1 total=1201 ms (wall=632 page=549 status=3 dock=16 tail=1)
osperf: title card on panel=23 ms, build done=1237 ms, flush event=1297 ms
boot: frames=29 elapsed=3247 ms visible=2969 ms fps=8 active=0 skipped=0
```

| 指标 | 改前 | 改后 | 变化 |
|---|---|---|---|
| 看到第一帧的时间 | **1254 ms** | **23 ms** | **−98.2%** |
| 页面构建完成 | 1215 ms | 1237 ms | +22 ms（遮罩本身的开销） |
| 开机动效总时长 | 2047 ms | 3247 ms | +1200 ms（有意：动效现在从页面建完才开始） |
| 动效期间 fps | 11 | 8 | 动效窗口变长，帧数几乎不变 |

代价是诚实的：**总时长变长了 1.2 秒**，但用户不再对着黑屏等 1.2 秒，而是先看到标题卡。
这一条是「把等待变成内容」，不是「把等待缩短」，所以两种读数都留着。

### 8.4 逐像素证据：静帧不变，只有动画相位不同

截图矩阵（`LW_FAKE_TIME=19:30` 钉住 ambience 相位、未压缩包，`--os-v2-test`，752 checks）：

| 对比 | 结果 | 说明 |
|---|---|---|
| 改前 vs 改后（z-order 还错） | 146 identical / **10 differ** | `v4-boot-00..06` 全屏差 579k–581k px（worst 166），即「有没有标题与压暗」 |
| 改前 vs 改后（z-order 修好） | 147 identical / **9 differ** | 只有 `v4-boot-01..09`，bbox `[380,354..503,635]`、diff 6.2k–6.5k px、worst 66–148 |

第二个对比里剩下的差异全部落在**左侧那个 288 scale 开机精灵的矩形内**，是位移动画的相位差
（动效起点从 0 ms 变成 1237 ms）；壁纸、压暗、标题、叶子、右精灵逐像素一致。这一改动
**没有动任何静态画面**，只是把动效往后挪。

### 8.5 教训

- 「先显示、后构建」的前提是**遮罩不能挂在那棵会被清空的树上**。
- 任何「按固定下标调整 z 序」的代码都要在截图里看一眼，这种 bug 不崩、不报错、只是少了一层内容。
- 用 PowerShell 的 `Set-Content -NoNewline` 回写 `Get-Content` 的数组会把整个源文件压成一行
  （82,480 B / 1 行）。这次靠 `git fsck --unreachable` 找回被 `stash pop` 丢掉的提交
  `90828ce6` 才恢复。**改 C 文件只用编辑器、`edit` 工具或 Python 显式读写换行。**
