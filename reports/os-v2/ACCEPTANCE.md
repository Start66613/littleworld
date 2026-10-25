# OS P0 P1 原型验收

后续P2验收见P2_INTEGRATION.md（2026-10-25晚间）：新入口、真实旧应用导航、锁屏与按钮边界已实现并运行，128项真实系统流程通过。以下保留P0/P1历史结果。

2026-10-25。P0已执行，P1可运行候选已交付；最终视觉需用户确认。本轮没有全面重写其他应用，也不将Windows Mock成功描述为ESP32功能通过。

## 实际交付

工作区D:\LittleWorld-Codex，独立分支agent/littleworld-os-v2，起点afa3233。本轮实现提交4e9fe1d35274e10d65263fc33647a58dc57326c9，固件appdesc为4e9fe1d；验收记录另行提交。P0_AUDIT.md记录仓库/安装/官方代码/角色到货事实；docs/design包含SYSTEM_DESIGN、DESIGN_TOKENS、NAVIGATION、COMPONENTS、ANIMATIONS、THEMES六份规范。lw_os为独立原型入口，lw_os_design提供统一原生控件、原创图标和可分层房间，lw_system提供共享状态和独立偏好。

真实截图均为1024×600 LVGL帧缓冲，经无损BMP→PNG转换，未用Meowa概念图替代。保存于本地忽略目录D:\LittleWorld-Codex\reports\ui-v2\os-v2：

1. 01-desktop.png：状态栏、纪念天数、小信封、房间、像素角色与Dock。
2. 02-control-center.png：活动页面上的独立浮层、真实亮度/音量滑块、网络/勿扰/锁屏/静音/主题入口。
3. 03-wifi-list.png：模拟网络、信号/加密信息、扫描/选择/断开/忘记。
4. 04-settings.png：七类侧栏与系统服务页面，开发者选项藏在关于设备。
5. 05-themes.png：三套场景与整体palette即时预览，确认/取消。
6. 06-lockscreen.png：大时间、日期、纪念信息与无密码模拟解锁。
7. 07-pixel-world.png：原生1× qianqian + TEST A、阴影、场景、行为协调入口。
8. 08-wifi-password.png：真实密码输入和屏幕键盘。
9. 09-moonlight-desktop.png：同一系统切换为夜间主题。

已逐张实际查看首批九张；检查后修复控制中心音乐标题裁切、屏幕键盘默认BOTTOM_MID导致的越界，以及设置触摸行高度。修复键盘后再次查看控制中心和密码页；最终重跑结果与截图以runtime.json为准。候选人物仍明确标记TEST A，正式男方没有擅自导入。

## 实际检查

Windows系统流程73项通过，含真实指针点击/滑块拖动/屏幕键盘输入、模拟扫描/短密码/认证失败/成功/忘记/返回、设置同步、主题预览取消/确认、重载偏好、CRC损坏保护、锁屏解锁。100次弹层循环保持原页对象/timer/lease数；100次页面循环释放替换视图；停止OS后像素lease为0、timer回到基线。

纯C系统服务22项通过，含后端失败不改状态、存储失败可重试、安全亮度下限、完整偏好往返和异步网络状态保护；角色行为82项通过，新增可配置安全区域测试，未改变旧业务。既有核心142项、通用真实LVGL资源94项、Python18项与字体一致性检查通过。原完整UI自动回归通过，对象数84→84；结果见日志/verification.json。

两人当前帧像素393216B，换帧峰值589824B（576KiB）。Windows工作集、CPU时间、壁钟和循环数写runtime.json。这是无等待虚拟时钟测试，不是日常空闲CPU、持续FPS、触摸延迟或ESP32堆/PSRAM实测。页面/定时器/lease计数稳定是已测的生命周期证据，不扩大成所有分配器路径绝无泄漏。

## 固件与硬件界限

ESP-IDF5.5.5、esp32p4 rev3_x/min300、Flash32MB、BSP3.0.1、esp_lvgl_adapter0.6.4、LVGL9.5.0离线编译通过。新增系统和OS代码已编译；默认app_main仍是原版入口，链接器会裁掉未使用代码，BIN不包含完整双人资源，也不代表已安装OS固件。最终BIN哈希/大小与源码提交见verification.json。

提供BSP亮度/音量适配工厂，不自动初始化硬件；Windows控制是Mock。网络提供异步接口，但ESP端仍unsupported，待官方esp_wifi_remote/esp_hosted链路审查与后续授权测试。真实音频播放、NVS设置、SD/自定义图片、网络校时、背光熄灭与实体唤醒尚未验收。本轮锁屏不调用MCU休眠。

固件分区和lw_core/arcade_core/main相对稳定基线未改变。原qianqian裸帧包22.2MB仍只用于Windows，不能部署到现有4MiB资源分区；本轮代码房间无需全屏背景解码。字体增加的Flash成本已反映在离线BIN，未把桌面内存或const当作PSRAM零成本。

## 修复与复现

首轮主机流程虽通过，人工验图发现键盘排出屏幕；修正TOP_LEFT并新增实际输入检查，不能用通过数掩盖截图问题。字体生成曾遭遇Node写目标文件UNKNOWN错误，改为D盘build/fontgen临时生成、规范化输出头、哈希变化才复制；-Check现在可复现通过。IDF首次对4处单行控制语句报misleading-indentation，已修正；初次失败日志保留。

入口：pwsh -NoProfile -File tools/run_os_v2.ps1；加-Test自动构建、模型检查、真实交互及本地截图。字体源缺失时只读复用D:\LittleWorld-Main-DSH\tools\fonts，不重新安装。构建并发固定2。测试存档隔离在截图目录/.state，交互存档在D盘build/host/LittleWorld-data；不覆盖用户现有记录。

日志在reports/logs/os-v2-*.log（不提交）；构建在build/host和build/firmware-v2。原版应用完整回归使用--auto独立输出目录，不能在其EXE仍运行时重链接。不要结束用户原来已打开的其他LittleWorld进程。

## 素材与下一步

已读取DSH实际PIXEL_MANIFEST.json batch2，男方215px/64色、女方198px/716色，并有头身/描边/明暗差异。精确路径、哈希、目标和禁止分数缩放的修订要求见docs/handoff/DSH_ART_TASKS.md；只读快照已交接D:\LittleWorld-Meowa\handoff\codex-os-v1\TO_DSH.md，未假称DSH已阅读。当前测试人偶不等于DSH候选，候选也不等于最终通过。

Meowa复用已安装官方game-assets技能和CLI，已认证读余额2904；本轮付费job0、消耗0，无新批次预算。未读取私人照片、未上传参考图/密钥/存档/Flash备份，未推送角色图或代码；无串口/刷写/擦除/分区/C6/eFuse操作。

下一步先由用户审阅桌面、控制中心、主题/锁屏和场景构图；男方需单独形象确认。其后才进入正式Shell迁移、角色定稿/动作、应用视觉与硬件接入。剩余项明确列在STATUS.md，不把原型称为完整OS成品。
