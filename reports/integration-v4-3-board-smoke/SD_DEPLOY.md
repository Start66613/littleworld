# TF 卡资源部署记录（V4.3）

日期：2026-10-27　分支：`integration/os-v4-3-board-smoke`　提交：`381ee38`

## 1. 卡

| 项 | 值 |
|---|---|
| 盘符 | `F:`（通过 `Get-Disk` 判定为 USB 总线可移动盘，非按盘符猜测） |
| 磁盘型号 | Generic STORAGE DEVICE（USB，Removable） |
| 卷标 | `LITTLEWORLD` |
| 文件系统 | **FAT32** |
| 容量 | 63,832,162,304 B（59.45 GB） |
| 分区形式 | MBR，单分区，起始偏移 16,777,216 B，扇区 512 B |
| 簇大小 | 32,768 B（64 扇区） |
| 部署后剩余 | 63,756,533,760 B |

原卡（exFAT，59.48 GB）已换下，**未被写入**：照片目录仍为 289 文件 / 39,187,722,029 B，与识别时统计完全一致。

## 2. 为什么必须是 FAT32 且必须重编固件

两个独立的不兼容点，都会让卡上的美术在板上完全不生效：

1. **exFAT 不被支持**：`esp-idf-v5.5.5/components/fatfs/src/ffconf.h:282` 为 `#define FF_FS_EXFAT 0`，Kconfig 里根本没有 exFAT 选项（搜索计数 0）。旧卡是 exFAT，插上就是挂载失败。
2. **长文件名此前是关闭的**：`firmware/sdkconfig.os` 原为 `CONFIG_FATFS_LFN_NONE=y`，对应 `FF_USE_LFN 0`；`ff.c` 的非 LFN 分支要求 8.3 短名，而资源包里 `littleworld-v4.json`（主名 12 字符）、`qianqian-wave.lwp`（13）、`daily-room-day.lwr` 等全部超长，`fopen` 会返回 `FR_INVALID_NAME`，表现为"资源清单缺失"，画面全部退化到内置资源。

本轮把 `firmware/sdkconfig.os` 改为：

```
# CONFIG_FATFS_LFN_NONE is not set
CONFIG_FATFS_LFN_HEAP=y
# CONFIG_FATFS_LFN_STACK is not set
CONFIG_FATFS_MAX_LFN=255
# CONFIG_FATFS_API_ENCODING_ANSI_OEM is not set
CONFIG_FATFS_API_ENCODING_UTF_8=y
```

`CONFIG_FATFS_SECTOR_4096=y` 保持不动：`ffconf.h` 里 `FF_MIN_SS`/`FF_MAX_SS` 取 SD 扇区(512) 与 WL 扇区(4096) 的 min/max，两者不冲突。

**这意味着本轮固件与上一版哈希必然不同**，不是构建不稳定。

FAT32 长名在卡上已实测：写入 `littleworld-v4.json` → `dir /x` 显示别名 `LITTLE~1.JSO` → SHA-256 与源一致 → 删除成功。

## 3. 卡上内容

目标路径 `<卡根>\littleworld\`，设备侧即 `/sdcard/littleworld/`。**没有 `littleworld\littleworld\` 双层**。

| 文件 | 字节 |
|---|---|
| littleworld-v4.json | 118 |
| littleworld-v4.sha256 | 1,280 |
| content.lwcx | 1,596 |
| skin.lwp | 835,708 |
| daily-room.lwr | 1,228,816 |
| daily-room-day.lwr | 1,228,816 |
| daily-room-night.lwr | 1,228,816 |
| rpg-room.lwr | 1,228,816 |
| scene-0.lwr | 1,228,816 |
| scene-1.lwr | 1,228,816 |
| scene-2.lwr | 1,228,816 |
| scene-0-thumb.lwr | 76,816 |
| scene-1-thumb.lwr | 76,816 |
| scene-2-thumb.lwr | 76,816 |
| ui.lwp | 4,021,372 |
| qianqian-wave.lwp | 3,145,852 |
| male.lwp | 17,892,100 |
| daily.lwp | 18,125,680 |
| **qianqian-base.lwp（私有）** | **22,218,124** |

共 **19 文件 / 75,074,074 B**。

- `PACKAGE.json`、`README.txt` **不上卡**（它们在 `dist/` 同级，只供人看）。
- 清单：`{"schema":1,"version":4,"pack":"littleworld-v4","themes":3,"characters":2,"payloadBytes":75072592}`。私有版 payloadBytes 与公开版（52,854,468）不同，因为多带了私有包 —— 这是预期差异，`lw_resource.c` 不校验该字段。

## 4. 校验

- 公开包 18 文件：robocopy 后逐文件 SHA-256 比对，**18/18 全部一致**。
- 私有包 19 文件：**19/19 全部一致**，`qianqian-base.lwp` SHA-256 `FD38DCA4CF666B2611FD49088660A29A3A036874A28AAF22CE7AB619138BD8F5`。
- 卡上实物用固件侧打包器只读复核：`package_v4_sd.py --verify-only --target F:\littleworld` → `"ok": true`，19 个文件全部通过几何与哈希检查，EXIT 0。
- `chkdsk F:`（只读）：`Windows has scanned the file system and found no problems.`，1,948,003 个分配单元，其中 1,945,695 可用，与 FSInfo 记录的 1,945,695 完全一致。
- 引导扇区结构抽查（裸卷读取）：`MSWIN4.1` / 512 B 扇区 / 64 扇区每簇 / 32 保留扇区 / 2 个 FAT / FAT32 大小 15,219 扇区 / 根目录簇 2 / FSInfo 扇区 1 / 备份引导扇区 6 / `0x55AA` 签名 / 备份引导与主引导逐字节相同。

### 用固件自己的判定逻辑复核卡（不是用打包器的逻辑）

`lw_resource_verify()` 只做三件事：读 `littleworld-v4.json` 取 `schema` 与 `version`，然后对 3 个主题各查 `scene-N.lwr`（须 `LWSR0001` + 1024×600）与 `scene-N-thumb.lwr`（须 `LWSR0001` + 256×150）。按同一套规则在卡上跑了一遍：

```
manifest exists   : True
schema            : 1 (firmware needs 1)
version           : 4 (firmware accepts 1..4)
scene-0.lwr           magic=True 1024x600 want=1024x600 -> OK
scene-0-thumb.lwr     magic=True  256x150  want= 256x150 -> OK
scene-1.lwr           magic=True 1024x600 want=1024x600 -> OK
scene-1-thumb.lwr     magic=True  256x150  want= 256x150 -> OK
scene-2.lwr           magic=True 1024x600 want=1024x600 -> OK
scene-2-thumb.lwr     magic=True  256x150  want= 256x150 -> OK
themes bitmask    : 111
status would be   : LW_RESOURCE_OK / 资源已验证
```

也就是说：**只要固件这一版带 LFN，卡挂载成功后资源校验会通过**，`lw_resource_fallback()` 返回 false，美术不会退化成内置资源。

注意 `lw_resource.c` **不校验** `littleworld-v4.sha256`（固件里没有这个逻辑），清单文件的消费方是打包器与人工核对，不是设备。设备只认 manifest 的 schema/version 与 6 个场景文件头。

## 5. 私有素材边界

`qianqian-base.lwp`（千千 13 段动画，22,218,124 B）**只在本机与这张 TF 卡上**：

- 不进公开仓库（`dist/littleworld-v4-sd-private/` 已在 `.gitignore`）；
- 不上传 GitHub / Meowa / 任何云端；
- 本记录只写文件名与哈希，不含素材内容。

公开包缺失该文件时，千千会退化为内置待机帧 + 招手包，功能不崩，但少 13 段动作。

## 6. 上板前必须确认

1. 固件必须是**带 LFN 的这一版**（见第 2 节），否则卡等于没插。
2. 卡插在板子的 TF 槽（不是 USB），开机日志应出现 `storage:` 相关行；若出现 `storage: no card mounted (...)` 或 `资源清单缺失`，先查卡结构与固件 LFN 配置，不要反复烧录。
