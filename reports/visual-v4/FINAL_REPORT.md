# Little World V4 · 最终交付

日期：2026-10-26（Asia/Shanghai）。工作区 `D:\LittleWorld-V4-Visual`，独立分支 [`agent/visual-v4-meowa-motion`](https://github.com/Start66613/littleworld/tree/agent/visual-v4-meowa-motion)，基线 `release/os-v3.1-stable@d95b806635659a1b3cf0282e306c9e603e7c734a`。本次实际编译、截图与回归源码为 **`96aee9b59126ff3cb4aacd8c05fd870dc814ca28`**；随后提交仅增加报告、预览和交接资料。分支最终交付提交由 Git 记录，不把报告提交冒充重新构建的固件版本。

**先看 [完整视觉预览](README.md)**：三套真实桌面、双人打招呼、开机、跟手控制中心、解锁和信封都在同一入口。双击项目根目录 `Start-Visual-V4.bat` 自动构建并打开可操作模拟器。

## 实际完成

1. **完整官方 Meowa Skill 已备份、同步并验证。** 安装目录 `D:\LittleWorld-Meowa\tools\meowa-skills\skills\game-assets`，官方提交 `33578ab708c1b4dfcd83fe79dffdbd47ff4083b2`；原/新 CLI 均为 `2026.10.23.2`，fast-forward 检查显示原安装已经最新，未伪造升级。完整目录和 LF 规范化 runner 哈希校验通过，认证原样保留。详见 [Skill 报告](MEOWA_SKILL_UPDATE.md)。
2. **三套完整场景已生产、审查并运行。** Sakura Morning / Strawberry Sunset / Moonlight Dream；每套保留 2048×1152 母版、1024×600 PNG、RGB565 运行版及缩略图。落日继承 DSH `364e985eba6d09020f20b669362d995b6ecf043c` 的房间母版，晨光/月光在其结构上生成，没有重画简陋房间。统一半尺寸最近邻转 1024×576 后上下各补 12px，不拉伸透视；没有烘焙人物或 UI 文字。
3. **统一美术规范** 在根目录 [ART_DIRECTION.md](../../ART_DIRECTION.md)，包含色板、光线、角色/家具/像素比例、文字与触摸尺寸、层级、动效和内存规则；共同几何见 [HIT_REGIONS.json](../../assets/visual-v4/HIT_REGIONS.json)。
4. **固定身份的角色动画已接入。** 男方沿用 Codex `c66af89` 的原角色，新增呼吸 8 帧、眨眼 4 帧、开心挥手 16 帧；千千保留 DesktopPet `b00a7005f4889644737092dbc7379e2a9b011315` 原版身份和 13 动作/113 帧包，额外增加 16 帧轻挥手/开心表情。双人先靠近、再同步挥手、1600ms 后返回待机，实测确认选择了双方真实 wave clip。
5. **逐帧资源而非 AI 视频。** 每动作有透明 PNG、帧序/时长 JSON、原始 Meowa WebP、转换记录、GIF/无损 WebP 预览；256×256，固定脚底锚点，不逐帧自动居中。挥手按既有核心时长从 16×125ms 重定时为 16×100ms；男方仅作统一 y=-1px 补偿。眨眼用已验收挥手闭眼像素与原呼吸帧局部复用，0 积分，变换可追溯。原版千千待机不被新生成版本替换。
6. **真实共享 UI 组件** 新增 `lw_motion.c` 和 `lw_visual_assets.c/.h`，接入原有 OS、相册、回忆、对话/奖励与 Host 注册器。业务、存档和冻结 RPG 不重写。
7. **桌面成为可操作小屋。** 三套光照、两人生活、轻纸签纪念区、独立小信封、Dock；窗户切主题、沙发进双人世界、墙上相框进相册。照片与信封命中区不重叠；小组件隐藏和地板长按编辑在插画分支也有效。
8. **控制中心/锁屏/开机是真实动效。** 下拉期间跟手、松手弹性吸附、短拖取消、上推关闭；锁屏复用、场景/角色待机、上滑揭开桌面；启动层约 1200ms 自动退出或点击跳过。声音和硬件能力保留明确模拟/不可用状态。
9. **系统级原生动效已覆盖。** 页面正反方向、Dock 反馈、卡片进入、模态打开/关闭、主题淡显、信封封口与信纸、照片放大、回忆展开、RPG 对话/奖励、Toast 和长按。Full / Reduced / Off 共用策略，尊重保存的动画关闭设置。详见 [动效与验收](MOTION_ACCEPTANCE.md)。
10. **67 张真实 LVGL 帧缓冲和 5 段运行预览** 已整理公开，PNG 与同名 BMP 逐像素一致；另附 1 张稳定基线 Windows 截图。采样运行预览与 Meowa 单角色素材预览明确区分，未用 Python 合成概念图冒充运行结果。
11. **原始和可转换资源已入库。** 位于 `assets/visual-v4`；`tools/prepare_visual_v4.py` 可从公开母版/原始动作副本重建运行资源，不依赖被忽略的付费原始下载目录，不会调用 API。全部原版千千资源包仍保存在本机，不公开私人存档、认证或 Flash 备份。
12. **消费受控且生产结束。** 5 笔成功任务实际共 **169 / 1000**，余额 2704→2535，剩余授权 831。没有为用完预算生成无用途资源；下述任务及 [账本](MEOWA_JOBS.json) 记录逐笔用途、输入、输出和验收。
13. **资源受限而非全展开。** 场景一个 full + 三 thumbnail；原版 Q、男方和新增 Q 动作分别有限文件缓存/预读，退出释放，实测与预算见下节。
14. **Windows 完整回归和 ESP-IDF 离线编译通过。** 具体检查数、日志哈希、二进制 SHA-256、受保护路径和测试源码在 [VERIFICATION.json](VERIFICATION.json)。编译产物不公开为可刷机固件。
15. **后端交接已写明。** [DSH_VISUAL_V4_RESOURCES.md](../../docs/handoff/DSH_VISUAL_V4_RESOURCES.md) 说明场景路径配置、格式、线程、租约、角色包区别和未来硬件集成要求；没有修改 DSH 独占底层。

## Meowa 任务与账单

收费前实际读取新 CLI/参考文档和报价，余额查询成功后才提交；没有上传用户真人照片。所有余额差与该任务报价一致：

- 男方呼吸：`animate-run`，`job_e4fd8f65087c42aa875a1521ae7208e1`，35。
- 樱花晨光：`general-image-run` / image-2.5，`job_ddaea059d1274b868e193d5af17084a4`，22。
- 月光梦境：同上，`job_9786e2bc1b33426e951822c844dc375e`，22。
- 男方挥手：`meowa-animation-run`，`job_f8c64c1f9a0842a09933adf436e65af7`，45。
- 千千挥手：同上，`job_c7484cd69b324e76a7a84ca7576fac52`，45。

晨光首次提交在 TLS 握手阶段失败，没有 Job ID、未被受理、余额未减少；核对后才重新提交。另一次余额查询失败时脚本在付费调用前停止。没有对已收费 Job 盲目重复提交，也没有对官方 Runner 作代理替换。详见 [预算说明](MEOWA_BUDGET.md)。

## 最终检查与资源成本

- 正常资源 **489**、缺角色 **472**、缺场景 **487** 项，全部零失败；100 次页面、浮层、快速浮层和锁屏复用，以及 Full/Reduced/Off 各 20 次循环。
- 系统模型 22、双角色模型 82、RPG 核心 85、真实文件存储 33；旧业务核心 142、资源生命周期 94、Python 工具 18，全部通过；字体 569 个非 ASCII 字符子集与源码一致。
- 原版完整 UI 自动回归通过，对象 **84→84**。素材审查 **114 项、零失败**；尺寸/透明通道/帧时长/像素转换/资源包边界和预算核对通过。
- 场景缓存实测峰值 **1,459,200 B**，OS 停止后 **0 B / 0 引用**；角色文件缓存 **1,572,864 B**，角色 lease 峰值 **983,040 B**。三者保守相加约 **3.83 MiB**，不含渲染/显示/字体/系统。
- Full 照片缩放可能另需约 **1.44 MiB** 瞬时渲染层和额外对齐成本，Reduced/Off 去掉缩放；不能用资源缓存统计代替整个系统 PSRAM 需求。最终 Windows 工作集峰值约 12.25MB，为 Host 指标，不是 ESP32 RAM。
- ESP-IDF **5.5.5**、目标 esp32p4、rev3_x 最低 300、Flash 32MB、BSP 3.0.1、esp_lvgl_adapter 0.6.4、LVGL 9.5.0。独立目录 `D:\build\visual-v4`，BIN **2,256,624 B / 0x226ef0**，既有 6MiB app 分区余约 64%，分区未改。

## 复现与日志

已有 D 盘依赖可直接运行，无需人工逐条搭建：

```powershell
# 交互运行；或双击 Start-Visual-V4.bat
pwsh -NoProfile -File tools/run_visual_v4.ps1
# 完整 Windows 回归，自动构建/运行/采集
pwsh -NoProfile -File tools/verify_visual_v4.ps1
```

资源审查与公开证据整理使用现有 Python：

```powershell
D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe tools/audit_visual_v4.py
D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe tools/collect_visual_v4_evidence.py
```

本轮离线编译通过 `tools/activate_env.ps1` 激活既有 IDF，使用本工作树独立 managed_components、依赖锁和 `D:/build/visual-v4/sdkconfig`，执行 `idf.py -C firmware -B D:/build/visual-v4 -D SDKCONFIG=D:/build/visual-v4/sdkconfig -D IDF_TARGET=esp32p4 reconfigure build`。未执行构建输出建议的任何设备命令。

原始运行/失败日志留在本机忽略目录 `reports/logs/v4-*.log`，脱敏摘要见 [BUILD_AND_TEST_LOG.md](BUILD_AND_TEST_LOG.md)，最终日志 SHA-256 在验证清单。所有新工程、缓存、备份、模拟器数据在 D 盘；没有卸载或覆盖用户既有环境。

## 尚未完成及明确限制

- **未板测、未刷机、未连接串口。** DSH 显示/背光/驱动、`firmware/main`、后端/网络/存档、业务核心、冻结 RPG 和分区与基线相同。高亮度可见频闪仍未定位，不加入亮度振荡/整屏闪光。没有板上固定 FPS 或稳定显示结论。
- **离线编译不代表新美术已在固件入口加载。** 当前稳定硬件入口保留旧嵌入资源；本轮新增 RGB565 场景和 LWPX 动作已在 Windows 真正使用，硬件 SD/VFS 路径配置、等价动作加载器和联合固件集成仍需 DSH 后端接入及未来许可后的板测。
- 三场景目前是单张房间底图，人物/UI/交互独立；未拆可绕行家具的前景遮罩。角色约束在地毯安全区，因此当前站位避开家具，不能据此宣称任意房间深度遮挡已完成。
- 主题使用单全图缓存下的新页淡显，未实现同时驻留双全图的交叉溶解；场景新图读取当前为同步。硬件存储延迟和预读策略尚待实际 SD 性能评估。
- 相册、音乐、回忆、小游戏、设置和宠物沿用已完成的 P5 业务/布局并接入主题和原生动效，不声称本轮为所有应用重新生产独立美术。Host 音频仍模拟；照片/故事是明确技术样例，不导入或编造真实私人内容。
- 可选的新宠物房、完整 RPG 探索美术、男方四向走/转身/坐下/睡觉/宠物动作未生产。冻结 RPG 核心已能进出和操作，但仍用标注 A/B 的测试角色；保留功能，不冒充正式人物动画。
- 原始呼吸结果没有明显眨眼，因此采用可追溯的局部像素复用补齐；逐帧已人工看过脸、肢体、衣服、锚点、首尾和透明边缘，但生成挥手的局部像素/表情仍须用户视觉验收，程序通过不代表美术定稿。

用户醒来后可先看三主题与双人动作，再判断角色表情和节奏是否符合心意。当前交付不依赖后续定时执行，没有创建夜间后台自动化或承诺尚未发生的工作。
