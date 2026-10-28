# P0-D · 开机动画

分支 `perf/v4-4-fluidity-overhaul`，基线 `v4.3-baseline`。

**本文件已拿到真机读数（§6）**。过程分三段：先在没有板子的情况下做静态分析并落地
插桩；板子接上后板上仍是 `v4.3-baseline-dirty` 旧镜像（`boot` 命令返回
`Unrecognized command`），只能给间接证据；用户明确批准烧录后，插桩首次在真机执行，
拿到 `boot phases` 与 `boot: frames=…` 两组数。

本轮做的是「让开机动画变得可测量」：加了分段计时和开机遮罩统计，把
「2000 ms 的动画为什么看起来卡」这个问题从「靠印象」变成「读两行日志就能回答」。

真机结论（§6）：**名义 2000 ms 的动画只画出 5 个画面（409 ms/帧）**，其中
`ui=1542 ms` 是首屏构建、`art=141 ms` 是挂卡与开包。开机的慢与 RPG/宠物页同源——
每帧重绘精灵的面积，不是缓存、不是 I/O。

---

## 1. 开机动画现在是什么样【静态证据】

### 1.1 入口条件

`lw_os_boot()`（`lw_os.c:850` 起）第一行：

```c
if (!s || lw_os_motion_get() == LW_MOTION_OFF) return;
```

**动画模式为 OFF 时整段开机动画被跳过** —— 桌面直接出现，没有遮罩、没有淡入。
`lw_os_motion_get()`（`lw_motion.c`）返回

```c
return !lw_app_state()->settings.animations ? LW_MOTION_OFF : motion_mode;
```

也就是说 `settings.animations == 0` 时**无论 motion_mode 是什么都是 OFF**。
这一点很重要：如果某台机器的 NVS 里 `animations` 被存成 0，那么「开机动画很流畅」
这个观察是假的 —— 根本就没播。`anim` 控制台命令的 `on|off` 参数可以在运行期
修这个（`lw_capture.c` 里 `lw_os_animations_set(on)`）。

### 1.2 组成

| 元素 | 参数 |
|------|------|
| 全屏遮罩 | `lw_os_box(s->root, 0,0,1024,600, palette()->bg, 0, 255)`，`LV_OBJ_FLAG_CLICKABLE`，点击即跳过 |
| 壁纸 | `lw_os_wallpaper(s->boot, s->painted_wallpaper, 0,0,1024,600)` |
| 压暗层 | `lw_os_box(..., 0x282438, 0, 195)` + `lw_os_motion_dim(light, 195, 0, 1450)` |
| logo | heart 图标 56 px @484,0 + `"Little World"` `lw_font_44` + 副标 `lw_font_22`，`lw_os_motion(logo, LW_MOVE_BOOT)` |
| 叶子 ×8 | `lw_os_icon(s->boot, OS_I_LEAF, 46+i*130, 180+(i%3)*90, 18+(i%2)*6, accent)`，`opa=170`，`lw_os_motion_vector(petal, -22,-72, 1450+i*40, i*45)` |
| 角色精灵 ×2 | `lw_sprite_create(s->boot, actors[i].x, actors[i].y, 288)`，`set_external_clock(true)`，`lw_os_motion_vector(obj, i?84:-84, 10, 900, 300+i*160)` |
| 跳过提示 | `"轻触跳过"` `lw_font_18` @888,24 |
| 淡出 | `lv_anim` 255→0，`delay = FULL ? 1740 : 80`，`duration = FULL ? 260 : 90` |

**FULL 模式的名义总时长 = 1740 + 260 = 2000 ms。** 这是「计划」，不是「实测」。

### 1.3 开机期间还在发生什么【静态证据】

`lw_os.c:794`：

```c
if (s->boot) { s->boot_frames++; s->boot_ms += dt;
    for (i<2) if (s->boot_sprites[i] && lw_os_motion_get()!=LW_MOTION_OFF) lw_sprite_seek(s->boot_sprites[i], s->boot_ms); }
update_actors(dt);
```

两个 288 scale 的精灵每 tick seek，**同时 `update_actors(dt)` 还在跑** ——
桌面角色在遮罩后面继续活着。所以开机动画的每帧成本不是「几个静态图标」，
而是「8 个叶子 + 2 个全尺寸角色精灵 + 壁纸 + 压暗层 + 桌面角色」。

而这一切发生在**卡读取前缓存刚打开、六个包头刚读完**之后，即 `lw_art_boot()`
之后的第一段重负载。

### 1.4 `LW_MOVE_BOOT` 的时长硬编码

`lw_motion.c:64` 附近：

```c
lw_os_motion_slide(o, 32, 0, 680, 160, false);
animate(o, appear, 0, 255, 560, false);
```

logo 的滑入是 680 ms + 160 ms、淡入 560 ms，**与 `lw_os_boot()` 里 1740 ms 的
淡出延迟是两处独立的硬编码**。改任何一处都不会自动影响另一处 —— 这是写报告时
需要点明的维护风险，不是本轮的改动对象。

---

## 2. 本轮加了什么（可测量性）

### 2.1 启动分段计时（`firmware/main/lw_boot.c`）

`start_os()` 开头：

```c
/* V4.4 P0-D: boot phase timing. Everything below runs inside the LVGL lock
 * and before the first frame, so the sum of these stamps is the real
 * "power-on to first frame" budget the user waits through. */
const int64_t t_start = esp_timer_get_time();
int64_t t_mark = t_start;
```

五个戳：

| 戳 | 位置 | 行 |
|----|------|-----|
| `t_hw` | `lw_hw_begin()` 之后 | `lw_boot.c:345` |
| `t_art` | `lw_art_boot()` 之后 | `lw_boot.c:399` |
| `t_content` | `lw_content_boot()` 之后 | `lw_boot.c:408` |
| `t_ui` | `lw_os_start()` 成功分支开头 | `lw_boot.c:418` |
| `t_apply` | `lw_content_boot_apply()` 之后 | `lw_boot.c:425` |

一行输出（`lw_boot.c:432`）：

```
ESP_LOGI(TAG, "boot phases: hw=%lld art=%lld content=%lld ui=%lld apply=%lld total=%lld ms", ...)
```

各段含义（写进代码注释，也写在这里）：

- `hw` = codec / LEDC / SD bring-up；
- `art` = 卡挂载 + 六个包头 + 读前缓存打开；
- `content` = 用户内容文件；
- `ui` = **整个首屏构建**（页面、精灵、字体）；
- `apply` = 纪念日/回忆安装。

判据：**任何一段 > 100 ms 都值得单独调查**。`ui` 段特别值得看 ——
它包含 `render()` 里 `lv_obj_clean(s->root)` 之后整棵对象树的重建，以及
`lw_os_boot()` 里那 8 个叶子 + 2 个精灵。

### 2.2 开机遮罩统计（`lw_os.h` + `lw_os.c`）

`lw_os.h` 新增：

```c
typedef struct {
    uint32_t frames, elapsed_ms, visible_ms;
    bool active, skipped;
} lw_os_boot_stats_t;
void lw_os_boot_stats(lw_os_boot_stats_t *out);
```

语义（写在头文件注释里）：

- `frames` = 遮罩存在期间跑过的 OS tick 数。**`frames / elapsed` 就是遮罩实际被
  动画的速率**：2000 ms 按 20 ms/tick 应该是 100 帧；如果只有 20 帧，说明是
  调度把 2 秒的片头拉成了 2 秒的幻灯片，不是动画本身慢。
- `elapsed_ms` = 从 `lw_os_boot()` 到遮罩被删除的墙钟（遮罩还在时是实时值）。
- `visible_ms` = 到**淡出开始**为止的完全可见时长（`lv_anim` 的 `start_cb` 里记录，
  因为 fade 动画要等完 delay 才真正开始）。
- `active` = 遮罩当前是否还在。
- `skipped` = 用户是否点了「轻触跳过」。

实现要点：

- `os_t` 新增 `uint32_t boot_frames, boot_started_tick, boot_visible_ms, boot_elapsed_ms; bool boot_skipped;`
- `lw_os_boot()` 开头重置五个字段并 `boot_started_tick = lv_tick_get()`
- `tick()` 里 `if (s->boot) { s->boot_frames++; ... }`
- `boot_deleted()` 里冻结 `boot_elapsed_ms = lv_tick_get() - boot_started_tick`
  —— **遮罩删掉之后读 `boot` 仍然能看到当时的数字**，不用抢在 2 秒内敲命令
- `boot_skip()` 里置 `boot_skipped = true`
- `boot_fade_start()`（挂在 `lv_anim_set_start_cb`）里记 `boot_visible_ms`

### 2.3 `boot` 控制台命令（`firmware/main/lw_capture.c:484`）

```
lw> boot
boot: frames=%u elapsed=%u ms visible=%u ms fps=%u active=%d skipped=%d
```

注册在 `CONFIG_LW_DIAG_CONSOLE` 块内（`lw_capture.c:633` / `:647`），
与 `sched` / `rpg` / `anim` 同一组。

---

## 3. 【待真机】怎么读这三个数

### 3.1 先确认动画真的在播

```
lw> anim            # motion_mode / animations_enabled / raw_mode
lw> boot
```

若 `animations_enabled=0`，开机动画**根本没播**，`boot` 会显示 `frames=0`。
这时先 `anim on` 再看。**这是最容易误判的一条**：没播 ≠ 播得好。

### 3.2 三种典型读数

| `frames` | `elapsed` | 解读 | 下一步 |
|----------|-----------|------|--------|
| ~100 | ~2000 | 动画按设计跑满 | 看 `visible_ms` 是否 ~1740 |
| ~50 | ~2000 | 20 ms 的 tick 被拉成 40 ms | 查 `sched`，与桌面页同源 |
| ~20 | ~2000 | tick 被拉成 100 ms，片头是幻灯片 | **真机数据预测落在这一档**，见下 |
| 任意 | ≫2000 | 淡出动画自己被拖长 | 查 `LV_DEF_REFR_PERIOD` 与实际 flush |

第三行是重点：**开机精灵是 288 scale、两个、每 tick seek**，如果此时前缓存
装不下它们的帧，就会每帧同步读卡 —— 这与宠物页 777 ms 是同一个机制
（`SPRITE_CACHE.md` §2）。区别只是开机时只有 idle clip（8 帧 × 2 角色）。

不过开机时的活跃集只有 8 帧 × 2 角色 = 16 帧 × 196,608 B = 3,145,728 B，
在旧的 16 槽 / 4 MiB 配置下**正好装得下**（`lw_storage.c` 头注释就是按这个
场景算的「Sixteen slots cover two full eight-frame cycles」）。所以开机动画
**不是**缓存不足的受害者；它慢的话更可能是绘制/调度问题，不是 I/O。

**真机已经给了这条推断一个很强的旁证。** RPG 页的活跃集同样是 16 帧 × 196,608 B
= 3,145,728 B（正好装下），真机实测 **30 秒零 miss、零 eviction**，但 `sched`
仍是 **117 ms**（`max_gap 119`，无尖刺）—— 也就是说**装得下、零 I/O 的纯绘制
每帧成本就是约 117 ms**。而 `anim off` 后同一页立刻回到 20 ms。

把这个数搬到开机：名义 2000 ms 的动画（`delay 1740 + duration 260`）按
**117 ms/帧**只能跑约 **17 帧**，正好落在上表第三行的「~20 frames」档。
⇒ **开机动画大概率是幻灯片，而且原因与 RPG 页同源（精灵重绘面积），
不是缓存、不是 I/O。** 这是预测，不是测量——等新镜像烧上去用 `boot` 证实或推翻。

这一点让 `boot` 读数成为很好的**对照组**：如果开机 tick 也远低于 50 帧/秒，
而开机没有 I/O 压力，那瓶颈就在绘制或调度一侧，与 `SPRITE_CACHE.md` §5.3
的结论一致。

### 3.3 分段计时的读法

串口抓 `boot phases:` 那一行。判据：

- `art` 段（卡挂载 + 六个包头 + 缓存打开）应该是**几十毫秒量级**。如果它是
  几百毫秒，说明卡或 FAT 有问题，与 LVGL 无关；
- `ui` 段包含整棵对象树重建 + 开机遮罩的 8 叶子 + 2 精灵。**这一段是本轮
  最想知道的数字**；
- `total` 与 `boot` 的 `elapsed` 相加 ≈ 用户从通电到看到桌面所等的时间。

---

## 4. 本轮**没有**解决的问题

开机动画的观感问题本轮只做到「可测量」，没有做任何优化。已知可疑点，
都留到有数据再动：

1. **8 个叶子各挂一个 `lw_os_motion_vector`** —— 8 个独立的 `lv_anim` 实例，
   每个都在 1450 ms 内改 `x/y`。这是 8 个动画对象，不是 1 个。
2. **两个 288 scale 精灵在遮罩期间 seek** —— 每个 seek 会走
   `lw_sprite_seek` 的逐帧累加 + `present()`，换帧时 `lv_asset_refill` +
   `lv_obj_invalidate(288×288)`。
3. **`update_actors(dt)` 同时还在跑** —— 桌面角色在遮罩后面继续 seek，
   也就是说开机期间实际有 **4 个** 288 scale 精灵在动（2 个 boot + 2 个桌面）。
4. **两处独立的时长硬编码**（`lw_motion.c:64` 与 `lw_os_boot()` 的 1740/260）。

以上都不是本轮改的。**在拿到 `boot` 与 `boot phases` 之前，任何「缩短某个
动画时长」的改动都是拿猜测换猜测**，而且交接书明确禁止「延长过渡时间掩盖跳帧」。

> **真机旁证（预测，非测量）**：RPG 页的活跃集与开机同为 16 帧 × 196,608 B
> = 3,145,728 B，真机实测**装得下、30 秒零 miss**，但每帧仍要 **117 ms**。
> 若开机的绘制成本与之同源，2000 ms 只能跑约 **17 帧** ⇒ 上面第 2、3 条的
> 「4 个 288 scale 精灵」很可能是主因，而不是缓存。用 `boot` 一读即可证实或推翻。

---

## 5. 本轮没做的事

- 没有关掉开机动画，没有缩短任何一段时长。
- 没有减少叶子数量或精灵数量（那属于「删素材换 FPS」）。
- 没有在未上板的情况下声称开机变快。
- 没有碰 NVS / 分区表 / C6 / eFuse。
- **已把 `boot` / `boot phases` 跑在真机上**（用户批准烧录后），读数见 §7。

---

## 6. 构建与测试（本轮实测）

| 检查 | 结果 |
|------|------|
| `ninja -C build/host` | 通过（`lw_os.c` 重编） |
| `tools/run_host_tests.ps1` | `142 checks, 0 failure(s)` `HOST TESTS PASSED` |
| `tools/run_core_regression.ps1` | `Duo 82` / `RPG 336` / `Storage 33`，全 0 failure |
| `idf.py build`（`os`） | `Project build complete`，2,359,248 B |

---

## 7. 真机读数（烧录本轮镜像之后）

烧录：`tools/flash_v4_safe.ps1 -Config os -Stage app -Execute -Authorized`，只写
`0x010000`，esptool `Hash of data verified.`，日志 `reports/logs/flash/flash-20261028-174129.log`。
烧后版本串 `v4.3-baseline-1-g04ad8e5-dirty`。

### 7.1 首屏分段（`boot phases` 日志，首次生效）

```
I (1686) littleworld: capabilities: brightness=2 volume=2 wifi=0 audio=0 storage=0
I (3228) littleworld: boot phases: hw=31 art=141 content=2 ui=1542 apply=0 total=1717 ms
I (4433) littleworld: boot: Little World ready, free heap 21724263 B
```

| 段 | 毫秒 | 占 total | 含义 |
|----|------|----------|------|
| `hw` | **31** | 1.8% | codec / LEDC / SD bring-up |
| `art` | **141** | 8.2% | 挂卡 + 六个包头 + 读前缓存打开 |
| `content` | **2** | 0.1% | 用户内容文件 |
| `ui` | **1542** | **89.9%** | 整个首屏构建（页面 / 精灵 / 字体） |
| `apply` | **0** | 0% | 纪念日安装 |
| `total` | **1717** | 100% | |

**结论：开机时间几乎全部是 `ui=1542 ms` 的首屏构建**，不是 I/O、不是内容加载。
`art=141 ms` 已经比预期好（六个包共 47 MB，只读头部与 clip 表）。
`ui` 这 1542 ms 里包含 `lw_os_start()` → `render()`（`lv_obj_clean` 全清 + 重建壁纸 /
ambience / content / status / dock）→ `build_dock()` 的 6 个九宫格按钮（每个 ≈9 个
`lv_image`，全部指向 835,584 B 的 `v43skin.atlas`）→ `update_actors(0)` 建两个 288
scale 精灵 → `lw_os_boot()` 建遮罩 / 8 片叶子 / 2 个开机精灵。**这是下一轮 P0-D
的第一优化目标**，但它属于「首屏构建」而不是「开机动画」。

### 7.2 遮罩动画统计（`boot` 命令，首次生效）

```
boot: frames=5 elapsed=2047 ms visible=1877 ms fps=2 active=0 skipped=0
```

| 字段 | 值 | 含义 |
|------|-----|------|
| `frames` | **5** | 遮罩存在期间跑过的 OS tick 数 |
| `elapsed` | **2047 ms** | 从 `lw_os_boot()` 到遮罩删除的墙钟 |
| `visible` | **1877 ms** | 到淡出开始为止的完全可见时长 |
| `fps` | **2** | `frames / (elapsed/1000)` |
| `active` | 0 | 遮罩已删除 |
| `skipped` | 0 | 没有被「轻触跳过」中断 |

`elapsed=2047 ms` 与设计值（`delay 1740 + duration 260 = 2000 ms`）几乎完全吻合 ⇒
**定时器与 `lv_anim` 的时间轴是对的**。问题在 `frames=5`：2000 ms 里只画了 5 个画面。

按 §3.2 判据表，`~20 frames` 档对应「绘制跟不上」；实测 **5 帧比那一档还差 4 倍**，
每帧 409 ms。原因与 RPG 页同源但更重：

1. **开机期间 `update_actors(dt)` 仍在跑**（`lw_os.c:792` 之后紧接着调），桌面角色
   并没有被暂停 ⇒ 实际同时有 **4 个 288 scale 精灵**在推进，不是 2 个。
2. 遮罩上还有 8 片叶子各自跑 `lw_os_motion_vector`（`lv_anim` 驱动位置），以及
   全屏 `light` 层 1450 ms 的 `bg_opa` 渐变 ⇒ 每帧都要重合成整块 1024×600。
3. RPG 页 117 ms/帧是两个精灵 + 全屏 RAM 背景；开机是四个精灵 + 全屏半透明遮罩 +
   8 个移动对象，409 ms/帧的量级与此相符。

### 7.3 由此得到的 P0-D 结论

- **开机「卡」不是时长配置错**：`elapsed 2047 ms` 精确等于设计值。
- **也不是缓存不足**：开机活跃集 16 帧 × 196,608 B = 3,145,728 B，旧的 16 槽 / 4 MiB
  也装得下（旧镜像开机期间 `evictions` 无异常增长可作旁证）。
- **是绘制**：5 帧 / 2047 ms。这与 `SPRITE_CACHE.md` §9.2 的结论一致——
  三个慢场景（RPG 119 ms、宠物 251 ms、开机 409 ms/帧）的共同分母都是
  「每帧要重绘的精灵面积 × 同屏精灵数」。
- **下一轮最有性价比的两步**（都不违反「不关动画」约束）：
  1. 开机期间暂停 `update_actors(dt)`（桌面角色此刻被遮罩完全盖住，画了也看不见）
     ⇒ 同屏精灵从 4 个降到 2 个。
  2. 给开机精灵做 C-4 的画布裁剪（288×288 → 实际不透明区域）。
  两项都不删素材、不改时长、不降画质。
