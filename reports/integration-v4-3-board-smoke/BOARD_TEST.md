# 实板测试清单

**全部 NOT TESTED。** 本轮结束时没有任何串口设备连接（`serial ports found: 0`），
所以下面每一项都是"待做"，不是"通过"。

板子接上后按 A → B → C → D 顺序做，每做完一段就把结果填进本文件的"结果"列。

## A. 显示

| # | 检查 | 怎么看 | 结果 |
| --- | --- | --- | --- |
| A1 | 正常启动 | 上电后无重启循环，串口出现 `boot: Little World ready` | NOT TESTED |
| A2 | 分辨率正确 | 1024×600，无黑边、无缩放拉伸 | NOT TESTED |
| A3 | 背景完整 | 客厅场景铺满，不是纯色块（纯色=场景没读到卡） | NOT TESTED |
| A4 | 不花屏/蓝屏 | 无撕裂条纹、无整屏闪蓝 | NOT TESTED |
| A5 | 不明显闪烁 | 静置 60 秒观察背光 | NOT TESTED |
| A6 | 亮度变化 | 拖控制中心亮度滑轨，屏幕应连续变亮/变暗 | NOT TESTED |

> A3 的判断依据：串口里 `card: N/N packs` 与 `scenes: ...`。如果打印的是
> `card: no card` 或 `content: no card`，先查 TF 卡路径（见 `SD_CARD_PACKAGE.md`），
> 不要先怀疑固件。

## B. 触摸

| # | 检查 | 结果 |
| --- | --- | --- |
| B1 | 点击 Dock 图标能进页面 | NOT TESTED |
| B2 | 底部 120×44 区域轻触/上滑能展开导航 | NOT TESTED |
| B3 | 4.5 秒空闲后导航自动收起 | NOT TESTED |
| B4 | 控制中心能开、能关、能重复开关 10 次不卡 | NOT TESTED |
| B5 | 顶部状态组（x=900–1012）点击能开控制中心 | NOT TESTED |
| B6 | 设置页各分区能点开 | NOT TESTED |
| B7 | 弹窗（奖励/信纸）能被点掉 | NOT TESTED |
| B8 | 锁屏上滑能解锁 | NOT TESTED |

## C. 美术

| # | 检查 | 结果 |
| --- | --- | --- |
| C1 | 男方正常显示（非占位图） | NOT TESTED |
| C2 | 千千正常显示 | NOT TESTED |
| C3 | 沙发坐姿位置正确（坐在坐垫上，不是悬空/陷进去） | NOT TESTED |
| C4 | 小狗在狗窝里（组合状态，不是贴上去的） | NOT TESTED |
| C5 | 时间与字体正确（19:30 演示态带"· 演示"） | NOT TESTED |
| C6 | 昼夜切换正确（设置页五个时段预览） | NOT TESTED |
| C7 | UI 皮肤显示（控制中心九宫格面板） | NOT TESTED |

## D. 稳定性

| # | 检查 | 结果 |
| --- | --- | --- |
| D1 | 页面连续切换 30 次不崩 | NOT TESTED |
| D2 | 控制中心反复开关 20 次 | NOT TESTED |
| D3 | 多次进出 RPG | NOT TESTED |
| D4 | TF 卡读取（拔卡/插卡各试一次） | NOT TESTED |
| D5 | PSRAM 余量（串口 `lw_diag` 行） | NOT TESTED |
| D6 | 内存峰值/最大连续块 | NOT TESTED |
| D7 | 启动原因与复位原因（`esp_reset_reason`） | NOT TESTED |

## 上板时的具体命令

```powershell
# 1. 只读识别
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 -Probe -Port COM5

# 2. 只写 app
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 `
    -Stage app -Port COM5 -Execute -Authorized

# 3. 抓 30 秒启动日志
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\serial_capture.ps1 -Port COM5 -Seconds 30

# 4. 需要截图（固件里预置了 LCAP 协议）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\board_shot.ps1 -Port COM5 -Out reports\board-v43
```

## 预期会看到的串口关键字

| 关键字 | 含义 |
| --- | --- |
| `card: 6/6 packs` | 六个包全部加载（理想） |
| `card: 5/6 packs; skin.lwp:...` | 某包失败，会指出文件名与原因 |
| `card: no card` | 没有 TF 卡或校验失败，回退内置画面 |
| `content: v1, 3 memories, ...` | 内容文件读到 |
| `console ready on UART0 (shot / tap / status / diag / diagjson / content / clock)` | 控制台命令可用 |

## 如果出问题

1. **不要**先 `erase_flash`，先看串口第一段日志（panic 会给出 MEPC/RA 与函数名）。
2. 纯色背景但系统能跑 → 多半是卡路径或包被拒，把 `content`/`diag` 命令的输出发回来。
3. 起不来 → 按 `FLASH_RECORD.md` 的回滚步骤刷回 V3.1。
4. 把日志文件路径发给我，我按日志定位。
