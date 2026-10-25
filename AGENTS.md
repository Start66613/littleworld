# Little World 2.0 · Codex 工作区规则

集成工作区 D:\LittleWorld-Codex-Integration / agent/os-v2-rpg-integration。rpg/ 与 tests/host/test_rpg.c 固定来源70484cfc1e585a70d403b23f93aae8802ab1fdbf，禁止修改；UI宿主、测试与文件缓存由Codex维护。

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
