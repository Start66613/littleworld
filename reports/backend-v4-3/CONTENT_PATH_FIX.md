# 内容路径修复（P0）

分支 `agent/dsh-v4-3-engine`，基线 `d168426`。

## 1. 缺陷

设备从 TF 卡读内容时拼出了一个**不存在的路径**：

```
/sdcard/littleworld/littleworld/content.lwcx
```

正确的路径是：

```
/sdcard/littleworld/content.lwcx
```

也就是说：**所有内容文件都加载不到**，而且失败信息是"文件不存在"，
看起来像卡没插好，而不是代码拼错。

## 2. 根因

`lw_sd_asset_root()` 返回的**已经**是 littleworld 目录：

```c
/* firmware/components/lw_storage/src/lw_sd.c:18 */
const char *lw_sd_asset_root(void) { return BSP_SD_MOUNT_POINT "/littleworld"; }
```

`CONFIG_BSP_SD_MOUNT_POINT = "/sdcard"`，所以返回值是 `/sdcard/littleworld`。
`lw_resource_mount.c:87` 把它设成资源根：

```c
lw_resource_set_root(lw_sd_asset_root());
```

而 `lw_content_boot.c:48` 又加了一层：

```c
snprintf(path, sizeof(path), "%s/%s/%s", root, "littleworld", "content.lwcx");
```

`root` 已含 `littleworld`，于是重复。

## 3. 修法：不是改字符串，是统一根路径 API

只把 `"littleworld"` 删掉能修好这一处，但同样的错误随时会在下一处重现。
所以本轮做的是：

### 3.1 新增 `lw_path_join()`（`firmware/components/lw_core`）

```c
/* include/lw_path.h */
bool lw_path_is_safe_relative(const char *relative);
bool lw_path_join(char *dst, size_t cap, const char *root, const char *relative);
```

规则（都有测试）：

| 输入 | 结果 |
| --- | --- |
| `root` 为 NULL 或空 | 拒绝，`dst` 写空串 |
| `root` 为裸 `/` 或 `\` | 拒绝 |
| `root` 带一个尾分隔符 | 容忍，不产生 `//` |
| `relative` 为空 | 拒绝 |
| `relative` 以 `/` 或 `\` 开头 | 拒绝（绝对路径） |
| `relative` 含盘符 `X:` | 拒绝 |
| `relative` 含任何反斜杠 | 拒绝 |
| `relative` 的某个组件恰为 `..` | 拒绝 |
| `relative` 名为 `notes..txt` | **允许**（不是路径穿越） |
| `relative` 为 `.` | 允许，原样保留 |
| 结果超长 | 硬失败，`dst` 写空串（不截断） |

### 3.2 唯一的内容路径构造点

```c
/* firmware/components/lw_content/src/lw_content.c */
bool lw_content_resolve_path(char *dst, size_t cap, const char *root)
{
    return lw_path_join(dst, cap, root, LW_CONTENT_FILE_NAME);
}
```

`LW_CONTENT_FILE_NAME` 定义为 `"content.lwcx"`。
`lw_content_boot.c` 不再自己拼字符串，改为调用它；失败时日志是
`content: path rejected (root too long?)`，而不是假装文件不存在。

### 3.3 统一到 `lw_resource_path()`

```c
/* firmware/components/lw_storage/include/lw_resource.h */
bool lw_resource_path(char *dst, size_t cap, const char *relative);
```

注释写明：**这是唯一受支持的根路径构造方式**，`relative` 必须是相对名，
root 已含 `littleworld`。

## 4. 审计：其他四处本来就是对的

| 位置 | 传入 | 结论 |
| --- | --- | --- |
| `firmware/components/lw_storage/src/lw_resource.c:26` `join()` | 文件名 | 正确，已改为调用 `lw_path_join` |
| `firmware/components/lw_storage/src/lw_storage.c:82` `read_file()` | `key->file` | 正确，已改为调用 `lw_path_join` |
| `firmware/components/lw_ui/src/lw_pack.c:201` `parse()` | 文件名 | 正确，已改为调用 `lw_path_join` |
| `firmware/components/lw_ui/src/lw_visual_assets.c:19/:26` | `"scene-%u.lwr"` | 正确，新增 `scene_path()` 统一走 `lw_path_join` |

`lw_resource_embedded_root()` 返回 `"/littleworld/embedded"`，除
`lw_resource_mount.c/h` 外无调用者，不参与这次改动。

## 5. 端到端验证（这是关键，不是"改完编译过"）

新增 `tests/host/test_lw_card_layout.c`（69 项，`tools\run_card_layout_tests.ps1`）：

1. 在 `LW_CARD_ROOT` 下搭一棵**与真实 TF 卡完全相同**的树：
   ```
   littleworld/littleworld-v4.json
   littleworld/content.lwcx
   littleworld/male.lwp
   littleworld/qianqian-wave.lwp
   littleworld/scene-{0,1,2}.lwr
   littleworld/scene-{0,1,2}-thumb.lwr
   ```
2. `content.lwcx` 由**真实的** `tools/package_v4_content.py` 生成后拷入；
3. 按设备启动顺序执行 `lw_resource_mount` → `lw_resource_verify` →
   `lw_content_resolve_path` → `lw_content_load`；
4. 断言：解析路径**恰为** `<root>/content.lwcx`、**不等于**旧的
   双层路径、双层目录**不存在**、内容解析出 3 memories / 2 photos /
   1 letter / 2 dialogues、anniversary 2024-10-25、`.lwp` 通过同一 root
   打开并读到帧、带尾分隔符的 root 不产生 `//`、空/NULL/裸 `/` root 被拒、
   未注册 root 被拒、缓冲区不足硬失败且写空串、dotdot/绝对路径/盘符/反斜杠
   被拒、无内容文件的卡返回 `LW_CONTENT_MISSING`。

## 6. 变异验证（证明测试真的能抓住 bug）

把 `lw_content_resolve_path()` 改回：

```c
return lw_path_join(dst, cap, root, "littleworld/" LW_CONTENT_FILE_NAME);
```

同一套件 **16 项失败**：

```
content loads / status / version / memories / photos / letters / dialogues /
anniversary / memories borrowable / memory count / slashed root /
remove content / content restored ...
```

从备份恢复后重新 **69/0 通过**。

这一步是必要的：一个"修完就绿"的测试可能只是没覆盖到缺陷。变异后失败，
才证明测试断言的是真实行为。

## 7. 不改的东西

- **内容格式一个字节都没改**：`.lwcx` 的 magic `LWCX0001`、schema 1、
  content_version 1、四个 section、所有上限都原样。
- **无卡回退不变**：`lw_resource_mount` 失败时资源根为空，
  `lw_content_boot()` 记 `content: no card (...)`，界面走内置回退。
- `tools/package_v4_content.py` 只改了一处：`DEVICE_LIMITS` 与固件头文件对齐
  （用于契约测试），输出字节没变。

## 8. 未做

- 没有在真实 TF 卡上跑过（本轮零硬件操作）。测试用的是 Host 上模拟的同构目录树。
- 没有验证 exFAT 长文件名的实际行为（测试用 NTFS 目录模拟）。
- 没有做"卡拔出后重新插入"的热插拔路径。
