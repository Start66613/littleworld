# 双角色像素系统状态

更新：2026-10-25（Asia/Shanghai）。工作区 D:\LittleWorld-Codex，分支 agent/pixel-duo。

- P0：通过。实际读取 DesktopPet b00a700 的 PNG、SpriteFrames、Godot 场景及状态机；qianqian 与小雪球已区分。19 项动画清点、113 帧本地导出、原图参考图集和像素契约已建立。
- P1：核心框架已运行。两人独立位置/方向/动画/睡眠；确定性自主行为、站位/碰撞、联合时钟、睡眠保护、页面退出释放。Windows 用真实 qianqian + TEST A；男方占位不是定稿。
- P2：待 DSH 男方像素候选；旧绘本批次仅归档。未收到用户男方形象确认，未授权公开角色图。
- P3：待双方正式动作素材。靠近/站位/同步/递信等语义调度可测；wave/sit/letter/celebrate 等缺美术时明确回退，不能说完整双人动作动画已完成。
- P4：本次主机验证与固件离线编译通过；最终双人素材容量、设备内存/性能待后续验证。固件仍保留原版入口，未包含完整双人资源；无开发板连接或板测。实际结果见 reports/pixel-duo/acceptance.md。

运行入口 tools/run_pixel_duo.ps1；可自动导出固定源、构建、打开原生 1× 预览。加 -Test 执行确定性真实 LVGL 测试与截图。图片/像素包/存档都在忽略的 private/pixel-duo；报告只提交非图像结果。

DSH 只读交接：D:\LittleWorld-Meowa\handoff\codex-pixel-v1.0.1\TO_DSH.md；当前版本指针 handoff/PIXEL_CONTRACT_CURRENT.json。修正了躺姿初稿锚点，实测 y=216；修改建议与缺项见 PIXEL_ASSET_REWORK.md。当前没有任何付费 API 调用或远程推送。
