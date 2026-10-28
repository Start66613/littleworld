# P3 · 精灵渲染引擎重构（本轮核心）

这是 V4.5 唯一一项**已经实现、逐像素验证通过、可量化收益**的改动。
它同时解决 P0（渲染性能）与 P2（桌面双稳态）两个问题，因为二者根因相同。

---

## 0. 结论

| 项目 | 值 |
|---|---|
| 根因 | `lv_image_set_scale(288/282)` ≠ `LV_SCALE_NONE(256)` ⇒ 每帧走 `transform_and_recolor` 慢路径 |
| 解法 | 把 1.125× 离线烘进像素 + 用 clip 并集 bbox 裁掉 66.9% 的画布 |
| 宿主收益 | `male.idle` **5.54×**、`couple.seated` **7.13×**、`dog.idle` **2.21×** |
| 视觉代价 | **0**（237 帧逐像素 diff=0；156 张整页截图 diff=0） |
| 素材体积 | `male.lwp` 17.9 MB → **8.9 MB**（反而小了 50%） |
| 回退能力 | 有：`bbox` 全 0 = 无元数据 = V4.4 行为，由 `prescaled` 标志区分 |

---

## 1. 根因：一行 `set_scale`

### 1.1 调用点

`grep lw_sprite_create` 全仓，绘制 scale 共 7 处：

| 文件:行 | scale | 说明 |
|---|---|---|
| `lw_os.c:244` | 288 | 桌面两个角色 |
| `lw_os.c:544` | 288 | 锁屏 |
| `lw_os.c:882` | 288 | 开机 |
| `lw_rpg_view.c:256` | 288 | RPG |
| `lw_app_pet.c:190` | 282 | 宠物 couple |
| `lw_app_pet.c:194` | 256 | 宠物 dog |
| `lw_scene.c:73` | 调用方传入 | 通用 |

`lw_sprite.c:142` 的上限：`if (!parent || !scale || scale>320) return NULL;`
（注释：`User-approved V4.3 proportion correction, bounded to 1.25x`）。

⇒ **除 dog（256）外全部不是 256。**

### 1.2 LVGL 的分支

`firmware/managed_components/lvgl__lvgl/src/draw/sw/lv_draw_sw_img.c`：

```c
/* line 223-224 */
bool transformed = draw_dsc->rotation != 0 ||
                   draw_dsc->scale_x != LV_SCALE_NONE ||
                   draw_dsc->scale_y != LV_SCALE_NONE ? true : false;

/* line 254-273: 直通快速路径 */
else if(!transformed && !radius && cf == LV_COLOR_FORMAT_RGB565A8 &&
        draw_dsc->recolor_opa <= LV_OPA_MIN && draw_dsc->colorkey == NULL) {
    /* 一次 lv_draw_sw_blend，无临时缓冲 */
}

/* line 300-311: 慢路径 */
else if(LV_RESULT_INVALID == LV_DRAW_SW_IMAGE(transformed, cf, ...)) {
    transform_and_recolor(t, draw_dsc, decoder_dsc, sup, img_coords, clipped_img_area);
}
```

`transform_and_recolor()` 每帧：

1. `lv_malloc(8192)` 临时缓冲；
2. `lv_draw_sw_transform()` → `transform_rgb565a8()` 定点重采样；
3. 二次 `lv_draw_sw_blend()`；
4. `lv_free()`。

### 1.3 antialias 已经是关的（重要，纠正一个误判）

`lw_sprite.c:194` 的 `lw_sprite_set_pixel_mode()`：

```c
{ if (!s) return false; lv_image_set_antialias(s->image, false); return true; }
```

`transform_rgb565a8()`（`lv_draw_sw_transform.c:656-765`）里双线性分支是
`if(aa && ...)`（第 709-741 行）。**aa=false ⇒ 每像素只有 1 次源读 + 1 次 alpha 读**，
没有三次 `lv_color_16_16_mix`。

⇒ 我们付的是**结构性开销**（临时缓冲 + 二次 blend + 定点步进），不是采样密度。
这就是为什么「只去缩放、面积不变」（A→C）能有 3.93–4.49×。

---

## 2. 量化：66.9% 的画布是空白

### 2.1 工具

`tools/sprite_bbox_report.py`（116 行，只读）读真实 PNG 帧序列，
按 `ALPHA_MIN = 8`（与 `tools/build_character_pack.py` 同阈值）求每个 clip 的
**并集 alpha bbox**，再按绘制 scale 折算屏幕像素。

### 2.2 结果（`reports/perf-v4-5/sprite_bbox.json`）

| clip | 画布 | scale | 内容 bbox | 绘制尺寸 | 画布 px | 内容 px | 浪费 |
|---|---|---|---|---|---|---|---|
| male.idle | 256×256 | 288 | 105×208 | 118×234 | 82,944 | 27,612 | 66.7% |
| male.blink | 256×256 | 288 | 103×208 | 115×234 | 82,944 | 26,910 | 67.6% |
| male.wave | 256×256 | 288 | 120×208 | 135×234 | 82,944 | 31,590 | 61.9% |
| male.walk_down | 256×256 | 288 | 105×212 | 118×238 | 82,944 | 28,084 | 66.1% |
| male.walk_left | 256×256 | 288 | 103×210 | 115×236 | 82,944 | 27,140 | 67.3% |
| male.walk_right | 256×256 | 288 | 105×211 | 118×237 | 82,944 | 27,966 | 66.3% |
| male.walk_up | 256×256 | 288 | 103×208 | 115×234 | 82,944 | 26,910 | 67.6% |
| couple.seated | 320×320 | 282 | 206×229 | 226×252 | 123,904 | 56,952 | 54.0% |
| couple.wave | 320×320 | 282 | 250×226 | 275×248 | 123,904 | 68,200 | 45.0% |
| couple.rest | 320×320 | 282 | 208×229 | 229×252 | 123,904 | 57,708 | 53.4% |
| dog.idle | 320×320 | 256 | 54×47 | 54×47 | 102,400 | 2,538 | **97.5%** |
| dog.sleep | 320×320 | 256 | 61×28 | 61×28 | 102,400 | 1,708 | **98.3%** |

**汇总**：画布 1,157,120 px，内容 383,318 px，**浪费 66.9%**，潜力 **3.02×**。

### 2.3 逐帧 vs 并集：差 ≤2%

| clip | 帧数 | 并集 bbox px | 逐帧最大 | 比值 |
|---|---|---|---|---|
| male.idle | 8 | 21,840 | 21,424 | 1.02× |
| male.walk_down | 16 | 22,260 | 21,840 | 1.02× |
| couple.seated | 16 | 47,174 | 46,350 | 1.02× |
| dog.idle | 8 | 2,538 | 2,538 | 1.00× |

⇒ **按 clip 并集裁即可**，不需要每帧元数据。这决定了 trailer 的格式（每 clip 一条记录）。

---

## 3. RenderBench：六配置对照

### 3.1 工具

`host/render_bench.c`：同一 LVGL 渲染器、同一 1024×600 画布、同一真实素材，
只测「invalidate 精灵 + `lv_refr_now`」，5 次预热 + 40 次迭代，
`QueryPerformanceCounter` 计时。六个配置：

| 配置 | 含义 |
|---|---|
| A | 现状：全画布 + `set_scale(288)` |
| B | bbox 裁剪 + 缩放（`set_size(bbox)` + `set_offset`，仍 scale 288） |
| C | 全画布 1×（`set_scale(256)`，画布不裁） |
| D | bbox 裁剪 1× |
| E | 预缩放素材（烘焙 1×）+ bbox 裁剪 |
| F | **预缩放 + 预裁剪**（烘焙时已裁好，运行时 `set_scale(256)` + 无 offset） |

### 3.2 结果（`render_bench.txt`）

| clip | A | B | C | D | E | **F** |
|---|---|---|---|---|---|---|
| male.idle | 0.426 | 0.286 (1.49×) | 0.108 (3.93×) | 0.090 (4.72×) | 0.087 (4.87×) | **0.077 (5.54×)** |
| couple.seated | 0.651 | 0.438 (1.49×) | 0.145 (4.49×) | 0.105 (6.20×) | 0.115 (5.67×) | **0.091 (7.13×)** |
| dog.idle | 0.138 | 0.060 (2.31×) | 0.126 (1.09×) | 0.060 (2.28×) | 0.062 (2.23×) | **0.062 (2.21×)** |

逐像素 diff（vs A）：

| clip | B | C | D | E | F |
|---|---|---|---|---|---|
| male.idle | **0** | 15,304 | 15,304 | 3,923 | **0** |
| couple.seated | 144 | 37,961 | 37,961 | 15,048 | **0** |
| dog.idle | **0** | **0** | **0** | 606 | **0** |

### 3.3 三条结论

1. **A→C 只改了缩放，画布面积不变，却有 3.93–4.49×** ⇒ 瓶颈是缩放本身。
2. **A→B 只做 bbox 裁剪，仍有 1.49×**（`male.idle` 与 `dog.idle` 且 diff=0）
   ⇒ 面积也有价值，但只有 1.5×，**这正是 V4.4 计划的 C-4 上限**。
3. **F 是唯一同时满足「最快」与「diff=0」的配置**：
   - F 的 diff=0 不是巧合——它是构造出来的：把 A 实际画出的内容按 A 的定点算法
     拷进 F 的素材，所以 F 画的就是 A 画的。
   - `couple.seated` 的 B 有 144 px 差异（**几何不符**），说明「只改对象尺寸不改
     素材」在 282 这种非整数倍上会错位 ⇒ **B 形态不能直接上，必须烘焙**。
   - `dog.idle` 的 C/D 也是 diff=0，因为 scale 本来就是 256；但 C 的收益只有
     1.09×，证明「无缩放但面积大」仍然慢。

---

## 4. 实现

### 4.1 数据面：`lw_asset_spec_t` 扩展

`firmware/components/lw_ui/include/lw_assets.h`：

```c
typedef struct {
    const char *id;
    uint16_t width, height;        /* 画布尺寸（源图） */
    uint16_t frames;
    int16_t anchor_x, anchor_y;    /* 源图坐标系里的锚点 */
    lv_color_format_t format;
    uint16_t bbox_x, bbox_y, bbox_w, bbox_h;  /* 新增：内容边界，全 0 = 无元数据 */
    uint16_t prescaled;                        /* 新增：1 = 像素已按 scale 烘好 */
    lw_asset_read_fn read;
    void *context;
} lw_asset_spec_t;

typedef struct {
    uint16_t scale;      /* 传给 lv_image_set_scale（烘焙后 = 256） */
    uint16_t obj_w, obj_h;   /* 传给 lv_obj_set_size */
    int32_t offset_x, offset_y;  /* 传给 lv_image_set_offset_x/y */
    int32_t anchor_dx, anchor_dy; /* 锚点相对对象的偏移 */
} lw_asset_draw_t;

void lw_asset_draw_geometry(const lw_asset_spec_t *spec, uint16_t scale, lw_asset_draw_t *out);
```

`lw_assets.c` 的实现：

```c
static int32_t scale_px(int32_t v, uint16_t scale) { return (v * (int32_t)scale + 0x80) >> 8; }
static int32_t place_px(int32_t v, uint16_t scale) { return (v * (int32_t)scale) / 256; }

void lw_asset_draw_geometry(const lw_asset_spec_t *spec, uint16_t scale, lw_asset_draw_t *out) {
    bool have = spec->prescaled || (spec->bbox_w && spec->bbox_h);
    if (!have) {
        out->scale = scale;
        out->obj_w = spec->width; out->obj_h = spec->height;
        out->offset_x = out->offset_y = 0;
        out->anchor_dx = place_px(spec->anchor_x, scale);
        out->anchor_dy = place_px(spec->anchor_y, scale);
        return;
    }
    out->scale = spec->prescaled ? LV_SCALE_NONE : scale;
    out->obj_w = spec->prescaled ? spec->bbox_w : (uint16_t)scale_px(spec->bbox_w, scale);
    ...
}
```

**两个取整语义必须分清**（这是 `RenderBench` 踩出来的坑）：

- `scale_px(v,scale) = (v*scale + 0x80) >> 8` — LVGL 重采样用的**四舍五入**尺寸；
- `place_px(v,scale) = (v*scale) / 256` — 锚点落位用的**截断**尺寸。

混用会让锚点差 1 px。

### 4.2 呈现层：`lw_sprite.c`

`present()` 三级策略（V4.4 已有，V4.5 加了几何）：

```c
/* 1. 同帧早退 */
if (s->frame == frame && s->shown_id == clip->asset_id) return true;
/* 2. 同 asset，换帧：只 refill + invalidate */
if (s->shown_id == clip->asset_id) { lw_asset_refill(...); lv_obj_invalidate(s->image); return true; }
/* 3. 换 asset：set_src + apply_geometry + set_pos */
```

V4.5 的 `apply_geometry()`：

```c
static void apply_geometry(lw_sprite_t *s, const lw_asset_spec_t *spec) {
    lw_asset_draw_t g; lw_asset_draw_geometry(spec, s->scale, &g);
    lv_image_set_pivot(s->image, 0, 0);
    lv_image_set_scale(s->image, g.scale);
    if (has_geometry(spec)) {
        lv_obj_set_size(s->image, g.obj_w, g.obj_h);
        lv_image_set_offset_x(s->image, g.offset_x);
        lv_image_set_offset_y(s->image, g.offset_y);
    }
    s->anchor_dx = g.anchor_dx; s->anchor_dy = g.anchor_dy;
}
```

**关键 LVGL 事实**（`lv_image.c:882-892`）：`img->w/img->h` 缓存的是**源图尺寸**，
不是对象尺寸；`LV_IMAGE_ALIGN_CENTER`（默认）下对象尺寸只影响 `obj->coords`
（进而影响裁剪），`draw_dsc.image_area` 从 `obj->coords.x1,y1` 起算，
唯一能平移绘制落点的是 `lv_image_set_offset`。

### 4.3 容器格式：LWPX geometry trailer

`firmware/components/lw_ui/include/lw_pack.h`：

```c
#define LW_PACK_TRAILER_MAGIC "LWTR0001"
#define LW_PACK_HEADER_BYTES  12u
#define LW_PACK_RECORD_BYTES  10u
#define LW_PACK_TRAILER_NONE  0u
#define LW_PACK_TRAILER_GEOMETRY 1u
```

- **header 偏移 +14 的 u16 是开关**（0 = legacy，无 trailer）；
- trailer = `"LWTR0001"` + `u16 clips` + `u16 reserved`，后接每 clip
  `u16 bbox_x, bbox_y, bbox_w, bbox_h, prescaled`（10 B）；
- 解析时拒绝越界 bbox 与未知 kind；clip 偏移校验上界从 `file_size` 改为 `payload_end`。

**踩坑（必须记住）**：trailer 头第 2 个 u16 是 **clip 数量**，不是 kind；
kind 在 header `+14`。读错会让整包被拒收，表现为「6 个测试失败」而不是「解析失败」。

### 4.4 烘焙：`tools/lwp_prescale.py`

核心是 `crop_axis()`：**从整画布 lattice 取窗口**。

```python
def crop_axis(canvas_len, src_lo, src_len, scale, mode):
    full_dest = max(1, scaled(canvas_len, scale))
    full = map_axis(0, canvas_len, full_dest, scale, mode)
    first = last = None
    for d, src in enumerate(full):
        if src_lo <= src <= src_lo + src_len - 1:
            if first is None: first = d
            last = d
    ...
    return full[first:last+1], first
```

**两次失败的教训**：

1. 第一版每个 bbox 单独建 lattice（锚在 bbox 原点）⇒ 错。因为 LVGL 的 `step`
   由**整个目标宽度**推导（`lv_draw_sw_transform.c:191-219`），子窗口独立重采样
   会选到不同的源列。表现：`male.idle` 2704 px 差异、worst delta 121。
2. 第二版窗口尺寸用 `scaled(src_len)`、起点用 `scaled(src_lo)` ⇒ 在取整边界差 1 px。
   表现：`qianqian-base sleep_turn` frame 1 差 14 px、worst 118（丢最后一列）。

实测正确结果：`male` bbox `(60,17,121,212)` ⇒ `dx0=67, dest_w=137, dy0=19,
dest_h=239, ax=77, ay=234`（注意 `dx0=67` 而不是 `scaled(60)=68`）。

---

## 5. 验证

### 5.1 逐帧逐像素：237 帧 / 0 差异

`host/prescale_verify.c` 用同一个 LVGL 渲染器、同一画布位置，分别绘制
「原包 + scale 288」与「烘焙包 + scale 256」，逐像素比对：

| 包 | 原始 | 烘焙后 | 画布 | 帧数 | 差异 |
|---|---|---|---|---|---|
| `male.lwp` | 17,892,100 B | **8,939,693 B** | 137×239 | 137 | **0** |
| `qianqian-base.lwp` | 22,218,124 B | **19,098,788 B** | 246×229 | 100 | **0** |
| `qianqian-wave.lwp` | 3,145,852 B | **1,425,746 B** | — | — | **0** |
| `daily.lwp` | 18,125,680 B | **12,311,676 B** | 276×252 | — | **0** |
| **合计** | | | | **237** | **0** |

帧字节：`male` 196,608 → **97,512 B**（-50.4%）。

### 5.2 整页截图：156/156 逐像素一致

`tools/shot_diff.py` 比对烘焙包跑出的 156 张整页截图 vs HEAD：

```
compared 156 files: 156 identical, 0 differ, 0 missing on the right
```

（`reports/perf-v4-5/baked_vs_head.json`）

### 5.3 测试矩阵

| 套件 | 结果 |
|---|---|
| `run_host_tests.ps1` | 142 checks / 0 failure |
| `run_core_regression.ps1` | Duo 82 / RPG 336 / Storage 33 — PASSED |
| `run_v4_resource_tests.ps1` | 261 checks / 0 failure |
| `--os-v2-test` 整页 | 752 checks / 1 failure（既有失败，HEAD 相同） |
| 固件构建 | `little_world.bin` 0x240470（2,359,408 B），分区余 62% |

### 5.4 三个被修掉的真实 bug（回归过程中发现）

1. **`host/pixel_pack.c` 完全没解析 geometry trailer** ⇒ 烘焙包被当未烘焙，
   仍按 288 缩放 ⇒ 角色画大 1.125×。视觉对比确认；平移搜索最佳 `dx=0 dy=-16`，
   mean abs diff 26.48 vs 无位移 35.22 ⇒ **是尺寸不是位移**。
2. **`read_trailer()` 把 trailer 头第 2 个 u16 当 kind**（实际是 clip 数量）
   ⇒ `male.lwp` 被整体拒收，表现为 6 个测试失败。
3. **`tests/host/test_lw_v4_resources.c:615-616` 的旧期望**
   `legacy draws scaled canvas`（`obj_w==288`）与修正后的无元数据语义矛盾
   ⇒ 改为 `legacy keeps canvas-sized object`（256×256）、`legacy applies no offset`。

---

## 6. 为什么这也解决了 P2（双稳态）

V4.4 遗留的未解现象：桌面/世界页在 21 ms 与 116 ms 之间成串跳变，两页同步，
计数器冻结，一次 `render()` 可重置。

**机制**：慢路径成本正比于 `clipped_img_area`（脏区 ∩ 变换后区域）。

- 两个角色**靠近**时，两个精灵的脏区在 `lv_invalid_area` 里合并成一大块，
  `transform_and_recolor` 对合并后的大块做一次昂贵重采样；
- 两个角色**分开**时，各自小块，成本线性叠加但总量小；
- 世界页共用 `actors()` / `update_actors()` ⇒ 同步跳变；
- 一次 `render()` 让角色回到默认位置（`x=450+i*196`，间隔 196 px）⇒ 分开 ⇒ 32 ms。

**不是泄漏、不是缓存、不是脚本错标。** 已逐项排除：`asset_leases` 恒 3、
`cache_resident` 恒 8,146,944、`cache_evictions` 冻结 2042、精灵数恒 2。

**F 形态把两态都压进 20 ms 档**，因为烘焙后 `scale == 256` 走直通路径，
成本不再随脏区形状变化 ⇒ P0 与 P2 是同一个解。

---

## 7. 未完成项与风险

| 项 | 状态 | 风险 |
|---|---|---|
| 固件烧录验证 | ⬜ 需用户明确授权 | 板子当前未连接 |
| `skin.lwp` / `ui.lwp` 烘焙 | ⬜ **不应烘焙** | 它们是切片图集（`lw_skin.c` 按 rect 切九宫格），烘焙会破坏切片 |
| RPG 点击回归 | ⬜ 待真机 | `LV_EVENT_HIT_TEST` 在对象尺寸 ≠ 图像尺寸时退回矩形命中（`lv_image.c:760-767`），hit box 是独立对象所以不受影响，但需实测 |
| 内存峰值 | ✅ 已测 | `--os-v2-test` 报 `peak pixels 1,999,872 B`，烘焙后每帧更小 |

### 7.1 为什么 `skin.lwp` 与 `ui.lwp` 不烘焙

- `skin.lwp`（512×544）被 `lw_skin.c` 当作**图集**用：`rects[SK_COUNT]` 17 个矩形，
  每个 piece 从图集里切一块出来，用 `lv_image_set_scale_x/y` 放大到目标尺寸。
  烘焙会改变整幅图集的坐标，切片全部错位。
- `ui.lwp`（544×352）同理，是 `v42ui.sheet` 7 帧的图标表。

它们的优化路径是另一条（机制 5：合并 9 个 piece），本轮未做。
