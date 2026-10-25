# UI V3 → DSH 后端接口约定

本轮只新增UI侧契约和Host测试。DSH继续独占firmware/main、硬件服务、NVS、驱动和固件部署。Codex不实现或修改这些后端。

1. lw_os_capabilities（lw_os.h）：UI线程传入亮度/音量/Wi-Fi/音频/存储的unavailable/simulated/ready快照；只描述能力，不初始化硬件。NULL从既有lw_system后端函数推导，音频/存储默认不可用。服务不可用时相关控件禁用，不能用ready掩盖未完成驱动。能力改变须在UI任务边界通知，当前会重建页，不从硬件任务直接操作LVGL。
2. lw_album_set_source（lw_ui_content.h）：相册关闭时设置稳定借用源（最多64项），title/load/release；一次至多一张解码图片。loader负责文件/格式/尺寸/预算，返回有效不可变LVGL描述符和token，错误必须能安全release部分结果；UI先删除widget并drop image cache再release。当前仅适用于非动画照片，不扫描路径，不上传文件。
3. lw_memories_set_source：回忆页关闭时设置最多32条稳定借用lw_memory_t，标题≤128字节、正文≤4096，日期有效。NULL/0回到显式示例。个人数据由后端保存；动态文字的完整中文字体覆盖仍需资源方案，不能假设编译子集涵盖所有导入文字。
4. 动效：关闭复用旧settings.animations格式；减少档暂为会话状态，未来DSH若持久化，请另定版本，不在原记录塞入新字段。可在低性能设备选择REDUCED或OFF。
5. 音乐仍是模拟进度，没有接音频播放函数；capabilities.audio不能让本轮页面自动具备真实播放。后续需要prepare/play/pause/seek/position/error和封面租约接口，由DSH实现后再对接。当前默认不输出声音且明确提示。
6. RPG继续使用已冻结的40字节load/save/reward宿主。地图/寻路/存档边界问题保留上一轮返工清单，本轮不修改核心。页面暂停/恢复只控制宿主时间，不写核心文件。

没有要求DSH改底层API才能运行本轮Windows体验。任何真实板测、部署、格式化、C6操作仍需用户独立授权。

已只读核对D:\LittleWorld-P4\docs\handoff\DSH_TO_CODEX_BACKEND.md、lw_hw.h和lw_boot.c的公开接口。DSH当前以NULL回调表示未验证亮度/音量，network_available=false；本轮默认能力推导能直接显示禁用态。音频codec可调音量不等于媒体播放链已实现。UI不接管C6/Wi-Fi、不假定背光极性、不调用lw_diag或NVS内部实现，也不合入DSH工作目录。旧偏好24字节、RPG40字节、旧宠物72字节保持不变；本轮关闭动效沿用旧72字节设置，不因为文档把animations写进24字节记录就自行扩展格式。此文档是已提供的接口交接，不代表DSH已确认或完成接入。
