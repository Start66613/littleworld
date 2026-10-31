# P2 · 架构对照：我们的渲染引擎 vs 六个参照项目

这份文档回答一个问题：**别人的嵌入式 UI 为什么能跑到 40–60 fps，而我们同一块
ESP32-P4 上只有 4–8 fps？** 答案不是「他们用了更快的硬件」，而是三处结构差异。
每一处都给出了我们的文件行号，以及「采纳 / 不采纳 / 已具备」的判断。

---

## 0. 结论先行

| 差异 | 参照项目 | 我们的现状 | 影响 | 判断 |
|---|---|---|---|---|
| **精灵缩放发生在运行时** | GB Studio / retro-go 都在**构建期**决定像素尺寸 | 运行时 `lv_image_set_scale(288)` 让每帧走 `transform_and_recolor` | **3.9–4.5×** | ✅ 本轮已改（`SPRITE_RENDER.md`） |
| **素材没有内容边界** | GB Studio 的精灵表带 per-tile 属性；TIC-80 的 sprite 是固定 8×8 | `lw_asset_spec_t` 只有 `width/height`，画布 = 绘制面积 | **1.5×**（含在上一项的 5.5–7.1× 里） | ✅ 本轮已改 |
| **页面生命周期没有「不可见」概念** | InfiniTime 的 `Screen::OnToBeHidden` 停掉动画 | 开机遮罩下仍在重绘桌面精灵 | 开机同屏精灵 4→2 | ✅ 本轮已改 |
| **首屏同步构建整棵树** | Brookesia 分阶段 init，先出可用界面再补内容 | `render()` + `lw_os_boot()` 一次同步 1542 ms | 首帧延迟 | ⬜ 未做（机制 4） |
| **按钮是 9 个独立 `lv_image`** | raylib 用图集 + 批处理 | `lw_skin_panel()` 3×3 九宫格 = 9 个对象、9 次 `set_scale` | 待测 | ⬜ 未做（机制 5） |

---

## 1. 渲染路径：构建期 vs 运行时决定像素

### 1.1 我们的现状（有源码行号）

`firmware/components/lw_ui/src/lw_sprite.c` 的 `present()` 第三级在换素材时：

```c
lv_image_set_src(s->image, lw_asset_image(lease));
lv_image_set_pivot(s->image, 0, 0);
lv_image_set_scale(s->image, s->scale);          /* 288 或 282，不是 256 */
lv_obj_set_pos(s->image, ...);
```

`firmware/managed_components/lvgl__lvgl/src/draw/sw/lv_draw_sw_img.c:223-224`：

```c
bool transformed = draw_dsc->rotation != 0 ||
                   draw_dsc->scale_x != LV_SCALE_NONE ||
                   draw_dsc->scale_y != LV_SCALE_NONE ? true : false;
```

`LV_SCALE_NONE` = 256（`lv_style.h:48`）。所以 **288 和 282 都不是 256**，
每一帧都进 `transform_and_recolor()`（第 300-311 行），它做三件昂贵的事：

1. `lv_malloc` 一块 8192 B 临时缓冲（每帧分配 + 释放）；
2. `lv_draw_sw_transform()` 定点重采样（每像素 1 次源读 + 1 次 alpha 读）；
3. 二次 `lv_draw_sw_blend()` 把临时缓冲混到目标上。

直通快速路径的条件（第 254-273 行）是：

```c
!transformed && !radius && cf == LV_COLOR_FORMAT_RGB565A8 &&
draw_dsc->recolor_opa <= LV_OPA_MIN && draw_dsc->colorkey == NULL
```

命中后只做一次 `lv_draw_sw_blend(t, &blend_dsc)`，没有临时缓冲、没有重采样。

**关键判断**：`lw_sprite_set_pixel_mode()`（`lw_sprite.c:194`）已经关掉了
antialias，所以 `transform_rgb565a8()` 里的双线性分支（`lv_draw_sw_transform.c:709-741`）
**不会执行**。也就是说我们付的是「定点缩放 + 临时缓冲 + 二次 blend」的钱，
**不是**「每像素 5–6 次乘加」的钱。这解释了为什么 A→C（只去缩放、面积不变）
就有 3.93–4.49×——省掉的是结构性开销，不是采样密度。

### 1.2 参照项目怎么做

| 项目 | 机制 | 源码位置 |
|---|---|---|
| **GB Studio** | 精灵的像素尺寸在构建期由 `.gbsres` 导出时确定；运行时只做「选 tile + 拷进 OAM」，没有任何缩放 | `appData/src/.../sprite` 相关，见 `OPEN_SOURCE_RESEARCH.md` §6.1 |
| **retro-go** | 显示缩放用**行级整数映射表** `map_viewport_to_source_x/y[]` 预计算，运行时每像素一次 `int16` 查表 | `components/retro-go/rg_display.c` |
| **TIC-80** | sprite 固定 8×8，缩放只在 shader 里做 | 未采纳（见下） |
| **raylib** | `DrawTexturePro` 在 GPU 上缩放；软件路径也走预乘图集 | `src/rtextures.c` |

**为什么 retro-go 的行映射表能给我们启发，但我们不直接抄**：retro-go 拥有整个
framebuffer，可以按行 memcpy；LVGL 的绘制是「解码 → 变换 → blend」三段，我们
无法绕过它的 blend。但它证明了一件事：**把缩放从「每帧算」变成「一次算好」，
收益是数量级的**。这正是 F 形态（离线烘焙）的思想来源。

### 1.3 判断

**采纳「构建期决定像素」这个原则，但实现方式不同。** retro-go 是行级查表，
我们是**离线整帧烘焙**：`tools/lwp_prescale.py` 用与 LVGL 完全相同的定点算法
（`scaled(v,scale)=(v*scale+0x80)>>8`、`step=(256*(up(dest_len-1)-up(0)))//(dest_len-1)`）
在打包时把 288/256 的映射算进像素，运行时 `lv_image_set_scale(256)` 走直通路径。

**不能照抄的部分**：GPL-3.0（FabGL）、GPL-2.0（retro-go）、GPL-3.0（InfiniTime）
与 FSAL（Boilerplate-3）**零代码复制**，只提取算法与结构思想。

---

## 2. 素材元数据：画布 ≠ 内容

### 2.1 我们的现状

`firmware/components/lw_ui/include/lw_assets.h`（V4.4 版本）的 spec 只有：

```c
typedef struct {
    const char *id;
    uint16_t width, height;      /* 画布尺寸，不是内容尺寸 */
    uint16_t frames;
    int16_t anchor_x, anchor_y;
    lv_color_format_t format;
    lw_asset_read_fn read;
    void *context;
} lw_asset_spec_t;
```

**没有 bbox 字段**。所以运行时无法知道「这幅图里哪里才有内容」，只能整画布绘制。

实测浪费（`reports/perf-v4-5/sprite_bbox.json`）：12 个 clip 画布绘制
1,157,120 px，实际内容 383,318 px，**浪费 66.9%**；`dog.idle` 浪费 **97.5%**
（内容 54×47 画在 320×320 上）。

### 2.2 参照项目

GB Studio 的精灵表天然带 per-tile 属性（flip / palette / priority），因为它的
导出器知道每个 tile 是否有内容；TIC-80 的 sprite 是固定 8×8，不存在「画布里有
空白」的问题。

### 2.3 判断

**采纳，且本轮已实现。** `lw_asset_spec_t` 新增：

```c
uint16_t bbox_x, bbox_y, bbox_w, bbox_h;   /* 内容边界，0 = 无元数据 */
uint16_t prescaled;                        /* 1 = 像素已按 scale 烘好 */
```

并新增 `lw_asset_draw_geometry()` 把元数据折算成 LVGL 的四个旋钮
（`obj_w/obj_h/offset_x/offset_y/anchor_dx/anchor_dy`）。

**兼容策略（重要）**：`bbox` 全 0 表示「无元数据」，此时行为与 V4.4 完全一致
（保持画布尺寸对象、零 offset）。这条策略是**逐像素回归验证倒逼出来的**：早期
版本把 embedded/LWFA 包的 bbox 初始化成 `0,0,width,height`（非零），导致对象被
放大到 `scale_px(256,288)=288`，露出 HEAD 从不绘制的边缘，125/156 张截图不同。

---

## 3. 页面生命周期：不可见就不该绘制

### 3.1 我们的现状

`firmware/components/lw_ui/src/lw_os.c` 的 `tick()` 每 20 ms：

```c
if (s->boot) { s->boot_ms += dt; for (i<2) lw_sprite_seek(s->boot_sprites[i], s->boot_ms); }
update_actors(dt);   /* 桌面两个 288-scale 精灵照常重绘 */
```

开机遮罩是**完全不透明的 1024×600 盒子**（`lw_os_boot()` 里的
`lw_os_box(s->root,0,0,1024,600,palette()->bg,0,255)`），桌面精灵在它下面，
用户看不见。真机 `boot` 读数 `frames=5 elapsed=2047 ms`（409 ms/帧）说明
开机期间每帧都在付两份看不见的精灵重采样。

### 3.2 参照项目

InfiniTime 的 `Screen` 基类有明确的 `OnToBeHidden` / `OnToBeShown` 回调，
隐藏时停掉动画与刷新；esp-brookesia 的 app 生命周期同样区分 `foreground` /
`background`。

### 3.3 判断

**采纳，本轮已实现。** `update_actors()` 改名为 `update_actors_paint(dt, paint)`，
`paint=false` 时仍推进 `lw_duo_tick()` 与 clip 名推导，但跳过所有 widget 写入。

**两次失败教训**（必须保留，否则下一轮会重复）：

1. 第一版直接整段跳过 `update_actors` ⇒ **35/156 张截图不同**（`v43-home-dusk`
   diff 19,873）。原因：连 `lw_duo_tick()` 也跳过了，duo 状态机停摆。
2. 第二版只按 `s->boot == NULL` 门控 ⇒ **2/156 不同**（`v4-boot-10` diff 9,237）。
   原因：淡出期间桌面已透出、精灵却停在旧帧，遮罩消失时可见地「跳一下」。

最终门控 `s->boot == NULL || s->boot_visible_ms != 0` ⇒ **156/156 逐像素一致**。

---

## 4. 首屏构建：同步一次 vs 分阶段

### 4.1 我们的现状

`lw_os_start()`（`lw_os.c:925-942`）在**一次同步调用**里做完：

```c
render();                      /* 整棵树 */
s->timer = lv_timer_create(tick, 20, s);
lw_os_boot();                  /* 遮罩 + 壁纸 + logo + 8 叶子 + 2 精灵 */
```

真机 `boot phases` 读数：`hw=31 art=141 content=2 ui=1542 apply=0 total=1717 ms`，
**`ui` 占 89.9%**。其中字体内嵌 3,718,114 B 是编译期常量、静态位图、无运行时
解析（`CONFIG_LV_USE_FONT_COMPRESSED=y`、`# CONFIG_LV_USE_FREETYPE is not set`），
所以字体不是成因；成因是整棵树的同步构建。

### 4.2 参照项目

esp-brookesia 的 `init` 分阶段：先建立可交互的壳，再逐步补内容，每阶段之间让出
CPU 让首帧先出去。

### 4.3 判断

**方向采纳，但本轮未实现**（机制 4）。理由是它改变首帧的可见顺序，需要真机
视觉验收，而板子当前未连接。可做的具体拆分（留给下一轮）：

| 阶段 | 内容 | 预期 |
|---|---|---|
| A（必须同步） | 壁纸 + 遮罩 + logo | 首帧可出 |
| B（延后一帧） | dock + 6 个 item + 状态栏 | 用户已在看开机动画 |
| C（延后到遮罩淡出后） | 桌面家具热区 + 角色 hit box | 完全不可见 |

---

## 5. 皮肤按钮：9 个 `lv_image` 对象

### 5.1 我们的现状

`firmware/components/lw_ui/src/lw_skin.c`：

```c
lv_obj_t *lw_skin_panel(p, id, w, h, b) {
    /* 3x3 = 9 个 piece，每个都是独立 lv_image */
    for (r = 0; r < 9; r++) { piece(...); }
}
```

`show()` 里每个 piece 都做 `lv_image_set_scale_x/y` + `lv_image_set_pivot(0,0)`，
**缩放不是 256 ⇒ 每个 piece 也走慢路径**。`lw_os.c` 里 `lw_skin_*` 调用 11 处，
`lw_skin_panel` 2 处（控制中心面板 500×486、控制中心 tile 108×196）。

### 5.2 参照项目

raylib 的图集 + 批处理：同材质的多边形合并成一次 draw call。

### 5.3 判断

**部分采纳，本轮未做**（机制 5）。LVGL 没有真正的批处理，但有两个可行的替代：

1. **预烘焙九宫格**：`skin.lwp` 是 512×544 的**切片图集**（`rects[SK_COUNT]` 17 个矩形），
   按九宫格尺寸离线拼好成整块 —— 但尺寸组合太多，不现实；
2. **只在非 256 缩放时才有问题**：如果皮肤 piece 按 `sx=sy=256` 绘制（即 1:1 切片），
   就直接走直通路径。当前 `show()` 的 `sx/sy` 来自 `w/h` 与 rect 尺寸的比值，
   控制中心面板是放大绘制 ⇒ 非 256。

**下一轮的第一件事**应该是量一下控制中心页的真实成本，再决定是否值得动。

---

## 6. 逐项对照总表

| 维度 | 参照项目最佳实践 | 我们的实现 | 差距 | 本轮动作 |
|---|---|---|---|---|
| 缩放时机 | 构建期（GB Studio / retro-go） | 运行时 `set_scale(288)` | **决定性** | ✅ 离线烘焙 |
| 内容边界 | per-tile 属性（GB Studio） | 无 bbox 字段 | 大 | ✅ 加 bbox |
| 快速路径 | 直通 blend（LVGL 自带） | 全部走慢路径 | 大 | ✅ 烘到 256 |
| 页面生命周期 | `OnToBeHidden`（InfiniTime） | 无「不可见」概念 | 中 | ✅ 遮罩期跳过 |
| 首屏构建 | 分阶段 init（Brookesia） | 一次同步 1542 ms | 中 | ⬜ 未做 |
| 图集批处理 | raylib draw call 合并 | 9 个独立 `lv_image` | 小 | ⬜ 未做 |
| 行级失效跳过 | retro-go 行校验和 | LVGL 失效区已覆盖 | 无 | ❌ 不采纳 |

---

## 7. 一句话总结

**我们不是缺硬件，也不是缺缓存——是缺「把缩放和内容边界从运行时挪到构建期」
这一步。** LVGL 提供了直通快速路径，但条件苛刻（`scale == 256` 且
`recolor_opa <= LV_OPA_MIN` 且无 colorkey）；我们因为 V4.3 的「用户批准的比例
修正」把 scale 定成 288，恰好绕开了它。V4.5 的 F 形态既保住了那个比例修正的
视觉效果（逐像素一致），又让每一帧回到快速路径。

---

## 8. 收尾：四条「跨项目共性」最后都落成了代码

§6 的对照表里有四项当时标着「未做」。本轮收尾时它们全部落地，而且都能追溯到某个
参照项目的一个具体决定：

| 共性 | 参照 | 落地形态 | 结果 |
|---|---|---|---|
| 把不变的成本移到构建期 | GB Studio 的精灵属性表 / retro-go 的整数步进 | `tools/lwp_prescale.py` 预缩放 + 预裁剪 + per-clip bbox | 宿主 5.5–7.1×，237 帧逐像素一致 |
| 传输层要按「块」而不是「帧」对齐 | retro-go 的行级映射表（一次算好、逐块复用） | LWPX 固定 stride + raw-deflate 块（stride 存在 trailer 保留 u16） | 单帧 97–209 KiB → 15–48 KiB，走路 max 353 → 23 ms |
| 页面生命周期决定「什么可以先不画」 | InfiniTime `OnToBeHidden` | 开机遮罩期不重绘被全遮的桌面精灵 | 156/156 逐像素一致 |
| 分阶段 init：先给用户看东西，再建页面 | Brookesia 的分阶段启动 | `boot_backdrop()` 先刷 → `lv_refr_now()` → `render()` → `boot_finish()` | 首屏可见 1254 → **23 ms** |

另外两项仍是「不做」：皮肤九宫格合并成图集（收益小、要重画美术）、retro-go 的行级
校验和（LVGL 失效区已覆盖，重复工作）。

### 8.1 一条没有写进 §6 的共性：内部 RAM 比 CPU 更紧

参照项目里没有一个把「内部 RAM 预算」当成一等公民，因为它们的板子要么 PSRAM 充足、
要么根本不跑 LVGL。这块 P7 板子的真实顺序是：

```text
挂 SD + 开六个包   → 内部 RAM 97 KB → 44 KB
首屏构建（壁纸+页面+遮罩） → 44 KB → 6 KB
控制台任务（16 KB 栈）   → 失败
RPG 页（需要分配对象）    → 失败
```

失败路径还会在 `printf` 取 stdout 递归锁时踩坏启动状态，表现为「栈保护/abort」。
最后靠两件事归零：**控制台提到首屏之前**（抢在内存最充裕时）与**主任务栈 32 → 16 KB**
（主任务实测只用 3.8 KB，栈是静态占内部 RAM 的）。详见 `BOOT_OPTIMIZATION.md` §7。

这一条值得作为下一轮的第一原则：**在这个项目里，判断一项优化值不值得做，先问它
占不占内部 RAM，再问它省不省 CPU。**
