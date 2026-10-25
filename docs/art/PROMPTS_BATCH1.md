# BATCH 1 提示词（待批准后直接执行）

所有提示词共用同一段 style block，保证风格统一：

```
STYLE: hand-drawn storybook illustration for a cozy couple's gift app; soft watercolor
and colored-pencil texture with light paper grain; warm pastel palette of cream
#FFFAF4, sakura pink #F58FAE, lavender #B79CED and mint #7FD3B4; soft light from the
upper left; rounded, huggable shapes; gentle darker-tone outlines (never pure black);
clean silhouette; no text, no letters, no watermark, no signature, no UI chrome.
```

负面词（放在提示词末尾）：`no text, no letters, no logo, no watermark, no signature,
no frame, no border, no drop shadow, no harsh black outline, not pixel art, not 3D render,
not photorealistic, no extra limbs, no duplicate body parts`

---

## A1 — 宠物概念 A：奶油团子兽（cream mochi creature）

```
STYLE ... A single original cute creature concept: a small round cream-colored
mochi-like animal, about one head tall, with a soft pear-shaped body, tiny stubby arms
and legs, two long droopy ears with pink inner shading, two large glossy dark-brown
eyes with soft highlights, a tiny smiling mouth, blush cheeks, and a small curled tail.
Standing upright, full body visible, three-quarter front view looking at the viewer.
Character fills 75% of the canvas height, centered, generous transparent margin.
Isolated on a plain transparent background.
```

## A2 — 宠物概念 B：苔藓精灵（moss sprite）

```
STYLE ... A single original cute creature concept: a soft blob-shaped moss sprite,
mint and sage green body with a lighter belly, tiny leaf sprout on top of the head,
two small rounded ears, big warm honey-colored eyes, short arms, no legs (floats just
above the ground), a faint blush and a gentle smile. Full body, three-quarter front
view, floating pose. Same proportions as a plush toy.
```

## A3 — 宠物概念 C：围巾小狐（scarf fox-cat）

```
STYLE ... A single original cute creature concept: a small fox-cat hybrid with cream
fur, peach-pink ear tips and tail tip, a fluffy tail, big dark eyes, a tiny nose, and a
soft lavender knitted scarf around the neck. Standing on four short legs, full body,
three-quarter front view, friendly expression.
```

## B — 宠物小房间（1024×600 场景）

```
STYLE ... A cozy tiny bedroom interior for a palm-sized creature, wide 16:9 landscape
composition, eye-level view with slight perspective. One warm window on the left with
soft afternoon light and sheer curtains, a small low bed with a pastel quilt on the
right, a round cream rug in the center-left, a tiny table lamp with warm glow, a low
bookshelf with a few books, a potted plant, and a small photo frame on the wall.
Leave a clear empty area about 40% of the width in the center-left where a small
character can stand, nothing blocking it. Cream walls, sakura pink and lavender
accents, dust motes in the light. No characters in the scene. No text.
```

## C — 首页氛围参考（2048×1152）

```
STYLE ... A key visual for a cozy desktop app made for a tiny creature companion:
a wide warm room in the background, the tiny creature standing in the center-left,
soft UI areas suggested ONLY as flat rounded blank panels on the right and along the
bottom (no icons, no labels, no text inside them). Cozy afternoon light, calm and
loving mood, lots of soft negative space. The creature must match the reference image
exactly in shape and color.
```

`C` 与 `B` 都建议带参考图：执行时用已选定的角色图作为 `--reference-image`（+2 积分/张）。

---

## 执行参数

| 素材 | 分辨率 | 比例 | 质量 | 抠图 |
|---|---|---|---|---|
| A1 / A2 / A3 | 2K | 1:1 | detailed | standard |
| B | 2K | 16:9 | detailed | none |
| C | 2K | 16:9 | detailed | none |

输出目录：`D:\LittleWorld-Meowa\concepts\`（A1–A3）、`rooms\`（B）、`concepts\`（C）。
