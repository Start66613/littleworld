# 内容文件格式（LWCX）

V4.1 引入的内容文件是**唯一**能让使用者在不重新编译固件的前提下，把自己的文字放到设备上的通道。
它存在 TF 卡上，路径固定为：

```
<卡根>/littleworld/content.lwcx
```

固件在启动时读一次、校验一次，只有整份文件解析通过才会替换掉内置的示例回忆与纪念日；
任何一处错误都让设备保持原样（内置示例表 + 已保存的纪念日），因为**写错内容不该让设备起不来**。

- 解析器：`firmware/components/lw_content/src/lw_content.c`
- 头文件与上限：`firmware/components/lw_content/include/lw_content.h`
- PC 端打包/校验工具：`tools/package_v4_content.py`
- 板端只读查看：串口控制台 `content` 命令

---

## 1. 最小可用文件

```
LWCX0001
schema=1
content_version=1
anniversary=2024-10-25
welcome=今天也想你。
```

`LWCX0001` 必须是第一行（UTF-8 BOM 和 CRLF 都容忍，`#` 开头的行是注释）。

## 2. 四个段落

每个段落用 `[名字]` 打开、`[/名字]` 关闭，**不能嵌套**，键值对写在中间，一行一个 `键=值`。

### `[memory]` 回忆

```
[memory]
date=2024-10-25
title=第一次一起吃饭
body=那家店很吵，但我们聊到打烊。
[/memory]
```

`date` / `title` / `body` 三个键都必填。日期必须是 `YYYY-MM-DD` 且真实存在
（`2025-02-30` 会被拒绝，`1969-12-31` 也会，因为设备时间从 1970 起算）。

### `[album]` 相册

```
[album]
title=海边
photo=photos/001.lwph
[/album]
```

`photo` 是**相对卡根的路径**：不能是绝对路径，不能含 `..`，不能用 FAT 非法字符，
后缀固定 `.lwph`（写入 `.lwcx` 时工具会自动把 `.png`/`.jpg` 改成 `.lwph`，并在报告里说明）。

### `[letter]` 信件

```
[letter]
title=给未来的你
body=这封信还没写完。
[/letter]
```

### `[dialogue]` 对话

```
[dialogue]
id=10
line=男方|照片盒里有一沓空白的相纸。
line=qianqian|先把它们放进第一页吧。
[/dialogue]
```

`id` 是 0..999 的整数，全文件不能重复；`line` 至少一条，格式是 `说话人|台词`，
说话人为空会被拒绝。

## 3. 上限

| 项目 | 上限 |
| --- | --- |
| 文件总字节 | 32,768 |
| 回忆 | 32 条 |
| 照片 | 64 条 |
| 信件 | 8 封 |
| 对话 | 16 段 |
| 对话行（全文件合计） | 64 行 |
| 标题 | 128 字节 |
| 正文 | 512 字节 |
| 路径 | 96 字节 |
| 说话人 | 16 字节 |

长度按 **UTF-8 字节**算，不是字符数：一个汉字占 3 字节，所以「标题 128 字节」实际约 42 个汉字。

## 4. 转义

值里可以写 `\n` 表示换行、`\\` 表示反斜杠。其他反斜杠组合按字面保留。

## 5. 编码

必须是 UTF-8。UTF-16（Windows 记事本的「Unicode」选项）、带 NUL 字节、以及非法 UTF-8 序列
（超长编码、代理区、超出 U+10FFFF、截断的多字节序列）都会被拒绝，并且报出出错行号。

## 6. 用工具生成与校验

```powershell
# 写一份带注释的模板
python tools/package_v4_content.py --init content

# 只校验，不写文件（退出码 0 通过 / 1 校验失败 / 2 用法错误）
python tools/package_v4_content.py --check

# 生成设备用的单文件（默认写到 dist/littleworld-v4-sd/littleworld/content.lwcx）
python tools/package_v4_content.py

# 机器可读结果
python tools/package_v4_content.py --check --json
```

`--source` 可以是一个目录（目录里所有 `.lwcx` 按文件名顺序合并）或单个文件。
工具会在写盘前把渲染结果**回灌给解析器**再跑一遍，所以「工具说通过」和「固件能读」是同一套规则。

自测：`tools/test_v4_content.ps1`（31 项，含 23 个必须被拒绝的反例）。

## 7. 板端确认

启动日志会有一行 `content: ...`：

- `content: no card (<根路径>)` —— 没插卡或卡没挂上，用的是内置示例。
- `content: <错误描述> (line N)` —— 文件有问题，第 N 行，设备保持原样。
- `content: v1, 3 memories, 2 photos, 1 letter, 2 dialogues` —— 读成功。

串口控制台 `content` 命令会重复这些信息，并额外打印状态、版本、计数、字节数和来源路径。
这个命令只读，不写卡、不写 NVS。

## 8. 隐私

`content/` 目录里提交的只是**示例**（每条都以「示例」开头），真实回忆请放在自己的目录里，
不要把私人内容提交到仓库。`.gitignore` 已经忽略了 `content/private/`。
