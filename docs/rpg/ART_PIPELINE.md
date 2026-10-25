# ART PIPELINE — Meowa 输入输出与运行时资源协议

版本 v0.1 · 2026-10-25 · DSH

## 1. 本机环境（本轮只读验证）

| 项 | 实测 |
|---|---|
| Skill 版本 | `2026.10.23.2`（`release.json` 校验一致） |
| CLI | `meowart_api.py 2026.10.23.2`，80+ 子命令 |
| Python | 隔离 venv `D:\LittleWorld-Meowa\tools\venv`（3.12.14，requests/Pillow/numpy） |
| 余额 | `total_credits = 2904`（paid 2110 / 订阅 600 / 试用 194，试用 2026-11-24 到期） |
| 认证 | 本机 `.env`，不入 Git、不打印、不回传 |
| 计费口径 | custom-size 256² = **20 积分/张**（按 2K 画布计），参考图 +2 |

本轮**未调用任何付费生成**。

## 2. 生成路径选择（按官方文档）

| 目标 | 命令 | 说明 |
|---|---|---|
| 256 原生像素角色 | `custom-size-pixel-gen-run --width 512 --height 512` → 本地 2× 最近邻降采样 | 先大后降，才能得到 1 px 网格 |
| 房间背景（像素） | `pixel-universal-gen-run` 或 `custom-size-pixel-gen-run 1024×600` | 不带角色、不带 UI、不带文字 |
| 场景氛围参考 | `general-image-run`（Image2.5 / 2K / 16:9） | 仅参考，不做运行时背景 |
| 表情差分 | 在合格正面图上局部重绘，或 `image-edit-run` | 避免整图重生成导致身份漂移 |

## 3. 后处理流水线（全部本地，0 积分）

```
原始输出 → 抠底(cut_bg.py，洪泛填充) → 尺寸/网格校验(parity test)
        → 对齐锚点(平移，不缩放) → 1×/2×/4× 最近邻预览
        → 与 qianqian 并排 sheet → QA 记录 → manifest
```

校验脚本：`tools/make_variants.py`（本地改脸）、`tools/cut_bg.py`（抠底）。
网格校验：行/列相邻差分能量比，0.85–1.15 视为原生 1 px（P1 实测 0.19 / 9.0 → 不合格）。

## 4. 运行时资源协议（给 Codex）

```json
{
  "schema": "littleworld.pixel-assets/1",
  "contract_version": "1.0.1",
  "asset_version": 1,
  "character_id": "male_pixel_v2",
  "canvas": [256, 256],
  "anchor": [128, 226],
  "animations": [{
    "id": "idle",
    "direction": "down",
    "loop": true,
    "frames": [{"path": "frames/000.png", "sha256": "…", "duration_ms": 125}]
  }],
  "memory": {"bytes_per_frame_rgb565a8": 196608, "frames_loaded": 3},
  "provenance": {"tool": "meowa custom-size-pixel-gen-run", "job_id": "…", "credits": 20}
}
```

- 每帧 256² RGB565+A8 = **196,608 B**；两人同屏 + 单帧换图峰值 ≈ 590 KB，需与 OS 预算一起核。
- 不得一次性把整套动画解码进内存；只加载当前动作+方向。
- 路径一律相对 Meowa 根，不用随机任务目录名。

## 5. 下一批报价（**待用户批准，未批准不执行**）

| # | 内容 | 数量 | 单价 | 小计 |
|---|---|---|---|---|
| 1 | 男方 A/B/C 三方向（512→256 原生） | 3 | 20 | 60 |
| 2 | 新版 qianqian 正面概念（路线 B，2 选） | 2 | 20 | 40 |
| 3 | 小房间背景 1024×600（像素、无角色/UI/文字） | 1 | 20 | 20 |
| | **小计** | **6** | | **120** |
| 4 | 失败重试预留（最多 2 张） | 2 | 20 | 40 |
| | **硬上限** | | | **160** |

- 只有第 1 项通过验收（网格/高度/头身三项）后，才会用到第 4 项重试。
- 若用户选择路线 A（只做局部修图，不重制 qianqian），总额降为 **80 + 40 重试 = 120**。
- 每次扣费后写 `logs/jobs.jsonl`（job_id / 类型 / 提示词 / 积分 / 路径 / 状态 / 时间）。

## 6. 素材去向

| 目录 | 内容 | 是否进公开仓库 |
|---|---|---|
| `characters/`、`previews/` | 候选图、对比图 | 经用户确认后进 `art-assets` |
| `private/photo/` | 真人照片 | **永不** |
| `handoff/pixel/` | manifest + 报告 | 是（不含照片/密钥） |
| 原 DesktopPet 素材 | 只读引用 | 已在 Batch 2 由用户授权公开 |
