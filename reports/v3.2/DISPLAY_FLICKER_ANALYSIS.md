# OS V3.2 屏幕频闪专项分析（离线，无硬件）

> 状态：**只做源码与构建层分析，不宣称已修复**。本文档中的每一条"可能原因"都标注了
> **它需要的板端证据**，以及**如果证据成立，对应的低风险改法**。板子当晚断电，任何
> 需要上电、串口、示波器、摄像头的结论都留到第二天（见 `TOMORROW_BOARD_TEST.md`）。

- 分析对象：`D:\LittleWorld-V32`，分支 `fix/os-v3.2-display-wifi-offline`，基线 `d95b806`
- 显示链路：ESP32-P4 → MIPI-DSI(2 lane, 1 Gbps) → EK79007 1024x600 → `esp_lv_adapter`
  → LVGL 9.5；背光 GPIO32 由 LEDC（低俗组，通道 1，timer 1，10 bit，5 kHz，`output_invert=1`）驱动
- 应用层：`firmware/main/app_main.c`、`firmware/components/lw_ui/*`、`firmware/components/lw_backend/lw_hw.c`

---

## 1. 结论先行

**不能断言"频闪"只有一个原因，也不能断言已经修好。** 从代码里能证明的只有三件事：

1. **背光是一路 5 kHz PWM**，不是直流。任何在运行期反复写 duty 的路径，都会把"亮度调节"
   变成"对 5 kHz 载波占空比的连续调制"，在相机/人眼下最容易被看成闪烁或滚动条纹。
2. **V3.1 存在多次运行期写 duty 的路径**（开机一次、设置页滑块每一次 VALUE_CHANGED 一次），
   而且开机时 BSP 先 `backlight_on()`=100%，随后 `lw_system_init()` 立刻把偏好值（默认 72%）
   写下去 —— 开机瞬间有一次 100%→72% 的可见跳变。
3. **刷新链路是 TRIPLE_PARTIAL 局部刷新**，buffer_height=50 行、RGB565、3 个 DPI framebuffer。
   局部刷新本身不是缺陷，但"每 20 ms 改一小块"的动画模式如果和 DSI 扫描不同步，就会出现
   **撕裂/局部滚动**，外观和"闪烁"极易混淆。这两者必须靠板端录像区分。

因此本次交付的是**三份编译期对照构建（A/B/C）+ 一份板端判据清单**，而不是一个"修好了"的补丁。

---

## 2. 参与频闪的六个候选原因

| # | 候选原因 | 代码依据 | 需要的板端证据 | 若成立的低风险改法 |
|---|---|---|---|---|
| C1 | 运行期反复写背光 duty | `lw_backend/src/lw_hw.c` `lw_hw_brightness_set()` → `bsp_display_brightness_set()`；`lw_ui/src/lw_os.c:130` 滑块 `LV_EVENT_VALUE_CHANGED` 每次都写 | 摄像头 30/60 fps 录"拖滑块"过程；示波器/逻辑分析仪看 GPIO32 | 已经做成**编译期**选项：diag-b 固定亮度、diag-c 最小负载 |
| C2 | 开机亮度阶跃（100%→偏好值） | `app_main.c` `bsp_display_backlight_on()` 之后 `lw_system_init()` 用 `prefs.brightness`(默认 72) 调 backend | 冷启动录像前 3 秒 | 开机只写一次目标值（diag-b 已实现：`LW_BACKLIGHT_BOOT_LEVEL` 单次写入） |
| C3 | 局部刷新撕裂 | `app_main.c` `.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL`；`sdkconfig` `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3` | 静态页（diag-c）是否仍闪；静止画面录像逐帧看 | 仅在 diag 分支上试 `DOUBLE_FULL`/`TRIPLE_FULL`（已验证 180° 旋转在 `esp_lv_adapter` 中受支持，见 §5） |
| C4 | 高频 UI 重绘 | `lw_os.c:604` 20 ms tick + `refresh()`；`lw_sprite.c:86` 每 20 ms seek；`lw_rpg_view.c:147`/`lw_duo_view.c:146` 各 20 ms | diag-c（静态 UI）与 diag-a 对照 | diag-c 已把 actor 动效与逐帧刷新整体关掉 |
| C5 | 电源/带宽争用 | DSI 2 lane@1 Gbps、60 Hz 1024x600 RGB565 ≈ 884 Mbps 原始像素率，占满 2 Gbps 的 44%；PSRAM 同时被 LVGL buffer(s) 与角色帧使用 | 频闪是否随角色动画/页面切换变化；测 PSRAM/DSI 负载 | 暂不改时钟与 lane；只做对照构建，避免"改硬件参数"式盲改 |
| C6 | 面板自身/排线/供电 | 与固件无关 | 换电源、压排线、看是否与系统负载无关地闪 | 不在本轮范围内，但必须由 A/B/C 对照排除后才动手 |

> 说明：GPIO32 在本板确为背光（`esp32_p4_wifi6_touch_lcd_7b.h:58 #define BSP_LCD_BACKLIGHT (GPIO_NUM_32)`）。
> **本轮没有改任何引脚，也没有试任何未确认的 DCS 亮度命令。**

---

## 3. 三份编译期对照构建

选择"编译期"而不是"运行期开关"，是因为运行期开关会让 LEDC 仍保持配置、动画仍在跑，
两次运行之间真正的差异会被稀释。三份配置见 `firmware/sdkconfig.extra.diag-a|b|c`，
由 `tools/build_v3_2_matrix.ps1 -Configs diag-a,diag-b,diag-c` 产出。

| 构建 | 片段 | 背光 | UI 负载 | 目的 |
|---|---|---|---|---|
| **diag-a** | `sdkconfig.extra.diag-a`（仅注释，等同出厂 V3.1） | 运行期可变，开机写偏好值 | 正常动画 | 对照组：复现用户的"闪" |
| **diag-b** | `CONFIG_LW_BACKLIGHT_FIXED=y` | **开机写一次**，之后 `lw_hw_brightness_set()` 直接返回 false，UI 正确显示"不可调" | 正常动画 | 只隔离"背光写 duty"这一个变量 |
| **diag-c** | `CONFIG_LW_BACKLIGHT_MINIMAL=y` + `CONFIG_LW_DIAG_STATIC_UI=y` | 开机写一次 30%，永不再写 | 角色不动、帧定时器暂停、只渲染一次 | 只隔离"UI 重绘负载"这一个变量 |

对应源码开关：

- `firmware/main/Kconfig.projbuild`：`LW_BACKLIGHT_RUNTIME/FIXED/MINIMAL` 选择 + `LW_DIAG_STATIC_UI`
- `firmware/components/lw_backend/src/lw_hw.c`：`LW_BACKLIGHT_RUNTIME` 编译期决定是否允许写 duty；
  `LW_BACKLIGHT_BOOT_LEVEL` 决定单次写入的百分比
- `firmware/components/lw_ui/src/lw_os.c`：`update_actors()` 与 `tick()` 在 `LW_DIAG_STATIC_UI`
  下只渲染一次，不再 seek 帧、不再移动影子与热区
- `firmware/components/lw_ui/src/lw_sprite.c`：同上，帧定时器保持暂停在 frame 0

---

## 4. 判读矩阵（板端执行）

| diag-a | diag-b | diag-c | 判读 |
|---|---|---|---|
| 闪 | 不闪 | 不闪 | **C1/C2（背光写 duty/开机阶跃）** 是主因，优先改背光路径 |
| 闪 | 闪 | 不闪 | **C4（UI 重绘量）** 是主因，优先减重绘 |
| 闪 | 闪 | 闪 | **C3/C5/C6（刷新同步、带宽、面板）**，进入 tear-avoid 与电源排查 |
| 不闪 | — | — | 说明现象与负载相关，需要按用户的复现步骤（哪个页面、什么操作）再收窄 |

> 注意：diag-c 同时改了背光和 UI 两个变量。若 diag-b 与 diag-c 都"不闪"，只能说明
> 二者之一即可消除，需再用 diag-b 单变量结论收窄（diag-b 只动背光）。

---

## 5. 撕裂模式与 180° 旋转（只做可行性核对，不构建）

`esp_lv_adapter_display.h` 提供的模式：`NONE(0)`、`DOUBLE_FULL(1)`、`TRIPLE_FULL(2)`、
`DOUBLE_DIRECT(3)`、`TRIPLE_PARTIAL(4)`、`TE_SYNC(5)`、`DOUBLE_PARTIAL(6)`；
MIPI-DSI 默认即 `TRIPLE_PARTIAL`，当前工程沿用默认。

旋转由 `bsp_display_cfg_t.rotation = ESP_LV_ADAPTER_ROTATE_180` 表达，也就是说**旋转是
adapter/驱动层能力，不依赖 LVGL 软旋转**，因此修改 refresh 模式与保持 180° 不冲突。
本轮**没有构建**这些变体（避免一次引入两个变量）；若判读矩阵落到 C3，第二天按
`DOUBLE_FULL → TRIPLE_FULL` 顺序各出一份 diag-d/diag-e，仍以录像为准。

---

## 6. 本轮明确没有做的事

- 没有上电、没有开串口、没有刷机、没有擦除、没有动分区表、没有更新 C6 固件
- 没有改 GPIO、没有写 DCS 亮度寄存器、没有改 DSI 时钟/lane 数
- 没有把旧日志当作新测量使用
- **没有宣称频闪已修复**：所有构建均为"待板端验证"

---

## 7. 交付物

| 文件 | 内容 |
|---|---|
| `reports/v3.2/DISPLAY_FLICKER_ANALYSIS.md` | 本文 |
| `reports/v3.2/BUILD_MATRIX.md` | 全矩阵尺寸/SHA-256/占用 |
| `reports/v3.2/TOMORROW_BOARD_TEST.md` | 第二天的板端判据与操作顺序 |
| `reports/v3.2/build_matrix.json` | 机器可读的矩阵结果 |
| `firmware/sdkconfig.extra.diag-a|b|c` | 三份对照构建配置 |
| `firmware/main/Kconfig.projbuild` | 诊断菜单与帮助文本 |
| `tests/host/test_lw_stress.c` | 离线压力/故障注入（2,035 断言，与显示无关，验证长跑不退化） |
| `reports/v3.2/NIGHT_TASK_STATUS.md` | 夜班状态与未验证清单 |

---

## 8. Bug 1 代码审计结论（本轮新增）

除了上面六条"可能原因"，本轮把 V3.1 的显示与交互代码逐行过了一遍，发现 6 个
**与频闪无关、但确实存在**的缺陷。它们都只影响显示正确性或交互手感，**没有一个能
单独解释"整体闪烁闪烁"**，所以不会被当作频闪的答案写进结论。修不修需要你决定，
因为它们都会改动已经"看起来正常"的界面。

| # | 位置 | 现象 | 严重度 | 低风险改法 |
|---|---|---|---|---|
| B1 | `lw_ui/src/lw_os.c:494-495`（`refresh()`，第 479 行） | 时钟文本每秒无条件 `lv_label_set_text_fmt` 一次，即使字符串没变也会标记 dirty、触发一次局部重绘。`lw_os.c:591` 每秒调 `refresh()` | 中（可能放大 C3/C4） | 先把新字符串格式化到栈上，`strcmp` 相同则跳过 set_text |
| B2 | `lw_ui/src/lw_os.c:127`（`slider_event`），注册于 `:141` | 拖动过程中每个 `LV_EVENT_VALUE_CHANGED` 都调 `lw_system_brightness()` → `bsp_display_brightness_set()` 写一次 LEDC duty，松手时 `LV_EVENT_RELEASED` 再写一次；每帧还有一次 `refresh()` | 中（C1） | 拖动时只更新标签与指示器；松手时才写 duty。只改 `slider_event` 一个函数 |
| B3 | `lw_sprite.c:35-37`（`present()`） | 每次换帧都无条件 `lv_image_set_src` + `lv_image_set_pivot` + `lv_image_set_scale`，源图未变也触发 invalidate | 中（C4） | 记住 `last_src/last_scale`，相同则跳过。注意 `:72` 的销毁路径必须无条件执行 |
| B4 | `lw_ui/src/lw_os.c:202-223`（`update_actors()`，20 ms 定时器在 `:604`） | 每 20 ms 对两个 actor 各调一次 `lw_sprite_seek`，seek 到同一帧也会走一遍 present | 低 | seek 前比较目标帧号 |
| B5 | `lw_ui/src/lw_os.c:223` 与 `:225` | 阴影与热区每 20 ms `lv_obj_set_pos`/`set_size`，位置没变 LVGL 仍会判 dirty | 低 | 保存 last rect，相同则跳过 |
| B6 | `lw_ui/src/lw_os.c` 的开机 `lw_app_state_init()` 位置 | V3.1 已修（OS 路径未初始化导致 740312 天），但**依赖 `lw_os_start` 被调用**；若将来再有第二个入口，会重现 | 低 | 把 `s_loaded` 做成带断言的一次性初始化 |

建议顺序：**先做 B1、B2**（只动两个函数，且都能被 diag-a/B/C 对照验证效果），
B3–B5 合并成一次"按需刷新"改动，B6 只加断言。**本轮一行未改**，因为任何显示层
改动都会让 A/B/C 三份对照失去"只差一个变量"的意义——对照必须先在没有修改的基线
上做完。

