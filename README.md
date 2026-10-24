# Little World

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
| 小游戏 | 部分完成 | 2048 可玩；贪吃蛇/打砖块为下一阶段移植 |
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
