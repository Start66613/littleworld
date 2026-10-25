# Little World 2.0 · Codex 工作区规则

适用本工作区全部文件。先读 docs/ART_CONTRACT.md、UI_V2_DESIGN.md、ANIMATION_CONTRACT.md、UI_V2_STATUS.md。
- 工作区 D:\LittleWorld-Codex，分支 agent/ui-v2；基线 8af0637。不得修改 DSH 的 D:\LittleWorld-Main-DSH 源码或覆盖其工作。
- 所有新增工程文件、资源、缓存、临时文件、模拟器存档、报告与构建均放 D 盘；不清理用户其他文件。
- 当前不连接开发板。禁止串口连接、刷机、擦除、分区更改、C6/eFuse/OTA 操作。只允许本机开发、测试、模拟器及固件编译；编译通过不代表板测。
- 官方 ESP-IDF 5.5.5 / esp32p4 rev3_x / 7B BSP 3.0.1 / esp_lvgl_adapter / LVGL 9.5.0；硬件参数只以固定官方代码为依据。
- 保留 lw_core、arcade_core、日期/宠物状态机、存档、三款小游戏和原版 UI 基线；不为视觉重写业务。
- Codex 独占维护 docs/ART_CONTRACT.md、UI_V2_DESIGN.md、ANIMATION_CONTRACT.md；DSH 只读。DSH 独占素材、ASSET_MANIFEST.json、ASSET_REPORT.md。
- 通过 D:\LittleWorld-Meowa\handoff\codex-contract-v1 的不可变快照交接，反馈另起版本文件；不依赖代理互发消息。
- 未获用户视觉确认、未验收素材前，不将生成素材替换默认页面；测试纹理必须标注技术测试，不能冒充正式角色或模拟器最终效果。
- 1024×600；固定脚底锚点，禁止拉伸、逐帧自动裁切、展开整个精灵图。资源/定时器/动画随页面退出释放；实际内存和打包后大小必须验证。
- 不假定 PSRAM 容量或全部可用；现有 6 MiB app / 4 MiB storage 不改。PSRAM XIP 可使常量资源消耗 PSRAM，不能把 const 当零成本。
- 私照、密钥、存档、原始 Flash 备份禁止上传公开 GitHub。仅本地提交，不自动推送。
