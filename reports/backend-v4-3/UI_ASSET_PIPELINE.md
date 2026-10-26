# UI 资产管线（P0）

分支 `agent/dsh-v4-3-engine`。把 Codex 的 PNG 变成设备能画的 `.lwu`，
并解决"九宫格拉伸必然变形"这个像素风 UI 的根本问题。

## 1. 为什么不能直接用 LVGL 的图片缩放

LVGL 9.5 **没有**内置 nine-slice / 9-patch。全库搜索 `nine` / `9slice` /
`NINE_PATCH` 无命中。可用的是：

- `lv_image_set_scale()` / `scale_x` / `scale_y`：缩放**整张**位图；
- `lv_image_set_pivot()` / `set_rotation()`；
- `lv_image_set_antialias(obj, bool)`：关闭即最近邻，像素风不会被插值糊掉；
- `LV_IMAGE_ALIGN_STRETCH/TILE/CONTAIN/COVER`；
- `LV_OBJ_FLAG_OVERFLOW_VISIBLE`（默认裁剪子对象，可做遮罩）。

问题：`lv_image_set_scale` 缩放整张图。一个 32×32 的圆角面板拉到 300×180，
**四角会变成椭圆、1 像素边框会变成 6 像素**。这是像素风 UI 最典型的"糊"。

## 2. 九宫格：切九块，各拉各的

```
+----+------------------+----+
| 0  |        1         | 2  |   角：原尺寸，永不缩放
+----+------------------+----+
| 3  |        4         | 5  |   边：只沿一个轴拉伸
+----+------------------+----+
| 6  |        7         | 8  |   中心：两轴拉伸
+----+------------------+----+
```

`firmware/components/lw_engine/include/lw_ui9.h` 只算矩形，不碰 LVGL：

```c
lw_ui9_status_t lw_ui9_slice(uint16_t src_w, uint16_t src_h,
                             const lw_ui9_insets_t *insets,
                             int16_t x, int16_t y, uint16_t target_w, uint16_t target_h,
                             lw_ui9_rect_t src[LW_UI9_PIECES],
                             lw_ui9_rect_t dst[LW_UI9_PIECES]);
```

拒绝条件（都是真实的素材错误）：

| 状态 | 含义 |
| --- | --- |
| `LW_UI9_BAD_SOURCE` | 源宽或高为 0 |
| `LW_UI9_BAD_INSETS` | `left+right >= width` 或 `top+bottom >= height`（没有中心可拉） |
| `LW_UI9_BAD_TARGET` | 目标小于两个角之和 |
| `LW_UI9_OVERFLOW` | 算出的矩形超出 int16 |

`lw_ui9_minimum()` 给出"不破坏角的最小目标尺寸"= `left+right+1` / `top+bottom+1`。
测试里有一个用例专门锁这个：24×24 面板、insets 6,6,6,6 → 最小 13×13（不是 12）。

## 3. 为什么必须 repack（这是最容易做错的地方）

`.lwu` 的像素格式是 **RGB565 或 RGB565A8**。RGB565A8 的布局是：

```
[ 整张图的 RGB565 平面 ][ 整张图的 A8 平面 ]
```

LVGL 的 `lv_draw_buf` 计算尺寸时就是这么算的
（`lv_draw_buf.c:668`：`if(cf == LV_COLOR_FORMAT_RGB565A8) size += (stride / 2) * h;`）。

**后果**：九宫格的一个子矩形**不能**用"指针偏移"来描述——它的 alpha 像素不在
颜色像素后面 2 字节处，而在整张图的 alpha 平面里。所以每个切片必须**重新
打包**成自己的连续缓冲区：

```c
size_t lw_ui_asset_copy_piece(const lw_ui_asset_t *asset, unsigned state,
                              const lw_ui9_rect_t *src, uint8_t *dst, size_t cap);
```

实现（`firmware/components/lw_engine/src/lw_ui_asset.c` 末尾）：

```c
const uint8_t *colour_plane = asset->payload + (size_t)state * image_bytes;
const uint8_t *alpha_plane  = colour_plane + image_w * image_h * 2;
/* 逐行拷颜色，再逐行拷 alpha */
```

测试 `test_ui_asset_copy_piece` 逐像素比对：RGB565 16×16 的 4×4 窗口、
RGB565A8 12×12 三段按钮的第 1 段右下 2×2 角（颜色 **和** alpha 都比），
外加六个失败用例（右越界、下越界、缓冲区不足且**不写入任何字节**、
非法 state、NULL）。

## 4. 容器格式 `.lwu`

32 字节小端头 + 像素载荷：

| 偏移 | 类型 | 字段 |
| --- | --- | --- |
| 0 | char[8] | magic `"LWUI0001"` |
| 8 | u16 | width（单个状态段） |
| 10 | u16 | height（单个状态段） |
| 12 | u8 | kind：0 图标 / 1 面板 / 2 按钮 |
| 13 | u8 | format：0 RGB565 / 1 RGB565A8 |
| 14 | u16 | states（按钮必须 3，其余 1） |
| 16/18/20/22 | u16 | insets L/T/R/B |
| 24 | u16 | flags，bit0 = 像素风（必须最近邻） |
| 26 | u16 | reserved，必须 0 |
| 28 | u32 | data_bytes |

载荷 = `states × width × height × (2 或 3)`，颜色平面在前、alpha 平面在后。

**为什么不用 JSON sidecar**：设备端没有 JSON 解析器，而且"清单与文件不一致"
会变成运行期惊喜。一个头、一个文件，解析失败就是失败。

拒绝状态：`TOO_SMALL` / `BAD_MAGIC` / `BAD_KIND` / `BAD_FORMAT` / `BAD_SIZE` /
`BAD_STATES` / `BAD_INSETS` / `BAD_PAYLOAD` / `BAD_RESERVED`。

## 5. PNG → `.lwu`

`tools/build_ui_assets.py`：

```powershell
python tools\build_ui_assets.py --source assets\ui --out dist\littleworld-v4-sd\littleworld\ui
```

- 按**文件名**判种类：`icon-*` / `panel-*` / `button-*`；
- 九宫格参数来自同名 `.json`（`inset_left/top/right/bottom`、`pixel_art`），
  以 `utf-8-sig` 读取（Excel/记事本存的 BOM 不会破坏解析）；
- 按钮图必须**竖排三段等高**，高度不能被 3 整除直接拒绝；
- `--check` 只校验不写盘；`--json` 输出机器可读结果。

转换后**用固件解析器验收**：

```
lw_engine_tests.exe --check-asset <file>
```

这是 `tests/host/test_lw_engine.c` 的 CLI 模式，链接的是真实 `lw_ui_asset.c`。
Python 侧的"我自己检查过了"不算数。

实测三个 fixture：

| 文件 | 大小 | 几何 |
| --- | --- | --- |
| `button-primary.lwu` | 6,944 B | 32×24，3 态，insets 6,6,6,6，最小 13×13 |
| `panel-card.lwu` | 1,760 B | 24×24，1 态，insets 6,6,6,6 |
| `icon-heart.lwu` | 800 B | 16×16，1 态 |

## 6. 设备端加载

`firmware/components/lw_ui/src/lw_ui_asset_view.c`（仅 `#ifdef ESP_PLATFORM`，
宿主构建里整个 TU 消失——桌面 LVGL 没有 `esp_heap_caps.h`）。

```c
lw_ui_asset_add_file("panel.card", "ui/panel-card.lwu");   /* 启动时一次 */
lv_obj_t *p = lw_ui_asset_panel(parent, "panel.card", x, y, 300, 180, LW_UI9_STATE_NORMAL);
lv_obj_t *b = lw_ui_asset_panel(parent, "button.primary", x, y, 120, 36, LW_UI9_STATE_PRESSED);
lv_obj_t *i = lw_ui_asset_icon(parent, "icon.heart", x, y, 24, 24);
```

设计要点：

1. **不复用 `lw_assets` 的租约系统**。`lw_asset_acquire()` 是为**动画帧**
   设计的（每 100 ms 换一张、可能堆积），UI chrome 是开机加载一次、永不变化。
   给它加租约只是多一层记账，真正的上限是"16 个槽位 + 单文件 1 MiB"。
2. **源文件不保留**。九块切片拷进各自的缓冲区后，`.lwu` 文件缓冲立即释放
   （总像素数不变，不额外占内存）。
3. **控件计数**。`s_widgets` 统计本模块创建的控件，`LV_EVENT_DELETE` 回调递减。
   `lw_ui_asset_reset()` 在还有控件时**拒绝**执行——否则就是 use-after-free。
4. **像素风**。`pixel_art` 为真时 `lv_image_set_antialias(view, false)`，
   最近邻，放大不糊。
5. **内存**。`heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`，失败退
   `MALLOC_CAP_8BIT`。

## 7. 测试

| 套件 | 项数 | 内容 |
| --- | --- | --- |
| `run_engine_tests.ps1` | 264 | 九宫格几何、`.lwu` 解析、repack 逐像素、时间、场景 |
| `test_ui_assets.ps1` | 34 | 三个 fixture 转换、固件解析器验收、确定性（重建比 SHA-256）、`--check` 不写盘、六种拒绝 |

`test_ui_assets.ps1` 的"确定性"检查值得单说：同一份 PNG 转两次，SHA-256 必须
相同。否则打包发布时每次构建的哈希都在变，无法判断"这份镜像用的是哪版美术"。

## 8. 未做

- **没有**把 `.lwu` 用在任何真实页面上。API 与测试就绪，接线未做。
- **没有**在真实面板上看过九宫格效果（本轮零硬件操作）。
- **没有**做九宫格的 LVGL 合成优化（现在是九个 `lv_image` 对象；更省的做法是
  自定义 draw 事件一次画完，但那是渲染层改动，风险高于收益）。
- **没有**支持多分辨率素材（一个 `.lwu` 一个尺寸）。
- **没有**处理"PNG 在 alpha=0 处 RGB 有杂色"——转换保留原始 RGB，
  放大时可能露彩边。这条写进了给 Codex 的交接文档。
