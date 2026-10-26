# DSH 只读交接 · V4 场景与角色资源

Codex 维护本契约，DSH 不并行修改；后端更改在 DSH 自己的分支完成。来源为 `agent/visual-v4-meowa-motion`，测试源码 `96aee9b59126ff3cb4aacd8c05fd870dc814ca28`。本轮没有修改 `firmware/main`、板级驱动、背光、网络、存档或冻结业务核心。

## 当前已实现的边界

Windows 是真实文件加载、worker 预读、有限缓存、按需帧解码与 LVGL 渲染，非 Mock 资源。ESP-IDF 5.5.5 / esp32p4 / LVGL 9.5.0 已编译新共享 UI 和场景读取器；现有硬件启动入口继续原样使用旧资源。不代表 V4 大场景和新增动作包已经在板上加载。

后续需要 DSH 在真实 SD/VFS 成功挂载后提供目录，在 LVGL/UI 线程、创建 OS 页面之前调用：

```c
lw_visual_assets_configure("/mounted-prefix/littleworld/v4");
```

上面是接口示例，`mounted-prefix` 不是宣称已存在的板上挂载点。Codex 不新增 SD 引脚、挂载代码、分区或 C6 操作。

## 场景文件

复制 `assets/visual-v4/runtime/scene-{0,1,2}.lwr` 与 `scene-{0,1,2}-thumb.lwr`。主题 0=晨光、1=月光、2=落日；文件精确对应 `SCENES.json`，不要重新排列。PNG 和 2K 母版用于美术审查，不在设备上一起展开。

格式为 16 字节头：`LWSR0001` 八字节 magic，偏移 8/10 为小端 uint16 宽/高，余下保留；后跟小端 RGB565 连续像素。完整背景 1024×600，像素 1,228,800 B、文件 1,228,816 B；缩略图 256×150，像素 76,800 B、文件 76,816 B。读取器验证 magic/尺寸/长度，失败返回 NULL，UI 清楚标识安全降级。

`lw_visual_scene_image()` 把 LVGL 图像对象和缓存租约绑定；删除对象自动释放引用。固定一个 full 槽、三个 thumbnail 槽。锁屏与桌面同主题共享 full 槽；不同主题的 full 槽仍被引用时拒绝覆盖。`lw_visual_assets_configure()` 在存在引用时拒绝改根目录；`shutdown()` 必须在全部场景对象删除后调用。所有配置、对象和缓存操作由同一 UI 线程调用，不是线程安全公共服务。

## 角色包与锚点

男方 `male.lwp` 包含 idle8/blink4/wave16；新增千千 `qianqian-wave.lwp` 只包含 wave16。既有原版千千 LWPX 不改，仍提供 13 动作/113 帧。正式图片画布 256×256、RGB565+A8、每帧 196,608 B；男方锚点 (128,225)，千千 (128,226)。动作表、帧序、时长和来源在 `characters/*/*.json`，不逐帧居中。

Host 的 `pixel_pack.c` 当前负责组合注册三包、read-only seek、worker 预读和计数。它不是已经接入硬件的 LWFA 后端：现有固件启动仍使用原嵌入资源，不能把 LWPX 文件直接改名当 LWFA。DSH 后续应复用现有 `lw_assets` 逐帧 lease 契约提供等价加载器，或复用经过评估的标准 C/VFS 读取部分；不同时抢改 Host 和后端接口。没有加入硬件包加载实现前，板上只能使用现有回退。

## 内存上限与实测

- 场景像素缓存：一个 full + 三 thumb = **1,459,200 B**；最终 Host 实测峰值相同，停止后 0 B、0 引用。
- 原版 Q 文件缓存 4 帧 = 786,432 B；男方 2 帧 = 393,216 B；新增 Q 挥手 2 帧 = 393,216 B；总上限/实测峰值 **1,572,864 B**，不把 113 帧一次读入内存。
- LVGL 角色帧 lease 实测峰值 **983,040 B**，锁屏/桌面共享时钟但各自有图像租约，过渡峰值是五帧；注册预算 2 MiB。
- 上述三个独立计数保守相加 **4,015,104 B（约 3.83 MiB）**，尚未包含缓存 metadata、显示缓冲、字体、LVGL 对象、线程栈、后端和系统。
- Full 下相册 944×400 容器缩放可能需要约 **1,510,400 B（约 1.44 MiB）** 的瞬时 ARGB 渲染层，另有对齐/stride 和驱动开销；Reduced/Off 跳过这类缩放。不得把 3.83 MiB 当系统总 RAM。
- 根据实际 LVGL 9.5 `lv_obj_style.c`，transform scale 会触发转换层；普通 `opa` 与 `opa_layered` 不同，不能笼统声称每次淡入都会额外分配一张全屏快照。

PSRAM 可分配连续块、显示带宽、DMA/缓存一致性和 SD 读取吞吐还需实板测量。读取器目前同步读取新场景；角色文件有异步预读。主题大图的硬件异步预读/失败重试应由后端按真实 SD 延迟评估，不能将 Host 的流畅度外推。

## 几何和能力

家具命中区、活动地面和页层坐标见 `assets/visual-v4/HIT_REGIONS.json`。当前家具仍在底图内，角色只在中间地毯活动，避开家具；尚未制作可绕行家具的独立前景遮罩。不要扩大行走区域后假定遮挡自动正确。

UI 延用 `DSH_UI_CAPABILITIES_V3.md` 的硬件能力和不可用态：Wi-Fi/亮度/音量在 Host 仍标识模拟，声音未接通不称真实播放。新增场景路径不构成修改存档格式或默认动画设置的授权。进入新页面、锁屏、主题、停止顺序继续由 UI 管理，不从 SD worker 直接操作 LVGL。

验证入口：`tools/verify_visual_v4.ps1`、`tools/audit_visual_v4.py`，实际图片索引 `reports/visual-v4/README.md`。无设备动作。下一步硬件集成必须另行得到用户针对本次刷机的许可。
