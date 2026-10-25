# SAVE CONTRACT — RPG 存档

版本 v0.1 · 2026-10-25 · DSH · 对应实现 `rpg/src/lw_rpg_save.c`

## 1. 设计原则

1. **独立于 OS 的 NVS**：RPG 存档由 RPG 模块自己编码成 40 字节 blob，由宿主决定写到哪（独立键 / 独立分区）。
2. **可校验**：magic + version + CRC-32，损坏就拒绝加载并回到默认状态，不半加载。
3. **可迁移**：version 字段保留；未来加字段只追加，不改变已有偏移。
4. **不覆盖 OS 数据**：建议 key = `lw_rpg_save`，与 OS 的 `littleworld` 键分开。

## 2. 二进制布局（40 字节，小端）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | magic | `0x5052574C`（"LWRP"） |
| 4 | 2 | version | 当前 1 |
| 6 | 1 | player.x | 0–15 |
| 7 | 1 | player.y | 0–7 |
| 8 | 1 | partner.x | 0–15 |
| 9 | 1 | partner.y | 0–7 |
| 10 | 2 | collected_mask | 位 0..7 对应 item 1..8 |
| 12 | 1 | quest_complete | 0/1 |
| 13 | 4 | unlocked_reward | 0 或 reward_id |
| 17 | 19 | reserved | 置 0，未来扩展 |
| 36 | 4 | crc32 | 覆盖 0..35 |

## 3. 解码校验顺序

1. 长度必须正好 40 → 否则 `-1`
2. magic → `-2`
3. version → `-3`
4. CRC → `-4`
5. 出生点坐标必须落在可走格 → `-5`
6. 校验通过后才写入状态，并清空移动路径/对话状态

任何一步失败，状态保持调用前的内容（调用方应自行决定是否重置）。

## 4. 持久化时机

| 时机 | 建议 |
|---|---|
| 任务完成 | 立即写 |
| 退出 RPG | 写 |
| 每 60 s | 兜底写（与 OS 的节流策略一致） |
| 对话中 | 不写（避免写半状态） |

## 5. 与 OS 的关系

- RPG 只提供 `lw_rpg_save_encode/decode`，不直接调用 `nvs_set_blob`。
- 宿主（Codex）负责：读写、失败重试、断电恢复策略、以及"是否允许 RPG 使用独立分区"。
- 如果 OS 决定统一存储，只要把 `lw_rpg_save` 作为子键存放即可，格式不变。

## 6. 测试（已实现）

`tests/host/test_rpg.c::test_save_roundtrip` 覆盖：

- 编码 40 字节 → 解码 → 坐标/收集位/进度一致
- 翻转任意字节 → CRC 失败
- 长度错误 / magic 错误 / version 错误 → 分别返回 -1/-2/-3
- 坐标越界（即使 CRC 正确）→ -5
- `lw_rpg_reset` 后回到出生点、清空进度
