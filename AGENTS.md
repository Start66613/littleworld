# Little World 2.0 · Codex 工作区规则

Meowa生产前必读 docs/meowa/PRODUCTION_WORKFLOWS.md 和官方对应模块/当前CLI帮助/产品参数契约。先核对原生角色可见高度、1024×600共同尺度与家具接触，再收费；禁止用缩小主角掩盖内存问题。多素材互动先做共同关键帧；生成式提取不当精确遮罩。文档/参数检查、生成样本、LVGL回归和用户美术验收分别报告；不为覆盖功能消耗积分。

V4.2美术纠正指令优先：用户明确要求停止手工切割/拼排房间，改用官方Meowa生成完整场景、动作和UI；本轮收费在前轮剩余831积分内逐笔核价、记Job、余额与验收，不重复已扣费任务。拒绝的拼接素材已撤至private/rejected-collage，不交付、不作为生成参考。仅作必要的转码/透明边界/自动分帧和LVGL几何适配。以下此前不收费限制由这次明确生成指令更新。

V4.2（2026-10-26）用户任务：从94efbf5建立D:\LittleWorld-V4-2 / agent/visual-v4-2-polish，精修RPG、宠物日常、开机、控制中心与应用UI。原本优先免费编辑，已由最新用户明确批准在剩余831积分内按需使用官方Meowa；V4固定角色和房间已获方向认可，允许本轮新UI/衍生资源/真实截图按既有交付授权推送。保留rpg核心/存档格式，UI可提供匹配美术的场景几何与旧位置安全恢复；角色保持正式256原生画布与共同视觉尺度；日常共同互动按实际关键帧验收，不能为缓存把RPG主角缩成128。运行不做分数平滑。保持.lwr/LWPX格式和现有lease契约。不得修改DSH的BSP、网络/音频/显示/背光、firmware/main、分区、真实挂载、NVS、刷写工具；无设备操作。所有新增文件/构建/缓存在D盘。

V4用户任务优先（2026-10-26）：工作区D:\LittleWorld-V4-Visual，分支agent/visual-v4-meowa-motion，基线release/os-v3.1-stable@d95b806。允许使用官方最新Meowa完整Skill，在1000积分硬上限内生产场景/现有角色动画；逐笔核价、记Job ID、余额和结果，失败先恢复原Job。默认男方固定Codex male-anchor-aligned，女方固定原版Qianqian，继承DSH房间；可直接完成本轮美术选择与集成，无须等待睡眠中的用户确认。允许公开本轮交付的生成素材、转换资源、动画预览和真实LVGL截图，不公开真人私照、密钥、私有认证、存档或Flash备份。以下旧无预算/待确认/仅本地的冲突限制以本轮明确授权为准。禁止修改DSH显示/背光/驱动、firmware/main、业务核心、冻结RPG、分区；不连接设备、不刷写。所有新增文件/构建/备份在D盘。高亮频闪未解决，不加入快速明暗、整屏闪光或高频闪烁。Full/Reduced/Off和生命周期检查是交付条件。

最新追加授权（2026-10-25）：用户要求将成功运行的新UI截图也推送GitHub。允许公开reports/os-v3/SCREENSHOTS.json明确列出的31张渲染后PNG（包括Qianqian画面），发布副本在reports/os-v3/screenshots；此条覆盖下方旧的截图仅本地限制，但不授权公开角色源文件、资源包、私照、密钥或存档。

本轮P5用户授权优先：分支agent/os-v2-experience-v3，基线31dba23，工作区D:\LittleWorld-Codex。主要修改UI/Host测试/规范；禁止修改firmware/main、lw_system硬件服务、lw_storage、冻结rpg核心及其85项测试、lw_core/arcade_core算法。新能力仅定义UI契约和Host样例，不抢DSH后端实现。允许推送本分支代码/非敏感报告；本轮31张UI截图按上述追加授权公开。无Meowa收费、无设备连接、无刷写。旧P0/P1范围不阻塞本轮已授权应用体验升级。

集成副本 D:\LittleWorld-Codex-Integration / agent/os-v2-rpg-integration；验证后同步正式Codex工作区 D:\LittleWorld-Codex / agent/littleworld-os-v2。rpg/ 与 tests/host/test_rpg.c 固定来源70484cfc1e585a70d403b23f93aae8802ab1fdbf，禁止修改；UI宿主、测试与文件缓存由Codex维护。

适用本工作区全部文件。本轮用户明确授权 P2 正式 Windows 系统入口、P3 RPG 集成、P4 文件资源基础设施及推送代码。以下旧 P0/P1 范围限制以此更新为准；固件稳定入口保留。
- 本轮男方 Meowa 挑战已在独立 agent/codex-meowa-challenge 分支完成一次，禁止自动追加收费。代码和男方测试图可以公开；所有含 Qianqian 的新图留本地。已有历史不擅自重写。
- OS 在 agent/littleworld-os-v2；RPG 跨分支集成另建独立分支，固定读取 DSH 的纯 C 文件，不能修改其核心。继续复用旧业务，允许按本轮要求完成导航。
- 工作区 D:\LittleWorld-Codex，当前分支 agent/littleworld-os-v2；双角色基线 afa3233 保留于 agent/pixel-duo，稳定基线 8af0637，绘本探索归档于 agent/ui-v2。不得修改 DSH 的 D:\LittleWorld-Main-DSH 源码或覆盖其工作。
- 所有新增工程文件、资源、缓存、临时文件、模拟器存档、报告与构建均放 D 盘；不清理用户其他文件。
- 当前不连接开发板。禁止串口连接、刷机、擦除、分区更改、C6/eFuse/OTA 操作。只允许本机开发、测试、模拟器及固件编译；编译通过不代表板测。
- 官方 ESP-IDF 5.5.5 / esp32p4 rev3_x / 7B BSP 3.0.1 / esp_lvgl_adapter / LVGL 9.5.0；硬件参数只以固定官方代码为依据。
- 保留 lw_core、arcade_core、日期/宠物状态机、存档、三款小游戏和原版 UI 基线；不为视觉重写业务。
- 用户已确定像素双角色：女方 qianqian 来自 DesktopPet；男方由 DSH 按私人真人参考生产。奶油团子兽、苔藓精灵、围巾小狐不再作主角；小雪球是独立辅助狗。
- Codex 独占维护像素契约/架构/返工文档；DSH 只读。DSH 独占素材、ASSET_MANIFEST.json、ASSET_REPORT.md；旧契约快照保留但已失效。
- 通过 D:\LittleWorld-Meowa\handoff\codex-pixel-v1.0.1 的只读快照交接，当前版本指针 handoff/PIXEL_CONTRACT_CURRENT.json；初稿 v1 保留但已替代，不依赖代理互发消息。
- 未获用户视觉确认、未验收素材前，不将生成素材替换默认页面；测试纹理必须标注技术测试，不能冒充正式角色或模拟器最终效果。
- 1024×600；像素角色 native 1× 或整数倍最近邻缩放，无平滑/分数缩放。固定脚底锚点；统一裁切/补边有可追溯变换，不逐帧自动居中。不能把 .tres 当 LVGL 图片，不展开全套动作到 RAM。
- 两人独立位置/朝向/动画/状态；睡眠不能被自主互动叫醒；双人先站位、再同步动作，不穿透、不瞬移。保持现有宠物存档与纪念日计算，P1 新状态先仅会话内，不擅自升级存档格式。
- 角色源文件、导出图集/像素包、包含角色的截图置 private/pixel-duo/ 并忽略；未经用户确认且明确授权公开，不提交公共仓库、不推送。文档/代码可本地提交。
- 不假定 PSRAM 容量或全部可用；现有 6 MiB app / 4 MiB storage 不改。PSRAM XIP 可使常量资源消耗 PSRAM，不能把 const 当零成本。
- 私照、密钥、存档、原始 Flash 备份禁止上传公开 GitHub。本轮用户授权推送代码及男方测试图，含 Qianqian 的新图禁止推送。
- 本轮先做系统桌面、控制中心、Wi-Fi 完整 Mock 流程、设置首页、主题预览和锁屏的实际 LVGL 原型；用户确认设计前不全面改造旧 App。通过独立 --os-v2 入口运行，不替换稳定固件入口。
- 复用 D:\LittleWorld-Meowa\tools\meowa-skills\skills\game-assets 的官方技能和现有本地认证。Codex 可准备 UI 美术；新付费任务必须先提交清单/单价/总价并获用户批次预算，当前无新预算。不得复制、打印密钥或上传私人参考图。
- Windows 硬件服务必须标明模拟；控制中心和设置共享服务状态。实际 BSP 适配、离线编译和板测分别记录。OS 设置使用独立版本化记录，不改旧存档。
- 包含角色的本轮截图置于忽略的 reports/ui-v2/os-v2，不提交图像；代码、设计规范及非敏感报告可本地提交。
