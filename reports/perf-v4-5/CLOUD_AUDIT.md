# CLOUD_AUDIT · 云端静态审计（agent/muse-cloud-audit）

> 口径声明（本文件全程适用）：
> - 本文件所有结论都是 **static**（静态推断：读代码 + LVGL 9.5 已知机制 + 本仓库
> 已落盘的真机报告交叉引用）。审计者在云端 Linux 沙箱里工作，**没有开发板、
> 没有串口、跑不了 Windows 宿主模拟器**，因此本文件不给出任何新的帧率数字。
> - 文中出现的毫秒数只有两类：① 引用 `FINAL_BENCHMARK.md` 等既有报告的
> **measured-by-user** 数据（逐处标注 § 编号）；② 代码里的名义时长
> （动画 duration、定时器周期，是常量不是测量）。
> - 每条都给了上板验证命令与判据；「预期量级」是 大/中/小 的静态分级，
> 不是收益承诺。收益要以板上 `frame:` 行（RENDER_START/RENDER_READY 计时，
> 见 FINAL §19.2）为准，**不要用 `sched max_gap` 当单帧耗时**（FINAL §14.3 /
> 本轮任务书的坑 #5）。
> - 基线（measured-by-user，任务书给定）：换页 42–77 ms、稳态 20–30 ms/帧、
> 控制中心首开一帧 256 ms 量级、RPG 走路 20–21 ms/帧、开机首屏 23 ms。

审计范围：`firmware/components/lw_ui/src/*.c`（32 个文件）全量 grep 七类反模式，
逐条人工复核命中上下文；`lw_core` 只跑测试不审业务（见 §8）。
基线提交：`fa1a25a`（`origin/perf/v4-5-render-engine`）。

已确认「不要重做」的 8 项（任务书清单）在本分支均在位：预缩放/裁剪烘焙
（`lw_assets.c` geometry、`lw_sprite.c:present`）、64 KiB PSRAM bounce +
64 槽缓存（`lw_storage.c` ENTRIES 64/QUEUE 24）、帧 deflate（`lw_pack.c`）、
夜间 `.night` 变体（`lw_app_pet.c:pick_clip`）、场景卡 LRU + 预热
（`lw_visual_assets.c` / `warm_pump`）、构建期 freeze（`lw_os.c`）、九宫格单对象
DRAW_MAIN（`lw_skin.c:art_pieces`）、对话框只位移（`lw_motion.c` LW_MOVE_DIALOG
只有 `move`，注释引 measured 765–810 ms → 38–41 ms）。以下只列**剩余**问题。

---

## 1. 模式一：给「有子对象的容器」设 opacity<255 或做 scale 动画

机制：LVGL 对带子对象的对象做 opacity/scale 动画时，把整棵子树渲进 layer
再混合回来（FINAL §19.3 的 measured 结论：对话框容器 scale+淡入 765–810 ms/帧，
只位移时同帧 38–41 ms）。这是本仓库最贵的一条规律，本轮仍有 5 处活口。

### A1每次换页，整个页面容器都在做 opacity 淡入
- 证据：`lw_os.c:540`
`refresh(); update_actors(0); lw_os_motion(s->content, … LW_MOVE_PAGE);`
`s->content` 是整页的父容器（`render()` 在它下面建全页控件）。
`lw_motion.c` 的 `LW_MOVE_PAGE`：`animate(o,move_x,36,0,260)` +
`animate(o,appear,195,255,180)`——`appear` 即 `lv_obj_set_style_opa`。
另外 `lw_os_motion()` 的 REDUCED 分支对**任何** kind 都先
`animate(o,appear,220,255,90)`，所以 REDUCED 模式同样中招。
- 影响：每一次换页（全部页面）的入场 180 ms 名义时长内，每一帧都付整页
layer 合成。对照：同文件 `LW_MOVE_RETURN` 只有 `move_x`、没有 `appear`，
说明「只滑入」在本代码库里已有先例且观感被接受过。
- 对照已修项：`LW_MOVE_DIALOG` 当年就是按同一机制去掉 scale/opacity 的。
- 验证（上板）：串口看 `frame: slow=… last=…` 行；反复
`tap 512 578` → 点不同页面，记录换页后 300 ms 内的 slow 帧数与 last 值；
再 `anim off` 复测一次作对照（测完立刻 `anim on`！FINAL §20.4：`anim off`
会存进 NVS）。判据：改后换页窗口内 slow(≥50 ms) 帧应显著减少，
且截图/肉眼页面内容不变、滑入仍在。

### A2相册每次翻页，对舞台容器做 scale + opacity 动画
- 证据：`lw_app_album.c:56`（`update_view()` 末尾）
`lw_os_motion(stage,LW_MOVE_PHOTO);`；`update_view()` 由
`next()/previous()/retry()` 每次翻页调用。`stage` 是 944×400 区域的容器，
子对象是照片 `lv_image` 与失败提示标签等。
`lw_motion.c` 的 `LW_MOVE_PHOTO`：先给对象设 pivot 到中心，再
`animate(o,scale,104,256,300)` + `animate(o,move,24,0,300)` +
`animate(o,appear,140,255,200)`——scale 与 opacity 双中。
- 影响：用户连续翻照片时，每翻一张就触发 300 ms 的整舞台 layer 变换；
这是「交互尖峰」型负载，正对应任务书说的「少数交互还有几百毫秒尖峰」。
- 验证：板上进相册，连翻 10 张，串口看 `frame:` 行；判据同 A1。
注意此项改法改变观感（照片入场效果），**本轮只审计、不动**，见
CLOUD_PATCHES 的「待用户挑」清单。

### A3LW_MOVE_PAPER：纸张/卡片容器的 slide+appear
- 证据：`lw_motion.c` `LW_MOVE_PAPER`：`slide(82→0,480ms)` +
`animate(o,appear,140,255,340)`。调用点：`lw_os.c:709`（信件 paper，
含两段正文标签）、`lw_os.c:714`（LETTER overlay 的面板 p）、
`lw_app_memories.c:268`（回忆详情 card，含书签图、日期/标题/正文/按钮）、
`lw_rpg_view.c:193`（RPG 对话面板 p）。
- 影响：打开信件/回忆详情/RPG 对话的入场帧。面积小于整页，但子对象多、
含整段文字，layer 合成后还要重排文字的风险在静态层面不能排除。
- 验证：打开信件与回忆详情各 10 次，看 `frame:` 行的最大 last。

### A4控制中心入场：子控件逐个 stagger（禁改区，只给证据）
- 证据：`lw_os.c:574`（`show_overlay()` 内，**属任务书禁改函数**）
`… lw_os_motion_slide(p,-542,0,320,0,true); lw_os_motion_dim(s->over,0,105,280);
for (i=1;i<child_count(p);i++) lw_os_motion_stagger(child(p,i),i%5);`
`lw_os.c:596` 下拉松手打开时同样对 `s->control_panel` 每个子对象 stagger。
stagger 是 `move`（translate_y），本身不触发 layer；但 FINAL §20.2 的
measured 结论是：慢帧起始时 `lv_anim_count_running()=17`，成本来自
「一批入场动画各自 invalidate 自己那块，LVGL 并脏区后每帧重画遮罩+面板」，
且 `anim off` 时无任何 ≥25 ms 帧。即 A4 的成本结构已被真机定位，
缺的是改法决策（§20.3 两条路都要产品判断）。
- 本轮处理：**不动**（禁改区 + 需观判断）。证据在此，改法列入
CLOUD_PATCHES 待挑清单第 1 位，单独提交、由用户决定合不合。

### A5回忆详情关闭：全屏 overlay 的 opacity 淡出
- 证据：`lw_app_memories.c:44`
`static void book_fade(void *o,int32_t alpha){lv_obj_set_style_opa(o,…);}`
`:53` 对 `overlay`（1024×600、挂着详情 card 子树）做 255→0、180 ms 动画，
完成回调才 `lv_obj_delete`。
- 影响：每次关回忆详情，180 ms 内全屏 layer 混合。与 A1 同机制、面积最大。
- 验证：反复开/关一条回忆详情，看 `frame:` 行。

### A6其余 appear/scale 命中（逐条定性，不一刀切）
- `LW_MOVE_TOAST`（`lw_os.c:726`）：toast 484×60、只有 1 个标签子对象，
appear 170 ms。面积小，量级小；保留。
- `LW_MOVE_LOCK`（`lw_os.c:651-652`）：作用在精灵对象（`lv_image` 叶子，
无子对象）与时间标签 t 上。叶子对象不产生子树 layer，量级小；保留。
- `LW_MOVE_BOOT` / `LW_MOVE_ACTOR`：精灵/leaf 为主；开机遮罩整屏 fade
（`lw_os.c:1175` 的 `fade` 255→0）在 BOOT_OPTIMIZATION §8 已作为
已知下限记录（遮罩每帧全屏半透明合成），属已决策的取舍，不重开。
- `LW_MOVE_CARD`（scale 236→256）：全仓 grep **无调用点**，是死 kind；
不是性能问题，仅记录，避免后人误用在容器上。
- `lw_os_motion_touch()` 的 `progress`（outline_opa）：作用于被点按的
卡片/按钮本体，outline 不走子树 layer 路径；量级小，保留。

---

## 2. 模式二：lv_timer 回调里无条件调 set_text / set_style / invalidate

机制：`lv_label_set_text[_fmt]` 每次都释放并重建文本缓冲、标脏并触发重排
（`lw_app_pet.c:51-57` 的注释把这条写得很清楚，且宠物页已按此守卫）。
本轮 grep `lv_timer_create` 共 16 处，逐个追进回调：

### B1首页 1 s 定时器无守卫（lw_app_home.c）
- `lw_app_home.c:178` 1 s 定时器 → `refresh()`（`:64-98`）每秒无条件：
`lv_label_set_text(s_mood_label, mood)`、`lv_label_set_text_fmt(s_days_label,"%d",days)`、
`lv_obj_align_to(s_days_unit,…)`、`lv_label_set_text[_fmt](s_next_label,…)`。
四个值里：days/next 以「天」为单位变化，mood 只随宠物状态变。
即稳态下每秒 3 次注定同值的标签重写 + 1 次对齐重排。
- 验证：上板停在首页 60 s，`diagjson` 前后对比 + 串口 `frame:` 行；
改后稳态应无因本项产生的周期性重绘（以 osperf/frame 计数为准）。

### B2回忆页 1 s 定时器无守卫（lw_app_memories.c）
- `lw_app_memories.c:492` 1 s 定时器（仅 tab 0）→ `refresh_today()`（`:218-240`）
与 B1 完全同构：days + next 两标签每秒重写，值按天变。

### B3音乐页播放中每秒全量 refresh（lw_app_music.c）
- `lw_app_music.c:250` 1 s 定时器，`:86` 已有 `if(!s_playing) return;`（好），
但播放时 `refresh()`（`:41-81`）每秒无条件重写：曲名、歌手、播放/暂停
标签、状态串、音量，以及对全部 `TRACK_COUNT` 行逐行
`lv_obj_set_style_bg_color` + `lv_obj_set_style_bg_opa` +
两个 `lv_obj_set_style_text_color`（模式七的同帧重复设置也在此）。
每秒真正变的只有进度条与时间标签；其余只在切歌/暂停/音量变化时变。
- 验证：播放中停留 60 s 看 `frame:`；切歌、暂停、改音量后肉眼核对
曲名/高亮行/状态串仍即时更新（这是本项的行为判据）。

### B4设置页 2 s 定时器：整屏对象树递归 + 标签重写（lw_app_settings.c）
- `lw_app_settings.c:158` 2 s 定时器 → `timer_cb`（`:76-87`）：
`lv_label_set_text_fmt(s_diag_label, "活动对象：%d\n…", count_objects(lv_screen_active()), …)`。
`count_objects()`（`:18-26`）每 2 s 递归走完**整屏对象树**只为数个数；
而格式化结果（对象数、互动次数、时钟模式、版本）在稳态下根本不变，
标签却每 2 s 被重写重排一次。本项含两层浪费：遍历 + 无条件 set_text_fmt。
- 验证：停在设置页 60 s；改后标签内容仍正确（对象数在打开/关闭 overlay
后应随之更新，这是判据），稳态不再每 2 s 重排该标签。

### B5旧壳顶栏时钟 1 s 定时器（lw_shell.c）
- `lw_shell.c:268` 1 s → `update_clock()`（`:36-63`）：
`lv_label_set_text(s_clock_label, "HH:MM")` 每秒重写一个**分钟级**的值；
日期标签每天才变，也每秒重写；`s_demo_pill` 每秒 add/remove flag。
注：OS 壳（lw_os.c）的时钟标签已守卫（见 §7 正面清单），这是旧壳的同名问题。
- 验证：旧壳/桌面构建下停留顶栏 60 s；时间跨分钟时标签正确跳变。

### B6OS 壳 refresh() 里滑杆无守卫（lw_os.c，非禁改函数）
- `lw_os.c` 的 `refresh()` 由 tick 每 1 s 调用一次（`:1065` 附近
`s->last_clock>=1000`）。其中标签部分已逐项守卫（同函数大量
`last_refresh_*` 静态缓存），唯独滑杆环（refresh 前段，遍历 2 层
controls）对每个存在的亮度/音量滑杆无条件
`lv_slider_set_value(…,LV_ANIM_OFF)` + `lv_obj_send_event(…,LV_EVENT_REFRESH,NULL)`；
REFRESH 事件会跑 `lw_skin.c` 的 `knob_follow`（重算并 `lv_obj_set_pos` 旋钮）。
值没变时这是纯重复劳动，且与同函数其它项的守卫风格不一致。
- 验证：控制中心打开停留 60 s，亮度/音量显示与旋钮位置不变；
拖动滑杆后数值标签仍即时更新。

### 已守卫、不重复改（正面清单）
- 宠物页 `lw_app_pet.c:60-112`：mood/三条 bar/睡眠按钮全部有 shown 缓存，
40 ms tick 下也成立——本轮 B1–B4 的改法就是照抄这一处既有范式。
- OS 壳 `refresh()` 的时钟/亮度值/Wi-Fi 文案/静音徽标：已守卫。
- 打砖块 `breakout_refresh()` 的 score/lives：已守卫（`lw_arcade.c:601-608`）。
- `lw_ambience_refresh()` 的整层 tint 失效：已被相位变化门控
（`lw_os.c` tick 内注释有 measured 说明）。

---

## 3. 模式三：refresh 路径里 lv_obj_create / lv_obj_delete

### C1相册翻页：lv_obj_clean(stage) + 全量重建（lw_app_album.c）
- `lw_app_album.c:39` `update_view()` 开头 `lv_obj_clean(stage)`，
随后重新 create 照片/图标/标签（`:46` 起）。每次翻页都走「删整棵子树 +
建整棵子树」，触发整页重排；内容本身（照片）确实每翻必换，但
承载结构（一个 `lv_image` + 标题标签）是可以复用的。
- 本轮处理：只审计。复用化改造会动宿主截图的对象结构预期，风险中，
列入待挑清单。

### C2贪吃蛇 refresh 中按需 create 段对象（lw_arcade.c）
- `lw_arcade.c:355` `snake_refresh()`：蛇变长时在 refresh 里
`lv_obj_create` 新段（有界：`ARCADE_SNAKE_MAX_LEN`，且只增不减、
多余段用 HIDDEN 收起——后半部分做法是对的）。
可在建场时一次预建满格段，消掉游戏中的对象创建尖峰；量级小。

### 其余 create/delete 命中定性
- 各游戏的建场/结果面板/重开（`lw_arcade.c` open_*/restart/result_panel）、
RPG 对话框（`lw_rpg_view.c:189-232`）都是**事件驱动**的一次性构建，
不是周期 refresh；`render()`/`show_overlay()` 的整页构建有 freeze 兜底
且已有 osperf 分段数据。不列为问题。

---

## 4. 模式四：定时器在页面不可见/暂停时仍在跑

结论：**这一类整体干净**，证据如下——
- 旧壳应用定时器全部 create/destroy 配对：`lw_shell.c:134-153` 切页时先
`s_current->destroy()` 再 `app->create()`；home/memories/music/settings/
pet 的 destroy 都 `lv_timer_delete`。页面不可见 ⇒ 定时器不存在。
- 精灵定时器是范式级实现：`lw_sprite.c` create 即 pause，play/resume/
pause/set_enabled/set_external_clock 全部成对处理，外部时钟精灵恒 pause
自有定时器（`:237、:248-262`）。
- 音乐定时器：页面存活期间 1 s 常跑，但回调首行 `if(!s_playing) return;`
——只剩唤醒成本（小）。可进一步在 play_toggle 时 pause/resume，
列入待挑清单（行为等价、收益小）。
- 宠物房间 40 ms 定时器在 overlay 打开时仍跑：`refresh()` 内精灵 seek
已被 `lw_os_overlay()==OS_OVER_NONE` 门控、标签/bar 已守卫，
剩余成本主要是 tick 本身（小）。
- OS 壳 20 ms 主 tick 与旧壳 1 s 时钟/10 s 状态定时器是全局常驻设计
（顶栏与存档节流的载体），不按本模式定性为问题。

---

## 5. 模式五：循环里 lv_malloc / 每帧新建临时缓冲

UI 层 grep `lv_malloc|malloc|heap_caps_malloc` 共 19 处，逐条定性：
- **每帧路径：无命中。** 精灵换帧走 `lw_sprite.c:present()` 的同 asset
refill + invalidate，注释明确记录了当年每帧 alloc/free 196 KB 缓冲的
教训；该路径现在零分配。
- `lw_skin.c:281-282` `art_baked()`（任务书点名禁改
函数）：缓存未命中时一次 `lv_malloc(972 KB 级 ARGB)` +
`lv_malloc(RGB565A8)` 现场烘焙。`SK_CC_PANEL` 已有离线 blob 覆盖，
但其它 9 宫格尺寸首次绘制仍可能落到这条运行时烘焙路径，形成首开尖峰。
只记录证据，不动；扩离线 blob 覆盖面是工具侧事项（见 §9）。
- `lw_ui_asset_view.c:119、:262` repack/load 时 malloc：加载期一次性，
注释说明 RGB565A8 平面重排不可避免。量级小。
- `lw_assets.c:184` lease 像素缓冲：acquire 时分配，present 的同 asset
路径不走它。正常。
- LVGL 内部 `transform_and_recolor` 的每帧 `lv_malloc(8192)` 不在本仓
代码里，它是模式一/精灵慢路径的下游成本，已由预缩放解决，不重复计。

---

## 6. 模式六：对父对象/全屏 lv_obj_invalidate(NULL) 大面积重绘

- 全仓 grep：**UI 代码里没有 `lv_obj_invalidate(NULL)`**，也没有对父容器的
周期性 invalidate。仅有命中：
- `firmware/main/lw_capture.c:556、:565`：`shot` 诊断命令主动失效整屏，
是截图工具的本意，非问题。
- `lw_os.c` `freeze_end()`：构建后对 active screen 失效一次——这正是
freeze 机制的设计（千次失效换一次），已 measured，不动（且属禁改函数）。
- `lw_skin.c` `art_show()`：状态切换只 invalidate 面板对象本身，正确。
- `lw_sprite.c:present()`：只 invalidate 精灵 image 本身，正确。
- 结论：本模式无剩余问题。

---

## 7. 模式七：同一帧里对同一对象重复设置同一个值

- G1音乐页 rows：见 B3（每秒对每行 4 次 style 写，值只随 active 行变）。
- G2贪吃蛇每步全段写色（lw_arcade.c snake_refresh）：
每步对每个段 `lv_obj_set_style_bg_color(seg, snake_color(i, i==0))`。
`snake_color` 只依赖**下标 i**（奇偶 + 是否头），而段对象与下标是
固定绑定（`s_snake.segments[i]`），所以每个对象收到的颜色值
**永远是同一个**——这是纯同值重复写。位置 `set_pos` 每步真变（身体平移），
不在此列。另 score/extra 两标签每步 `set_text_fmt`，只在吃食时变。
- G32048 `game_2048_refresh()`（`lw_arcade.c:111-126`）：每次滑动
对 16 格全量写 bg_color/text/font/text_color。事件驱动（非定时器）、
16 格规模小，量级小；仅记录。
- G4打砖块 `breakout_refresh()`：每步对 60 块砖 add/remove flag。
LVGL 的 flag setter 对同值是否早退属库实现细节，静态不能断言零成本；
paddle/ball 的 set_pos 是真变化。量级小，仅记录。
- G5：OS 壳滑杆 REFRESH，见 B6。

---

## 8. 纯 C 核心测试证据（本机实跑，非推断）

沙箱内 gcc 13.3 实跑（命令与任务书一致，另补 Duo/RPG 两套最小依赖编译）：

```sh
gcc -std=c11 -O2 -Ifirmware/components/lw_core/include \
tests/host/test_lw_core.c \
firmware/components/lw_core/src/lw_pet.c \
firmware/components/lw_core/src/lw_state.c \
firmware/components/lw_core/src/lw_memories.c \
firmware/components/lw_core/src/lw_time.c \
-o /tmp/core && /tmp/core
# -> 142 checks, 0 failure(s) / HOST TESTS PASSED

gcc -std=c11 -O2 -Ifirmware/components/lw_duo/include \
-Ifirmware/components/lw_core/include \
tests/host/test_lw_duo.c firmware/components/lw_duo/src/lw_duo.c -o /tmp/duo -lm
# -> Duo model: 82 checks, 0 failures

gcc -std=c11 -O2 -Irpg/include -Irpg/data tests/host/test_rpg.c \
rpg/src/lw_rpg.c rpg/src/lw_rpg_save.c rpg/data/lw_rpg_home.c -o /tmp/rpg -lm
# -> 336 checks, 0 failure(s) / RPG CORE TESTS PASSED
```

**Duo 82 + RPG 336 = 418 项全过**，与目标一致；任务书给定的 lw_core 命令
另跑出 142 项全过（那是 test_lw_core 套件本身的计数，不是 418 的组成）。
全量 `run_core_regression.ps1` 的编译方式在 Linux 上不可直接复现：
`lw_storage.c` 非 ESP 分支 `#include <windows.h>`，是 Windows 宿主专用
路径——如实记录，未改接口绕过。本轮未改任何 lw_core 业务代码与存档格式。

---

## 9. 工具侧（面板烘焙采样）

`tools/lwp_panel_bake.py` 的 `nine_slice()` 目前按**整张图集**建格
（`lattice(atlas_len, scale)` 后以截断偏移取窗），与已验证 237 帧 0 差异的
`tools/lwp_prescale.py: crop_axis()` 的教训相悖（窗口必须从整画布 lattice
里按源矩形裁出，起点由 lattice 决定、不是 `scaled(src_lo)` 的截断值）。
详见 CLOUD_PATCHES 的 P-08 与其 numpy 对比脚本结果——本机无宿主截图基线
全集，只能报告新旧算法的差异像素数变化，**不声称已对齐**。

---

## 10. 报告已定位、本轮不重复审的剩余项（measured-by-user，出处在案）

- R1 控制中心首开最坏一帧仍 256–270 ms 量级，`anim off` 后消失、
与内容绘制无关（FINAL §19–§20）。与 A4 同源，等用户挑改法。
- R2 开机预热把遮罩动画从 29 帧压到 5 帧（FINAL §18.4）：场景卡同步读
在 LVGL 任务里。改法（卡读异步化）要动 `lw_visual_assets` 池实现与
`warm_pump`，均为禁改区，本轮不碰。
- R3 `render()` 仍是一次同步构建（`ui≈1244 ms`，FINAL §12.7）：两阶段
首屏已把「可见时间」解决，剩余是总时长问题，优先级低于 A1/A4。

## 审计结论（按预期量级排序）

| # | 项 | 量级 | 频率 | 本轮处置 |
|---|---|---|---|---|
| A1 | 换页整页容器 opacity 淡入 | 大 | 每次换页 | 补丁 P-07（单独提交，观感变化已标注） |
| A4 | 控制中心 stagger 入场动画群 | 大 | 每次打开 | 禁改区，仅证据，待用户挑 |
| A2 | 相册翻页舞台 scale+opacity | 大 | 每次翻页 | 仅证据，待用户挑 |
| B3/G1 | 音乐每秒全量 refresh | 中 | 播放中每秒 | 补丁 P-01 |
| B1 | 首页每秒无守卫标签 | 中 | 每秒 | 补丁 P-02 |
| A3/A5 | PAPER 容器 appear / 回忆全屏淡出 | 中 | 每次打开/关闭 | 仅证据，待用户挑 |
| B6/G5 | OS 壳滑杆每秒 REFRESH | 中/小 | 每秒（控件存在时） | 补丁 P-05 |
| C1 | 相册翻页 clean+重建 | 中 | 每次翻页 | 仅证据，待用户挑 |
| B4 | 设置页 2 s 整树计数+标签重写 | 中/小 | 每 2 s | 补丁 P-03 |
| G2 | 贪吃蛇每步同值写色+标签 | 小/中 | 每步 | 补丁 P-06 |
| B2 | 回忆页每秒无守卫标签 | 小 | 每秒 | 补丁 P-04 |
| B5 | 旧壳时钟每秒重写分钟值 | 小 | 每秒 | 补丁 P-04b（与 P-04 分开提交） |
| — | 打砖块定时器 8 ms 半数空转 | 小 | 125 次/秒唤醒 | 补丁 P-09 |
| — | 面板烘焙采样格对齐 prescale | 中（像素正确性） | 离线 | 补丁 P-08（工具） |

补丁细节、风险与逐项上板判据见 `CLOUD_PATCHES.md`。
