# 烧录记录 —— 未执行写入

**结论：本轮没有任何字节被写入。** 板子当前没有连接：`-ListPorts` 与默认 dry run 都显示
`serial ports found: 0`；后来系统出现过 `COM7` / `COM8`，经设备名核对是
「蓝牙链接上的标准串行」，不是开发板，工具按名字拒绝自动选用。
所以只做了只读的镜像/分区/地址校验。

> **镜像已更新（2026-10-27 17:39）**：为了让板子读得到 `littleworld-v4.json` 等长文件名，
> `firmware/sdkconfig.os` 打开了 `CONFIG_FATFS_LFN_HEAP=y` 并重编，app 变成
> **2,352,784 B / `0114fe684c636d144a88b988ab3232a29d4ef7f87b1fb00d9d13be3d235c9f51`**。
> 下文表格已按新镜像更新；bootloader 与分区表未变。

## 已完成的只读检查（8 项）

`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1`（默认 dry run，日志 `reports/logs/v43-board-flash-dryrun.log`）

| # | 项目 | 实测结果 |
| --- | --- | --- |
| 1 | COM 端口列表 | **0 个可用**（`(no serial port present)`；系统里的 COM7/COM8 是蓝牙串行，不是板子） |
| 2 | ESP32-P4 芯片识别 | 未做——需要端口；镜像头部声明 `esp32p4 (id 0x0012)`，rev v3.0(300)..v3.99(399) |
| 3 | Flash 配置 | 镜像声明 32 MB、mode DIO（与设备 32 MB 一致，历史探针已确认） |
| 4 | 现有固件与分区兼容性 | 新分区表 sha256 `8404023636b162d4c94c1911a9ba1f152968769e86a683bbe2134e4fcf828fc6`，**与上次 V3.1 刷入的完全相同** → 兼容 |
| 5 | 旧版备份是否可恢复 | 存在：`D:\Espressif\LittleWorld\private\backups\LittleWorld\20261025-005021`（`p4-full-A.bin` / `p4-full-B.bin` 各 33,554,432 B + `flash_id.txt` + `manifest.json`） |
| 6 | 预计写入地址 | `0x002000` bootloader 22,016 B / `0x008000` 分区表 3,072 B / `0x010000` app 2,352,784 B |
| 7 | 新镜像 SHA-256 | app `0114fe684c636d144a88b988ab3232a29d4ef7f87b1fb00d9d13be3d235c9f51`（校验器输出小写） |
| 8 | 是否需要更新 TF 卡 | **已更新**：V4.3 的 `daily.lwp` / `ui.lwp` / `skin.lwp` / 四个房间场景 + 私有 `qianqian-base.lwp` 已写入 FAT32 新卡，见 `SD_DEPLOY.md` |

镜像校验原样输出：

```
PASS  little_world.bin   chip esp32p4 revs 300..399  flash 32MB  DIO
PASS  bootloader.bin
INFO  partition-table.bin   nvs 0x9000/0x6000  phy_init 0xF000/0x1000
                            factory 0x10000/0x600000  storage 0x610000/0x400000
3 image(s), 0 failure(s)
[partitions] app fits: 2352784 / 6291456 B (37.4% of factory)
[partitions] partition layout: ascending, no overlap
```

## 分区表相同 → 建议 App-only

分区表 sha256 与上次刷入的一致（`8404023636…`），bootloader 也没有布局变更，
所以**板子接上后优先用 `-Stage app`**，只写 `0x010000` 一个区，风险最小：

```powershell
# 1) 只读探针（不写）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 -Probe -Port COM5

# 2) App-only 写入（两个开关缺一不可）
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 `
    -Stage app -Port COM5 -Execute -Authorized
```

如果探针报告分区表不兼容（芯片 revision 不是 v3.x、Flash 不是 32 MB、分区表哈希对不上），
**停下来先解释，不要改 -Stage full 硬写**。

工具已内置的边界（代码里写死，不是口头承诺）：

- 默认 dry run；`-Execute` 与 `-Authorized` **两个都要**，缺一个就拒绝
- 只写 `0x2000` / `0x8000` / `0x10000`，**绝不写 nvs（0x9000..0xF000）**、不写 phy_init、不写 storage
- **没有 `erase_flash`**、没有整片写入、没有失败自动重试
- 不碰 ESP32-C6 固件、不动 eFuse、不改分区表
- 不会自动挑一个"看起来像"的 COM 口

## 刷完之后

写入成功 ≠ 板端运行成功。刷完必须抓串口日志：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\serial_capture.ps1 -Port COM5 -Seconds 30
```

日志落在 `reports/logs/`，然后按 `BOARD_TEST.md` 一项项看，没看到的写 NOT TESTED。

## 回滚

1. **不要** `erase_flash`——NVS 里是存档，本工具从不写它。
2. 先试 `-Baud 115200` 重刷（CH343 在高速率下偶尔不稳）。
3. 回到上一版固件（V3.1 稳定版镜像仍在 `D:\LittleWorld-V3\firmware\build\os\little_world.bin`，
   2,249,088 B，sha256 `ef5202c965bc23db2cf2d870ed60f29abfa056f2b0f597c5e1aa94ab4cc66be2`）：
   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\flash_v4_safe.ps1 `
       -Config os -Stage app -Execute -Authorized -Port COM5
   ```
   （用 `D:\LittleWorld-V3` 那份 `build/os`，或把该镜像复制到本工作区对应位置）
4. 只有板子彻底不启动时才考虑整片恢复：
   `python -m esptool --chip esp32p4 -p COM5 -b 460800 write_flash 0x0 <backup>.bin`
   —— **这会连 NVS 一起覆盖**，非必要不要用。

## 本轮没有做的事（明确记录）

- 没有打开任何串口
- 没有执行 `-Probe`
- 没有 `-Execute`
- 没有擦除、没有改分区、没有更新 C6 固件、没有动 eFuse
