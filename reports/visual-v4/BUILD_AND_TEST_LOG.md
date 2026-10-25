# V4 构建与检查日志摘要（脱敏）

测试源码：`96aee9b59126ff3cb4aacd8c05fd870dc814ca28`。最终原始日志保存在本机 `D:\LittleWorld-V4-Visual\reports\logs`，对应 SHA-256 在 [VERIFICATION.json](VERIFICATION.json)；此处仅发布结果摘录，不发布环境展开、账号认证或设备建议命令。

## 最终 Windows 与模型检查

```text
System model: 22 checks, 0 failures
Duo model: 82 checks, 0 failures
85 checks, 0 failure(s)
RPG CORE TESTS PASSED
Storage: 33 checks, 0 failures; handles 76 -> 76
OS V2: 489 checks, 0 failures; 100 overlay cycles; peak pixels 983040 B.
File cache: peak 1572864 / 1572864 B; hits 4375; prefetched 4726; evictions 5818.
```

日志：`v4-verified-host.log`。67 张最终 PNG 逐一与原 BMP 相等。运行使用加速虚拟时钟，不是板上 FPS 测量。

```text
Character pack unavailable: running complete OS with labelled no-art fallback.
OS V2: 472 checks, 0 failures; 100 overlay cycles; peak pixels 0 B.
File cache: peak 0 / 0 B; hits 0; prefetched 0; evictions 0.

OS V2: 487 checks, 0 failures; 100 overlay cycles; peak pixels 589824 B.
```

分别为 `v4-verified-fallback.log` 和 `v4-verified-missing-scene.log`。缺图不会阻塞设置、导航、交互与退出，页面有明确资源提示。

```text
142 checks, 0 failure(s)
HOST TESTS PASSED
94 checks, 0 failures
Ran 18 tests
OK
glyph set: 569 non-ASCII characters
font outputs are up to date
objects: 84 -> 84
AUTO RUN: PASSED (0 failure(s))
Visual asset audit: 114 checks, 0 failures; actual spend 169/1000.
```

依次对应 core、resources、python、fonts、legacy 日志及 ART_AUDIT.json。最后的原版完整回归在最终源提交后重跑通过。

## 最终离线固件编译

```text
ESP-IDF v5.5.5
target: esp32p4
project_version: 96aee9b
little_world.bin binary size 0x226ef0 bytes.
Smallest app partition is 0x600000 bytes.
0x3d9110 bytes (64%) free.
Project build complete.
```

日志：`v4-final-idf.log`。使用独立 `D:\build\visual-v4` 及本工作树的 managed_components。固件入口和后端与稳定基线相同；新共享 UI 编译通过，新大图/动作文件尚未由硬件启动挂载。没有设备操作。

## 已定位并修复的过程问题

- 首次 Meowa 晨光提交：TLS 握手失败，无 Job ID、无扣费；查询余额时另一次网络失败使脚本停止在付费调用之前。核实后通过已有本地代理连通官方 API，保留证书校验，未改官方 Runner；一次新提交成功，没有重复收费。
- 初次 IDF 构建：`lw_visual_assets.c` 中紧接 `if` 的同一行 `return` 触发 `-Werror=misleading-indentation`；补齐花括号后编译成功，没有关闭 Werror。
- 开发中 Host 检查曾使用错误的角色字段 `technical`；查实际接口后改为 `placeholder`，未擅自扩展业务接口。
- 缺角色套件曾因采集目录未建立而提前退出；自动验证脚本现主动创建各目录，最终 472 项完整运行通过。
- Python 读取 UTF-8 元数据在 Windows 默认 GBK 下失败；工具显式使用 UTF-8，未改系统区域设置。
- 插画桌面的早期返回曾绕过组件隐藏和长按编辑；补齐行为与回归。相框早期触摸区没有覆盖真实画框，已根据插画改为 (700,80,90,100)，并与 (888,66,96,80) 的信封区做不重叠检查。

所有修复后的最终结果均以本文上方和验证清单为准；没有把中途失败静默当作通过，也没有把编译通过写成板测通过。
