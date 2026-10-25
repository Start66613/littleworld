# 本轮交付：独立美术、P2、P3、P4

2026-10-25。正式Codex工作区D:\LittleWorld-Codex，独立集成副本D:\LittleWorld-Codex-Integration；均在D盘，未覆盖DSH目录。美术在D:\LittleWorld-Codex-Challenge / agent/codex-meowa-challenge。P3/P4先在agent/os-v2-rpg-integration实现验证，再快进合入agent/littleworld-os-v2，稳定main不改。

## 已实现

Windows无参数启动OS，--legacy保留原桌面；Dock进入原有相册/音乐/回忆/小游戏，小世界可进入原版宠物照顾和RPG。旧应用导航由lw_shell_host_t委托到当前系统，不创建隐藏旧页面；离开释放旧App timer和视图。锁屏文字和房间分区，页面/控制中心淡入位移与关闭淡出。音乐下一首越界是实际指针回归发现并修复的问题。

RPG复用DSH冻结70484cfc1e585a70d403b23f93aae8802ab1fdbf，rpg/和原85项测试与该提交完全一致。实际触摸移动/碰撞地块/对话/任务本/奖励/退出读档，弹窗和OS浮层暂停RPG；Windows独立40字节原子存档，原存档不升级。测试场景TEST A+B，不是正式角色。宿主协议与核心待补边界见docs/handoff/CODEX_RPG_INTEGRATION.md、P3_RPG.md。

文件资源为真实Windows stdio读取+后台线程预读+有界LRU，已接现有角色帧provider。ESP版本使用FreeRTOS与PSRAM分配；官方04_sdmmc和7B BSP挂载适配已编译，自动格式化配置禁止。不展开全部帧，不扫描私人照片。缺资源时保留有效帧或明确显示无角色降级，基础OS/应用继续运行。详见docs/TF_RESOURCES.md。

## 美术挑战

一次image-edit-run / nano-banana 1K / pixel strict，任务job_466dffc77e5647cfa02ea58434e8070c。提交前查当前官方技能与价格，预计12≤独立40上限；余额2704→2694，观测净减10，返回无独立账单字段。仅一张原版精灵作参考，无真人照片；没有追加动画或重试。原始提供方PNG逐字节保留，去背景用免费本地工具，公开脚本已禁用再次付费执行。

候选256²、可见104×208，原版98×198；男方偏高10px，4889色比原版716色更碎。先冻结独立评价后才看DSH结果；DSH A/B同口径可见220/221px，色板更收敛但偏高、带半透明边缘。原生1×并排/房间/四方对比只留本地。男方尚待用户审美验收，不作为系统默认人物。详细方法、数据和自评在美术分支reports/meowa-challenge。

## 实际验证与可复现步骤

代码验证提交2764c1bb4e10d9f30c53455d66d9bef27a48a675；随后仅字体检查脚本/换行属性与交付元数据修正。最终Windows检查：系统22、角色82、冻结RPG85、文件缓存33、OS有/无角色各174、旧核心142、真实LVGL资源94、Python18。原版完整UI回归通过，对象84→84。OS100次浮层/100次所有页面（含RPG/旧应用）循环无对象/timer/lease增长；Storage100次关闭在途worker无句柄增长，最后73→73。不是所有分配器路径绝无泄漏或持续帧率的证明。

- tools/run_os_v2.ps1 -Test：自动构建、模型/文件/实际指针流程与截图。独立副本可传-LvglDir D:\LittleWorld-Codex\firmware\managed_components\lvgl__lvgl与-PackPath D:\LittleWorld-Codex\private\pixel-duo\qianqian\qianqian.lwpx复用源和包。
- build/host/LittleWorld.exe --os-v2-test - reports/ui-v2/os-v2-fallback：已创建目录后跑无资源降级。
- tools/run_host_tests.ps1；LittleWorld.exe --ui-v2-test reports/ui-v2/technical；Python -m unittest discover -s tests -p test_*.py；tools/gen_fonts.ps1 -Check。
- 原版回归：LittleWorld.exe --auto reports/ui-v2/os-legacy-regression；交互回退：--legacy。
- 固件：加载tools/activate_env.ps1后，idf.py -C D:\LittleWorld-Codex\firmware -B D:\build\firmware-v2 build。绝对路径避免-C/-B相对目录歧义；本轮实际构建缓存为D:\build\firmware-v2。

20张真实1024×600 LVGL帧缓冲截图在D:\LittleWorld-Codex\reports\ui-v2\os-v2；SCREENSHOTS.json记哈希。已实际查看桌面/锁屏/宠物/音乐/RPG房间、对话和奖励，修正剧情缺字后重新运行。BMP→PNG无损转换，不是Meowa概念图。含Qianqian新图按用户要求不推送。

源码中的550个非ASCII字符已纳入字体。发现字体Check最初受绝对工作区路径和Git CRLF影响；改成仓库相对元数据、规范化比较、生成字体LF属性后，两个路径可复现。失败日志与修复日志均保留，不通过的中间测试没有当作最终结果。

最终ESP-IDF5.5.5/esp32p4 min300/32MB/BSP3.0.1/adapter0.6.4/LVGL9.5.0编译通过，含lw_rpg、lw_rpg_view、lw_storage、lw_sd对象。BIN1323152B，SHA-256 43c5182b8ccb983630491a01891dd1003885b906f081ecc0f0ce29dfa8cd96f6，appdesc2764c1b。原app_main仍为稳定入口，链接裁剪未使用OS/资源代码；这个BIN不能被称为已部署完整OS的固件。分区、lw_core、arcade_core和firmware/main相对8af0637未改。

## 尚未完成

角色和系统审美验收、正式男方动画/场景；RPG长地图/不可信数据和语义存档校验等DSH改进；实际TF卡吞吐、热插拔、PSRAM可用量、真实无线/音频与未来部署。当前网络/亮度/音量仍为Windows模拟服务，原相册与音乐保留示例资源和原有逻辑。文件cache miss仍可能同步读盘，不保证硬件帧率。没有连接板卡、刷写/擦除/分区/C6/eFuse操作。

缓存上限786432B，LVGL角色像素换帧峰值589824B，合计上界1376256B加2592B缓存元数据和worker/文件系统/显示/字体；这是局部预算，不是ESP整机PSRAM实测。完整LWPX22.2MB仍仅本地文件，未尝试塞入4MiB资源分区。
