# 构建与产物

## 工具链

- ESP-IDF **v5.5.5**，target `esp32p4`，`CONFIG_ESP32P4_REV_MIN_300=y`
- 编译器 `riscv32-esp-elf` esp-14.2.0_20260121
- 构建脚本 `tools/build_v3_2_matrix.ps1` 的同构调用（单配置）：

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Set-Location D:\LittleWorld-V43-Board\firmware; . D:\LittleWorld-V43-Board\tools\activate_env.ps1; idf.py -B build/os -DSDKCONFIG=build/os/sdkconfig build"
```

日志：`reports/logs/v43-board-os.log`（合并后）、`reports/logs/v43-board-os2.log`（几何修复后）。

## 镜像

| 分区 | 地址 | 文件 | 大小 | SHA-256 |
| --- | --- | --- | --- | --- |
| bootloader | `0x002000` | `firmware/build/os/bootloader/bootloader.bin` | 22,016 B | `624d7f4b461a6d5dc05e3dfc30ab0152a241b63c692a49d84fca656121d240d1` |
| partition table | `0x008000` | `firmware/build/os/partition_table/partition-table.bin` | 3,072 B | `8404023636b162d4c94c1911a9ba1f152968769e86a683bbe2134e4fcf828fc6` |
| **app** | `0x010000` | `firmware/build/os/little_world.bin` | **2,347,472 B** | **`92c05c2d26922935178bb5996cefcca1649c464964a7b77fa4a2faba5da19036`** |

`idf.py` 报 `little_world.bin binary size 0x23d1d0 bytes`（= 2,347,472），
`Smallest app partition is 0x600000 bytes. 0x3c2e30 bytes (63%) free`。

## 分区适配

`firmware/partitions.csv` **未改动**：`git rev-parse HEAD:firmware/partitions.csv` 与基线 `05bfc36` 都是
`93e4a90aee6edc5d46655abd38fbc91e400648b5`。

```
  NAME       TYPE   SUBTYPE  OFFSET     SIZE
  nvs        1      2        0x009000   0x006000
  phy_init   1      1        0x00F000   0x001000
  factory    0      0        0x010000   0x600000
  storage    1      130      0x610000   0x400000
```

app 2,347,472 / 6,291,456 B = **37.3% of factory**，放得下。
NVS（`0x9000..0xF000`）与 `phy_init` 不在写入计划里。

镜像校验（`tools/check_v3_images.ps1` 的 image_check，由 `flash_v4_safe.ps1` 在 dry run 中调用）：

```
PASS  little_world.bin   chip esp32p4 (id 0x0012)  rev v3.0 (300) .. v3.99 (399)
                         flash 32MB, mode field DIO
PASS  bootloader.bin     同上
INFO  partition-table.bin (4 个分区, ascending, no overlap)
3 image(s), 0 failure(s)
```

## 代码是否真的进了固件（ELF 符号）

```
riscv32-esp-elf-nm.exe --defined-only firmware/build/os/little_world.elf
```

| 符号 | 代码来源 | 结果 |
| --- | --- | --- |
| `lw_os_start` | 壳 | PRESENT |
| `lw_skin_place` / `lw_skin_fit` / `lw_skin_panel` | Codex `lw_skin.c` | PRESENT |
| `lw_polish_clip` / `lw_polish_art` | Codex `lw_polish.c` | PRESENT |
| `lw_ambience_phase` / `lw_ambience_refresh` | Codex `lw_ambience.c`（已改） | PRESENT |
| `lw_visual_daily_image` / `lw_visual_aux_configure` | Codex `lw_visual_assets.c` | PRESENT |
| `lw_ui_clock_read` / `lw_ui_clock_attach` | DSH 时钟 | PRESENT |
| `lw_pack_registry_add` | DSH LWPX | PRESENT |
| `lw_art_boot` | DSH 启动接线 | PRESENT |
| `lw_ambience_previewing` | 新增声明 | **ABSENT**（声明了但还没有调用点，`-ffunction-sections` 回收；不影响功能） |

同时检查了 `.bin` 里的**资源文件名常量**，确认六包与四个房间场景都在镜像里：

```
male.lwp  qianqian-base.lwp  qianqian-wave.lwp  daily.lwp  ui.lwp  skin.lwp   → IN BINARY
rpg-room  daily-room  daily-room-day  daily-room-night  scene-%u%s.lwr  %s/%s.lwr → IN BINARY
v43skin / v42ui / v42 → IN BINARY
```

> 只说"编译通过"是不够的：这几个名字如果没进 `.bin`，板上就是一屋子纯色背景。

## 本轮修掉的三处真实阻碍

上板前**必定失败**、而在 Windows 上看不出来的三件事：

1. **`lw_pack` 只认 256×256 / 6 clip / 2 namespace。**
   正式美术是 male 256×256(7 clip)、daily 320×320(8)、ui 544×352(1)、skin 512×544(1)，
   除 male 外全部会被 `LW_PACK_ERR_GEOMETRY` 拒绝。现在几何从文件头读，
   上限 1024×600 / 单帧 1,250,000 B / 32 clip / 5 namespace / 8 文件。
2. **租约预算 786,432 B 小于 skin 图集 835,584 B**，即使注册成功也 `acquire` 不到。
   提到 2 MiB（`lw_assets` 自己的上限）。
3. **`lw_art.c` 只接 male / qianqian-wave**，daily / ui / skin 与辅助场景目录从未注册。
   改成一张表逐文件判断返回码：必需包失败才报错，可选包 `LW_PACK_ERR_OPEN` 视为旧卡静默跳过。

细节与测试见 `OPEN_ISSUES.md` 与 `SD_CARD_PACKAGE.md`。

## 测试

宿主侧（全部 EXIT 0，日志 `reports/logs/v43-board-*`）：

| 套件 | 结果 |
| --- | --- |
| `run_backend_tests.ps1` | Store 29 / System 22 / net 216 / stress 2035 / diag 132 / recovery 150 / faults 157 / content 124 = **2,865 checks, 0 failures** |
| `run_engine_tests.ps1` | 264 checks, 0 failures |
| `run_v4_resource_tests.ps1` | **203 checks, 0 failures**（新增 `test_pack_geometry` 与 `test_pack_clip_budget`） |
| `run_card_layout_tests.ps1` | 69 checks, 0 failures |
| `run_host_tests.ps1` | 142 checks, 0 failures |
| `test_v4_sd_pack.ps1` | 11 通过 0 失败 |
| `run_windows_regression.ps1` | System 22 / Duo 82 / RPG 85 / Storage 33（句柄 58→58）/ OS V2 **752 checks 0 failures**, 100 overlay cycles, peak pixels 1,999,872 B, **WINDOWS REGRESSION OK** |

变异证明：把 `LW_PACK_MAX_CLIPS` 改回 8、`LW_PACK_MAX_ACTORS` 改回 2，
资源套件立刻 **20 项失败**（`base walk resolvable`、`daily family resolvable`、`ui family resolvable`、
`skin family resolvable` …），改回后 203/0 通过。

## Windows 联合版

`tools/build_demo.ps1` 从**本分支**重新构建（不是拿别的分支的 exe）：

- `build/host/LittleWorld.exe` 1,401,344 B
- sha256 `9ed1414dd822d016ac31b61fd979ed52aa24b38d6113e1f787503bddb5b1c55c`
- 截图导出见 `WINDOWS_JOINT.md`
