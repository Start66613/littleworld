# CODEX_HANDOFF — Little World 交接说明

更新：2026-10-25（Asia/Shanghai）。作者：DSH。接收方：Codex / 后续任意会话。

## 1. 这是什么

`D:\LittleWorld-Main-DSH` 是 **DSH 独立开发工作区**，用于推进正式 Little World 的 B/C/D 三个部分。
它 **不是** 正式工程 `C:\Projects\LittleWorld`，也 **不是** 小游戏工程 `D:\LittleWorld-Arcade`。

- 正式工程没有被修改：DSH 只读取了它的文档、复用了 `D:\Espressif\LittleWorld` 里的工具链。
- 小游戏工程没有被修改：DSH 复制了它的 `arcade_core`（MIT，自己写的）到本工程 `firmware/components/arcade_core`。
- 本工程有完整的本地 Git 历史（无远程、未公开）。合适的时候把提交 cherry-pick 或整目录合并回正式工程即可。

## 2. 架构入口

```
firmware/components/lw_core     纯 C、无 LVGL/IDF 依赖、可主机测试
firmware/components/lw_ui       LVGL 9 UI（固件与 Windows 模拟器共用同一份代码）
firmware/components/arcade_core 来自 Little World Arcade 的游戏规则（2048/贪吃蛇/打砖块）
firmware/main/app_main.c        仅做 BSP 初始化 + 安装 lw_platform + 调 lw_ui_start()
host/                           Windows 演示程序（Win32 + GDI，无外部依赖）
tests/host/test_lw_core.c       142 项主机测试
tools/                          环境、构建、测试、字体、模拟器截图
```

### 关键接口

```c
/* lw_ui/include/lw_ui.h */
void lw_ui_start(void);                  // 建 shell 并进入首页，需持有 LVGL 锁

/* lw_core/include/lw/lw_platform.h —— UI 与平台的唯一耦合点 */
typedef struct {
    bool (*get_time)(struct tm *out);
    bool (*storage_read)(const char *key, void *buffer, size_t *length);
    bool (*storage_write)(const char *key, const void *buffer, size_t length);
    uint32_t (*uptime_seconds)(void);
    void (*log)(const char *message);
} lw_platform_t;
void lw_platform_set(const lw_platform_t *platform);
```

固件里 `app_main.c` 装的是 IDF 版本（NVS/esp_timer 待接），Windows 里 `host/host_platform.c`
装在 `%LOCALAPPDATA%\LittleWorld\` 下。

### 应用模型

```c
/* lw_ui/src/lw_shell.h */
typedef struct lw_app {
    const char *id;      // "home" / "pet" / "album" / "music" / "memories" / "arcade" / "settings"
    const char *name;    // Dock 标签
    void (*create)(lv_obj_t *content, const struct lw_app *self);
    void (*destroy)(void);
} lw_app_t;
```

新增一个应用 = 写一个 `lw_app_t` + 在 `lw_apps.c` 的 `s_apps[]` 加一行（Dock 顺序在 `s_dock[]`）。
shell 负责删除旧页面对象、调用 `destroy()`、切换 Dock 高亮。

## 3. 模块关系

```
app_main.c ──lw_platform──> lw_ui_start()
                              │
                              ├─ lw_app_state  (加载/保存 lw_state_t，时钟，DEMO 时钟)
                              ├─ lw_shell      (顶栏/内容区/Dock/导航/1s 时钟/10s 宠物 tick)
                              ├─ lw_theme      (调色板 ×3、字体、卡片样式、进场动画)
                              ├─ lw_widgets    (card/label/button/pill/toggle/bar/icon badge)
                              ├─ lw_icons      (纯 LVGL 图元画的 6 个 Dock 图标)
                              ├─ lw_pet_view   (宠物绘制 + 呼吸/眨眼/情绪动画)
                              └─ lw_app_*      (home / pet / album / music / memories / settings / arcade)

lw_core: lw_time (日历) + lw_pet (状态机) + lw_state (存档 + CRC) + lw_memories (DEMO 内容)
```

## 4. 构建方法

```powershell
# Windows 演示程序（首次会 CMake 配置）
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\build_demo.ps1 -Run

# 单元测试
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\run_host_tests.ps1

# 自动交互回归（无窗口，出 17 张 BMP）
.\build\host\LittleWorld.exe --auto reports\screenshots

# 字体子集（新增中文必须重跑，否则出方框）
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\gen_fonts.ps1

# 固件
. D:\LittleWorld-Arcade\tools\activate_env.ps1
$env:SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.rev3_x"
cd firmware
python $env:IDF_PATH\tools\idf.py -B ..\build set-target esp32p4
python $env:IDF_PATH\tools\idf.py -B ..\build build
```

工具链复用 `D:\Espressif\LittleWorld`（ESP-IDF v5.5.5、BSP 3.0.1、LVGL 9.5.0），没有重新安装。
Windows 构建用的 LVGL 来自 `firmware/managed_components/lvgl__lvgl`（IDF 组件管理器拉取的同一份）。

## 5. 已完成

- 桌面壳：1024×600、顶栏（时间/日期/DEMO 标记/设置）、6 项 Dock、页面切换动画、主题三选一。
- 宠物：6 情绪状态机、点击/抚摸/喂食/玩耍/休息、呼吸 + 眨眼 + 情绪姿态、NVS/文件持久化。
- 纪念世界：天数/下一个纪念日/日期编辑、时间线（6 条 DEMO）、惊喜盒子（解锁状态）、小地图（5 个可进入地点）。
- 设置：音效/动画开关、主题、诊断（活动对象数/互动次数/时钟状态/版本）、清空存档。
- 相册/音乐：可交互的 DEMO 实现（占位图查看、模拟播放进度与音量持久化）。
- 小游戏中心：2048 已可玩（复用 arcade_core）；贪吃蛇/打砖块标记为“移植中”。
- 测试：142 项核心单元测试；17 张截图的无头回归，含 10 次页面往返后对象数不增长。

## 6. 已知问题

1. **小游戏只接了 2048**。贪吃蛇/打砖块的核心逻辑在 `arcade_core` 里且已测试，缺的是这层 UI（全屏 overlay + 定时器 + 滑动/拖动输入），可照 2048 的模式补。
2. **相册/音乐是 DEMO**：没有真实图片解码与音频输出；“音效”开关只持久化偏好。
3. **固件端时间仍是占位**：`app_main.c` 的 `get_time` 返回 false（板子没有 RTC），因此固件使用 DEMO 时钟，
   界面会标注 “DEMO 时间”。存储已经接 NVS（`littleworld` namespace）。下一步是接 Wi-Fi 校时或外接 RTC。
4. **动画开关还没有真正生效**（`settings.animations` 已持久化，但 `lw_theme_enter_anim` 未读取它）。
5. 宠物页的“点一点它试试”提示与宠物区域没有做点击热区高亮，交互可用但反馈偏弱。
6. **未做任何实板验证**：屏幕、触摸、帧率、内存、NVS 掉电保持全部未测。

## 7. 尚需实板验证（按优先级）

1. 固件能否启动、屏幕方向与触摸坐标是否与 `ROTATE_180 + mirror_x/y` 一致。
2. 三套字体的实际可读性（尤其 18px 中文）与 1024×600 下的触摸目标大小。
3. 动画在 P4 上的帧率（宠物呼吸、页面切换、2048 滑动）。
4. 连续进出页面后内部堆是否稳定（模拟器已证明对象数不增长）。
5. 宠物状态在断电重启后是否正确恢复（当前固件存储尚未接 NVS）。

## 8. 与正式工程合并的建议

- 先把 `firmware/components/lw_core`、`lw_ui`、`arcade_core` 作为组件目录拷进 `C:\Projects\LittleWorld\firmware\components`。
- `main/app_main.c` 用本仓库的实现替换（它只做 BSP 初始化 + 平台注入）。
- 分区表、sdkconfig、CMakeLists 与本仓库一致即可（已按 rev3_x/32MB 验证过编译）。
- 迁移前先在本仓库跑一遍 `tools/run_host_tests.ps1` 与 `--auto` 回归，确认基线。
