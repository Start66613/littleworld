# Meowa 能力审计与画风一致性诊断

> 审计对象：`D:\LittleWorld-Meowa\tools\meowa-skills`（官方 `Meowa-AI/meowa-skills`）+ 本地 CLI `meowart_api.py`
> 审计方式：只读安装目录、`--help` 实测、阅读 4 份 references、本地免费测算；**本轮零生成、零扣费**
> 审计时间：2026-10-25 20:20–20:55（Asia/Shanghai）
> 适用对象：Little World 像素双人（qianqian + 男方）静态与动画资产

---

## 0. 一句话结论

男方与 qianqian 画风不统一的**主因不是模型不行，而是我选错了能力**：我用 `custom-size-pixel-gen-run`（服务端实现为 `one_click_pixelate` 工作流：先生成柔和插画、再量化成像素）**从零、分两次、独立生成**两个角色，两次调用之间没有任何共享的画布、调色板或像素网格约束；而官方本来就有**同画布双槽位角色模板 `large_portrait`（256px、count=2）**、**同风格克隆 `asset-clone-run`** 和 **像素保真编辑 `image-edit-run --strict`** 三种专为"同一套资产"设计的能力，我一个都没用过。

同时确认：**Skill 是最新的、CLI 是正常工作的**，问题在调用方式。

---

## 1. 安装核查

| 项目 | 实测值 | 结论 |
|---|---|---|
| 仓库 | `D:\LittleWorld-Meowa\tools\meowa-skills` | 存在 |
| 本地 HEAD | `33578ab708c1b4dfcd83fe79dffdbd47ff4083b2` | — |
| `origin/main` | `33578ab708c1b4dfcd83fe79dffdbd47ff4083b2` | **一致，0 ahead / 0 behind** |
| `release.json` | `version 2026.10.23.2`，`runner meowart_api.py`，`sha256 282cdd06…75b70f` | 与 CLI `--version` 输出一致 |
| `git status` | 干净，无本地修改 | 未污染 |
| 文件完整性 | 21 个文件，含 `SKILL.md`、`meowart_api.md`、`web_parameter_contract.json`、`references/` 10 份、`scripts/` 4 份、`agents/openai.yaml` | **完整** |
| 4 份指定文档 | `capability-routing.md`(12.4KB)、`pixel-and-hd-assets.md`(23.3KB)、`ui-and-image-editing.md`(27.2KB)、`animation-and-video.md`(26.3KB) 全部存在 | **已逐份阅读** |

```
meowart_api.py 2026.10.23.2
Python 3.12.14
requests 2.34.2 / Pillow 13.0.0 / numpy 2.5.4
命令总数 82
```

**依赖**：venv 内三个库齐全，无缺失。CLI 不需要额外系统依赖（Win32 下 `character-multi-view-run --orientation {纵版,横版}` 的中文选项在 GBK 控制台显示为乱码，但参数本身可用，属于显示问题不是功能缺失）。

**结论 1：Skill 已是最新，不需要重装。**
**结论 2：CLI 正常，82 条命令可枚举，`--help` 逐条可读。**

---

## 2. 八个能力的真实参数（全部来自 `--help` 实测，未编造）

原始输出存档在 `reports/audit/*.help.txt`。

### 2.1 `pixel-gen-run` — 模板驱动像素生成

```
--template-name TEMPLATE_NAME   （必填，来自 pixel-gen-template-info）
--requirement REQUIREMENT       （必填）
[--aspect-ratio] [--direction]
[--remove-bg-method none|standard|advanced]
[--reference-file] [--reference-files]   ← 接受参考图
```

- 能做什么：按**预设模板**产出固定尺寸/数量的像素资产。
- 参考图：接受。
- 固定尺寸：**最强**（尺寸由模板决定，不靠提示词）。
- 保风格：**强**，同一模板 = 同一画布、同一像素密度、同一调色板倾向。
- 计费：模板定价，**需服务端确认**（本地无定价表）。

### 2.2 `custom-size-pixel-gen-run` — 自定义尺寸像素

```
--prompt（必填） --width --height（必填）
[--reference-image]×8  [--generation-model nano-banana|image-2]（默认 nano-banana）
[--content-mode portrait|illustration|asset_pack|other]
[--remove-bg-method none|standard|advanced]  [--fill-canvas|--no-fill-canvas]（默认 fill）
[--strong-pixelation]   ← 必须配合参考图
```

- 内部 endpoint：`/api/workflows/one_click_pixelate/run`（代码 297 行），即"先画后像素化"。
- 文档明确：**文本生成较稳，参考图引导较不稳**（pixel-and-hd-assets.md:210）；fill-canvas 是"构图请求，不是几何保证"（:214）；背景移除在此命令中**故意关闭**（:213）。
- 参考图：接受，但引导不稳。
- 固定尺寸：**中等**，实际会 ±10–20px 漂移（本批 179–227px）。
- 保风格：**弱**——每次调用重新推导调色板与明暗。本批所有产出被量化成 **64 色**，而 qianqian 原图是 **716 色**。
- 计费：实测 **20 积分 / 张**（256×256，含参考图不加价）。

### 2.3 `general-image-run` — 通用生成

```
--prompt --generation-model image-2.5|image-2|nano-banana（默认 image-2.5）
[--reference-image]×8  [--resolution 1K|2K]
[--aspect-ratio 1:1|3:4|4:3|9:16|16:9]  [--quality standard|detailed|ultimate]
[--remove-bg-method none|standard]（Image2.5 原生免费抠图）
```

- 参考图：接受最多 8 张，可指定"参考图 1 管什么、参考图 2 管什么"。
- 固定尺寸：**无**（只有比例档位）。
- 保风格：取决于参考图与提示词，**无结构约束**。
- 实测计费：2048² 1:1 detailed = 10；2048×1152 16:9 = 10–12（房间 10、首页 14）。

### 2.4 `image-edit-run` — 单张精修

```
--reference-image（必填） --prompt（必填） [--mode pixel|hd]
[--strict]              ← 保持精确像素结构
[--regional-pixelation] 或 [--multi-pixelation]（三份结果不加价）
[--generation-model nano-banana|image-2|image-2.5]（像素默认 nano-banana）
[--resolution 1K|2K] [--quality] [--remove-bg-method]
```

- 能做什么：对**已有图**做一次受控变换（改衣服、改表情、转风格）。
- 参考图：1–8 张。
- 固定尺寸：沿用输入画布。
- 保风格：**最强**——它是在原图上改，不是重画。`--strict` 保证像素结构不动。
- 计费：**需服务端确认**（文档说明区域像素化有 2 积分附加项）。

### 2.5 `asset-clone-run` — 同风格成组克隆

```
--reference-image×36  [--prompt]  [--clone-count 1|4|9|16|25|36]
[--mode pixel|hd]  [--generation-model nano-banana|image-2.5]
[--cell-width] [--cell-height] [--fit-mode pad|crop]  ← 居中补边/裁切，绝不缩放
[--quality] [--resolution 1K|2K] [--remove-bg-method]
```

- 能做什么：把参考资产克隆成**一张风格一致的网格图**（默认提示词就是"保持美术风格一致"）。
- 参考图：1–36 张；尺寸不一致时用 cell + pad/crop 对齐。
- 保风格：**专为此设计**。文档给了模型取舍：Nano Banana 形状/像素边缘更稳，Image2.5 配色更接近参考（ui-and-image-editing.md:151-153）。
- 计费：像素默认 Image2.5 / 1K / Detailed；具体价格**需服务端确认**。

### 2.6 `one-click-upgrade-run` — 变体/升级序列

```
--reference-image --variant-prompt×1..8  [--mode pixel|hd]
[--generation-model nano-banana|image-2] [--quality] [--resolution] [--remove-bg-method]
```

- 能做什么：同一张源图 → 1–8 个**同风格同尺寸**变体（同画布、同锚点、同比例）。
- 保风格：**强**，官方定位就是"similar style, scale, and canvas"。
- 固定尺寸：沿用源画布。
- 前置免费步骤：`one-click-upgrade-prompts --count N`（只出提示词，不扣费）。

### 2.7 `character-multi-view-run` — 八方向

```
--reference-image（必填） [--mode pixel|hd] [--canvas-resolution 1K|2K]
[--orientation 纵版|横版] [--extra-constraint]
```

- 必须给**已有角色图**；输出八方向静态表；左右通常镜像（不是物理一致的转身）。
- 对 Little World：动画前的方向参考有用，但不能直接当动画帧。
- 计费：Nano Banana 统一定价，**需服务端确认**。

### 2.8 `animation-edit-run` 与 `meowa-animation-run`

用户列表里写的是 `meowa-animation-run`，这两个不是同一件事，必须分开说：

```
animation-edit-run:  --animation-file（GIF/WebP）+ --prompt [--reference-image] [--mode pixel|hd]
meowa-animation-run: --image-file + --prompt [--last-image-file] [--style-mode pixel|hd]
                     [--resolution 480p|720p|1080p|1080p_full]
                     [--output-frames 8|16|24|32] [--animation-mode] [--optimize-prompt]
                     [--alpha-mode sharp|soft] [--remove-bg-method] [--background-color]
```

- `meowa-animation-run` 是 **V2 帧动画**路线（默认 16 帧 / 480P / pixel / sharp alpha），像素输入**长边不得超过 320px**。
- 官方明确：V1（`animate-run`）在大幅动作上失败率高；V2 更吃动作幅度，但常有需修剪的多余帧（animation-and-video.md:138-144）。
- 计费：480P 为基准价；720P +10；1080P Fast 30/35、Full 40/45（1–2 秒），抠图另计 `ceil(frames/batch)×5`。

---

## 3. 我此前的使用错误（按严重度）

| # | 错误 | 证据 | 后果 |
|---|---|---|---|
| 1 | **没查模板目录就用自定义尺寸** | SKILL.md 要求"模板依赖型能力必须先列目录让用户选"；`pixel-gen-template-info` 有 **58 个模板**，其中 `large_portrait` 正是 **px=256 / count=2 / character** | 放弃了一个天然"同画布双角色"的能力 |
| 2 | **每个角色独立从零生成** | 男方 3 次、qianqian 2 次，全部独立调用，无共享参考 | 调色板/明暗每次重推，是风格漂移的根因 |
| 3 | **长关键词堆叠 + 否定式提示** | 我用了"exactly one character, no second figure, no companion, no props…" | SKILL.md 明令禁止（会干扰模型自解释、降低一致性） |
| 4 | **没用 `--strong-pixelation` 配参考图** | 有参考图的场景一次都没加该参数 | 丢掉了"强制重绘而非照抄"的唯一开关 |
| 5 | **没用编辑类能力** | `image-edit-run --strict`、`one-click-upgrade-run`、`asset-clone-run` 零次调用 | 只能用"再生一张"来改，成本与漂移都翻倍 |
| 6 | **把像素生成当动画资源生产线** | 直接拿单张静帧当"可动画基础" | 头身比/锚点/描边不统一时，后续动画必然崩 |

---

## 4. 画风不一致的量化诊断（免费，本地测算）

脚本：`tools/style_metrics.py`；数据：`reports/audit/style_metrics.json`；对比图：`reports/audit/style_side_by_side.png`。

| 资产 | 可见尺寸 | 锚点(中心x, 底y) | 像素网格 行/列 | 描边 | 头身比 | 颜色数 | 暗部占比 | 平均饱和 | 平均亮度 |
|---|---|---|---|---|---|---|---|---|---|
| **qianqian 原版** | 98×198 | (126,225) | 0.97 / 1.01 | 2.0px | **2.5 头** | **716** | 0.50 | 0.18 | **132** |
| male_B_gentle | 106×209 | (128,226) | 0.92 / **1.16** | 2.0px | **2.1 头** | 64 | **0.60** | 0.23 | **111** |
| male_C_playful | 96×188 | (128,226) | 1.04 / 1.02 | **1.0px** | 2.1 头 | 64 | 0.58 | 0.25 | 117 |
| qianqian_v2_a | 108×218 | (128,226) | 0.96 / 1.06 | 2.0px | 2.5 头 | 64 | 0.50 | 0.21 | 132 |
| qianqian_v2_b | 117×227 | (128,226) | 0.99 / 0.97 | 2.0px | 2.5 头 | 64 | 0.49 | 0.17 | 134 |

**读出来的四件事：**

1. **头身比差 0.4 头**：qianqian 2.5 头，男方 2.1 头 —— 男方的头相对更大、四肢更短，这就是"看着不像同一套资产"最直观的原因。qianqian 重制版保住了 2.5 头，男方没有。
2. **男方整体偏暗 16%**（亮度 111 vs 132），**暗部占比高 20%**（0.60 vs 0.50）。主因是黑色发量 + 更重的阴影，读起来更"实"，而 qianqian 是轻薄的粉白调。
3. **列方向网格比 1.16**（原版 1.01）：男方的横向存在连续渐变，缺少 qianqian 那种"色块化"的硬边明暗。
4. **颜色数 64 vs 716**：服务端把结果量化到 64 色，而原图保留了抗锯齿与细腻过渡。这意味着**任何"重出一张"都不可能像素级复刻原版**，只能做到"同一调色板家族 + 同一色块逻辑"。

**机制结论**：`custom-size-pixel-gen-run` = 画一张软插画 → 量化。两次独立调用会得到两套独立的调色板与明暗分布；再加上 2.1 头 vs 2.5 头的比例漂移，就形成了"各自好看、放一起不像一对"。

---

## 5. 三个候选工作流（本轮不执行）

### A. 用 qianqian 原图作参考，直接生成男方

```bash
python meowart_api.py custom-size-pixel-gen-run \
  --prompt "圆润可爱的像素男孩，黑色蓬松短发与厚刘海，白色短袖，深色短裤，表情自然亲切" \
  --width 256 --height 256 \
  --reference-image refs/DesktopPet/assets/sprites/idle/idle_01.png \
  --strong-pixelation \
  --generation-model nano-banana \
  --content-mode portrait --output-dir characters/male_v3
```

- 素材：qianqian 原图 1 张（无需自拍）。
- 一致性能力：**中**。参考图给的是"风格气质"，不是硬约束；`--strong-pixelation` 只是让重绘更狠。
- 尺寸/透明/网格控制：中（±10–20px 漂移，需后期对齐）。
- 风险：文档已警告"参考图引导更不稳"，且仍是从零画。
- 单次花费：约 **20 积分**。

### B. 先生成，再用编辑向 qianqian 靠

```bash
# 第 1 步：拿现成 male_B_gentle 当底
python meowart_api.py image-edit-run \
  --reference-image characters/normalized/male_B_gentle.png \
  --prompt "保持人物身份与姿势不变，把配色收敛到参考的奶油白与浅粉体系，明暗改为两块硬边色块，描边统一为 2px 深灰" \
  --mode pixel --strict --generation-model nano-banana --resolution 1K \
  --output-dir characters/male_v3_edit
```

- 素材：现有 male_B + （可选）qianqian 原图作为第二参考。
- 一致性能力：**强**——在原图上改，身份与网格自动保留。
- 尺寸：完全沿用 256×256。
- 风险：`--strict` 下改动幅度有限；想把 2.1 头改成 2.5 头**做不到**（那是重画）。
- 单次花费：服务端报价，**未验证**（文档提到区域像素化 +2 积分，普通编辑按模型计）。

### C. 同画布/同风格克隆（**推荐**）

**C1 首选：模板双槽位**

```bash
python meowart_api.py pixel-gen-run \
  --template-name large_portrait \
  --requirement "左边是我方男主：黑色蓬松短发、白T、深色短裤、自然亲切；右边是 qianqian：黑色侧刘海与侧辫、粉白水手裙、大蝴蝶结、手持小玫瑰。两人同一像素画风、同一头身比、同一描边粗细" \
  --reference-file refs/DesktopPet/assets/sprites/idle/idle_01.png \
  --remove-bg-method standard \
  --output-dir characters/duo_preset
```

- 模板事实：`large_portrait` = px 256、**default_count 2**、输出 128–256、2K、labels `pixel,character,portrait`。
- 一致性能力：**最强**——一次调用、一张画布、两个槽位，比例与配色被同一模板约束。
- 风险：模板风格是否贴合"Rose Sailor"需先看模板预览；两人会被同一模型同时绘制，可能互相污染细节。
- 花费：模板定价，**需服务端确认**。

**C2 备选：克隆成组**

```bash
python meowart_api.py asset-clone-run \
  --reference-image characters/normalized/qianqian_v2_a.png \
  --clone-count 1 \
  --prompt "参考图 1 的画风与像素密度，生成 1 个同风格的男孩角色：黑色蓬松短发、白T、深色短裤，保持同样的头身比、描边粗细与色块明暗" \
  --mode pixel --generation-model nano-banana \
  --cell-width 256 --cell-height 256 --fit-mode pad \
  --output-dir characters/male_clone
```

- 一致性能力：强（专为"同风格资产组"设计）；`--fit-mode pad` 保证不缩放。
- 风险：单元格是参考网格，不是最终锚点；仍需后期对齐脚底。

### 排序

1. **C1（large_portrait 双槽位）** — 唯一能在**同一次生成**里锁死两人比例与配色的路线。
2. **B（image-edit --strict 收敛）** — 最稳的"改造现有"路径，适合先把 male_B 拉到同一色调再谈比例。
3. **A（参考图重生）** — 风险最高、最不可控，只适合在没有其他手段时兜底。
4. C2 作为 C1 失败时的同风格备选。

---

## 6. 账目核对：140 积分 vs jobs.jsonl 的 0

**结论：没有重复扣费，也没有少记费；是本地记账脚本的解析缺陷。**

- `logs/jobs.jsonl` 共 15 条。Batch 1–2 的 8 条有真实积分（10/10/10/12/14/20/20/20）；Batch 3 的 7 条（M1、M2A、M2B、M2C、Q1、Q2、M2A2）`credits` 全为 `0`。
- 真实扣费以余额为准：Batch 3 前 **2904**，Batch 3 后 **2764**，差值 **140 = 7 × 20**，与报告一致。
- 根因：`tools/collect.ps1` 用正则从日志里取 `"credit_cost": N` 写入 `credits`；`general-image-run` 会返回该字段，而 `custom-size-pixel-gen-run`（走 `one_click_pixelate` 工作流）**不返回**，`[int]$credits` 于是落成 0。
- 修复建议（未改代码，仅记录）：收集时若取不到 `credit_cost`，则用"调用前后余额差"写入 `credits`，并在 `notes` 标注来源。

**累计花费**：Batch1 56 + Batch2 60 + Batch3 140 + 房间 10 = **266**；余额 3020 → **2754**。

---

## 7. 免费提升现有角色的可行性（不花钱）

可以，且建议先做，再决定是否付费重出：

1. **调色板重映射**：把 male_B 的 64 色按亮度/饱和度映射到 qianqian_v2_a 的 64 色，直接拉平"偏暗 16%"的问题。
2. **描边统一**：male_C 的 1.0px 描边 → 2.0px，与其余资产一致。
3. **高度归一**：209 → 198（脚底 226 不变，顶部裁 11px 或整图位移），消除并排时的身高差。
4. **色阶两块化**：对渐变区域做 posterize（4–6 级），逼近 qianqian 的硬边色块。
5. **做不到的**：头身比 2.1 → 2.5（需要重画头部/身体），这只能靠生成解决。

---

## 8. 下次生成的建议（等待授权）

| 项 | 建议 |
|---|---|
| 能力 | **C1 `pixel-gen-run --template-name large_portrait`**（双槽位，一次出两人） |
| 模型 | 模板内置（2K 档），不额外指定 |
| 尺寸 | 模板原生 256px 输出，后期只做锚点对齐，不缩放 |
| 参考图 | qianqian 原版 `idle_01.png`（1 张）；**不传自拍** |
| 提示词 | 短句描述，不用否定式堆叠，只写"谁、穿什么、什么表情、同一画风" |
| 备选 | 若模板风格不合 → `asset-clone-run`（C2） |
| 预算 | **40–60 积分**（模板单价未验证；按 20/张估 2–3 次尝试） |

**本轮不执行任何生成。**

---

## 9. 未验证 / 需服务端确认

- 模板类命令（`pixel-gen-run`、`asset-clone-run`、`one-click-upgrade-run`、`image-edit-run`）的**精确单价**：本地无定价表，`--help` 不含价格，文档只给相对说明（如 720P +10、区域像素化 +2）。
- `large_portrait` 模板的**实际风格预览**：`pixel-gen-template-info` 只给元数据，不含样图；需在网页端或首次生成后确认它是否贴合"Rose Sailor"。
- `meowa-animation-run` 480P 的基准生成价：文档只写了 720P/1080P 的加价与抠图费用。
- 任何"参考图 + strong-pixelation 能稳定复刻 qianqian 配色"的说法：**未验证**，本轮禁止生成。

---

## 10. 附件

- `reports/audit/style_metrics.json` — 五个角色的完整测算
- `reports/audit/style_side_by_side.png` — 并排对比（含数值标注）
- `reports/audit/*.help.txt` — 八个命令的原始 `--help`
- `reports/audit/pixel-templates.txt` — 58 个像素模板目录
- `tools/style_metrics.py` — 测算脚本（可复跑）
