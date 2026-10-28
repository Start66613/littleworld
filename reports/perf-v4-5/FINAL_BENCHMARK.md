# P7 · 综合验收对照

这份文档是 V4.5 的验收总表。**它明确区分「已经证明的」与「还没证明的」**，
因为交接书与 V4.5 指令都要求结论必须有真机数据。

---

## 0. 一句话

**V4.5 完成的是「把瓶颈定位到一行 `set_scale`，并在宿主逐像素证明解法成立」，
不是「所有页面已经达标」。** 五个场景里没有一个在本轮拿到新的真机帧率数据——
板子在整个 V4.5 期间未连接（`Get-PnpDevice -Class Ports -PresentOnly` 只有
COM7/COM8 两条蓝牙串口）。

---

## 1. 交接书验收项对照

| 验收项 | 目标 | 本轮状态 | 依据 |
|---|---|---|---|
| 桌面页 40–50 FPS | `sched` ≤ 25 ms | 🟡 **V4.4 真机 21 ms**（未烧 V4.5 固件） | `perf-v4-4/BASELINE.md` |
| 世界页 40–50 FPS | 同上 | 🟡 **V4.4 真机 21 ms** | 同上 |
| RPG 页 40–50 FPS | 同上 | ❌ **V4.4 真机 119 ms** | `RPG_ACCEPTANCE.md` |
| 宠物页 40–50 FPS | 同上 | ❌ **V4.4 真机 251 ms** | `perf-v4-4/SPRITE_CACHE.md` |
| 开机动画 40–50 FPS | `boot` 帧数 / `elapsed` | ❌ **真机 5 帧 / 2047 ms** | `BOOT_OPTIMIZATION.md` |
| 点击 < 50 ms | p95 | ⬜ **未测** | 板子未连接 |
| 30 分钟无异常 | 0 error / 0 safe_mode | ✅ **V4.4 32.1 分钟通过** | `perf-v4-4/FINAL_PERFORMANCE.md` |
| 禁关动画 / 降画质 / 删素材 | — | ✅ **未违反** | 见 §4 |
| 禁碰 NVS / 分区表 / C6 / eFuse | — | ✅ **未违反** | 见 §4 |
| 烧录须明确授权 | — | ✅ **V4.5 未烧录** | 见 §3 |
| 新增文件全在 D 盘 | — | ✅ **是** | `D:\LittleWorld-V43-Board` |

---

## 2. 本轮实际产出

### 2.1 代码改动（11 个文件）

| 文件 | 改动 |
|---|---|
| `firmware/components/lw_ui/include/lw_assets.h` | `lw_asset_spec_t` 加 `bbox_x/y/w/h` + `prescaled`；新增 `lw_asset_draw_t` 与 `lw_asset_draw_geometry()` |
| `firmware/components/lw_ui/include/lw_pack.h` | LWPX geometry trailer 常量与错误码 |
| `firmware/components/lw_ui/src/lw_assets.c` | `spec_bbox()`、`scale_px()`、`place_px()`、`lw_asset_draw_geometry()` |
| `firmware/components/lw_ui/src/lw_pack.c` | trailer 解析；embedded 注册点 bbox 归零 |
| `firmware/components/lw_ui/src/lw_char_pack.c` | spec 初始化 bbox 归零 |
| `firmware/components/lw_ui/src/lw_sprite.c` | `apply_geometry()`、`has_geometry()`、`anchor_dx/dy` |
| `firmware/components/lw_ui/src/lw_os.c` | `update_actors_paint(dt, paint)`；开机遮罩期跳过重绘 |
| `host/pixel_pack.c` | geometry trailer 解析（烘焙包宿主路径） |
| `host/os_host.c` | 契约测试按资产表解析画布尺寸 |
| `host/CMakeLists.txt` | 新增 `RenderBench`、`PrescaleVerify` 目标 |
| `tests/host/test_lw_v4_resources.c` | legacy 语义期望修正 |

### 2.2 新增工具（7 个）

| 工具 | 用途 |
|---|---|
| `host/render_bench.c` | 隔离 LVGL 软件绘制成本，六配置 A–F + 逐像素 diff |
| `host/prescale_verify.c` | 烘焙包 vs 原包逐帧逐像素比对 |
| `tools/sprite_bbox_report.py` | 读真实 PNG 求 clip 并集 bbox 与浪费率 |
| `tools/lwp_prescale.py` | 离线烘焙（预缩放 + 预裁剪 + trailer） |
| `tools/lwp_frame_dump.py` | LWPX 单帧导裸 RGB565A8 |
| `tools/prescale_verify_all.py` | 四个包批量逐帧验证 |
| `tools/shot_diff.py` | BMP 逐像素整页比对 |

### 2.3 新增配置片段（4 个，V4.4 遗留，本轮未动）

`sdkconfig.extra.perf-{ppa,unit1,double,full}` + `Kconfig.projbuild` 的
`choice LW_TEAR_AVOID`。V4.4 已构建五个变体（`build_matrix.json`），未烧。

---

## 3. 为什么本轮没有真机数据

### 3.1 板子未连接

六轮复核（含本轮最后一轮）：

```
[System.IO.Ports.SerialPort]::GetPortNames()  ->  COM7, COM8
Get-PnpDevice -Class Ports -PresentOnly       ->  蓝牙链接上的标准串行 (COM7) OK
                                                  蓝牙链接上的标准串行 (COM8) OK
```

**没有 CH343 / COM5**（V4.4 期间板子出现过为 COM5，`Status=OK`）。

### 3.2 即使连上也不能烧

V4.5 指令原文要求：**任何新的真机烧录必须获得用户对本次烧录的明确授权，
不得沿用 V4.4 的历史授权。** 本轮没有收到该授权。

⇒ 固件已构建（`little_world.bin` 2,360,432 B），烘焙素材已生成（四个 `.lwp`
在 `build\v45-assets\`），但**没有写入设备**。

---

## 4. 硬约束逐条自查

| 约束 | 自查结果 |
|---|---|
| 禁关动画换 FPS | ✅ 未关。所有 `lw_sprite_seek` / `lv_anim` 调用保持；开机遮罩期只是**不绘制被完全遮挡的内容**，遮罩本身照常动画 |
| 禁降画质 | ✅ 未降。烘焙是**逐像素等价重采样**（237 帧 diff=0），不是降采样或抖动 |
| 禁删素材换 FPS | ✅ 未删。四个包全部保留，体积反而变小（内容裁剪） |
| 区分「显示 FPS / 调度间隔 / 切帧频率」 | ✅ 见 §5 |
| 不许继续靠扩缓存解决 | ✅ 本轮**没有**动缓存预算或槽位数（V4.4 的 12 MiB / 32 槽保持不变） |
| 不许以「逻辑坐标有小数」宣布解决瞬移 | ✅ RPG 的瞬移结论建立在「表现层直接写连续坐标 + 删除 `lv_os_motion_step`」上，`SPRITE_RENDER.md` 也未用小数坐标作为性能论证 |
| 不格式化 TF / 不清 NVS / 不改分区表 | ✅ 未做 |
| 不改 C6 / 不操作 eFuse | ✅ 未做 |
| 保留回退固件 | ✅ `firmware/build/os/little_world.bin` 旧镜像仍在 `build/` 各变体目录 |
| 新增文件全在 D 盘 | ✅ 全部在 `D:\LittleWorld-V43-Board` |

---

## 5. 三个必须分开的指标

V4.5 指令特别要求区分这三个量，本轮的口径：

| 指标 | 怎么测 | 当前值 |
|---|---|---|
| **LVGL 调度间隔** | `sched`（`cmd_sched` 统计 20 ms 定时器的实际间隔） | V4.4 真机：音乐 20 / 桌面 21 / 世界 21 / RPG 119 / 宠物 251 ms |
| **真实显示 FPS** | `fps`（只统计有 flush 的秒） | ⚠️ **该命令在慢页几乎无效**：RPG 读 `fps: 1`，因为 1 秒内可能只有 1 次 flush |
| **精灵切帧频率** | `anim`（`clip` / `frame` / `animation_ms`） | `male.idle` 8 帧 @100 ms、`couple.seated` 16 帧 @125 ms |

**关键区别**：`sched` 慢 ≠ 显示慢。`sched avg_gap = 119 ms` 的意思是
「LVGL 每 119 ms 才有机会跑一次 tick」，用户看到的画面变化频率也就是 ~8 fps。
而 `fps` 命令统计的是「有多少秒发生过 flush」，在慢页上会给出误导性的低值
（因为计数器只在 flush 时递增，分母是秒）。

⇒ **本报告一律用 `sched avg_gap` 作为帧率判据，`fps` 只作辅助。**
`boot` 命令的 `fps` 字段是 `frames*1000/elapsed_ms`，是真正的帧率，可以用。

---

## 6. 下一轮的第一优先级（按顺序）

| # | 事项 | 前置条件 | 预期收益 |
|---|---|---|---|
| 1 | **烧录烘焙固件 + 四个烘焙 `.lwp` 到 SD 卡** | 用户明确授权 + 板子接入 | 五个场景全部重新测量 |
| 2 | 复测 `sched`：桌面/世界/RPG/宠物 | 同上 | 期望进入 20–30 ms |
| 3 | 复测 `boot` 帧数 | 同上 | 期望从 5 帧上升 |
| 4 | 点击延迟探针（p50/p95/p99） | 同上 | 补交接书未测项 |
| 5 | `ui = 1542 ms` 分段（机制 4） | 真机视觉验收 | 首帧提前 300–600 ms |
| 6 | 皮肤九宫格 9 个 `lv_image` 合并（机制 5） | 先量控制中心页成本 | 待测 |

### 6.1 烧录与素材部署的具体步骤（供下一轮执行）

```powershell
# 1. 固件（应用分区，不动 bootloader / 分区表）
& .\tools\flash_v4_safe.ps1 -Config os -Stage app -Execute -Authorized

# 2. 烘焙素材必须同时放到 SD 卡（固件按文件名在 /sdcard/littleworld 下查找）
#    firmware/main/lw_art.c 的 packs[] 表：
#      male.lwp / qianqian-base.lwp / qianqian-wave.lwp / daily.lwp / ui.lwp / skin.lwp
#    只替换前四个（后两个是切片图集，不烘焙）
```

**注意**：烘焙包与旧包**必须成对替换**。如果只换 `male.lwp` 而 `qianqian-base.lwp`
还是旧的，两个角色的绘制路径会不一致（一个走直通、一个走慢路径），
虽然画面都正确，但性能对比会失去意义。

---

## 7. 报告清单与证据文件

| 文件 | 内容 | 证据 |
|---|---|---|
| `README.md` | 结论、三个可验证数字、六项机制、复现命令 | — |
| `OPEN_SOURCE_RESEARCH.md` | 14 个开源项目的许可证与机制 | — |
| `ARCHITECTURE_COMPARISON.md` | 与六个参照项目的逐项对照 | — |
| `SPRITE_RENDER.md` | 根因、方案、逐像素验证 | `render_bench.txt` / `prescale_verify.json` / `baked_vs_head.json` |
| `BOOT_OPTIMIZATION.md` | `ui` 拆分、遮罩期跳过重绘 | V4.4 真机 `boot phases` / `boot` |
| `RPG_ACCEPTANCE.md` | RPG 正确性与性能验收 | V4.4 真机 `sched` / `anim` |
| `DUAL_STATE_INVESTIGATION.md` | 双稳态机制解释 | V4.4 探针日志 |
| `FINAL_BENCHMARK.md` | 本文件：验收总表 | — |
| `render_bench.txt` | RenderBench 原始输出（3 clip × 6 配置） | 宿主实测 |
| `sprite_bbox.json` | 12 clip 画布/内容像素 | 静态测量 |
| `prescale_verify.json` | 237 帧逐像素结果 | 宿主实测 |
| `baked_vs_head.json` | 156 张整页截图比对 | 宿主实测 |

## 8. 诚实的边界

这份报告**没有**说：

- ❌「V4.5 让五个页面达到 40–50 FPS」——没有真机数据，不能说。
- ❌「烘焙包一定能让 RPG 从 119 ms 降到 25 ms」——宿主 RenderBench 说
  `male.idle` 单帧从 0.426 降到 0.077 ms（5.54×），但 RPG 页 119 ms 里精灵占多少
  没有真机分解，只能推测「大头是精灵」。
- ❌「双稳态已经消失」——机制解释成立、烘焙路径在宿主压平了两态，
  但需要真机 `probe_desktop_slow.ps1` 跑 8 分钟确认。

这份报告**说了**：

- ✅ 瓶颈是 `lv_image_set_scale(288) ≠ 256` 导致每帧走 `transform_and_recolor`。
- ✅ 离线烘焙 + bbox 裁剪在宿主上是 **5.54–7.13×**，且 **237 帧逐像素 diff=0**、
  **156 张整页截图 diff=0**。
- ✅ 开机遮罩期跳过被遮挡重绘，**156/156 截图一致**。
- ✅ 宿主测试矩阵全绿（142 / 82+336+33 / 261），固件可编译（分区余 62%）。
- ✅ 双稳态的机制解释有真机单变量证据（`anim off` 回 20 ms、`render()` 可重置）。
