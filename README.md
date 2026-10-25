# Little World OS 2.0 当前开发入口

当前工作在 `agent/littleworld-os-v2`；系统设计见 [SYSTEM_DESIGN.md](docs/design/SYSTEM_DESIGN.md)，本轮进度见 [STATUS.md](reports/os-v2/STATUS.md)。`pwsh -NoProfile -File tools/run_os_v2.ps1` 自动构建并打开可操作的 Windows LVGL 原型；加 `-Test` 自动运行检查与截图。网络/亮度/声音为 Windows Mock，真实硬件适配状态独立记录。所有新数据在 D 盘，稳定固件入口保留。

本地真实截图位于 `reports/ui-v2/os-v2`（包含角色，已忽略，不公开）。男方仍使用明确标注 TEST A；DSH 候选尚待返工和用户形象确认。新 Meowa 付费任务需要另行批次预算。

# Little World

> **2026-10-25 · Codex 像素双角色开发分支**：当前工作区 `D:\LittleWorld-Codex`，分支 `agent/pixel-duo`；稳定 DSH 主线与 `art/batch-1` 保留。女方复用 DesktopPet 的 qianqian，男方尚待 DSH/用户确认，当前只用 TEST A 占位。见 [当前状态](docs/PIXEL_DUO_STATUS.md)、[像素契约](docs/PIXEL_CHARACTER_CONTRACT.md)、[架构](docs/PIXEL_DUO_ARCHITECTURE.md)。默认旧版应用未替换，不涉及刷机。

```powershell
# 自动导出原角色、构建并打开 Windows 真实 LVGL 双角色技术预览
pwsh -NoProfile -File D:\LittleWorld-Codex\tools\run_pixel_duo.ps1
# 同一入口执行模型/双角色生命周期与真实截图验证
pwsh -NoProfile -File D:\LittleWorld-Codex\tools\run_pixel_duo.ps1 -Test
```

角色参考图集与实际运行截图位于本地忽略的 `private/pixel-duo/`；未经用户确认与另行授权公开不提交公共仓库。下文为稳定基线的原说明，早期 C 盘路径仅作历史记录，当前新增内容一律使用 D 盘。

送给她的两周年礼物：一台能长期长大的小世界。

硬件是 **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B**（7 英寸 1024×600 触摸屏，ESP32-P4 v3.2，32 MB Flash），
正式系统包含三部分：桌面小系统（B）、可互动电子宠物（C）、情侣纪念世界（D）。

> 本仓库是 **DSH 在 2026-10-25 夜间开发的独立工作区**，与正式工程 `C:\Projects\LittleWorld`
> 和小游戏工程 `D:\LittleWorld-Arcade` 隔离；不影响它们，随时可以按 `CODEX_HANDOFF.md` 交回。

## 现在就能玩（Windows）

```
双击 Start-LittleWorld.bat
```

或者：

```powershell
.\build\host\LittleWorld.exe
```

鼠标就是手指：点按 = 点击，拖动 = 滑动。Esc 退出；F12 保存一张截图到 `%LOCALAPPDATA%\LittleWorld\`。

**功能状态**

| 模块 | 状态 | 说明 |
|---|---|---|
| 桌面 / 顶栏 / Dock / 页面切换 | 可运行 | 6 个应用入口 + 首页 |
| 电子宠物 | 可运行 | 6 种情绪、点击/抚摸/喂食/玩耍/休息、呼吸与眨眼动画、状态持久化 |
| 纪念世界 | 可运行 | 在一起天数、下一个纪念日、时间线、惊喜盒子、小地图（DEMO 内容） |
| 设置 | 可运行 | 音效/动画开关、三套主题、诊断信息、清空存档 |
| 相册 / 音乐 | 部分完成 | DEMO 占位图与模拟播放器（无真实音频输出） |
| 小游戏 | **通过** | 2048 / 贪吃蛇 / 打砖块 三款都可玩（复用 arcade_core，全屏运行，可回游戏中心） |
| ESP-IDF 固件 | 见 `MORNING_REPORT.md` | 同一套 UI 代码可在 esp32p4 / rev3_x 下编译 |

## 目录

```
firmware/            ESP-IDF 工程（components/lw_core + lw_ui + arcade_core）
host/                Windows 演示程序（Win32 + GDI + LVGL，无外部依赖）
tests/host/          纯 C 核心单元测试
tools/               构建、测试、字体生成、模拟器截图
reports/             截图与日志
MORNING_REPORT.md    明早验收报告
CODEX_HANDOFF.md     交给 Codex 的交接说明
```

## 设计

- 奶油白 / 浅粉 / 薰衣草紫，圆角卡片 + 柔和阴影，1024×600 横屏。
- 全部图形由 LVGL 原生图元绘制，没有图片资源依赖；字体是 Noto Sans SC 子集（SIL OFL 1.1）。
- `lw_core` 是纯 C（时间、宠物、存档、回忆数据），可在主机上单元测试。
- `lw_ui` 只依赖 LVGL 和一个平台接口 `lw_platform`（时间/存储/日志），固件与 Windows 共用。

## 构建

```powershell
# Windows 演示程序
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\build_demo.ps1

# 核心单元测试
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\run_host_tests.ps1

# 自动交互回归 + 截图（无窗口）
.\build\host\LittleWorld.exe --auto reports\screenshots

# 中文字体子集（改动了中文文案后）
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\gen_fonts.ps1

# ESP-IDF 固件（需要 D:\Espressif\LittleWorld 的工具链）
. ..\LittleWorld-Arcade\tools\activate_env.ps1
cd firmware
idf.py -B ..\build set-target esp32p4
idf.py -B ..\build build
```

## 许可

MIT（见 `LICENSE`）。字体 Noto Sans SC 使用 SIL OFL 1.1，许可文本在 `firmware/components/lw_ui/fonts/OFL.txt`。
小游戏规则移植自同样由本工作区开发的 Little World Arcade（MIT）。
