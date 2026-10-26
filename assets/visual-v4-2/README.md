# V4.2 美术资源

完整 Meowa 房间、官方 UI 组件切分、共同沙发姿态/待机/挥手/休息，以及参考原版千千步态制作的男方四向行走。正式千千原始资源不覆盖。

`scenes/*-master.png`、`ui/generation-original.png`、`characters/*/generation-original.*` 保留生成原件；`couple-source/generation-contact.png` 是共同接触关键帧，`generation-extraction.png` 是**未保持原坐标**的透明提取原件。`TRANSFORM.json` 明确其整体技术转换，不假称精确遮罩。

运行包沿用 LWSR0001 / LWPX0001。`MANIFEST.json` 记录尺寸、锚点、帧序、时长、SHA-256；`HIT_REGIONS.json` 对应实际 UI 地面/家具和坐姿接触位置。男方左行剔除正面开场一帧，原件保留，`REVIEW.json` 记录取舍；四向步态明确重定时为800ms。未逐帧重画、自动居中或拼排房间。

复现技术导出：`tools/prepare_sofa_interaction.py`、`tools/prepare_meowa_v4_2.py`。后者在缺少 generated/ 时使用公开原件和已验收 UI 帧；设置 `LW_ART_EXPORT_PUBLIC_ONLY=1` 可强制检查这一路径。仍需已有固定版本 DesktopPet 辅助狗源和 V4 男方待机/眨眼/挥手。官方下载中间件在忽略的 generated/；公开的生成原件和运行包可直接使用，不需要再次付费生成。报价、Job、余额和拒收实验见 `reports/visual-v4-2/MEOWA_JOBS.json`。

美术由 Codex 在真实 LVGL 中自检，用户最终审美验收待进行。Windows 文件加载已实现；真实 SD/VFS 挂载由 DSH 维护，未经板测不能据此宣称硬件性能。
