# BATCH 2 预算：男方像素角色（静态概念）

> 状态：**待批准**。未批准前不提交任何收费任务。
> 余额查询：2026-10-25 12:50，`total_credits = 2964`（paid 2110 / 订阅 600 / 试用 254）。
> 上一批（绘本风）已花费 56，本次另计。

## 1. 计价依据

`custom-size-pixel-gen-run` 默认 Nano Banana（官方文档：10/20/40 积分 @1K/2K/4K，每张参考图 +2）。
256×256 属于 1K 档，**文本生成 10 积分/张**；若带 1 张参考图则 12。

官方在 `references/pixel-and-hd-assets.md` 的建议：

- custom-size **优先纯文本**生成，参考图会让结果不稳定；
- 迭代阶段**先不要买抠图**，定稿后再单独跑 `remove-background-run`；
- 我另外可以用 Pillow 本地抠纯色背景，**0 积分**。

## 2. 本批内容

| # | 素材 | 命令 | 参数 | 单价 | 数量 | 小计 |
|---|---|---|---|---|---|---|
| 1 | 男方正面像素图 | custom-size-pixel-gen-run | 256×256 / 文本 / 不抠图 | 10 | 2（含 1 次重试） | 20 |
| 2 | 男方 3/4 视角 | 同上 | 256×256 / 文本 / 不抠图 | 10 | 2 | 20 |
| 3 | 重试预留 | 同上 | — | 10 | 1 | 10 |
| | | | | | **合计** | **50** |

上限 **60**，本方案 50。

不生成：动画、精灵图、四方向行走（第二阶段，角色定稿后另批）。
不生成：qianqian 的任何替代品（直接复用现有素材，0 积分）。

## 3. 免费的部分（本地完成，不扣费）

- 本地抠背景（纯色 → 透明），Pillow 洪泛填充，结果写入 `characters/boy/`。
- 与 qianqian 并排对比图（256² 原尺寸 + 2× 放大两版）。
- 双人场景静态预览（用上一批的房间或纯色底，按契约锚点摆放）。
- 像素尺度校验：对比两人的可见像素块大小、描边颜色、颜色数量。

## 4. 真人照片的使用

- 照片只存在 `D:\LittleWorld-Meowa\private\photo\`，**不上传 GitHub**。
- 默认方案：**不上传照片到 Meowa**，只用文字描述特征（发型、眼神、白 T、体型）。
- 若你对相似度不满意，可以授权我把它作为 `--reference-image` 使用（+2 积分/张），但意味着照片会发送到 Meowa 服务器，且官方提示参考图会降低 custom-size 的稳定性。

## 5. 提示词（正面）

```
single pixel-art chibi boy character, full body, front view, standing straight,
arms relaxed, short slightly chubby body, 2.4 head proportions, black fluffy
medium-length hair with a thick messy fringe falling over the forehead and partly
covering the eyebrows, dark brown calm sleepy eyes, neutral relaxed expression,
plain loose white crew-neck t-shirt, dark slate grey shorts, small feet,
warm dark grey outline (#393834), soft warm palette, crisp 1-pixel edges,
no anti-aliasing, no gradient shading, flat plain light background,
no text, no watermark
```

3/4 视角：同上，把 `front view` 换成 `three-quarter view facing slightly left`，
并把 `arms relaxed` 换成 `right hand slightly raised`。

## 6. 失败处理

- 失败先 `custom-size-pixel-gen-poll --job-id` 恢复，不重复提交。
- 同一提示词连续 2 次不达标 → 改提示词再来，计入重试额度。
- 每次扣费后写 `logs/jobs.jsonl` 并报余额。
