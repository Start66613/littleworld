# TF 卡资源包

## 复制方法（请照这个做）

```
把 D:\LittleWorld-V43-Board\dist\littleworld-v4-sd\littleworld
整个文件夹复制到  TF 卡根目录
```

复制完成后，卡上必须是：

```
<卡根>\littleworld\littleworld-v4.json
<卡根>\littleworld\male.lwp
...
```

即 **`/sdcard/littleworld/`**，`littleworld` 与卡根同级。
**不要再套一层**，也就是不存在 `/sdcard/littleworld/littleworld/content.lwcx`——
这个双重目录问题在 V4.0 出现过（内容文件被读成 `<root>/littleworld/littleworld/content.lwcx`），
本轮已用下面的路径核对逐条确认。

`dist/littleworld-v4-sd/` 下的 `PACKAGE.json` 与 `README.txt` **不要复制到卡上**，
它们只是打包记录（留在电脑上存档）。

## 卡上内容（18 个文件，52,855,866 B）

| 文件 | 字节 | SHA-256 | 来源 |
| --- | ---: | --- | --- |
| `littleworld-v4.json` | 118 | `74eb97dbc18a569837691aa818020f2e82534d171f1525a8dfe4323a6531528e` | 生成 |
| `littleworld-v4.sha256` | 1,280 | `b9cdad29e6bc624285bfeb7f7fbbacecbf97a965e7d0a63aa31bcd7e078a939b` | 生成（16 个资源文件的逐文件校验清单） |
| `scene-0.lwr` | 1,228,816 | `0f8bf7982c0f586635a368c497e7d96c421d5a234af0d8a8a9c51b32a19e269f` | visual-v4 |
| `scene-1.lwr` | 1,228,816 | `f5d4c146d921987255070d54420b82dbc78b60c085aaa12d95f3289706f1beae` | visual-v4 |
| `scene-2.lwr` | 1,228,816 | `f4e63792048e9dfe6cba262b627df19fdfdb3b33da7e894cff56022aa6550dd7` | visual-v4 |
| `scene-0-thumb.lwr` | 76,816 | `0b3d37cea72b220a2fbb7e9456dc036ab9331ca689148d804966c31e562b05bf` | visual-v4 |
| `scene-1-thumb.lwr` | 76,816 | `d101b7df816e5ad01768c1ef243e67efd80435b7e7f532032476a0d02d9942d8` | visual-v4 |
| `scene-2-thumb.lwr` | 76,816 | `3bd3458a2018b52a478af751a13f6e7df7e7618319ebfa497152371dc5ccc98e` | visual-v4 |
| `male.lwp` | 17,892,100 | `bd8f0ccbe1938fbfd902f4374139e2c15475891195c7ce7ea847f4c5e085df2d` | visual-v4-2 |
| `qianqian-wave.lwp` | 3,145,852 | `b838d35c5af1bdc6c1a7646afce0432747d714e9979eb0f1d14711dc951f96e6` | visual-v4 |
| `daily.lwp` | 18,125,680 | `0cc7b372b12279a1409f2d57fbf08377e37710a089d55144f1fce30294ee082b` | visual-v4-3 |
| `ui.lwp` | 4,021,372 | `4fee1daf7b472e010a8e8aaf55838c2b015fd195304af1832e29ddd2be5cb8d1` | visual-v4-2 |
| `skin.lwp` | 835,708 | `b25b343973a945be8e24a37fe90d22e1adb7a09f9008b9ea4c3aa07e6c246109` | visual-v4-3 |
| `daily-room.lwr` | 1,228,816 | `da50673917fef45c696b68464257f329b7957dfcab39afdc2f1e35fa9dcebc39` | visual-v4-2 |
| `daily-room-day.lwr` | 1,228,816 | `004adf3f2d6f93687a0056c54c430421410f48bf785714fbf7c954ac96872555` | visual-v4-2 |
| `daily-room-night.lwr` | 1,228,816 | `16d0df155398e0a01f10e5bbec8cb86c7c274ed12eea19b0c5730a16e799dd3c` | visual-v4-2 |
| `rpg-room.lwr` | 1,228,816 | `2321df177c36f76da222dadf3a9e890eb80a148ad2b5e0ac8c58dcce723c4244` | visual-v4-2 |
| `content.lwcx` | 1,596 | `ad093249a140fcce3caf58e69580491d141b013fa3012a663de8f30d638409be` | 仓库 `content/content.lwcx` |

清单内容：`{"schema":1,"version":4,"pack":"littleworld-v4","themes":3,"characters":2,"payloadBytes":52854468}`。
`schema` 必须是 1、`version` 1..4，这两个数字由 `lw_resource.c` 的行扫描器校验，**不要改**。

## 每个包实际提供什么

用 `tools/lwpx_dump.py` 直接读包头得到（不是文档抄的）：

| 包 | 画布 | clip 数 | 内容 |
| --- | --- | ---: | --- |
| `male.lwp` | 256×256 | 7 | idle 8f / blink 4f / wave 16f / walk_down 16f / walk_left 15f / walk_right 16f / walk_up 16f，anchor (128,225)/(128,226) |
| `qianqian-wave.lwp` | 256×256 | 1 | wave 16f，anchor (128,226) |
| `daily.lwp` | 320×320 | 8 | couple.seated 16f / couple.wave 16f / couple.rest 1f / dog.idle 8f / dog.sleep 8f / dogbed.empty 1f / dogbed.sleep 1f / dogbed.look 8f |
| `ui.lwp` | 544×352 | 1 | sheet 7f（Meowa 组件图集） |
| `skin.lwp` | 512×544 | 1 | atlas 1f（17 种皮肤九宫格） |

## 私有素材（不在公开包内）

`qianqian-base.lwp`（22,218,124 B，13 clip：idle / sleep / sleep_enter / sleep_idle / sleep_turn /
walk_down / walk_down_left / walk_down_right / walk_left / walk_right / walk_up / walk_up_left / walk_up_right）
是私有素材，Codex 明确要求不公开。它**不在** `dist/littleworld-v4-sd/` 里，
只在 `dist/littleworld-v4-sd-private/`（该目录已加入 `.gitignore`，不提交、不发布）。

没有它时千千用**内置待机帧 + 招手包**，画面正常、只是动作少；
`PACKAGE.json` 的 `missingOptional` 会记为 `["qianqian-base.lwp"]`，这是预期结果，不是错误。

要在自己机器上做一张完整卡（含千千 13 段）：

```powershell
$py = 'D:\LittleWorld-Meowa\tools\venv\Scripts\python.exe'
& $py tools\package_v4_sd.py --private `
    --source D:\LittleWorld-Meowa\handoff\codex-v43-frozen-2900569\runtime `
    --out dist\littleworld-v4-sd-private
```

私有包：21 个文件 75,081,965 B，`--verify-only` EXIT 0。

## 校验

```powershell
& $py tools\package_v4_sd.py --verify-only --target dist\littleworld-v4-sd\littleworld --json
```

本轮结果：公开包 **EXIT 0**、私有包 **EXIT 0**；
`tools/test_v4_sd_pack.ps1` **11 通过 0 失败**（含"内容被改动被哈希清单发现""清单超出固件读取上限被拒绝"等负例）。

## 为什么这版包才是对的

旧默认源是 `assets/visual-v4/runtime`，里面只有 3 clip / 5,505,364 B 的旧 `male.lwp`，
且打包器强制 256×256、只列 male 与 qianqian-wave。
所以"上一版 SD 包校验通过"并不代表 V4.3 素材被打进去了——**旧包发的是旧 male**。

现在默认按 `assets/visual-v4-2/runtime` → `assets/visual-v4-3/runtime` → `assets/visual-v4/runtime`
的顺序逐文件找第一个存在的，几何按每个包自己的文件头校验，
`daily/ui/skin` 与四个房间场景都进包。
逐 SHA 比对确认：包内 male / daily / skin / ui / qianqian-wave / scene-0 与仓库源文件**完全一致**。
