# Codex → DSH：V4.3 首次联合版资源与兼容修复

2026-10-27。DSH是唯一集成负责人；Codex没有修改DSH工作区、启动入口或硬件配置，没有刷机或调用Meowa。

正式视觉冻结为`agent/visual-v4-2-polish@2900569e381f1e06ecf88d720a3f032a9bd97db6`。本轮只在Codex补丁分支`agent/v43-integration-support`工作，不创建另一套联合系统。开始检查时Codex工作树干净，无V4.4未提交改动；没有reset、clean或覆盖历史。

## 可立即读取的文件

稳定资源交接目录：`D:\LittleWorld-Meowa\handoff\codex-v43-frozen-2900569`。

- `runtime/`：16个原字节运行资源，75,070,996B；没有重采样、重生成或改格式。
- `LOCAL_RESOURCE_INVENTORY.json`：每文件SHA-256、格式、尺寸、文件大小、ID、动作帧数/时长、锚点、加载器和缺失回退。
- `support/`：下面两份兼容补丁、测试结果、运行说明和当前截图验收结论。
- 原版千千完整基础包仅本地复制为`qianqian-base.lwp`，内容仍是已有的LWPX0001，22,218,124B、13动作。未修改原文件，也不把这个私有源包加入公开Git。

这是给DSH打包器的资源输入，**不是已经具备清单并通过设备挂载验证的TF成品包**。请继续由DSH自己的打包器生成/验证`littleworld-v4.json`，保留自己的内容与私人数据。本轮没有访问TF设备。

仓库内完整说明：`reports/v43-integration-support/RESOURCE_RUNTIME.md`和`RESOURCE_INVENTORY.json`。后者公开清单排除了本地千千基础源包。

## 已真实复现的阻碍及补丁

DSH审查基线：`agent/dsh-v4-3-engine@05bfc36b335a5a4636f38d641a0fefde4dbd6599`，工作树未修改。

1. `lw_pack`只接受256×256、每namespace最多6动作、2个namespace/4文件。正式男方7动作，原千千13动作，日常320×320，旧UI544×352，皮肤512×544，均会拒收。
2. `lw_sprite_create`只接受scale≤256，直接拒绝冻结UI的288站姿/282坐姿，角色对象返回NULL。`set_pixel_mode`也拒绝这两个比例。
3. 旧786,432B的lease预算小于图集835,584B，图集即使成功注册也无法acquire。旧同尺寸file cache也放不下这个单帧。
4. 现有`lw_art.c`只接male/qianqian-wave及嵌入idle，未接daily/UI/skin，也未配置辅助场景目录。

`lw-pack-v43-compat.patch`只改`lw_ui/include/lw_pack.h`和`src/lw_pack.c`：接受四种已验证几何；逐包计算stride/偏移/anchor边界；32动作、5namespace、8文件、总64spec。保留LWPX、LWFA、cache、静态表、原解析拒绝与回滚语义。没有放开任意尺寸，也没有改后端引擎。

`lw-sprite-v43-scale.patch`只改精灵上限为320和nearest-neighbor接受条件，以及头文件注释。保留DSH的`lw_sprite_stats`链表、空clip保护、`CONFIG_LW_DIAG_STATIC_UI`开关和所有诊断生命周期。不要为了V4.3整文件覆盖DSH的sprite实现。

两份补丁针对以上DSH SHA分别执行`git apply --check`通过。**DSH决定应用时机**，若集成分支已变化，先检查再合入，Codex未在DSH目录执行apply。补丁提交号在本地交接`SUPPORT_DELIVERY.json`及最终回复中。

## 启动接线：保留DSH引擎

在任何页面/精灵/皮肤创建前，向同一registry添加：

```c
/* root由DSH现有挂载/清单验证提供；此处不挂卡、不改硬件。 */
lw_pack_t info;
lw_pack_registry_add(root, "male.lwp", "male", cache, &info);
lw_pack_registry_add(root, "qianqian-base.lwp", "qianqian", cache, &info);
lw_pack_registry_add(root, "qianqian-wave.lwp", "qianqian", cache, &info);
lw_pack_registry_add(root, "daily.lwp", "v42", cache, &info);
lw_pack_registry_add(root, "ui.lwp", "v42ui", cache, &info);
lw_pack_registry_add(root, "skin.lwp", "v43skin", NULL, &info);
lw_pack_registry_register(2u * 1024u * 1024u);
```

以上是顺序示例；DSH实际接线必须检查每次返回码，记录失败文件并启用对应回退，不能忽略错误继续宣称全部就绪。千千基础包不可用时，保留已有embedded LWFA idle补入路径，再append wave；它是明确的动作降级。

atlas使用NULL cache是有意的：第一次经stdio读835,584B并由`lw_skin`一份lease保持，后续组件共享。原786,432B cache可继续用于其他单帧，避免为图集盲目提高卡缓存；DSH若调整cache预算则另做实机内存测量。lease上限2MiB不是预分配，当前代表占用1,999,872B；不是系统总RAM或PSRAM保证。

桌面调用`lw_visual_assets_configure(root)`；RPG/日常还必须`lw_visual_aux_configure(root)`。辅助目录可以与桌面同根。桌面按`scene-0/1/2`寻址，日常按`daily-room-day/daily-room/daily-room-night`，RPG按`rpg-room`。不把所有场景常驻。

`.lwu`的`LWUI0001`与`skin.lwp`的`LWPX0001`不同，禁止直接改扩展名。首次上板可保留DSH的`.lwu`代码和引擎，同时让当前V4.3页面继续用`lw_skin`+共享LWPX图集；不需要迁移17种皮肤。当前三态是普通/选中/按下，DSH `.lwu`按钮是普通/按下/禁用，不能按序直接互换。

## 合并UI时保留的并集

- CMake保留DSH的`lw_pack.c`、`lw_ui_asset_view.c`、`lw_ui_clock.c`，再加入Codex的`lw_polish.c`、`lw_ambience.c`、`lw_skin.c`；依赖保留`lw_storage lw_engine log`。不要用任一方旧CMake整体覆盖。
- `lw_os.c`保留冻结版纸签布局、顶部右对齐组、收起Dock、场景生命周期，同时保留DSH有效时钟与能力状态接线。不要简单ours/theirs解决整文件冲突。
- 时钟：DSH `lw_ui_clock_read`的有效时段、theme及可信度是联合版权威；保留演示/手动/未校时标签。Codex旧规则09:00/19:00，DSH规则08:00/20:00；联合版必须由同一个phase/theme驱动时间标签、桌面、日常和UI色温，不能一边用新引擎一边继续旧分钟判断。无时钟显示`--:--`，不要把uptime伪装现实时间。本轮未抢改DSH时钟后端或擅自设定现实时间。
- 首屏1024×600。日常页面父级在屏幕y=52，背景本地y=-52抵消；坐姿本地锚点(516,316)→屏幕(516,368)，狗窝(906,412)→屏幕(906,464)。320包锚点(160,274)，不要把这两个坐标当左上角，也不要再次叠加52px。
- 狗窝包已含狗、前沿和阴影，不再叠旧狗sprite或再用opening裁成第二个窝。抬头8×125ms，睡/空各1帧；不是四态竖条。需要DSH stage映射时，EMPTY→dogbed.empty、SLEEP→dogbed.sleep、HEAD_UP/IDLE→dogbed.look。
- 字体18/22/28使用冻结文楷C位图；44/88数字保留。合入字体与头文件，检查链接符号与中文，不能从旧分支留下另一套子集。

## 本机验证与当前未通过项

`tools/test_v43_pack_compat.ps1`实际编译原版和补丁后的DSH读取器。原版拒收已复现；补丁版用6个完整包、31个ID、全部帧逐字节比对，**1,597检查零失败**。另链接实际LVGL9.5与保留诊断的DSH精灵实现，站姿/坐姿/320狗窝、20轮创建删除/脚底锚点/timer回收，**230检查零失败**。测试使用实际文件stdio，没有启动板端cache worker；不宣称卡速或硬件性能。

冻结版原Host回归结果保留在`reports/visual-v4-3/VERIFICATION.json`。本轮没有代替DSH构建联合固件，也没有把这两组局部测试称为联合系统或板测通过。

当前找到的DSH Windows截图仍是旧版；宠物是几何头像、RPG是TEST A+B，四张PNG与V4.1旧图哈希完全相同，且与各自BMP逐像素一致。真实旧帧也不能当V4.3联合证据，详见`JOINT_ACCEPTANCE.md`。Dock文字拥挤仅登记，Cream Glass推迟，不作为上板前置。

请DSH在自己的联合分支完成接线后，将真实截图和该运行的HEAD/资源manifest放入交接`joint-captures/`，Codex再验收。需要：首页日/夜、双人坐姿、狗窝睡/抬头、控制中心、RPG、故事书以及从应用返回首页。截图不能只从Codex旧报告复制，板上照片与Windows帧缓冲分别标明。
