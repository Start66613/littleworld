# V4.1 板端资源装载链 —— 代码路径与验收点

本文件说明 **V4.1 固件到底怎么把 V4 美术装到板子上**，以及用户上电后必须亲自确认哪些点。
它不声称屏幕上的效果已经验证过：本轮全程离线，没有上电、没有刷机。

---

## 1. 两个美术来源，一条装载链

| 来源 | 内容 | 谁提供 | 是否随固件走 |
| --- | --- | --- | --- |
| TF 卡 `/sdcard/littleworld/` | 完整 V4：`male.lwp`（idle 8f + blink 4f + wave 16f）、`qianqian-wave.lwp`（wave 16f）、三套场景 `.lwr`、`littleworld-v4.json` 清单 | 用户拷贝到卡上 | 否 |
| 应用镜像内嵌 `art/characters.lwfa`（572,960 B） | 每个角色的 `idle` 一组（male 1f 108×212、qianqian 8f 104×202） | 编译期 `EMBED_FILES` | 是 |

**为什么不能只靠内嵌**：三套场景 1,228,816 B ×3 + 两套角色 8.6 MB，远超 6 MiB 的 factory 分区。
**为什么不能只靠卡**：卡可能没插、可能拔了、可能格式不对。所以两条都要有。

启动顺序（`firmware/main/lw_boot.c` → `lw_art_boot()`）：

```
lw_resource_mount()                     ← 卡在就挂 /sdcard，校验 littleworld-v4.json
   ├─ 成功 → lw_pack_registry_add(root,"male.lwp","male")
   │          lw_pack_registry_add(root,"qianqian-wave.lwp","qianqian")
   └─ 失败 → 记录原因（无卡 / 无目录 / 清单缺失 / 清单损坏 / 版本不兼容），继续
逐个角色检查是否已有 "idle"
   └─ 没有 → lw_pack_registry_add_embedded(characters_lwfa, …, "idle")
lw_pack_registry_register(4 × 196608 B)
   └─ 发布 lw_actor_art_t[2] 给 lw_os_start()
```

**关键点**：卡和镜像内嵌的 clip 进的是**同一张 spec 表**。`lw_assets_register()` 是整表替换，
所以分两次注册会静默注销前一次；这就是两个装载器都写进 `lw_pack.c` 同一个静态表的原因。

---

## 2. 本轮真正改动的代码

| 文件 | 改动 | 为什么 |
| --- | --- | --- |
| `firmware/main/lw_art.c` / `.h` | **新增**。`lw_art_boot()` 做"卡优先、内嵌补缺"，返回 `lw_actor_art_t[2]`；`lw_art_report()` 给出可读摘要 | 原来 `lw_boot.c` 只调 `lw_char_pack_register()`，V4 的 8/4/16 帧动画在板端根本没有入口 |
| `firmware/components/lw_ui/src/lw_pack.c` | 新增 `lw_pack_registry_add_embedded()`；`read_frame()` 增加 `memory` 分支（memcpy）和每 clip 的 `frame_bytes` | LWFA 每帧尺寸随角色变（108×212 / 104×202），原来写死 256×256×3 会读越界 |
| `firmware/components/lw_ui/include/lw_pack.h` | 声明上面这个函数 | — |
| `firmware/main/lw_boot.c` | 用 `lw_art_boot()` 取代 `lw_char_pack_register()`；`ESP_LOGI(TAG,"art: %s",lw_art_report())` | 装载结果必须能从串口看出来，而不是"看起来能跑" |
| `firmware/main/CMakeLists.txt` | 加 `lw_art.c`、`REQUIRES lw_storage` | — |

`lw_char_pack.c` 保留不动（V3.1 的路径仍在，回归测试还在用），但启动路径不再经过它。

---

## 3. 用户上电后要确认的 8 件事

按顺序做，每一条都要能给出"是/否"，不要凭感觉。

### 3.1 串口日志（最省事，先看这个）

接 USB 到 COM5，115200，上电。日志里必须出现：

```
littleworld: art: card: <原因> [no card]            ← 没有卡时
littleworld: art: card: male.lwp + qianqian-wave.lwp [card]   ← 有卡时
```

- 如果打印 `[no card]` 而桌面**仍然有两个角色**：内嵌降级生效，符合预期。
- 如果打印 `art: no character source loaded`：**失败**，两个来源都没读到，桌面会是占位图。

### 3.2 桌面有角色，且是像素小人

- 桌面中央偏下应有两个角色（左男右千千），脚下有阴影椭圆。
- 不是灰色方块、不是 "TEST A" 文字、不是空白。

### 3.3 角色在动

- 站着不动时，男角色每 5 秒眨眼一次（这是 `blink` clip）。
- 千千是 8 帧循环，动作连续。
- **有卡时**才会有 8 帧 idle / 4 帧 blink / 16 帧 wave；无卡时是 1 帧 male + 8 帧 qianqian。

### 3.4 走进"世界"页打招呼

- 点底部 `世界` → 点 `见面打招呼`。
- 两个角色应各自走向对方、进入 `wave` 动作、挥手，然后回到 `idle`。
- 挥手期间角色**不应该跳位**（锚点 128,225 / 128,226 必须对齐；跳位说明锚点没被正确读入）。

### 3.5 主题切换

- `设置 → 主题` → 依次应用三套。
- 有卡时：桌面背景换成樱花 / 月光 / 草莓三张实景图。
- 无卡时：背景是代码画的几何房间，**不是**黑屏、**不是**花屏。

### 3.6 锁屏

- `控制中心 → 锁定`，锁屏页应出现两个角色。
- 上滑解锁回到桌面，角色还在。

### 3.7 拔卡重启（关键降级测试）

- 断电 → 拔 TF 卡 → 上电。
- 必须：正常启动、桌面有角色（内嵌降级）、无 panic、无反复重启。
- 日志 `art:` 行应带 `[no card]`。

### 3.8 坏卡 / 空卡

- 卡里没有 `littleworld-v4.json`，或清单版本写错（例如 `"version": 99`）。
- 必须：正常启动，日志说明原因（`资源清单缺失` / `资源版本不兼容`），角色仍来自内嵌包。

---

## 4. 明确"未验证"的部分

本轮**没有**上电、没有刷机、没有打开串口。以下全部只是代码与主机测试成立：

| 项 | 状态 |
| --- | --- |
| `lw_art_boot()` 在真实 P4 上的行为 | 未验证（仅在 host 侧以文件路径跑通装载器） |
| TF 卡挂载（`bsp_sdcard_mount`）在这块板子上 | 未验证 |
| `lw_file_cache` 的 PSRAM 帧缓冲在真实卡上的读速 | 未验证 |
| 8/4/16 帧动画的实际观感与帧率 | 未验证 |
| 场景 `.lwr` 在 MIPI-DSI 上的显示 | 未验证（Windows 侧已验证 3 套切换） |
| 无卡降级在硬件上的表现 | 未验证 |
| 频闪是否与 V4 美术负载相关 | 未验证（`reports/v3.2/DISPLAY_FLICKER_ANALYSIS.md` 的 A/B/C 对比仍待上板） |

**不要把"Windows 上很流畅"当成"板子上很流畅"**，也不要把"能编译"当成"能显示"。

---

## 5. 刷机前置条件（等用户授权后才做）

本轮指令明确**不授权任何刷机**。真到那一步时，按这个清单核对：

1. 芯片型号：`esptool.py --port COM5 flash_id` → ESP32-P4 rev v3.x
2. 分区表：`partitions.csv` 未改动（factory 0x10000 / 6M）
3. 镜像版本与哈希：与 `reports/integration-v4/FINAL_REPORT.md` 的表一致
4. 资源包版本：卡上 `littleworld-v4.json` 的 `version` ≤ 4（固件 `LW_RESOURCE_PACK_VERSION`）
5. NVS 保护：只写 0x2000 / 0x8000 / 0x10000，**绝不写 0x9000**（存档）
6. C6 是否需要动：不需要。本轮 Wi-Fi 仍是独立实验配置，默认关闭
7. 回滚路径：`release/os-v3.1-stable` 的镜像（sha256 `ef5202c9…`）+ 全片备份
   `D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`

---

## 6. 已知限制

- 卡上 `qianqian-wave.lwp` 只有 `wave`，千千的 `idle` 仍来自内嵌 LWFA 包。所以**有卡时她的 idle 是 8 帧低分辨率版**，wave 才是 16 帧 V4 版。这是刻意取舍：她的 13 组原始动作在 V3.1 的 LWFA 里，没有 V4 重制版。
- 内嵌 LWFA 的 male 只有 1 帧（108×212）。有卡才有 8 帧 idle。
- 场景只在 `lw_visual_assets_available()` 为真时替换背景；该函数只探测 `scene-0.lwr` 是否存在，不校验另外两套。缺其中一套时会在切主题时回落到绘制背景，不崩溃但也不报错。
