# Windows 联合版

## 是不是"联合版"

是从 `integration/os-v4-3-board-smoke`（HEAD `041bf46`）用 `tools/build_demo.ps1` **重新编译**的：

- `build/host/LittleWorld.exe` 1,401,344 B
- sha256 `9ed1414dd822d016ac31b61fd979ed52aa24b38d6113e1f787503bddb5b1c55c`
- 没有任何单独分支的 EXE 被拿来充数

## 截图来源

截图不是概念图，是 LVGL 真实帧缓冲：`LittleWorld.exe --os-v2-test <pack> <dir>` 里
`lw_host_screenshot_bmp()` 直接 dump 1024×600 RGB565。原始 BMP 在
`reports/ui-v2/os-v2/`（156 张，gitignore 本地产物），本目录的 PNG 是其中的联合版关键帧，
每张的 SHA-256 记在 `windows/SCREENSHOTS.json`。

## 看这几张就够

| 截图 | 证明了什么 |
| --- | --- |
| `01-desktop.png` | 客厅背景 + 双人站立 + "一起走过 731 天" + 19:30 演示时间丝带 + 底部收起态导航 |
| `02-control-center.png` | 九宫格皮肤面板、三态开关、亮度 72% / 音量 40% 真实值滑轨、模拟能力标注 |
| `06-lockscreen.png` | 锁屏时间用同一份时钟快照，"2026 年 10 月 25 日 · 演示日期"明示未校时 |
| `07-pixel-world.png` | 世界页四个入口（一起休息 / 一起读信 / 见面打招呼 / 小冒险）用皮肤九宫格 |
| `11-pet-idle.png` | **双人沙发坐姿比例正确**、**小狗在狗窝里**、饱腹/精力/好感面板 |
| `v43-daily-night.png` | 夜间版沙发双人 + 狗窝，昼夜场景与主题一致 |
| `17-rpg-room.png` | RPG 用独立房间场景，任务本与寻路正常 |
| `20-rpg-reward.png` | RPG 奖励弹窗 |
| `21-album-empty.png` | 相册空态不崩（缺资源路径） |
| `v4-greet-00.png` | 双人招手动画帧 |
| `v43-dock-open-03.png` | Dock 展开态六入口 |
| `v43-home-night.png` | 首页夜间场景（月亮/星空/暖灯） |
| `v43-time-settings.png` | 设置页"日期与时间"的五个时段预览按钮 |
| `v43-book-open-00.png` | 时间线故事书展开 |
| `component-dog-sleep.png` | 狗窝 sleep 组合状态单独验证 |

## 用户要求的八条，逐条对照

| 要求 | 结果 | 证据 |
| --- | --- | --- |
| 客厅背景正常 | ✅ Windows 通过 | `01-desktop.png` |
| 男方、千千正常显示 | ✅ Windows 通过 | `01-desktop.png`、`v4-greet-00.png` |
| 沙发坐姿比例正确 | ✅ Windows 通过 | `11-pet-idle.png`（世界锚点 (516,368)，坐垫接触校准） |
| 小狗正常待在狗窝里 | ✅ Windows 通过 | `11-pet-idle.png`、`component-dog-sleep.png`（组合状态，不是把狗贴到空窝上） |
| UI 皮肤正常显示 | ✅ Windows 通过 | `02-control-center.png`、`07-pixel-world.png`、`v43-dock-open-03.png` |
| 文字正确渲染 | ✅ Windows 通过 | 全部截图（18/22/28 文楷子集 + 44/88 Noto） |
| 没有资源损坏或异常大内存分配 | ✅ 有硬数据 | peak pixels 1,999,872 B，file cache peak 3,597,312 B，均低于 2 MiB 注册上限与 1.5 MiB 缓存预算 |
| 缺少可选资源时不会崩溃 | ✅ 有测试 | `21-album-empty.png`；`lw_art.c` 对可选包 `LW_PACK_ERR_OPEN` 静默跳过；回归里 `LW_VISUAL_ASSETS` 缺省路径同样跑通 |

**这八条在 P4 上全部是 NOT TESTED**（板子没插），见 `BOARD_TEST.md`。

## 时间与昼夜

一次时钟快照喂所有地方：`lw_ui_clock_read()` → 状态栏 / 锁屏 / `lw_ambience_phase()`。
真实时间可信 → 按真实时间；未同步 → 带"· 演示"后缀；手动预览 → 只改会话内氛围，
不改纪念日、不改宠物存档（`v43-time-settings.png` 里那句"仅改变场景氛围，不改纪念日与宠物存档"就是这条约束的 UI 表达）。

只在一个时段**跨越**时重绘（`s->phase != phase` 判据），不会每秒重画背景。

## 已知缺陷（不阻止第一轮上板）

1. **Dock 文字拥挤**：展开态六个入口 76×52，中文标签在 18px 字体下贴边。
   用户已知并同意记录为后续视觉修复，不在本轮扩大范围。
2. RPG 页任务本与右上返回箭头略有重叠（`17-rpg-room.png` 可见）。
3. 相册空态标题与副标题距离偏紧（`21-album-empty.png`）。

## 回归覆盖

`run_windows_regression.ps1`（`reports/logs/v43-board-winreg.log`）：

```
System 22 / Duo 82 / RPG 85 / Storage 33 (handles 58 -> 58) / OS V2 752 checks, 0 failures
100 overlay cycles; peak pixels 1999872 B
File cache: peak 3597312 / 3597312 B; hits 8327; prefetched 8106; evictions 10844
Actual LVGL screenshots: 156
WINDOWS REGRESSION OK
```

OS V2 那一套覆盖了启动/锁屏、Dock 展开收起、控制中心反复开关、RPG 进出、相册、
缺失资源模拟、三种动效模式下的对象数/定时器数不增长（`boot/lock/control lifecycle stays bounded in each motion mode`）。
