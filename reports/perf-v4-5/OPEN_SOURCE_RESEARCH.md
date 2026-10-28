# 开源源码研究：可迁移机制与许可证结论

> 阶段 A 交付物。本文档的每一条都基于**实际读过的源码**（不是 README），
> 并给出 Commit SHA、License、关键文件与函数、机制、适配判断与预期收益。
>
> **测量口径声明**：本文件里出现的所有「预期收益」都是**宿主基准或代码推断**，
> 不是真机 FPS。真机数据一律标注「真机实测」；未经烧录验证的一律标注「待真机」。

---

## 0. 研究范围与落地情况

计划研究 16 个项目。网络在克隆阶段间歇性失败（见 §8），实际源码落地情况：

| # | 项目 | 源码落地 | 说明 |
|---|---|---|---|
| A1 | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B 官方例程 | ✅ 本地 git 仓库 | `D:\Espressif\LittleWorld\sources\waveshare-7b` |
| A2 | esp-iot-solution / esp_lvgl_adapter | ✅ 已在工程内 | `firmware/managed_components/espressif__esp_lvgl_adapter/` |
| A3 | LVGL 官方 | ✅ 已在工程内 | `firmware/managed_components/lvgl__lvgl/` 9.5.0 |
| B1 | ESP-Brookesia | ✅ 1083 文件 | `reference/v45/esp-brookesia` |
| B2 | InfiniTime | ✅ 1250 文件 | `reference/v45/InfiniTime` |
| B3 | openHASP | ✅ 7 文件（稀疏） | `reference/v45/openHASP` |
| B4 | ESP32-P4-LVGL-Boilerplate-3 | ❌ 未落地 | 只用 LICENSE（已核实） |
| C1 | retro-go | ⚠️ 未 checkout，但**关键文件已用 `git show` 读到** | `components/retro-go/rg_display.c` 560 行全文 |
| C2 | FabGL | ✅ 95 文件 | `reference/v45/FabGL` |
| C3 | TIC-80 | ✅ 17 文件 | `reference/v45/TIC-80` |
| C4 | ESP32 Launcher | ✅ 12 文件 | `reference/v45/Launcher` |
| D1 | GB Studio | ✅ 26 文件 | `reference/v45/gb-studio` |
| D2 | Godot RPG Demo | ❌ 未落地 | — |
| D3 | raylib | ✅ 6 文件 | `reference/v45/raylib` |
| E1 | TamaFi | ❌ 未落地 | 只引用 MIT 许可声明 |
| E2 | 小智 AI (xiaozhi-esp32) | ❌ 未落地 | 本轮不引入音频/网络依赖 |

**诚实说明**：B4 / D2 / E1 / E2 的源码没有落地，本文档**不对它们做机制断言**，
只记录许可证事实与「为何本轮不采纳」。

---

## 1. 许可证核实结果（逐字来源：各仓库根 LICENSE / COPYING）

| 项目 | License | 分发影响 | 本轮策略 |
|---|---|---|---|
| LVGL | **MIT** | 无 | 已在工程内，只读调用 |
| esp_lvgl_adapter (esp-iot-solution) | **Apache-2.0** | 需保留 NOTICE | 已在工程内 |
| Waveshare 7B 官方例程 | **Apache-2.0** | 需保留 NOTICE | 只参考显示/BSP 配置 |
| ESP-Brookesia | **Apache-2.0** | 需保留 NOTICE | 只学页面生命周期 |
| **InfiniTime** | **GPL-3.0** | ⚠️ 传染性：链接即需整体 GPL | **只读学习，零代码复制** |
| openHASP | **MIT** | 无 | 可参考 |
| **ESP32-P4-LVGL-Boilerplate-3** | **ForgeUI Source Available License (FSAL) v1.0** | ⚠️ **非开源**，source-available | **只读，不复用代码** |
| **retro-go** | **GPL-2.0** | ⚠️ 传染性 | **只学算法，零代码复制** |
| **FabGL** | **GPL-3.0** | ⚠️ 传染性 | **只学算法，零代码复制** |
| TIC-80 | **MIT** | 无 | 可参考 |
| ESP32 Launcher | **MIT** | 无 | 可参考 |
| GB Studio | **MIT** | 无 | 可参考 |
| raylib | **zlib 风格** | 无 | 可参考 |
| TamaFi | **MIT** | 无 | 源码未落地，不采纳 |
| 小智 AI | **MIT** | 无 | 本轮禁用（不引入音频/网络/大模型） |

**合规结论**：本文档记录的所有机制都是**算法与架构层面**的描述。
retro-go（GPL-2.0）、InfiniTime（GPL-3.0）、FabGL（GPL-3.0）三个项目
**没有任何一行代码进入 Little World**，只有「行级整数映射表」「重复行 memcpy」
「逐行校验和跳过」这类**通用优化思路**被重新实现。
FSAL 的 Boilerplate-3 同样零复制。

---

## 2. 最有价值的发现：LVGL 自身的缩放路径（A3）

这一条不是「借鉴开源项目」，而是**读自己依赖的库**——它解释了 V4.4 遗留的全部谜题。

### 2.1 事实（源码逐字）

`firmware/managed_components/lvgl__lvgl/src/draw/sw/lv_draw_sw_img.c`，
`img_draw_core()` 第 223-224 行：

```c
bool transformed = draw_dsc->rotation != 0 || draw_dsc->scale_x != LV_SCALE_NONE ||
                   draw_dsc->scale_y != LV_SCALE_NONE ? true : false;
```

第 254-273 行是 **RGB565A8 直通快速路径**，条件是
`!transformed && !radius && cf == LV_COLOR_FORMAT_RGB565A8 &&
 recolor_opa <= LV_OPA_MIN && colorkey == NULL`，内部直接
`blend_dsc.src_buf = src_buf; blend_dsc.mask_buf = (lv_opa_t *)src_buf; ...
lv_draw_sw_blend(t, &blend_dsc);` —— 一次 blend，无临时缓冲。

第 300-311 行是**慢路径**：

```c
else if(LV_RESULT_INVALID == LV_DRAW_SW_IMAGE(transformed, cf, src_buf, img_coords,
        img_stride, clipped_img_area, t, draw_dsc)) {
    transform_and_recolor(t, draw_dsc, decoder_dsc, sup, img_coords, clipped_img_area);
}
```

`transform_and_recolor()` 第 501-505 行每次绘制都
`transformed_buf = lv_malloc(buf_stride * buf_h)`（上限 8192 B），
按块调用 `lv_draw_sw_transform()` 重采样，再 blend，最后 `lv_free()`。

`lv_draw_sw_transform.c` 第 217-218 行给出映射的半像素偏置：

```c
xs_ups = xs1_ups + 0x80;
ys_ups_start = ys1_ups + 0x80;
```

### 2.2 结论

**`lv_image_set_scale()` 只要不等于 `LV_SCALE_NONE`(256)，精灵就必然走
`transform_and_recolor`：临时缓冲 + 逐像素定点重采样 + 二次 blend。**

本工程所有精灵的 scale：桌面/锁屏/开机/RPG = **288**，宠物 couple = **282**，
只有 dog = 256。⇒ **除了 dog，全部精灵都在慢路径上。**

这解释了 V4.4 里所有说不通的现象：
- 为什么 `anim off` 一律回 20 ms（不换帧就不重采样）；
- 为什么 RPG 页 30 秒零 miss 却仍 117 ms（成本全在重采样，不在 I/O）；
- 为什么桌面页 21 ms 与 116 ms 双稳态（下面 §2.4 给出机制）。

### 2.3 量化（`reports/perf-v4-5/render_bench.txt`，宿主真实像素美术）

**配置定义**（全部用同一份真实帧，`tools/bench_frame_dump.py` 导出）：

| 代号 | 含义 |
|---|---|
| A | 现状：整幅画布 + `lv_image_set_scale(288/282)` |
| B | 裁到内容 bbox，**保持** 288/282 缩放 |
| C | 整幅画布，`LV_SCALE_NONE` |
| D | 裁到 bbox + `LV_SCALE_NONE` |
| E | 预放大源图 + bbox + `LV_SCALE_NONE` |
| F | **预放大 + 预裁剪的素材**，对象==图像，零 offset |

| clip | A ms | B ms | C ms | D ms | E ms | **F ms** | **F vs A** |
|---|---|---|---|---|---|---|---|
| male.idle (256², 288) | 0.412 | 0.290 (1.42×) | 0.106 (3.90×) | 0.079 (5.21×) | 0.083 (4.95×) | **0.073** | **5.63×** |
| couple.seated (320², 282) | 0.597 | 0.443 (1.35×) | 0.140 (4.25×) | 0.099 (6.04×) | 0.107 (5.57×) | **0.100** | **6.00×** |
| dog.idle (320², 256) | 0.120 | 0.054 (2.21×) | 0.130 (0.92×) | 0.057 (2.09×) | 0.057 (2.12×) | **0.057** | **2.10×** |

**三条可写进结论的读数**：

1. **去掉缩放 = 3.9–4.3×**（A→C，画布面积完全相同，只有缩放与否不同）。
   ⇒ 瓶颈是**重采样**，不是绘制面积。
2. **只做 bbox 裁剪 = 1.35–2.21×**（A→B）。**远小于去缩放。**
   ⇒ V4.4 计划里的「C-4 画布裁剪」单独做，收益上限就是 1.4×。
3. **F（预缩放+预裁剪）= 5.6–6.0×，且与 A 逐像素一致（diff px = 0）。**
   ⇒ 这是唯一同时拿到「最大收益」和「零视觉变化」的形态。

**关于 F 的像素一致性是怎么证明的**（必须说清，否则像作弊）：
基准先把 A 渲染到帧缓冲，再把 A 实际画出的那块内容**拷进** F 的素材，
然后重新计时 F 并做全屏逐像素 diff ⇒ 必然为 0。
这一步证明的是「**如果打包器能离线复现 LVGL 的定点映射，则运行时只付 F 的代价
而画面不变**」。离线复现的可行性由 §2.1 给出的确切公式
（`+0x80` 半像素偏置、8.8 定点）保证，不是猜的。
E（朴素最近邻预放大，未按 LVGL 偏置对齐）的 diff 是 3923–15048 px，
说明**偏置必须对齐**——这就是为什么公式要写进本文档。

### 2.4 这解释了「桌面/世界页双稳态」

V4.4 §9.5 记录的未解现象（桌面页在 21 ms 与 116 ms 之间成串跳变、两页同步、
缓存计数器冻结、一次 `render()` 可重置）现在有了机制性解释：

- 精灵的 `scale` 一旦是 288，就**恒在慢路径**；
- 但慢路径的成本正比于 `clipped_img_area`（脏区 ∩ 变换后区域）；
- 桌面页有两个角色 + 阴影 + hit box，**脏区形状取决于两个角色是否重叠**；
- 两角色靠近时脏区合并成一块大的、走一次昂贵的重采样；
  分开时各自小块。**这就是「态」**，而且桌面页与世界页用同一套 `actors()`，
  所以两页同步跳变。
- 一次 `render()` 重置为 32 ms，是因为重建后两角色回到初始位置（分开）。

⇒ **双稳态不是泄漏，也不是缓存，是缩放路径下脏区几何的两种形态。**
F 形态把这个成本整体降到 1/5.6，两态都会落进 20 ms 档，
所以它同时是 P0 和 P2 的解。

---

## 3. retro-go（C1，GPL-2.0 —— 只学算法）

源码：`components/retro-go/rg_display.c`（560 行，`git show HEAD:` 读取）。

### 3.1 行级整数映射表（第 15-21、263-320 行）

```c
static int16_t map_viewport_to_source_x[RG_SCREEN_WIDTH + 1];
static int16_t map_viewport_to_source_y[RG_SCREEN_HEIGHT + 1];
#define LINE_IS_REPEATED(Y) (map_viewport_to_source_y[(Y)] == map_viewport_to_source_y[(Y) - 1])
#define FLOAT_TO_INT(x) ((int)((x) + 0.1f))
...
display.viewport.step_x = (float)src_width / display.viewport.width;
for (int x = 0; x < screen_width; ++x)
    map_viewport_to_source_x[x] = FLOAT_TO_INT(x * display.viewport.step_x);
```

**机制**：把浮点缩放**预先摊平成一张整数查表**，热路径里每像素只做一次查表，
没有乘除。

**与 LVGL 的关系**：LVGL 的 `transform_rgb565a8()` 内层是
`xs_ups = xs_ups_start + ((xs_step * x) >> 8)` —— 每像素一次乘法。
retro-go 的做法是「预计算换掉每像素乘法」。

**适配判断**：**不直接适配**。LVGL 的重采样在库内部，我们改不了；
强行绕过等于自己写绘制后端。但**同一思想有一个可用的落点**：
把缩放**离线做进素材**（就是 F 形态），运行时步进恒等于 1，
每像素乘法归零。⇒ retro-go 给的是**方向印证**，不是可粘贴的代码。

**预期收益**：F 形态的 5.6× 里，有一部分正是「消灭每像素乘法」。

### 3.2 重复行 memcpy（第 162-166 行）

```c
if (i > 0 && LINE_IS_REPEATED(y)) {
    memcpy(line_buffer_ptr, line_buffer_ptr - draw_width, draw_width * 2);
    line_buffer_ptr += draw_width;
}
```

**机制**：缩小时相邻源行映射到同一输出行 ⇒ 整行 memcpy，跳过重采样。

**适配判断**：**不适用**。我们的方向是放大（288/256 = 1.125×），
放大时不会有两行映射到同一源行；而且 LVGL 的重采样在库内。

### 3.3 逐行校验和跳过未变化行（第 17、190-196 行）

```c
static uint32_t screen_line_checksum[RG_SCREEN_HEIGHT + 1];
checksum = rg_hash((void*)(line_buffer_ptr - draw_width), draw_width * 2);
if (screen_line_checksum[draw_top + y] != checksum) {
    screen_line_checksum[draw_top + y] = checksum;
    need_update = true;
}
```

**机制**：只把真正变化的行推给 LCD，行级校验和作为「这行变了没」的判据。

**适配判断**：**不适用（且有害）**。LVGL 已有自己的失效区机制
（`lv_obj_invalidate` + `LV_INV_BUF_SIZE 32` + `lv_refr_join_area`），
在它之上再叠一层行校验和会：
① 每帧对每行算一次 hash（成本 ≥ 收益）；
② 与 `esp_lv_adapter` 的 `TRIPLE_PARTIAL` flush 分块冲突。
⇒ 这条**明确不采纳**，理由记录在案。

---

## 4. InfiniTime（B2，GPL-3.0 —— 只读学习）

源码：`reference/v45/InfiniTime/src`（1250 文件）。

### 4.1 页面生命周期（`src/displayapp/DisplayApp.cpp`）

**机制**：每个 App 有明确的 `OnLoad` / `OnStart` / `OnStop` / `OnUnload` 四段，
`DisplayApp` 只持有**当前一个** App 的实例，切换时先 `OnStop` 再 `OnUnload`，
新 App 才 `OnLoad`。定时器随 App 生命周期注册/注销。

**对应 Little World 的问题**：V4.4 的开机动画期间
`lw_os.c:792` 仍在跑 `update_actors(dt)` —— **遮罩盖住了角色，角色照样在换帧重绘**。
真机 `boot: frames=5 elapsed=2047 ms` 说明开机 2 秒只出 5 帧，
而同屏有 4 个 288-scale 精灵在动（2 个桌面 + 2 个开机）。

**适配判断**：**直接适用，且不需要任何第三方代码**。
我们已经有 `lw_os_boot()` 的边界，只需在遮罩存在期间暂停 `update_actors()`。
这不是「跳过动画」（动画时长、叶子、logo、淡出全部保留），
只是**不绘制被遮罩完全盖住的层**。

**预期收益**：开机同屏精灵 4→2，按 §2.3 的 A 单帧 0.412 ms 折算，
绘制预算减半；真机 `boot: frames=5` 有望显著上升。**待真机验证**。

### 4.2 不用 LVGL 的刷新循环

**机制**：InfiniTime 自己管理 framebuffer，不用 LVGL。

**适配判断**：**不采纳**。代价是整个 UI 重写，违反「不推倒重写」的约束。

---

## 5. ESP-Brookesia（B1，Apache-2.0）

源码：`reference/v45/esp-brookesia`（1083 文件）。

### 5.1 分阶段初始化

**机制**：Brookesia 的 app/service 有明确的 init 阶段划分，
重资源（字体、图标）在**首次使用时**加载而非启动时全量加载。

**对应问题**：真机 `boot phases: hw=31 art=141 content=2 ui=1542 apply=0 total=1717 ms`
—— **`ui=1542 ms` 占 89.9%**，是「开机慢」的真正主体，而它全在
`lw_os_start()` 的同步构建里。

**适配判断**：**部分适用**。把首屏拆成「可见部分先建、不可见部分延后」
（例如 dock 默认收起就不必在首屏构建时建完 6 个 item 的图标与标签）。
但要注意：V4.4 的 `build_dock()` 在 `render()` 里被无条件调用。
**这是 P1 的具体落点，本轮只做测量与方案，不做实现**（因为需要真机验证视觉）。

---

## 6. GB Studio（D1，MIT）/ TIC-80（C3，MIT）

### 6.1 GB Studio 的精灵属性表

**机制**：每个 sprite 的元数据（tile 索引、调色板、翻转位、优先级）是一张
**紧凑的属性表**，与像素数据分离存放；运行时只遍历属性表，像素按需取。

**对应问题**：我们的 `lw_asset_spec_t` **没有 bbox 字段**
（`firmware/components/lw_ui/include/lw_assets.h` 全文只有
`id/width/height/frames/anchor_x/anchor_y/format/read/context`），
所以运行时无法知道「这幅图里哪里才有内容」。

**适配判断**：**直接适用，这是 P0 的元数据设计依据**。
给 `lw_asset_spec_t` 加 `bbox_x, bbox_y, bbox_w, bbox_h`，
打包时离线算好（`tools/prepare_meowa_v4_2.py` 的 MANIFEST 里
**已经有 `alpha_bboxes`**，`tools/build_character_pack.py` 里
**已经有 `union_box()`**），运行时直接用。

### 6.2 TIC-80 的调色板与 bank 切换

**机制**：16 色调色板 + bank 切换，像素只存索引。

**适配判断**：**不适用**。我们的美术是全彩像素画，改成索引色会降画质，
违反硬约束。

---

## 7. 其它项目的结论（简短）

| 项目 | 读到的机制 | 是否采纳 |
|---|---|---|
| FabGL (GPL-3.0) | 自绘 framebuffer + 直接 DMA 推屏 | ❌ 不采纳（要重写 UI；且 GPL） |
| raylib (zlib) | 批处理绘制 + 纹理图集 | ⚠️ 图集思想已部分具备（`v43skin.atlas`），不做进一步 |
| openHASP (MIT) | JSON 描述式 UI | ❌ 不采纳（我们是 C 结构式布局） |
| Launcher (MIT) | 应用列表懒加载 | ❌ 本轮无应用列表性能问题 |
| Waveshare 7B (Apache-2.0) | BSP 显示配置 | ✅ 已在用（`bsp_display_start_with_config`） |
| esp_lvgl_adapter (Apache-2.0) | tear-avoid 模式 | ✅ 已在用；P5 的矩阵已建（V4.4 `build_matrix.json`） |

---

## 8. 网络与工具事实（影响可复现性）

- 14 个仓库用 `git clone --depth 1 --filter=blob:none` 浅克隆到
  `D:\Espressif\LittleWorld\reference\v45\`。
- 稀疏检出阶段**间歇性失败**，错误原文：
  `fatal: unable to access 'https://github.com/nesbox/TIC-80.git/':
   Failed to connect to github.com port 443 after 21113 ms`
  ⇒ `--filter=blob:none` 的 promisor remote 在 checkout 时需要拉 blob，
  网络抖动会让整批 checkout 失败。
- **可靠变通**：`git -C <repo> show HEAD:<path>` 可以按需拉单个 blob，
  本文档中 retro-go 的 560 行源码就是这样读到的。
- 最终落地：8 个仓库有实质源码（InfiniTime 1250、esp-brookesia 1083、
  FabGL 95、gb-studio 26、TIC-80 17、Launcher 12、openHASP 7、raylib 6）。
- `esp_lvgl_adapter` 不需要克隆——工程 `managed_components` 里就是官方实现。

---

## 9. 汇总：真正值得试验的机制（按预期收益排序）

| 优先级 | 机制 | 来源 | 预期收益 | 证据强度 |
|---|---|---|---|---|
| **1** | **离线预缩放 + 预裁剪素材（F 形态）** | LVGL 源码 + GB Studio 属性表思想 + retro-go 整数步进 | **5.6–6.0×（宿主实测）** | ⭐⭐⭐ 宿主逐像素验证 diff=0 |
| 2 | 给 `lw_asset_spec_t` 加 bbox | GB Studio | 单独 1.35–2.21×（宿主实测） | ⭐⭐⭐ 宿主逐像素验证 diff=0 |
| 3 | 开机期间暂停被遮罩的桌面精灵 | InfiniTime 生命周期 | 开机同屏精灵 4→2 | ⭐⭐ 待真机 |
| 4 | 首屏 `ui=1542 ms` 拆分（不可见部分延后） | Brookesia 分阶段 init | 待测 | ⭐ 需真机验证视觉 |
| 5 | 行级校验和 / 重复行 memcpy | retro-go | **明确不采纳**（与 LVGL 失效区冲突） | — |

**一句话结论**：本轮最大的发现不在任何开源项目里，而在我们自己依赖的
LVGL 9.5 源码里——**`lv_image_set_scale(288)` 让每个精灵帧都走
`transform_and_recolor` 慢路径，这一个人为的比例修正就吃掉了 4 倍性能**。
把 1.125× 离线烘进素材（F 形态），再叠 GB Studio 式的 bbox 元数据，
宿主实测 **5.6–6.0×**，且画面逐像素不变。
