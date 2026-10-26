# V4.1 内容管理格式（任务 7）交付说明

分支：`agent/dsh-v4-1-backend-sprint`
日期：2026-10-27
状态：**已编译、已在宿主端测试；板端未验证**（本轮没有可用串口）

---

## 1. 这一轮到底做了什么

V4.2 的核心诉求是「内容可以换，固件不用重编」。要让这句话成立，先得有一个**格式**：
一份使用者能编辑、固件能严格校验、错了还不会把设备搞挂的文本文件。

任务 7 做的就是这件事，但它刻意**没有**做 UI：

| 做了 | 没做（留给 V4.2 的 UI 侧） |
| --- | --- |
| 内容文件格式与解析器 | 回忆页的编辑界面 |
| 严格校验与错误定位 | 相册的实际图片解码与显示 |
| 从 TF 卡启动时读取 | 内容变更后的热重载 |
| 纪念日覆盖 | 信件/对话的播放逻辑 |
| PC 端打包/校验工具 | 手机或 PC 的图形化编辑器 |

换句话说：**管道通了，水龙头还没装**。这一轮保证「把文件放对位置，设备就能读到」，
并且保证「文件写错了，设备照常启动」。

---

## 2. 为什么不是 JSON

考虑过三种格式：

| 方案 | 为什么不选 |
| --- | --- |
| JSON | 32 KB 上限内要一个真正的解析器（转义、嵌套、数字格式），出错信息还只能给字节偏移；写错一个逗号整份文件报废，使用者看不出是哪一行 |
| 二进制 | 使用者无法用记事本编辑，和「内容可以换」的目标直接冲突 |
| **行式文本（选用）** | 一行一个 `键=值`，出错能精确到行号；`#` 注释能写说明；UTF-8 直接可读；解析器不需要递归，栈深可控 |

选行式文本的代价是没有嵌套结构，所以用 `[section]` / `[/section]` 显式配对，
并且**不允许嵌套**——嵌套会让「这一行属于谁」变得需要缩进敏感，而行号报错的价值就在于不模糊。

---

## 3. 实现

### 3.1 新组件 `lw_content`

```
firmware/components/lw_content/
├── CMakeLists.txt
├── include/lw_content.h     5,307 B
└── src/lw_content.c        20,971 B
```

- **零堆分配**：整份文件解析进一个静态 arena（`LW_CONTENT_MAX_BYTES` = 32,768 B），
  所有字符串都是 arena 内的切片。这样解析失败不会留下半份堆内存，
  也不需要为「最大可能内容」预留堆。
- **严格 UTF-8**：拒绝超长编码（overlong）、代理区（U+D800..U+DFFF）、超出 U+10FFFF、
  以及截断的多字节序列。UTF-16 存盘的文件会在第一个 NUL 或非法序列处被拒绝。
- **容忍**：UTF-8 BOM、CRLF、`#` 注释行、空行。
- **失败即清空**：解析失败会 `lw_content_clear()`，绝不留下「一半新一半旧」的状态。
  这一点是刻意的：混搭的内容比没有内容更难排查。
- **转义**：`\n` 换行、`\\` 反斜杠，其他组合按字面保留。

### 3.2 上限（全部按 UTF-8 字节计）

| 项目 | 值 | 项目 | 值 |
| --- | --- | --- | --- |
| 文件 | 32,768 B | 对话 | 16 段 |
| 回忆 | 32 条 | 对话行（合计） | 64 行 |
| 照片 | 64 条 | 标题 | 128 B |
| 信件 | 8 封 | 正文 | 512 B |
| 路径 | 96 B | 说话人 | 16 B |

按字节而不是字符，是因为最终要落在 32 KB 的文件上限和卡上的实际字节里；
一个汉字 3 字节，所以「标题 128 B」实际约 42 个汉字。这个换算写进了文档，
否则使用者会以为是 128 个汉字。

### 3.3 启动接线

`firmware/main/lw_content_boot.c`（4,257 B）+ `lw_content_boot.h`（1,009 B）：

```
lw_art_boot()          <- 解析出 TF 卡根路径
      ↓
lw_content_boot()      <- 读 <根>/littleworld/content.lwcx，解析，只记录不应用
      ↓
lw_os_start()          <- 建桌面
      ↓
lw_content_boot_apply()<- 安装回忆数据源 + 覆盖纪念日
```

顺序不是随便排的：

1. **必须在 `lw_art_boot()` 之后**——卡根路径由它解析，早于它就没有路径可读。
2. **必须在 `lw_os_start()` 之前读**——读文件是 I/O，放在建 UI 之前，
   这样即使文件很大也不会卡住首帧。
3. **必须在 `lw_os_start()` 之后应用**——`lw_memories_set_source()`
   在回忆页已经存在时会拒绝（`lw_app_memories.c:33` 的 `if (s_page || ...) return false`），
   而回忆页是懒加载的，`lw_os_start()` 之后、用户点进去之前是它唯一「关闭」的时刻。

第 3 点是这一轮最容易踩的坑：如果按直觉在 `lw_os_start()` 之前应用，
`lw_memories_set_source()` 会成功（那时页面确实还没建），但页面建起来时用的是
`lw_memories_set_source()` 之前的内置表——数据源被后来的 `build_page()` 覆盖掉。
现在的顺序是唯一正确的那个。

### 3.4 纪念日走公开头文件

`firmware/components/lw_ui/include/lw_ui_content.h` 新增：

```c
bool lw_ui_set_anniversary(lw_date_t date);
lw_date_t lw_ui_anniversary(void);
```

为什么绕一层：`lw_app_state.h` 在 `firmware/components/lw_ui/src/` 里，
**不是**导出的 include 目录，`main` 无法包含它。直接改 `lw_os.c` 去暴露内部状态
会把 `main` 和 UI 内部结构绑死；加一个内容注入用的公开 API，
既让 `main` 不碰 UI 内部，也不动 `lw_os.c` 一行。

`lw_ui_set_anniversary()` 用 `lw_date_valid()` 校验，日期不变时返回 `false` 不写盘，
变了才赋值并 `lw_app_state_save()`。

### 3.5 板端只读查看

`firmware/main/lw_capture.c` 新增 `content` 控制台命令（`CONFIG_LW_DIAG_CONSOLE` 下）：

```
lw> content
content: v1, 3 memories, 2 photos, 1 letter, 2 dialogues
  status=ok version=1 memories=3 photos=2 letters=1 dialogues=2 lines=4 bytes=1596
  source=/sdcard/littleworld/content.lwcx
```

失败时给行号：

```
lw> content
content: date is not a real calendar day (line 12)
  status=bad-date version=0 memories=0 ... bytes=0
  rejected at line 12
```

这个命令只读：不写卡、不写 NVS、不改任何状态。

---

## 4. PC 端工具

`tools/package_v4_content.py`（25,090 B）：

```powershell
python tools/package_v4_content.py --init content      # 写模板
python tools/package_v4_content.py --check             # 只校验
python tools/package_v4_content.py                     # 生成设备文件
python tools/package_v4_content.py --check --json      # 机器可读
```

退出码：`0` 通过 / `1` 校验失败 / `2` 用法错误。

关键设计：工具在写盘前会把渲染结果**回灌给同一套校验规则**再跑一遍。
所以「工具说通过」和「固件能读」不是两套逻辑碰巧一致，而是同一套。
`--source` 支持目录（按文件名顺序合并多个 `.lwcx` 片段），
这样真实内容可以按主题拆成几个文件，而设备上仍是一个文件。

`tools/test_v4_content.ps1`（9,628 B，UTF-8 **带 BOM**）：31 项，
其中 23 项是必须被拒绝的反例，每项都匹配固件里那句具体的拒绝文本，
而不是笼统的「失败了」。

---

## 5. 验证

| 项目 | 结果 |
| --- | --- |
| `tests/host/test_lw_content.c` | **124 checks, 0 failures**（`CONTENT TESTS PASSED`） |
| `tools/test_v4_content.ps1` | **31 通过, 0 失败**（`CONTENT TOOL TESTS PASSED`） |
| 后端全量 `tools/run_backend_tests.ps1` | Store 29 / System 22 / net 216 / stress 2035 / diag 111 / recovery 150 / fault 157 / content 124，**0 失败** |
| Windows 回归 | System 22 / Duo 82 / RPG 85 / Storage 33（handles 58→58）/ OS V2 489，67 张截图，**WINDOWS REGRESSION OK** |
| `firmware/build/os/little_world.bin` | **2,350,272 B**，sha256 `a022fafd4d615c84b008a9311faed43add2dd243f943562494606c1d3e0cfe19`（+6,032 B） |
| 符号 | `lw_content_boot` / `lw_content_boot_apply` / `lw_content_load` / `lw_content_parse` / `lw_content_memories` / `lw_memories_set_source` / `lw_ui_set_anniversary` 全部链接进 os 镜像 |

分区余量：factory 6 M = 6,291,456 B，os 镜像占 37.4%，仍然宽松。

---

## 6. 板端未验证的部分（明确列出）

这一轮**没有**任何设备操作：没上电、没开串口、没刷机、没插卡、没改 NVS。

因此以下都只是「编译并链接成功」，不是「在板子上跑通」：

1. 真实 TF 卡上的 `content.lwcx` 能否被读到（卡挂载路径由 `lw_resource_mount` 决定，
   代码里用的是 `lw_resource_root()`，但实际卡是 FAT 还是 exFAT 未在板端确认）。
2. 回忆页是否真的显示文件里的内容（依赖 `lw_memories_set_source()` 的调用时机，
   逻辑上正确，但没有肉眼验证）。
3. 纪念日覆盖后首页天数是否变化。
4. `content` 命令的实际输出（串口没开过）。
5. 一份故意写错的文件是否真的让设备保持原样（宿主端测过解析器，
   但「设备照常启动」这条链路没有板端证据）。

**不能把上面任何一条说成已验证。**

---

## 7. 下一步（V4.2 UI 侧）

1. 回忆页读取 `lw_content_memories()` 而不是内置表（解析器已经给了数据）。
2. 相册页把 `lw_content_get()->photos[].path` 交给图片解码器。
3. 内容变更后的热重载（需要先解决「页面已建」与 `set_source` 的互斥）。
4. 板端验证清单见 `reports/backend-sprint/BOARD_ACCEPTANCE.md`。
