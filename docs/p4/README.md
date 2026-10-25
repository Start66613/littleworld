# P4 连接层文档（中文详解）

这一层是本轮（P5）新增的服务层。文档分两套，各司其职：

| 英文（仓库根 `docs/`，Codex 读） | 中文详解（本目录，给人读） |
| --- | --- |
| `../WIFI_BACKEND.md` | `WIFI_BACKEND.md` |
| `../TIME_SERVICE.md` | `TIME_SERVICE.md` |
| `../AUDIO_BACKEND.md` | `AUDIO_BACKEND.md` |
| `../INTEGRATION_TESTS.md` | `INTEGRATION_TESTS.md` |
| `../DSH_TO_CODEX_SERVICES.md` | —（纯交接，只保留英文） |

英文版是**契约摘要**：接口、状态、配置项、能依赖什么。中文版是**设计说明**：为什么这样分层、踩了哪些坑、哪些是真验证过的、哪些是没做的。两套内容不冲突，深度不同。

## 一句话现状

模型层（Wi-Fi / 时间 / 音频）已实现并通过 182 项主机断言；适配层写了真实 `esp_wifi` 实现但**默认不参与编译**，因为板上的 Wi-Fi 在 C6 上，需要 `esp_hosted` + 匹配的 C6 固件。三套固件配置（os / legacy / noaudio）全部编译通过。**本轮没有刷机，没有上板，没有动 C6。**

## 代码入口

- `firmware/components/lw_net/` — 服务层全部代码
- `firmware/main/lw_boot.c` — `start_connectivity()` 与 500 ms `esp_timer` 轮询
- `tests/host/test_lw_net.c` — 131 项断言
- `tests/host/lw_net_esp_stub.c` — 主机端适配层桩

## 复现

```powershell
. tools\activate_env.ps1
cd firmware
$env:SDKCONFIG_DEFAULTS = 'sdkconfig.defaults;sdkconfig.defaults.rev3_x'
python $env:IDF_PATH\tools\idf.py -B ..\build\os -D SDKCONFIG=$PWD\sdkconfig.os build
cd ..
tools\run_backend_tests.ps1   # 182 checks, 0 failures
```
