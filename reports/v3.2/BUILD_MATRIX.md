# Little World V3.2 — 编译矩阵

> 本轮全部为**离线编译**。没有上电、没有串口、没有烧写、没有擦除、没有改分区表。
> 表中每个镜像都能在 `D:\LittleWorld-V32\firmware\build\<dir>\little_world.bin` 找到。
> **只有 os-offline 是日常镜像**；diag-a/b/c 是一次性对比镜像；os-wifi-experimental
> 是冒烟镜像。

## 1. 配置定义

| 配置 | 目录 | sdkconfig 片段 | 关键差异 | 用途 |
| --- | --- | --- | --- | --- |
| os-offline | `build/os` | 无（基准） | `LW_NET_WIFI_ENABLE=n`，背光运行时可调 | **日常** |
| diag-a | `build/diag-a` | `sdkconfig.extra.diag-a` | 与 os-offline 完全一致（仅注释） | 频闪对照组 A |
| diag-b | `build/diag-b` | `sdkconfig.extra.diag-b` | `CONFIG_LW_BACKLIGHT_FIXED=y`，开机写一次占空比后永不再写 | 频闪对照组 B |
| diag-c | `build/diag-c` | `sdkconfig.extra.diag-c` | `CONFIG_LW_BACKLIGHT_MINIMAL=y` + `CONFIG_LW_DIAG_STATIC_UI=y`，30% 固定 + 静态界面 | 频闪对照组 C |
| os-wifi-experimental | `build/wifi` | `sdkconfig.extra.wifi` + hosted manifest | `LW_NET_WIFI_ENABLE=y` + `LW_NET_SNTP_ENABLE=y`，esp_hosted/esp_wifi_remote | Wi-Fi 冒烟 **不日常** |
| legacy | `build/legacy` | `sdkconfig.extra.legacy` | `LW_BOOT_MODE_LEGACY=y`，回到 OS 之前的旧外壳 | 回滚验证（本轮未重跑） |
| noaudio | `build/noaudio` | `sdkconfig.extra.noaudio` | `LW_ENABLE_AUDIO=n` | 音频故障隔离（本轮未重跑） |

配置全部通过 `$base + 片段` 的方式叠加，**不改 `sdkconfig.os` 本身**，所以日常镜像的
默认值没有被诊断开关污染。

## 2. 结果

脚本每编译完一个配置就增量写一次 `reports/v3.2/build_matrix.json`，后面失败不会抹掉
前面的结果。本轮实际跑完的五个配置：

| 配置 | 目录 | 字节 | SHA-256 |
| --- | --- | --- | --- |
| os-offline | `build/os` | 2,248,992 | `b60158420a85d001c9170599afc0e0e554d65721b36982497711db0a3461c7ea` |
| diag-a | `build/diag-a` | 2,248,992 | `8af55911024b9adf459d8e72e4ebb56f4726cf7f0f8a087d3715543ebbf99d69` |
| diag-b | `build/diag-b` | 2,249,072 | `1e3d20640908a5a3ad4a00ece1706f97c34148653e5e42cef7520caa7e524b97` |
| diag-c | `build/diag-c` | 2,245,472 | `f5e7ef859345b52f5b4ad4012151b87494754c3acebb1d59e6e78b32e28ee771` |
| os-wifi-experimental | `build/wifi` | 2,558,064 | `d25afda0f4b87c9d8ce9700e46bf9448ece8b4ba502de69ce859f84892de7bab` |

读法：

- **os-offline 与 diag-a 的字节数完全相同、哈希不同**。这是符合预期的：diag-a 的
  片段是空注释，编译选项与 os-offline 一模一样，差异只来自构建时间戳与路径嵌入。
  两者的 `sdkconfig` 中背光三项逐行相同（已核对）。
- **diag-b 比 os-offline 大 80 字节**：多了"固定亮度"分支的字符串与日志格式。
- **diag-c 最小（-3,520 字节）**：静态 UI 把 `update_actors()` 的动效路径与帧时钟
  相关代码一起裁掉，链接器做了 dead-code elimination。
- **wifi 镜像大 309 KB**：esp_hosted + esp_wifi_remote + SDIO 传输层 + lwIP 的
  esp_netif 胶水，全部是链接进来的真实代码，不是桩。

复核哈希：

```powershell
Get-FileHash D:\LittleWorld-V32\firmware\build\os\little_world.bin -Algorithm SHA256
```

## 3. 分区占用

`firmware/partitions.csv` 本轮**没有改动**。factory 分区 6 M（6,291,456 B），
os-offline 2,248,992 B 占 35.7%，os-wifi-experimental 约 2.55 MB 占约 40.6%。
两者都远低于上限，NVS（0x9000）、OTA 与存储分区的位置一个字节都没动。

## 4. 本轮没有做的事（避免误读）

- 没有把任何镜像烧进板子，所以表中每个镜像都只是"编译通过"，不是"板上验证过"。
- 没有重跑 legacy / noaudio：这两个配置与 V3.1 相比代码路径没变，本轮只改了
  lw_backend/lw_ui/lw_net，重跑价值低于占用编译时间。需要时一条命令即可补：
  `tools/build_v3_2_matrix.ps1 -Configs legacy,noaudio`。
- 没有做压测（板端）——离线压测见 `tests/host/test_lw_stress.c`，2,035 项断言，
  纯模型层，不需要硬件。
