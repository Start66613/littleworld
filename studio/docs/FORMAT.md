# Little World 内容包格式（littleworld-content/1）

本文描述 Studio 产出的 `.lwpack` / `.lwstudio` 容器。两者是**同一个格式**，只有用途不同：
`.lwstudio` 是项目文件，`.lwpack` 是导出的内容包，二者都可以被 Studio 重新导入。

> 当前固件**没有**这个格式的读取器。这里的 `compatibility` 字段只是自我声明，
> 用来防止旧版本误读新包，不代表设备已经能解析。

## 1. 容器

- ZIP（`application/zip`），UTF-8 文件名，不加密。
- Studio 导出时使用 **store（不压缩）**；导入时同时接受 store 与 deflate。
- 中央目录必须在文件尾部 64 KiB 内，拒绝 ZIP64、分卷、重复文件名、路径穿越。
- 条目总数 7 ~ 87（7 个固定 JSON + 最多 80 个媒体文件）。

固定 7 个 JSON：

```
manifest.json  anniversary.json  letters.json  memories.json
album.json     story.json        resources.json
```

媒体文件路径形如：

```
assets/<照片 uuid>/original.png      # 或 original.jpg / original.webp
assets/<照片 uuid>/preview.png       # 永远 PNG，最长边 1024×600
```

## 2. manifest.json

除下面 6 个分节外的全部顶层字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `schema` | `"littleworld-content/1"` | 版本标识，必须完全一致 |
| `id` | UUID | 项目 ID；导入时会被重新生成 |
| `name` | string ≤80 | 项目名，不能为空 |
| `demo` | boolean | 是否为演示项目 |
| `createdAt` / `updatedAt` | ISO 8601 | 保存时刷新 |
| `theme` | `sakura` / `moonlight` / `sage` | 预览主题 |
| `compatibility` | `"studio-only; firmware reader not implemented"` | 固定字符串，不匹配即拒绝 |

## 3. 各分节

所有对象都是 **strict** 的：出现未定义字段会直接判为损坏。

### anniversary.json（≤32 条）

```jsonc
{ "id": "<uuid>", "title": "≤80 字", "date": "YYYY-MM-DD", "description": "≤2000 字" }
```

### letters.json（≤100 条）

```jsonc
{
  "id": "<uuid>",
  "type": "today | wish | letter",
  "date": "YYYY-MM-DD",
  "unlockDate": "YYYY-MM-DD | null",   // 解锁日期，仅 Studio 模拟
  "title": "≤80 字",
  "subtitle": "≤160 字",
  "body": "≤20000 字"
}
```

### memories.json（≤300 条）

```jsonc
{
  "id": "<uuid>",
  "date": "YYYY-MM-DD",
  "title": "≤80 字",
  "body": "≤10000 字",
  "photoId": "<uuid> | null",
  "tags": ["≤24 字", "..."],   // 最多 12 个
  "visible": true              // 是否公开到设备
}
```

### album.json

```jsonc
{
  "coverId": "<uuid> | null",
  "photos": [
    {
      "id": "<uuid>",
      "title": "≤80 字",
      "original": "assets/<id>/original.png",
      "preview":  "assets/<id>/preview.png",
      "width": 640, "height": 420,           // 原图，1–20000
      "previewWidth": 640, "previewHeight": 420,  // ≤1024 × ≤600
      "deviceStatus": "pending",             // 固定值：尚未在设备上验证
      "estimatedRGB565Bytes": 537600         // 必须等于 previewWidth*previewHeight*2
    }
  ]
}
```

### story.json

```jsonc
{
  "quest": { "title": "≤80 字", "objective": "≤500 字", "reward": "≤80 字" },
  "chapters": [                       // ≤8 章
    {
      "id": 1,                        // 1–8
      "title": "≤80 字",
      "scene": "≤80 字",
      "event": "≤500 字",             // 剧情策划说明，不是可执行热点
      "lines": [                      // 1–8 句
        { "speaker": "≤40 字", "text": "≤1000 字" }
      ],
      "choices": [                    // ≤2 个
        { "text": "≤80 字", "nextId": 2 }   // 0 表示结束
      ]
    }
  ]
}
```

### resources.json（≤80 条）

```jsonc
{
  "path": "assets/<uuid>/original.png",
  "role": "original | preview",
  "mime": "image/png | image/jpeg | image/webp",
  "bytes": 21418,                     // ≤10 MiB
  "sha256": "<64 位小写十六进制>"
}
```

## 4. 交叉校验

导入时除逐字段校验外还会检查：

1. 各列表内部 `id` 不重复；`resources` 的 `path` 不重复。
2. `coverId` 必须存在于 `album.photos`；`memories[].photoId` 必须存在。
3. 照片的 `original` / `preview` 路径必须与自身 `id` 严格对应，
   扩展名必须与 `mime` 一致，`preview` 必须是 PNG。
4. 每个照片的两条资源都必须在 `resources` 中，且 `role` 正确；
   `resources` 里不允许存在未被引用的文件（数量必须恰好相等）。
5. 媒体总字节 ≤ 40 MiB；单文件 ≤ 10 MiB；JSON 合计 ≤ 4 MiB；整包 ≤ 64 MiB。
6. 图片按**文件头**判定真实格式，与声明不符即拒绝；不接受 SVG。
7. `choices[].nextId` 必须指向存在的章节，或为 0（结束）。
8. 文字不允许 C0 控制字符（`\t` `\n` `\r` 之外的 0x00–0x1F 与 0x7F）。
9. 日期必须是 1970–2200 内的真实公历日期，`2023-02-29`、`2026-4-1` 都会被拒绝。

任何一条不满足都会给出中文错误提示，导入失败不会留下半个项目。

## 5. 与固件的兼容边界

`shared/compatibility.ts` 只做**文本适配**检查，用于 `--emit-c`：

- 公开（`visible: true`）回忆 ≤ 32 条 —— 对应 `lw_memories_set_source()` 上限
- 每条标题 ≤ 128 UTF-8 字节、正文 ≤ 4096 UTF-8 字节

满足后 `npm run validate -- file.lwpack --emit-c out.h` 会生成：

- `lw_memory_t studio_memories[]`（照片路径故意留空）
- `lw_rpg_dialogue_t studio_dialogues[]`，每章一组 `lw_rpg_line_t` / `lw_rpg_choice_t`

**尚未适配**：信件解锁规则、图片加载与 RGB565 转换、房间热点与任务配置、章节 `scene` /
`event` 的运行时含义。这些字段目前只是元数据。

## 6. 版本演进

`schema` 是硬开关：读到自己不认识的版本会直接拒绝，不做猜测式降级。
新增字段必须同时提升版本号，并保留旧版读取路径；由于对象是 strict 的，
旧版读新版会明确失败，而不是悄悄丢掉字段。
