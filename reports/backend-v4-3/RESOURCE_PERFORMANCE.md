# 资源与性能（P1）

分支 `agent/dsh-v4-3-engine`。本轮**没有上板**，所有数字分三类标注：

- **[实测·Host]** —— 在本机编译运行、可复现；
- **[静态]** —— 由常量与结构体尺寸直接推出，不依赖运行；
- **[待上板]** —— 必须真机才能测，本轮不做，也不假装做过。

## 1. 预算表（静态）

| 项 | 值 | 出处 |
| --- | --- | --- |
| 面板 | 1024×600 RGB565 | `bsp_display_start_with_config` |
| 单帧全屏 | 1,228,800 B | 1024×600×2 |
| DPI framebuffer 数 | 3 | `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3` |
| DPI framebuffer 合计 | 3,686,400 B | 3×1,228,800 |
| LVGL 绘制缓冲 | 50 行/块 | `ESP_LV_ADAPTER_DEFAULT_CONFIG` MIPI profile `buffer_height=50` |
| 单块绘制缓冲 | 102,400 B | 1024×50×2 |
| 角色单帧（.lwp） | 196,608 B | `LW_PACK_FRAME_BYTES = 256×256×3`（RGB565+A8） |
| 角色帧租约预算 | 786,432 B | `LW_ART_LEASE_BUDGET = 4×LW_PACK_FRAME_BYTES` |
| 文件缓存预算 | 786,432 B | `LW_ART_CACHE_BUDGET = 4×LW_PACK_FRAME_BYTES` |
| 场景整图 | 1,228,800 B | `lw_visual_assets.c:32` 1024×600×2 |
| 场景缩略图 | 76,800 B | 256×150×2 |
| 场景槽位 | 4（1 整图 + 3 缩略图） | `lw_visual_assets.c:8` |
| 场景峰值（满槽） | 1,459,200 B | 1,228,800 + 3×76,800 |
| UI 素材槽位 | 16 | `LW_UI_ASSET_SLOTS` |
| UI 单文件上限 | 1 MiB | `LW_UI_ASSET_FILE_MAX` |
| 启动后可用堆 | ~27.9 MB | 串口日志 `boot: Little World ready, free heap 27942883 B` |

内部 SRAM 与 PSRAM 的区分：`lw_ui_asset_view.c` 申请时用
`heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`，失败再退 `MALLOC_CAP_8BIT`。
角色帧与场景走 `lv_malloc`（LVGL 配置为 `LV_USE_STDLIB_MALLOC` 默认堆，
在 P4 上落在 PSRAM）。

## 2. 减少无效刷新（本轮实际改了什么）

### 2.1 精灵统计不再遍历

新增 `lw_sprite_stats(unsigned *live, unsigned *running)`：精灵在
`lw_sprite_create()` 里挂进一条双向链表，删除回调里摘除。此前诊断要回答
"有几个精灵在跑"只能遍历 LVGL 对象树，而那是 O(全屏对象数)、每秒一次。

### 2.2 精灵 tick 的早退

```c
static void tick(lv_timer_t *timer)
{
    lw_sprite_t *s = lv_timer_get_user_data(timer);
    if (!s || !s->clip) return;          /* 新增 */
    if (!s->enabled || s->paused) return;
    ...
}
```

`lw_sprite_seek()` 在 `!s->clip` 时本来也会立刻返回，但那次调用要穿过
LVGL 定时器回调、取用户数据、进函数、读四个字段。早退把这条路径压到两次判空。
**这不是性能优化的大头**，它的真正价值是让 `tick()` 的运行条件与
`lw_sprite_stats()` 的判定完全一致——统计说"没在跑"的精灵，tick 里也一定
不会推进帧。

### 2.3 静态 UI 诊断构建（V3.2 已有，本轮复用）

`CONFIG_LW_DIAG_STATIC_UI=y` 时 `update_actors()` 与 `tick()` 直接返回，
只渲染一次。这是 diag-c 的对照构建，用来把"闪烁"归因到刷新负载。

### 2.4 没做的（重要）

- **没有**实现帧包围盒差分：需要 hook LVGL 的 invalidate 区域，属于渲染层改动，
  与 Codex 的视觉迭代冲突。
- **没有**做静态背景缓存：`lw_visual_assets` 已经做到"整图只读一次、之后复用
  LVGL 图像缓存"，再叠一层缓存只会重复占用 1.2 MB。
- **没有**做状态卡局部刷新：`lw_os.c` 的 `refresh()` 目前仍是整块重建文本，
  改动它需要和 Codex 的布局一起做。

## 3. 文件缓存（`lw_storage.c`）

```c
if (key->bytes > c->stats.budget_bytes) return NULL;          /* 单块过大直接拒绝 */
...
if (slot && key->bytes <= c->stats.budget_bytes - c->stats.resident_bytes) break;
```

即：**先看剩余预算再决定是否驱逐**，而不是先驱逐再看放不放得下。这样一次
超大请求不会把整个缓存清空——这正是"SD 异常降级"要的行为：读失败只是这次
读失败，缓存里已有的帧不该被牵连。

`lw_file_cache_read()` 命中时 `stats.hits++`，未命中走 `load_key()`（工作线程
读文件 + `stats.misses++`），`reserve()` 驱逐时 `stats.evictions++`，
队列满丢请求时 `stats.drops++`。这些计数现在全部出现在
`lw_diag_report` 的 `cache:` 行与 JSON 里：

```
cache: open hits=42 misses=7 evictions=3 drops=0 resident=589824 peak=786432 budget=786432
```

**注意**：`resident` 可以等于 `budget`，因为驱逐条件是
`key->bytes <= budget - resident`，等号成立时允许继续装入。这是有意为之——
不允许装满会让最后 1 字节永远空着。

## 4. 内存峰值（Host 实测）

`tests/host/test_lw_engine.c` 与 `tests/host/test_lw_diag.c` 不模拟 PSRAM 容量，
它们验证的是**账本一致性**：

- `lw_visual_assets`：`acquire()` 时 `live_bytes += bytes` 并更新 `peak_bytes`，
  `free_entry()` 时 `live_bytes -= e->image.data_size`。测试断言同一轮
  acquire/release 之后 `live_bytes` 回到 0，`peak_bytes` 保留历史最高值。
- `lw_assets`（角色帧租约）：`lw_asset_acquire()` 拒绝
  `bytes > budget - live_bytes` 的请求并 `failures++`；`lw_asset_release()`
  归还原额。测试断言超过预算的请求被拒且 `live_bytes` 不变。
- `lw_file_cache`：见上。

真实数字（Host，V4.1 交付时记录，本轮未重测，标记为**沿用**）：

| 项 | 峰值 |
| --- | --- |
| 文件缓存 | 3,597,312 B |
| 场景缓存 | 2,688,000 B |
| 角色帧 | 1,999,872 B |

这三项是 Codex 的 V4.3 资源预算表给的 Host 数字（`docs/handoff/DSH_VISUAL_V4_3.md`），
对应的是**它那套**资源尺寸（两张 1,228,816 B 的全屏场景）。本分支的
`lw_visual_assets` 只有 4 个槽位、峰值 1,459,200 B，比它小——因为本分支还没有
V4.3 的昼夜双场景。

## 5. 测试覆盖（哪些是新的）

| 场景 | 覆盖情况 |
| --- | --- |
| 多角色同时动画 | 引擎测试：两个 actor 独立投影，互不影响；**未**测真机帧率 |
| 多组件过渡 | **未覆盖**（需要 LVGL 动画，Host 没有面板） |
| 昼夜切换 | 引擎测试：五时段边界、迟滞、显式设置立即生效 |
| 导航展开收起 | **未覆盖**（UI 层） |
| 长时间 idle | `test_lw_stress.c::test_long_run` 两小时 500 ms 轮询，14,400 次 |
| 突然停止 | `test_lw_stress.c::test_wifi_churn` + 精灵 pause/resume |
| 资源损坏 | `test_lw_engine.c` 的 `.lwu` 拒绝用例（magic/尺寸/kind/format/states/insets/payload/reserved） |
| SD 读取失败 | `test_lw_card_layout.c` 的缺文件、空 root、超长路径 |
| 重复进 RPG | **未覆盖**（UI 层） |
| 缓存峰值/泄漏 | 引擎与 diag 套件断言账本；`test_lw_stress.c` **不**覆盖缓存 |

这里要明确说：`test_lw_stress.c` 的六族压力测试是**网络服务**的压力测试，
不是渲染压力测试。把它当作"性能已验证"是错的。

## 6. 帧率

**没有测。** 原因：没有面板就没有 `lv_timer_handler` 的真实节拍，Host 上的
`lv_tick_get()` 是墙上时钟，测出来的"帧率"没有意义。真机需要：

1. 在 `lw_os.c` 的 `tick()` 里统计两次 `refresh()` 之间的 `lv_tick_get()` 差值；
2. 连续记录 10 秒，输出 p50/p95/max；
3. 分别在 os / diag-a / diag-b / diag-c 四个镜像上跑。

这套代码没有写，因为写在没有硬件验证的情况下等于交一个假指标。上板那一轮
再做。

## 7. 结论

- 预算表是**静态可算**的，已经算清：最坏情况（满场景槽 + 满角色租约 + 满文件缓存）
  约 3.0 MB，对 27.9 MB 可用堆没有压力。
- 减少无效刷新只做了两处**低风险、可验证**的改动（精灵统计改链表、tick 早退），
  其余留给 UI 那一轮。
- 内存账本的一致性有 Host 测试锁住；真实峰值与帧率**未测**，不声称。
