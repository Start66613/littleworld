# Codex → DSH：V4.3 最新组件交接

Codex维护、DSH只读。本轮在agent/visual-v4-2-polish继续；源码SHA见reports/visual-v4-3/VERIFICATION.json。main、BSP、网络、音频、显示/背光、分区、NVS、存储挂载和冻结业务核心没有修改。

## 资源与注册

保留原V4/V4.2路径和LWSR/LWPX格式。新增assets/visual-v4-3/runtime/skin.lwp：一个512×544 RGB565+A8帧，像素835,584B，请通过现有lw_assets provider注册为v43skin.atlas。lw_skin模块共用一个lease，按组件引用计数，删除对象先解绑图像源，最后一个组件释放；不为每个按钮分配完整图集。17种皮肤及九宫格源区见components/MANIFEST.json和lw_skin.c。

新增daily.lwp：原坐姿/狗动作加dogbed.empty、dogbed.sleep、dogbed.look三个完整组合状态，继续使用320×320公共帧与v42.* ID前缀。look为8帧轻待机。Host运行器优先此包，旧polish.lwp仍可回退。源是Meowa整窝组合，不是运行时把狗贴到空窝上。

辅助场景目录继续包含rpg-room.lwr、原daily-room.lwr和daily-room-{day,night}.lwr；lw_visual_aux_configure接口不改，一个辅助full槽。新增两张完整时段图各1,228,816B，按需读，不一起展开。

Host已真实实现文件读取/worker预读和组合注册；硬件LWFA/LWPX桥接与SD/VFS挂载由DSH处理，不能改扩展名代替转换。新包不因本次固件编译而自动挂载。

## UI与显示

加入lw_skin.c/.h与lw_ambience.c/.h，已列入lw_ui/CMakeLists。UI皮肤缺失时保留原生控件。控制中心使用九宫格面板、三态开关和真实值驱动的滑轨/滑块；初始化只发UI刷新事件，不调用设备亮度/音量setter。缺服务继续禁用，模拟能力继续说明。

顶部状态组x=900–1012，三图标中心920/960/1000，24px、等距；返回x=840–884，与状态区隔16px。左侧为时间/日期丝带。整组打开控制中心。

主场景、双人、日常、RPG默认收起导航；120×44底部触摸区轻触/上滑展开。展开导轨约512×44，六入口76×52，4.5秒空闲收起；普通应用保持展开。lw_os_scene_focus只是一项UI注意力提示，没有后端或存档行为。手势释放会等待抬手，避免再次点击反转。

按最新用户明确指令，站立角色显示scale_256=288（+12.5%），共同坐姿282（约+10.16%）。最近邻，无抗锯齿；源256/320帧不变。站立脚底位置不变，触摸盒124×242；共同坐姿世界锚点(516,368)，以坐垫接触校准。旧的“显示只能原生1×”限制已由这次用户要求覆盖，不应回退缩小人物。

原版Qianqian及冻结RPG核心/40字节存档不改。RPG仍复用原任务/寻路，UI仅校准新家具区域及旧坐标恢复；RPG创建函数接收正式lw_actor_art_t[2]。

## 时间、字体、内存

lw_ambience读取已有platform->get_time(struct tm*)，需要当地时间、有效年份≥2020。Windows用本机时间。无可信时间回退明确标识的演示模式，不做网络校时。05–09清晨、09–17白天、17–19黄昏、19–23夜晚、23–05深夜。设置内预览仅会话内，恢复实时不改系统时间/纪念日/宠物记录。首页和日常已有昼夜，RPG独立昼夜尚未制作。锁屏/开机共用当前场景，避免争用唯一full槽。

18/22/28字体为固定文楷屏幕版C位图子集，44/88数字保留Noto。保留两份OFL；设备无需TTF。字体脚本固定SHA，不安装系统字体。

Host实测文件缓存上限3,597,312B，场景缓存峰值2,688,000B，常规asset帧峰值1,999,872B，仍低于2MiB注册上限；atlas只有一份lease。它们不是系统总RAM，显示缓冲、字体/XIP、LVGL临时绘制层、线程栈、后端另计。需要后续确认连续PSRAM、真实文件延迟、DMA/缓存一致性、PPA回退、触摸与帧率。

## 验证边界

完整Windows回归/真实帧缓冲/GIF/源码与产物哈希见VERIFICATION.json、SCREENSHOTS.json。共享UI ESP-IDF5.5.5/esp32p4 rev3_x/32MB离线编译；稳定firmware/main入口不变，新大资源未在板上接入。未连接设备、刷机、擦除、改分区或C6。最终美术仍待用户验收。
