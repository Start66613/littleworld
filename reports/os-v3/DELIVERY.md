# P5 · OS V2 体验升级交付

2026-10-25；工作区 `D:\LittleWorld-Codex`，独立分支 `agent/os-v2-experience-v3`。开始前已 fetch 并确认 OS 基线为 `31dba23d8f14e414b95dad46c8426cd3e61583c8`。本轮复用 P2/P3/P4，不重新开发业务和 ESP32 后端。代码及非敏感报告可公开。用户随后追加授权发布本轮成功运行的31张UI截图（包括含Qianqian的画面），PNG已收录于[screenshots](screenshots/README.md)；角色原始资源保持私有。

## 实际实现

- 桌面：左侧纪念卡片、今日信封与右侧房间重新分配空间；窗户、沙发、相框分别能打开主题、生活互动、相册。角色独立于房间绘图，脚下阴影、双人前后排序、家具前景和 Dock 分层；原版 Qianqian 保留，男方仍明确标记 TEST A。日间/月光/绿色主题共用布局；夜间选中 Dock 保留浅底并加描边，避免图标失去对比。
- 控制中心与设置：统一线条图标、滑块百分比和按压反馈；音乐入口进入真实音乐页，主题入口去重。Wi-Fi、亮度和音量未接入时禁用，音乐页音量也遵守能力状态；后端拒绝调音量时回滚并提示，不能假装成功。二级动态设置和返回路径可操作。
- 锁屏：时钟、日期和纪念日文字放在独立安全区域；上扫超过 72px 松手解锁，短扫取消，也可点按钮。重复打开 100 次复用一个锁屏实例；时钟只更新文字，能力变化不会意外解除锁屏。
- 动效：页面先退出再入场、Dock 按压回弹、控制中心侧入、对话弹入、提示滑入、任务奖励回弹、主题渐变分别有时序。完整/减少/关闭三档；关闭沿用既有存档，减少仅本次会话。页面删除取消关联动画，快速关闭又打开会清理退出中的旧浮层。旧宠物 UI 的呼吸/晃动也遵守减少/关闭。
- 相册：OS 默认精致空状态，明确选择后才看代码绘制示例；照片列表、全屏、左右手势、上一张/下一张、错误提示、重试、返回均可用。增加借用照片源接口，一次持有一张解码图；真实文件技术样例已验证加载与释放。未实现任意照片目录扫描或 PNG/JPEG 解码后端。
- 回忆与音乐：纪念日沿用现有日期计算；日期触摸按钮扩大，时间线和故事详情重新排版，长正文可滚动。可注入故事源，样例明确标识，未编造私人经历。音乐使用唱片式封面、曲目信息、可拖动模拟进度、切歌/暂停反馈，持续显示“没有真实声音输出”。
- 小游戏：2048、贪吃蛇、打砖块算法未变；宿主新增暂停、继续、返回游戏中心/桌面，暂停期间确实停止算法推进，恢复打砖块时不会补算暂停时间。
- RPG：保留冻结纯 C 核心；LVGL 房间改用绘图地板和对应地图阻挡的家具，增加落点标记、角色位置插值、物品提示、收集徽章、任务本、对话、奖励弹入与独立存档恢复。角色 A/B 是明确的技术测试标记，没有用未确认角色替换动画。

## 已执行的验证

最终源码版本、二进制哈希和日志清单见 `VERIFICATION.json`；机器生成的实测值见 `runtime.json`。

- Windows OS：含角色资源 340 项、缺失角色资源降级 340 项，均零失败；覆盖真实指针点击、拖动、滑动和 LVGL 帧缓冲输出。
- 系统模型 22、双人模型 82、冻结 RPG 核心 85、真实文件缓存/worker 33、旧业务核心 142、LVGL 资源 94、Python 工具 18 项通过；字体生成一致性检查通过。
- 原版完整 UI 自动回归通过，对象数 84→84。另测 100 次浮层循环、100 次页面循环、100 次快速关闭/重开、三个动态档各 20 次跨页循环及 100 次锁屏复用。
- 对象/timer/lease/动画按进入前基线比较，退出后角色 lease 为 0，照片持有数为 0，主题上下文为 0。退出 OS 恢复旧首页时原有呼吸动画恢复，动画数 1→1 是基线，不是泄漏。
- ESP-IDF 5.5.5 / esp32p4 / rev3_x / 32MB 本机编译成功。只编译现有稳定固件入口及共用 UI 源码，没有合入 DSH 新后端入口；链接裁剪意味着这不是完整 OS V3 与硬件后端联合运行证明，更不是板测。

31 张 1024×600 PNG 来自真实 LVGL 帧缓冲 BMP 的无损转换，均在忽略目录 `D:\LittleWorld-Codex\reports\ui-v2\os-v3`；缺资源回归和原版回归分别在相邻的 `os-v3-fallback` / `os-v3-legacy`。用户追加授权后，将这31张PNG按原校验值复制到公开目录 `reports/os-v3/screenshots`；`SCREENSHOTS.json` 记录公开路径、尺寸和校验值。已人工查看桌面、夜间、控制中心、锁屏、相册/错误、纪念卡片、音乐、动态设置、暂停层及 RPG 房间/任务/对话/奖励的文字与层级；美术质量仍由用户验收。

## 内存和适用范围

- 本轮没有新增高清背景包，也没有一次展开全套动画。沿用文件缓存预算 786432B（768KiB），实测峰值不超预算；LVGL 角色帧像素峰值 589824B（576KiB）。这些统计口径不同，不应当作整个固件峰值。
- 照片源一次至多一张，接受的数据上限为 2MiB；Host 样例每张 12288B，实际峰值一张。2MiB 是接口上限，不是 ESP 已通过压力测试的承诺；解码器、对齐与缓存开销还需 DSH 评估。
- 主题渐变在同一个绘图对象上叠绘旧/新场景，额外上下文 8B；没有额外两张全屏壁纸缓冲。过渡仍增加绘图工作量，可用减少/关闭档降级。
- 桌面对象实测 70，OS 运行时 LVGL timer 总数 8，包含框架和已暂停 timer，不能解释成 8 个新增活动服务。Windows 工作集与虚拟时钟数据记录在 runtime.json，不代表 ESP PSRAM 用量、真实 FPS 或触摸延迟。

## 问题修复与保留日志

实现期间发现并修复：LVGL 9 枚举名称不符；相册 image cache 头文件缺失导致 IDF 编译失败；照片手势冒泡至 OS 导致无法切换；RPG 位置插值反复重启导致角色移出房间；旧首页呼吸动画被错误当作 OS 泄漏；音乐在服务不可用/拒绝时仍改变偏好；夜间 Dock 选中态对比度不足。首次失败日志保留于本地 `reports/logs/p5-*`，最终检查使用 `p5-final-*`，未将失败结果描述为通过。

## 复现

在 `D:\LittleWorld-Codex` 使用已有 D 盘工具链，脚本自动构建和运行，不需要用户逐条人工操作：

```powershell
pwsh -NoProfile -File tools/run_os_v2.ps1 -Test -OutputDir reports/ui-v2/os-v3 -ReportDir reports/os-v3
build/host/LittleWorld.exe --os-v2-test - reports/ui-v2/os-v3-fallback
build/host/LittleWorld.exe --ui-v2-test reports/ui-v2/technical
build/host/LittleWorld.exe --auto reports/ui-v2/os-v3-legacy
pwsh -NoProfile -File tools/run_host_tests.ps1
D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe -m unittest discover -s tests -p 'test_*.py' -v
pwsh -NoProfile -File tools/gen_fonts.ps1 -Check
. tools/activate_env.ps1
idf.py -C D:/LittleWorld-Codex/firmware -B D:/build/firmware-v2 build
```

正常体验运行 `pwsh -NoProfile -File tools/run_os_v2.ps1`；`build/host/LittleWorld.exe --legacy` 保留旧版入口。角色包须使用本机已有私有资源；角色包保持私有；本轮31张PNG按用户追加授权公开。未执行编译日志建议的任何设备命令。

## DSH 交接与尚待完成

正式接口见 `docs/handoff/DSH_UI_CAPABILITIES_V3.md` 和 `lw_ui_content.h`，只读快照交付到 `D:\LittleWorld-Meowa\handoff\codex-ui-v3`。已只读核对 DSH 的能力表、启动和 RPG 宿主接口，未修改或合并其工作目录；交付文件不代表 DSH 已确认。

后续仍需：用户验收系统视觉与正式男方角色；DSH 接入真实音频 transport、照片解码/资源源、故事持久化和能力状态，减少动态的版本化持久化另议；动态中文正文需要完整字体覆盖方案；联合后端固件编译与未来获许可的板上触摸、PSRAM、SD 速度、长时稳定性验证。本轮没有重做 P4 文件缓存或 TF 驱动，没有新增硬件能力声明。

安全边界：main、原 OS 分支、DSH main/硬件服务、NVS/存档后端、冻结 RPG 核心和小游戏算法保持原样；没有串口访问、刷机、擦除、分区修改、C6/eFuse 操作或 Meowa 收费调用。
