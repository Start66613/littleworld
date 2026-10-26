# TF 卡资源包：制作、校验、安装（V4.1）

这份文档写给**不写代码的人**。照着做，就能把 Little World 的三套场景和角色动画放到 TF 卡里，
插回设备开机就能用。

---

## 一、一次性准备

1. 装好 **Python 3.10 或更高版本**（安装时勾选 “Add Python to PATH”）。
   - 如果电脑上已经装了 ESP-IDF 工具链，可以跳过这步：脚本会自动找到它自带的 Python。
2. 把仓库下载到本地（GitHub 页面 → Code → Download ZIP，或用 git clone）。
3. 打开 PowerShell，进入仓库目录：

   ```powershell
   cd <你放仓库的路径>
   ```

> 如果你的 Python 不在 PATH 里，可以先设置一次：
> `$env:LW_PYTHON = "C:\Python312\python.exe"`

---

## 二、制作资源包（一条命令）

```powershell
powershell -ExecutionPolicy Bypass -File tools\package_v4_sd.ps1
```

成功后会看到类似输出：

```
[OK] 资源包已生成
  输出目录: ...\dist\littleworld-v4-sd\littleworld
  文件数:   9（含清单与哈希清单）
  总大小:   12,568,230 字节
  清单大小: 118 字节（固件上限 1023）
```

**资源包在哪**：`dist\littleworld-v4-sd\littleworld\`
里面固定是这些文件（名字不能改）：

```
littleworld-v4.json        版本清单（固件开机先读它）
scene-0.lwr                场景 0：樱花清晨
scene-1.lwr                场景 1：月光梦境
scene-2.lwr                场景 2：草莓黄昏
scene-0-thumb.lwr          场景 0 缩略图（设置页用）
scene-1-thumb.lwr
scene-2-thumb.lwr
male.lwp                   男方角色动画包（idle / blink / wave）
qianqian-wave.lwp          千千的挥手动画包
littleworld-v4.sha256      校验清单（可选，留着更方便排查）
```

---

## 三、校验（上卡前建议跑一次）

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_v4_sd.ps1
```

- 全部正常：`[OK] 校验通过`，退出码 0
- 有问题：会逐条列出，比如

  ```
  [FAIL] 发现 2 个问题：
    - male.lwp: 文件大小 5,503,364 字节，按 3 clip / 28 帧应为 5,505,364（可能截断或帧数据损坏）
    - scene-1.lwr: 魔数错误（应为 LWSR0001，实际 b'XWSR0001'）
  ```

它检查的内容和固件开机时的检查**完全一致**：清单的 schema/version、场景文件魔数与尺寸、
角色包的画布/clip 数/帧数/文件长度、以及 SHA-256 是否对得上。

也可以校验一张已经复制好的卡：

```powershell
powershell -ExecutionPolicy Bypass -File tools\verify_v4_sd.ps1 -Target E:\
```

---

## 四、复制到 TF 卡

### 方式 A：用脚本（推荐）

```powershell
# 第一步：先只看信息，不会写任何东西
powershell -ExecutionPolicy Bypass -File tools\install_v4_sd.ps1

# 第二步：确认卡符（例如 E:\）后再复制
powershell -ExecutionPolicy Bypass -File tools\install_v4_sd.ps1 -Target E:\
```

脚本会：

- 显示目标盘符、卷标、文件系统、剩余空间、**实际写入目录**；
- 如果目标不是可移动磁盘，默认**拒绝**（防止误写系统盘）；
- 如果卡上已经有 `littleworld` 文件夹，把它改名备份成 `littleworld.bak-时间戳`，**不删除**；
- 复制完自动重新校验一遍。

### 方式 B：手动复制

把 `dist\littleworld-v4-sd\littleworld` 整个文件夹拖到 TF 卡根目录即可。
**注意**：卡里必须是 `E:\littleworld\littleworld-v4.json` 这一层，
不要变成 `E:\littleworld\littleworld\...`。

---

## 五、常见问题

**Q：设备开机后画面没变化？**
A：检查三件事：① 卡里路径对不对（见上）；② 跑一次 `verify_v4_sd.ps1 -Target E:\`；
③ 卡是不是 FAT32/exFAT（设备读不了 NTFS）。

**Q：能不能只替换其中一张场景？**
A：可以。把新的 `scene-1.lwr` 覆盖进 `littleworld/` 后，记得重新生成哈希清单
（再跑一次 `package_v4_sd.ps1`），否则校验会提示哈希不一致。

**Q：`littleworld-v4.json` 能不能加自己的字段？**
A：可以加，但它会被固件**整块只读 1023 字节**，而且必须保留 `"schema": 1` 和
`"version": 4` 两个数字字段。超过 1023 字节脚本会直接拒绝。

**Q：资源坏了会不会把设备搞崩？**
A：不会。固件側任何一项校验失败都会回退到内置画面，并在设置里显示中文原因
（例如“资源清单缺失”“资源版本不兼容”）。

---

## 六、给开发者：跑回归

```powershell
powershell -ExecutionPolicy Bypass -File tools\test_v4_sd_pack.ps1
```

会用仓库里真实的 V4 资源做一次完整打包+校验，再造 8 种坏包（缺失、魔数错、截断、
主题越界、版本不兼容、哈希不符、清单过大、空目录），确认每一种都被指出来。
当前结果：**11 项全过**。
