# 像素双角色 · P0/P1 实际验收

2026-10-25（Asia/Shanghai）。本次 P0 与不依赖新素材的 P1 通过，P4 的主机检查及固件离线编译通过。P2 男方形象、P3 正式双人动作与场景尚未通过。代码与资源加载实际运行，未将占位图当成定稿，未连接或操作开发板。

## 工作区与来源

- 工作区：D:\LittleWorld-Codex；分支 agent/pixel-duo。稳定基线为 Little World 8af0637c3ce72844e9dfec828ea1eb1bd9e194bb。此前绘本探索连同已测通用组件以本地提交 72a4ea6 归档在 agent/ui-v2，未覆盖 main、art/batch-1 或 DSH 工程。
- 本次实现提交：58d7d431e5b1e98240db4f0c60a2a8efe55f0d85。固件 appdesc 对应 58d7d43；后续验收记录提交只包含报告，不改变该构建源码。
- 已实际读取 GitHub 仓库及本地源码。DesktopPet 固定 b00a7005f4889644737092dbc7379e2a9b011315，路径 D:\Espressif\LittleWorld\sources\DesktopPet；读取真实 PNG、SpriteFrames、场景、pet.gd 和 ASSET_CATALOG。运行脚本检查 SHA 与源工作树清洁状态。
- DSH 工作树 D:\LittleWorld-Main-DSH 保持清洁、仍为稳定基线。lw_core、arcade_core、firmware/main 和 firmware/partitions.csv 相对基线没有变更；旧存档布局和版本未改。
- D:\LittleWorld-Meowa 为素材工作目录，不是假定的 Git 分支。旧素材与 DSH 自己维护的清单未覆盖。全部新生成数据、缓存、临时文件和构建在 D 盘，调用现有 C 盘 MSVC 安装不代表重新安装到 C。

## qianqian 实证与正式契约

qianqian 是粉白裙黑发少女，小雪球是独立辅助狗。idle 为 8 张 256×256，直立可见高 192–198 px，原生细像素轮廓约 1 px；不是可任意缩放的低分辨率大色块。实测直立锚点 (128,226)，sleep_idle 接地锚点 (128,216)。初稿躺姿暂定 y250 导致 34px 悬空，复核原图底边后已修正到契约 1.0.1，并重新导出、运行、检查截图。

SpriteFrames 有 19 项，default 为空，dog_interaction 含狗。17 项非空非狗动画逐项统计见 qianqian-analysis.json；包含道具/龙的动作不能冒充普通人物或双人动作。idle 125ms/帧，八向 walk 实际 100ms/帧；源 duration 权重不能丢弃。非整数帧时用累计四舍五入补偿。

本地非 AI 参考图集：D:\LittleWorld-Codex\private\pixel-duo\qianqian\qianqian-reference.png。它由源 Atlas Rect2/PNG 抽取拼排，未重绘、未上传。当前导出 13 套、113 帧，包含 idle、八向走动及睡眠；sleep_enter 的接地过渡曲线留待 P3 审核，本轮实际播放器使用 sleep_idle。

DSH 当前只读交接：D:\LittleWorld-Meowa\handoff\codex-pixel-v1.0.1\TO_DSH.md。当前指针 PIXEL_CONTRACT_CURRENT.json、三份规范和 CONTRACT_RELEASE.json 的哈希已写入并核对；codex-pixel-v1 保留但被替代。确认文件可读取，不假称 DSH 已阅读或已执行。

## 实现与真实测试

lw_duo 实现两个独立角色、八向移动、确定性自主调度、碰撞与步长限制；互动协调器完成接近、对齐、共同时钟、结束与取消。睡眠及待入睡角色拒绝随机联合活动，显式唤醒单独处理。双人新增模型仅会话内，复用原日期和旧宠物状态，不升级存档。

lw_duo_view / lw_sprite / lw_assets 提供原生 1× 最近邻、按脚底深度排序、每人触摸菜单、暂停、单帧按需加载、失败保留上一帧及父页面删除清理。host/pixel_pack 读取实际 LWPX 单帧；TEST A 是代码生成的棋盘纹测试人偶，没有使用真人照片或付费生成。技术房间和英文测试按钮不是最终中文 UI。

- 双角色纯 C：78 项检查，0 失败；覆盖所有互动目录、睡眠保护、随机行为、边界、碰撞、异常时间输入及取消。
- Windows 真实 LVGL：27 项检查，0 失败，100 次场景切换；退出像素 lease/字节归零，timer/对象回到基线；禁用动画后不再读帧。
- 既有通用资源：94 项真实 LVGL 检查通过。
- 既有业务核心：142 项检查通过。旧完整 UI 自动回归通过，0 失败，对象数 84→84。
- Python：18 项通过（源导出 8 项、旧素材审计器 10 项）。旧审计器不冒充新版男方像素素材验收器。
- 7 张真实 1024×600 LVGL 帧缓冲截图保存在 private/pixel-duo/screenshots，覆盖独立移动、会合、递信语义回退、独立睡眠和人物菜单。已实际查看待机、躺姿、菜单及参考图集；无概念图冒充模拟器。未收到正式 wave/sit/letter/celebrate 图时显示 pose fallback，未宣称动作美术已完成。

最新运行测量见 runtime.json：两人当前帧 393216 B，换帧峰值 589824 B（576 KiB），加载 1194 帧，100 次退出检查没有 lease 残留。壁钟 1398.649ms、进程 CPU 156.250ms、进程峰值工作集 6889472 B。此为无等待、确定性虚拟时钟测试的实际 Windows 测量，不是日常空闲 CPU 百分比、持续动画 FPS 或 ESP32 内存/性能。

## 固件与容量边界

ESP-IDF 5.5.5（b774170ff46c393eeb5e495ea37936038d3f4f4f）、esp32p4 rev3_x、32MB、7B BSP 3.0.1 → esp_lvgl_adapter 0.6.4 → LVGL 9.5.0。没有并列添加 esp_lvgl_port。本地 MSVC / CMake / Ninja 版本与二进制哈希见 verification.json。

离线构建成功；新 lw_duo 与 lw_duo_view 源码已编译。默认固件仍保留稳定原版 UI 入口，未挂载或嵌入双角色完整资源，未使用到的段可能被链接器裁剪，因此该 BIN 不是双角色最终可交付固件。构建目录 build/firmware-v2；最终产物大小/哈希、构建源码提交见 storage.json。

保留 app 6 MiB / storage 4 MiB。113 帧 RGB565+A8 裸像素 22216704 B，含目录的 LWPX 22218124 B，不能部署进现有 4 MiB storage；本地标准化 PNG 合计 1491122 B 仅用于评估，不代表男方/背景/其他动作总包已满足容量。后续应实测 PNG/逐帧压缩、缓存和读取延迟，不修改分区来掩盖超额。

esp_idf_size 对当前 P4 rev3 段产生 memory-region 警告，原始结果里的 DRAM/IRAM 0 值不能当成零占用。firmware-size*.json 已附警告及直接 ELF 段结果，firmware-elf-sections.txt 保留实值。没有 PSRAM 实机测量，也不把 const 资源、XIP 或桌面工作集视作零成本。

## 已定位并修复的问题

- 最初离线编译 cc1plus 内存不足：将 IDF/Ninja 并发固定为 2 后完成。
- IDF 编译 lv_image_cache_drop 声明不可见：显式包含 LVGL 9.5 缓存头后完成。
- 曾在模拟器占用 EXE 时链接被拒绝：先结束上一轮运行再构建，最终自动脚本串行构建/运行通过。
- 躺姿锚点初稿偏差：实测修正为 216，版本化交接，新增源图片底边测试并重新验图。

本地日志：reports/logs/pixel-full-host-check.log、pixel-python-tests.log、pixel-core-regression.log、pixel-resource-regression.log、pixel-stable-ui-regression.log、pixel-firmware-build-final.log；初次失败构建日志也保留。日志不包含密钥、完整 MAC、照片或备份内容，按工程规则不提交日志文件。

## 自动复现与下一步

在本工作区执行 powershell/pwsh -NoProfile -File tools/run_pixel_duo.ps1 -Test，脚本自动激活 D 盘环境、核对源、导出、构建、运行并保存实际截图；不要求用户逐条手工搭建。去掉 -Test 打开可操作的 Windows 窗口。图像处理 Python 位于 D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe（Pillow 13.0.0），可用 -Python 指向另一套已装 Pillow 的解释器。

离线固件复现：先点源 tools/activate_env.ps1，设置 IDF_CCACHE_ENABLE=0，再以 python $env:IDF_PATH/tools/idf.py -C firmware -B D:\LittleWorld-Codex\build\firmware-v2 -DSDKCONFIG=D:/LittleWorld-Codex/build/firmware-v2/sdkconfig -DIDF_TARGET=esp32p4 build；仅 build，未运行日志里工具自动打印的刷写建议。

截至本次复查，约定的新 handoff/pixel/ASSET_MANIFEST.json 与 ASSET_REPORT.md 尚未出现。下一步先验收 DSH 的男方正面/侧面概念、与 qianqian 的原生 1× 对照及哈希，交用户确认形象，再生产/接入动作。随后补双方挥手、坐下、递信、看窗、庆祝和相容房间。完整返工/缺项为 docs/PIXEL_ASSET_REWORK.md，不能以未到货素材假装验收失败。

当前只本地代码提交，无推送、无角色图公开、无私人照片读取、无付费 API 调用；无串口、Flash/分区/C6/eFuse 操作。正式男方形象、完整场景构图与最终中文界面仍待未来视觉确认；无需再次确认像素方向。
