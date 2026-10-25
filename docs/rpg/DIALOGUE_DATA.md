# DIALOGUE DATA — 对话数据结构与示例内容

版本 v0.1 · 2026-10-25 · DSH · 对应实现 `rpg/include/lw_rpg.h` + `rpg/data/lw_rpg_home.c`

## 1. 数据结构

```c
typedef struct { const char *speaker; const char *text; } lw_rpg_line_t;
typedef struct { const char *text; uint32_t next_id; }  lw_rpg_choice_t;
typedef struct {
    uint32_t id;
    uint8_t  line_count;
    const lw_rpg_line_t *lines;
    uint8_t  choice_count;          /* 最后一行之后询问 */
    const lw_rpg_choice_t *choices; /* next_id = 0 表示结束 */
} lw_rpg_dialogue_t;
```

- 说话人直接用显示名（`男方` / `qianqian` / `旁白`），渲染层按名字取头像。
- 分支只在**最后一行之后**出现，选项 2–3 个，点击后跳 `next_id` 或结束。
- 条件与触发器本版用最简形式：对话结束 → 收集对应 `item_id` → 推进任务；复杂条件留待 M3。

## 2. 运行时状态

```c
uint8_t  dialogue_active;
uint32_t dialogue_id;
uint8_t  line_index;
uint8_t  choice_pending;
uint8_t  pending_item;   /* 对话结束时才真正收集，防止"没读就算完成" */
```

## 3. 现有对话（全部为示例文本）

| id | 热点 | 内容 |
|---|---|---|
| 10 | 照片盒 | 男方：「照片盒里有一叠空白的相纸。」→ qianqian：「（示例）先把它们放进第一页吧。」 |
| 11 | 信封桌 | 男方：「桌上有一封还没写地址的信。」→ qianqian：「（示例）留到最后一页再封口。」 |
| 12 | 小盆栽 | qianqian：「（示例）小盆栽今天也长高了一点点。」 |
| 13 | 分支示例 | 同一句话 + 两个选项：「放回照片盒」/「再摆一会儿」 |

**红线**：不得写入两人的真实日期、真实回忆、真实地点；所有文案保持"明显可替换"。

## 4. 文本规范

| 项 | 规范 |
|---|---|
| 字号 | 中文 18–22 px（1024×600 下 20 px 为默认） |
| 单行 | ≤ 24 个汉字；超出自动换行，最多 3 行 |
| 单条 | ≤ 72 个汉字，超出拆成两行对话 |
| 标点 | 全角，句末不加空格 |
| 人名 | 固定 2 个：`男方`、`qianqian` |
| 分支按钮 | ≤ 6 个汉字，触摸高度 ≥ 44 px |

## 5. 渲染建议（给 Codex）

- 对话框固定在底部 132 px（y 452–584），左侧头像 100×100，右侧文本区 ≥ 440 px 宽。
- 未读过的对话在任务本里标记一个小圆点；读过即消（本版未实现，M2 可选）。
- 打字机效果只在首次播放；点击可瞬间显示完整行，第二次点击才推进。
- 对话打开时屏蔽地图点击，关闭后恢复；不改变任何 OS 全局输入状态。

## 6. JSON 导出

```json
{
  "dialogues": [
    {"id": 10, "lines": [
      {"speaker": "男方", "text": "照片盒里有一叠空白的相纸。"},
      {"speaker": "qianqian", "text": "（示例）先把它们放进第一页吧。"}],
     "choices": []}
  ]
}
```

导出仅供工具链与本地化使用；固件里仍是编译期常量，避免运行时解析开销。
