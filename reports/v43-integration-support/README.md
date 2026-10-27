# V4.3 首次联合版：Codex支援交付

正式视觉冻结：`2900569e381f1e06ecf88d720a3f032a9bd97db6`。DSH独占集成，Codex仅提供冻结资源和最小兼容补丁。当前新联合版视觉与板测均未通过验收；没有用旧画面代替。

- [DSH实际接线与合并说明](../../docs/handoff/CODEX_V43_FIRST_BOARD_SUPPORT.md)
- [逐资源运行说明](RESOURCE_RUNTIME.md) / [完整公开元数据与SHA](RESOURCE_INVENTORY.json)
- [LWPX读取兼容补丁](lw-pack-v43-compat.patch) / [保留DSH诊断的缩放补丁](lw-sprite-v43-scale.patch)
- [补丁DSH基线与哈希](PATCH_BASE.json) / [本机验证](VERIFICATION.json)
- [当前联合截图验收：旧UI，未通过](JOINT_ACCEPTANCE.md) / [截图来源](JOINT_EVIDENCE.json)

实际结果：完整资源读取1,597检查零失败；真实LVGL精灵/生命周期230检查零失败。原版拒收日志、补丁版日志均保留在logs/。这些不是板端速度或联合固件测试。

本地资源输入：`D:\LittleWorld-Meowa\handoff\codex-v43-frozen-2900569\runtime`，16文件75,070,996B，31个ID；附加的完整千千原始导出仅本地，未公开。输入不是TF成品包，DSH仍负责清单、挂载、打包、编译、刷写和诊断。文件交接状态见同目录SUPPORT_DELIVERY.json。

复现：`tools/audit_v43_frozen.py`审计不收费；`tools/test_v43_pack_compat.ps1`编译原版/补丁版DSH读写路径并链接已有真实LVGL库。`prepare_v43_pack_compat.py`只在Codex build/生成快照和补丁，不写DSH目录。原冻结UI、main和硬件配置不改，Meowa与设备操作均为0。

待DSH完成资源注册与预算/时间并线后，再交回有来源的新Windows截图。Dock文字拥挤保留登记，Cream Glass/V4.4后延。
