# Studio 桥（P1）

分支 `agent/dsh-v4-3-engine`。工具：`tools/studio_to_device.py`（27,555 B），
测试：`tools/test_studio_bridge.ps1`（26 项，0 失败）。

## 1. 为什么需要它

Codex 的 Studio（分支 `agent/littleworld-studio-v1`，HEAD `1779a0d`）导出 `.lwpack`，
设备端 `lw_content` 读 `.lwcx`。两者**不兼容**：

| | .lwpack（Studio） | .lwcx（设备） |
| --- | --- | --- |
| 容器 | ZIP | 纯文本，行式 |
| 结构 | `project.json` + `story.json` + `resources.json` + 媒体 | `[memory] [album] [letter] [dialogue]` |
| 媒体 | PNG/JPEG/WebP 原图 | 只有路径引用，像素另存 `.lwph` |
| 版本 | `formatVersion` 整数 | `schema` + `content_version` |
| 用途 | 编辑与预览 | 设备启动时解析进固定内存 |

**不能**让设备直接读 ZIP：`lw_content` 是无堆分配的文本扫描器，ZIP 需要
inflate、需要随机访问、需要在 32 KB 上限内解压——任何一个都会推翻现有
"一个静态 arena 扫一遍"的设计。所以桥必须做在 PC 侧。

## 2. 格式事实（来自 Studio 仓库，不是猜的）

`studio/docs/FORMAT.md` 与 `studio/shared/model.ts`：

```
project.json     { formatVersion: 1, id, title, createdAt, updatedAt }
story.json       { chapters: [ { id, title, lines: [ { speaker, text } ] } ] }
resources.json   [ { path, mime, bytes, sha256, role: "original"|"preview" } ]
media/<path>     原图
```

`formatVersion` 目前只有 1。

## 3. 转换规则（显式，无隐式行为）

| Studio | 设备 | 规则 |
| --- | --- | --- |
| `project.title` | `welcome` | 直接取，超过 128 UTF-8 字节报错，**不截断** |
| `project.createdAt` | `anniversary` | 必须 `YYYY-MM-DD` 且是合法日期 |
| `story.chapters[i].title` | `[letter]` 的 title | 每个 chapter 一条 |
| `story.chapters[i].lines` | `[letter]` 的 body | 逐行 `speaker: text`，超过 512 字节报错 |
| `resources[role=original]` 且 mime 是图片 | `[album]` 的 path | 见第 4 节 |
| `resources[role=preview]` | **丢弃** | 明确写进报告 |
| 其他未知字段 | **丢弃** | 明确写进报告 |

`[memory]` 与 `[dialogue]` 在 Studio 侧没有对应概念，转换结果里不生成——
报告会写 `memories: 0, dialogues: 0`，而不是编造内容。

## 4. 图片：不做假转换

Studio 的资源是**图片文件**，设备端 `[album]` 要的是 `.lwph` 路径。桥**不**把
PNG 改名成 `.lwph` 就算转换——那会让设备读到一个假文件。实际规则：

- 图片条目**逐条校验**：mime 必须在 `{image/png, image/jpeg, image/webp}`；
- 路径必须真实存在于 ZIP 内；
- 声明的 `bytes` 与 `sha256` 必须与实际内容一致；
- 校验通过后，**该条目被丢弃**并在报告中记录
  `resources.json: N media entries verified (mime, size, sha256) and then dropped`。

理由：`.lwph` 是 RGB565 + A8 的原始像素平面，需要解码 + 缩放 + 色彩空间转换，
属于**资源打包**（`package_v4_sd.py` / `build_character_pack.py` 的职责），
不是内容转换的职责。桥把它当作"已校验但不搬运"，比"改名搬运"诚实。

`validate_resources()` 就是这个函数的实现位置：在 `convert()` 里读完
`resources.json` 之后立刻调用。

## 5. 失败即失败

`ConversionError` 覆盖的情况（全部有测试）：

| 情况 | 报错 |
| --- | --- |
| 不是 ZIP | `not a .lwpack archive` |
| 缺 `project.json` / `story.json` | 指名缺哪个 |
| `formatVersion != 1` | `formatVersion 2 is not supported` |
| `story.chapters` 不是数组 | `story.chapters must be a list` |
| 章节数 > 8（`MAX_LETTERS`） | 报实际数量与上限 |
| 单章正文 > 512 字节 | 报实际字节数与上限 |
| speaker > 16 字节 | 报实际内容 |
| 日期非法（2025-02-30） | `not a real date` |
| 非法 UTF-8（含 NUL / UTF-16 保存） | 指名文件 |
| 资源 mime 是 SVG | `image/svg+xml is not a supported device format` |
| 资源 sha256 不匹配 | 指名条目 |
| ZIP 路径穿越（`../`、绝对路径、盘符） | `unsafe path` |
| 输出无法被固件解析 | 退出码 1，保留原文件 |

**无法完整转换时默认失败**，输出文件不写，源包不动。

## 6. 用真实固件解析器校验输出

这是本轮最关键的设计：转换完成后，桥**不**用自己的 Python 逻辑自证，而是调用
`lw_content_tests.exe --check-content <file>`——一个由 `tests/host/test_lw_content.c`
新增的 CLI 模式，链接的是**真实的** `lw_content.c`。

```python
def verify_with_firmware(path: Path) -> tuple[bool, str]:
```

找不到可执行文件时，报告里写 `firmware verification: skipped (no binary)`，
**不算通过**——这一步被跳过时测试脚本会失败。

## 7. 测试（`tools/test_studio_bridge.ps1`，26 项）

用 Studio 仓库的演示项目（`git show origin/agent/littleworld-studio-v1:studio/...`）
构造真实 `.lwpack`，然后：

- 正常转换 → 输出被固件解析器接受，字段逐个核对；
- `formatVersion: 2` → 拒绝；
- 章节数 9（超 `MAX_LETTERS`）→ 拒绝并报数量；
- 正文 513 字节 → 拒绝；
- speaker 17 字节 → 拒绝（这条是调试时发现的：初版把上限写成 16 但没测边界，
  加上 17 字节的用例后暴露了 `check_text` 用的是 `>` 还是 `>=` 的歧义）；
- UTF-16 保存的 `project.json` → 拒绝；
- 资源 mime `image/svg+xml` → 拒绝；
- sha256 不匹配 → 拒绝；
- ZIP 内 `../../etc/passwd` → 拒绝；
- 缺 `story.json` → 拒绝；
- 输出文件在失败时不存在（不留半成品）。

## 8. 未做

- **没有**搬运图片。`.lwph` 生成需要 `package_v4_sd.py` 的像素流水线，
  本轮没接。报告里如实写"media dropped"。
- **没有**支持 `[memory]` / `[dialogue]`：Studio 的模型里没有这两个概念，
  编造映射等于伪造内容。
- **没有**在真实 Studio UI 上点过导出：测试用的是仓库里的演示项目文件，
  不是从运行中的 Studio 导出的包。这一点在 `BUILD_AND_TEST.md` 里也标了。
- Studio 仓库在本轮结束时是 `1779a0d`，如果它改了 `formatVersion`，桥会
  以"不支持的版本"失败而不是猜——这是有意的。
