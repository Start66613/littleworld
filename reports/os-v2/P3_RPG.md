# P3：RPG 进入真实 LVGL 系统

独立分支 agent/os-v2-rpg-integration，工作区 D:\LittleWorld-Codex-Integration。仅导入 DSH 70484cfc1e585a70d403b23f93aae8802ab1fdbf 的 rpg/、tests/host/test_rpg.c 和交接文档；未合并角色资产、提示词或mock图，未修改DSH核心。

入口：默认OS → 小世界 → 小冒险。lw_rpg_view 接受 lv_obj_t* 父节点及独立40字节 load/save/reward 回调。OS负责返回和删除，页面删除时停止timer并保存；OS浮层/锁屏、对话和任务本暂停RPG时间。Windows存档 rpg-save.dat 与原版/OS偏好分开，先写临时文件再原子替换，不升级旧存档。ESP-NVS持久化尚未接入。

真实LVGL技术场景明确标注TEST A+B；触摸地板→BFS，家具拒绝通行，靠近物品后打开对话、任务本，完成3件物品后显示奖励。未用DSH的Python mock图代替运行截图，未冒充正式角色。相同格子的两个测试标记作横向错位显示，未改变核心逻辑。

## 验证

MSVC编译通过；原封不动的DSH核心85项通过。OS流程增至172项：实际指针触摸到达热点、对话期间时间暂停、OS浮层暂停、3件任务、一次奖励、退出/重进保存恢复。100次页面循环包含RPG，最终timer/对象/lease回基线。20张真实1024×600截图在 reports/ui-v2/os-v2，本地忽略；已打开房间、对话、奖励图。首轮验图发现RPG数据文件未扫描进字体，已修正扫描rpg/、重新生成并重跑172项。运行数字见runtime.json，日志reports/logs/rpg-*。

## 只读核心审查与交接

固定demo为16×8、3热点、短路径，当前API足够此范围。任意外来world尚不能接收：init未充分检查网格总量/热点item_id/对话链；BFS数组128、路径64，超长路径截断语义需DSH补强。save_decode要求已初始化state，CRC保证损坏检测但不验证完成标记与奖励/收集位的一致性。宿主只接固定demo和自己保存的记录，不宣传为不可信世界/存档验证器。

核心tick不主动暂停对话，且伙伴可走到玩家同格；宿主已暂停对话，不声称实现人物碰撞。新内容、长地图、第三种对话选项、存档语义校验、正式人物素材需DSH继续；本轮界面只支持demo所需的最多两项选择。未经用户审美验收，不把测试标记换成男方候选。

已增加ESP-IDF lw_rpg组件适配及UI构建列表，最终离线编译另记；Windows通过不是实板速度、内存或SD稳定性通过。
