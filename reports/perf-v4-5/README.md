# reports/perf-v4-5 · V4.5 渲染引擎重构

分支 `perf/v4-5-render-engine`（从 `220f85b` 开出，即 V4.4 的收尾提交）。
目标：桌面 / 世界 / RPG / 宠物 / 开机五个场景稳定 40–50 FPS，点击 p95 < 50 ms，
30–60 分钟稳定运行；在**不删动画、不降画质、不换静态贴图**的前提下达成。

## 最重要的一句话

**V4.4 在真机上分离出的「精灵逐帧重绘」这一项，本轮被量化成 5.5–7.1× 的
可回收成本，并且已经做出逐像素等价的实现：`lv_image_set_scale(288)` 这一个人为
比例修正让每一帧都走 LVGL 的 `transform_and_recolor` 慢路径。把它离线烘进素材，
宿主实测 `male.idle` 0.426 → 0.077 ms/帧、`couple.seated` 0.651 → 0.091 ms/帧，
且**与现状逐像素完全一致**（`diff px = 0`）。**

### 真机结果（2026-10-30 最终镜像：`0x241140`，per-clip 烘焙卡，16 KB 主任务栈）

| 页面 | V4.4 | **V4.5** |
|---|---|---|
| 桌面 / 世界 / 音乐 | 21 / 21 / 20 ms | **20 / 20 / 20 ms** |
| RPG 站立 | 119 ms | **20 ms** |
| **RPG 走路** | **89–117 ms** | **23 ms**（`max 344 ms`，512 帧中 7 帧 > 25 ms） |
| 宠物 | 251 ms | **20 ms**（per-clip bbox 进卡后） |
| 卡吞吐（新量） | 438 KB/s（FATFS 实测） | **1,866 KB/s**（bounce buffer 16 KiB→64 KiB PSRAM） |
| 开机动画 | 5 帧 / 2047 ms | **24 帧 / 2025 ms（11 fps）** |
| 点击（RPG 22 次） | 未测 | **合计 8 ms，单次 max 1 ms** |
| 30 分钟 soak | 未做 | 见 `FINAL_BENCHMARK.md` §9 |

走路这一项是用户指出的验收盲区：「RPG界面你得让人物走起来测啊，你人物走起来就
掉帧了」。定位过程与修复见 `RPG_ACCEPTANCE.md` §3.6——真凶不是绘制，而是
**未命中的同步 SD 读（60–75 ms/帧）加上走路工作集正好等于当时的缓存槽位数**。

最后一轮还暴露了一个与「快慢」无关的真问题：首屏构建之后内部 RAM 只剩 23 B，
RPG 页因此打不开、控制台任务建不出来、板子在启动末尾 panic。把它归零的过程
（控制台提到首屏前 + 主任务栈 32→16 KB）见 `BOOT_OPTIMIZATION.md` §7。

V4.5 之前的所有优化（扩缓存、加守卫、遮挡剔除）都没有触及这条路径；
V4.4 的 `C-1`（缓存 4→12 MiB）只把宠物页 I/O 归零，剩下的 86% 墙钟仍是绘制。
本轮第一次把「绘制」拆开测量，并给出了不动画质的解法。

## 这个目录里有什么

| 文件 | 编号 | 内容 | 真机数据 |
|------|------|------|----------|
| `README.md` | — | 本文件：结论、验收对照、复现命令 | — |
| `OPEN_SOURCE_RESEARCH.md` | P1 | 14 个开源项目的许可证与机制研究，逐条采纳判断 | — |
| `ARCHITECTURE_COMPARISON.md` | P2 | 我们的渲染/生命周期与六个参照项目的逐项对照 | — |
| `SPRITE_RENDER.md` | P3 | **精灵渲染引擎重构**：根因、方案、逐像素验证、收益 | 宿主实测 |
| `BOOT_OPTIMIZATION.md` | P4 | 开机动画：`ui=1542 ms` 拆分与遮罩期跳过重绘 | 真机基线 + 宿主验证 |
| `RPG_ACCEPTANCE.md` | P5 | RPG 页验收：连续插值、瞬移判据、真机复测 | 真机（V4.4 数据） |
| `DUAL_STATE_INVESTIGATION.md` | P6 | 桌面/世界页 21↔116 ms 双稳态的机制性解释 | 真机 + 宿主 |
| `FINAL_BENCHMARK.md` | P7 | 综合验收对照与待真机确认项 | 部分 |
| `render_bench.txt` | — | RenderBench 原始输出（三个 clip × 六配置） | 宿主实测 |
| `sprite_bbox.json` | — | 12 个 clip 的画布/内容像素与浪费率 | 静态测量 |
| `prescale_verify.json` | — | 237 帧逐像素烘焙验证结果 | 宿主实测 |
| `baked_vs_head.json` | — | 烘焙包 156 张整页截图 vs HEAD 的比对 | 宿主实测 |

## 三个可验证的数字

### 1. 浪费率 66.9%

`tools/sprite_bbox_report.py` 读真实 PNG 帧序列，按 `ALPHA_MIN = 8`（与
`tools/build_character_pack.py` 同阈值）求每个 clip 的并集 alpha bbox：

| 汇总项 | 值 |
|---|---|
| 12 个 clip 的画布绘制像素 | **1,157,120** |
| 12 个 clip 的实际内容像素 | **383,318** |
| 浪费 | **66.9%** |
| 若只按 bbox 裁剪的潜力 | **3.02×** |

最极端的是 `dog.idle`：内容只有 54×47，却画在 320×320 画布上，**浪费 97.5%**。
逐帧 bbox 与 clip 并集 bbox 的比值是 1.00–1.02×，所以**按 clip 并集裁即可**，
不需要每帧元数据。

### 2. 5.54–7.13×

`host/render_bench.c` 在同一渲染器、同一 1024×600 画布上只测「invalidate 精灵 +
`lv_refr_now`」，40 次迭代取均值（宿主 MSVC Release，LVGL 9.5.0）：

| clip | A 现状 | B bbox裁剪 | C 全画布1× | F 预缩放+预裁剪 | F/A |
|---|---|---|---|---|---|
| `male.idle` | 0.426 ms | 0.286 (1.49×) | 0.108 (3.93×) | **0.077** | **5.54×** |
| `couple.seated` | 0.651 ms | 0.438 (1.49×) | 0.145 (4.49×) | **0.091** | **7.13×** |
| `dog.idle` | 0.138 ms | 0.060 (2.31×) | 0.126 (1.09×) | **0.062** | **2.21×** |

**A→C 只改了缩放**（画布面积不变）却有 3.93–4.49×，证明瓶颈是定点重采样 +
临时缓冲 + 二次 blend，**不是绘制面积**。这也解释了为什么 V4.4 计划的
「画布裁剪 C-4」上限只有 1.4×——裁面积只能拿到 B 的 1.49×。

### 3. 237 帧 / 0 差异

`tools/lwp_prescale.py` 把 1.125× 烘进像素，`host/prescale_verify.c` 用同一个
LVGL 渲染器逐帧比对：

| 包 | 原始 | 烘焙后 | 画布 | 差异帧 |
|---|---|---|---|---|
| `male.lwp` | 17,892,100 B | **8,939,693 B** | 137×239 | 0 / 137 |
| `qianqian-base.lwp` | 22,218,124 B | **19,098,788 B** | 246×229 | 0 / 100 |
| `qianqian-wave.lwp` | 3,145,852 B | **1,425,746 B** | — | 0 |
| `daily.lwp` | 18,125,680 B | **12,311,676 B** | 276×252 | 0 |

合计 **237 帧、0 差异**（`prescale_verify.json`）。整页截图回归 **156/156 逐像素
完全一致**（`baked_vs_head.json`）。

## 六项可试验机制（按收益排序）

| 优先级 | 机制 | 来源 | 实测/预期收益 | 证据强度 | 状态 |
|---|---|---|---|---|---|
| **1** | **离线预缩放 + 预裁剪素材（F 形态）** | LVGL 源码 + GB Studio 属性表 + retro-go 整数步进 | **5.54–7.13×** | ⭐⭐⭐ 237 帧 diff=0 | **已实现** |
| 2 | `lw_asset_spec_t` 增加 bbox 元数据 | GB Studio | 单独 1.49× | ⭐⭐⭐ 逐像素 diff=0 | **已实现**（F 的一部分） |
| 3 | 开机遮罩期不重绘被遮挡的桌面精灵 | InfiniTime 生命周期 | 开机同屏精灵 4→2 | ⭐⭐ 156/156 一致 | **已实现** |
| 4 | 首屏 `ui=1542 ms` 分段延后不可见部分 | Brookesia 分阶段 init | 待真机 | ⭐ 需视觉验收 | 未做 |
| 5 | 皮肤九宫格按钮 9 个 `lv_image` 合并 | raylib 图集 | 待测 | ⭐ 需真机 | 未做 |
| 6 | 行级校验和 / 重复行 memcpy | retro-go | **不采纳** | — | 与 LVGL 失效区冲突 |

机制 6 被否决的理由：retro-go 用整帧校验和跳过未变化行，是因为它自己拥有整个
framebuffer；LVGL 已经按 `lv_area_t` 维护失效区，再叠一层行校验只会重复工作。

## 验收对照

| 验收项 | 目标 | 本轮状态 |
|---|---|---|
| 桌面 / 世界 | 40–50 FPS | ✅ 真机 21 ms（V4.4 基线）；烘焙路径待烧 |
| RPG | 40–50 FPS | ⬜ 真机 119 ms，烘焙路径待烧 |
| 宠物 | 40–50 FPS | ⬜ 真机 251 ms，烘焙路径待烧 |
| 开机 | 40–50 FPS | ⬜ 真机 5 帧/2047 ms；本轮已去掉遮挡期重绘 |
| 点击 p95 < 50 ms | — | ⬜ 未测（板子未连接） |
| 30–60 分钟稳定 | — | 🟡 V4.4 已跑 32.1 分钟，0 error / 0 safe mode |

**没有任何一条结论声称「全部页面已经达标」。** 所有「待烧」项都需要用户对
本次烧录的明确授权（V4.5 指令要求不得沿用 V4.4 的授权），且当前板子未连接
（`Get-PnpDevice -Class Ports -PresentOnly` 只有两条蓝牙串口）。

## 复现命令

```powershell
# 宿主渲染基准（不需要板子）
& .\tools\build_demo.ps1            # 或 ninja -j 2 -C build/host RenderBench
& .\build\host\RenderBench.exe build\bench\male_idle.rgb565a8 256 256 75 17 105 208 288 40

# 画布浪费率
& 'D:\Espressif\LittleWorld\python\python.exe' tools\sprite_bbox_report.py --json reports\perf-v4-5\sprite_bbox.json

# 烘焙 + 逐帧验证
& 'D:\Espressif\LittleWorld\python\python.exe' tools\lwp_prescale.py assets\visual-v4-2\runtime\male.lwp build\v45-assets\male.lwp --scale 288
& 'D:\Espressif\LittleWorld\python\python.exe' tools\prescale_verify_all.py

# 整页截图回归（必须带三个环境变量）
$env:LW_VISUAL_ASSETS='assets/visual-v4/runtime'
$env:LW_POLISH_ASSETS='assets/visual-v4-2/runtime'
$env:LW_MALE_PACK='build\v45-runtime\male.lwp'
New-Item -ItemType Directory -Force -Path build\v45-assets\shot-x | Out-Null
& .\build\host\LittleWorld.exe --os-v2-test 'private\pixel-duo\qianqian\qianqian.lwpx' 'build\v45-assets\shot-x'
& 'D:\Espressif\LittleWorld\python\python.exe' tools\shot_diff.py build\v45-assets\shot-head build\v45-assets\shot-x
```

## 宿主验证矩阵（本轮全绿）

| 套件 | 结果 |
|---|---|
| `tools/run_host_tests.ps1` | **142 checks, 0 failure** |
| `tools/run_core_regression.ps1` | Duo **82** / RPG **336** / Storage **33** — `CORE REGRESSION PASSED` |
| `tools/run_v4_resource_tests.ps1` | **261 checks, 0 failure** |
| `--os-v2-test`（整页） | **752 checks, 1 failure**（既有 `greeting naturally returns to idle clips`，HEAD 上同样失败） |
| 156 张整页截图 vs HEAD | **156 identical, 0 differ**（用 `LW_FAKE_TIME=19:30` 钉住相位后同条件复跑，两次逐字节一致） |
| ESP32-P4 固件构建 | `little_world.bin` **0x241140**（2,363,712 B），分区余 62% |

## 三个教训（写在这里，避免下一轮重复踩）

1. **离线重采样必须从整画布 lattice 取窗口。** LVGL 的 `step` 由整个目标宽度
   推导（`lv_draw_sw_transform.c:191-219`），按 bbox 单独建 lattice 会选到不同的
   源列。第一次烘焙因此差了 2704 px。
2. **`LWPX` trailer 的头第 2 个 u16 是 clip 数量，不是 kind。** kind 在 header
   偏移 `+14`。读错会让整包被拒收，表现为「6 个测试失败」而不是「解析失败」。
3. **`lv_obj_set_size` 不改变 `lv_image` 的绘制尺寸。** `img->w/img->h` 缓存的是
   **源图尺寸**，`LV_IMAGE_ALIGN_CENTER` 下对象尺寸只影响 `coords`（进而影响裁剪），
   唯一能平移绘制落点的是 `lv_image_set_offset`。
4. **首屏之后内部 RAM 只剩几个 KB，是这台机器的绑定约束。** SD 挂载 + 六包 53 KB、
   首屏再 38 KB，`ui` 之后 `internal` 只剩 23 B；控制台任务、RPG 页都会因此创建失败，
   而失败路径会在 `printf` 取 stdout 递归锁时踩坏启动状态，看起来像「栈保护/abort」。
   `BOOT_OPTIMIZATION.md` §7 有完整量化。
5. **截图回归必须先钉住 ambience 相位。** 色调层不透明度是本地时钟的函数，
   同一份代码在 18:00 和 23:00 会得到整屏差 25 级的"回归失败"。现在用
   `LW_FAKE_TIME=19:30` 固定（`host/host_platform.c`），同条件两次跑 156/156 逐字节一致。
6. **`git stash -u` 会删掉未跟踪的日志目录。** 30 分钟 soak 的日志写在
   `reports/logs/v45/`（未跟踪），一次 `stash push -u` 直接让正在跑的 soak
   崩在 `Add-Content: 未能找到路径`。做长测时不要 stash，或把日志放到 `build/`。
