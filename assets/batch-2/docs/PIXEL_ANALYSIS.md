# qianqian 像素素材分析（DesktopPet）

分析对象：`https://github.com/Start66613/DesktopPet`（克隆到 `D:\LittleWorld-Meowa\refs\DesktopPet`）
分析时间：2026-10-25，方法：直接读 PNG + `.tres`，Pillow/NumPy 实测像素。

## 1. 帧与画布

| 项目 | 实测值 |
|---|---|
| 单帧画布 | **256 × 256**，RGBA |
| 角色可见尺寸 | 宽 98 px × 高 **198 px** |
| 角色包围盒（idle_01） | x 78–175，y **28–225** |
| 水平中心 | x ≈ **126**（画布中心 128） |
| 脚底 | y = **225** |
| 顶部留白 | 28 px；底部留白 30 px |
| 行走帧脚底 | 223–225（≤2 px 漂移） |
| 行走帧高度 | 194–198 |

**锚点规范：脚底中心 y=225，身体水平中心 x=126；在 256² 画布内的归一化坐标 (0.492, 0.879)。**

## 2. 像素与配色

- 半透明像素：**0 个**（alpha 非 0 即 255）→ 硬边像素，无抗锯齿。
- 不透明像素 12,719 个；**716 种颜色**，但 **35 色覆盖 90%**，589 色覆盖 99%（存在大量近似色微渐变）。
- 主色（出现频次）：

| 色值 | 用途 | 像素数 |
|---|---|---|
| `#393834` | 头发/轮廓主色 | 3,130 |
| `#FCF5F1` | 衣服白/高光 | 1,641 |
| `#FCE3D2` | 皮肤 | 1,314 |
| `#F4B2B0` | 粉色装饰 | 743 |
| `#23221F` | 深色阴影 | 527 |
| `#504F4C` | 次级灰 | 416 |
| `#F1D6D1` / `#EEAE98` / `#E39392` | 皮肤阴影/腮红 | 各 200–360 |

- 描边：暖深灰褐（`#393834` / `#23221F`），不是纯黑。
- 头身比：约 **2.4 头身**（Q 版），头宽约 70 px，身高 198 px。

## 3. 动画清单（assets/runtime/pet_sprite_frames.tres）

| 动画 | 帧数 | 速度 fps |
|---|---|---|
| idle | 16 | 8 |
| default | 16 | 5 |
| walk_down / left / right / up / down_left / down_right / up_left | 8 | 8 |
| walk_up_right | —（结构不同） | 8 |
| sleep | 13 | 2 |
| sleep_enter | 8 | 8 |
| sleep_idle | 16 | 3 |
| sleep_turn | 8 | 8 |
| dog_interaction | 16 | 8 |
| dragon_ride | 16 | 8 |
| music | 4 | 6 |
| badminton / handheld | 由独立资源包提供 | 7 / 6 |

- 行走精灵图：768×768，3×3 网格，每帧 256×256，共 8 帧。
- 睡眠/转身：1024×1024，4×4 网格。
- `assets/sprites/idle/` 另有 8 张独立 PNG（256²），是 idle 的另一套导出。
- 八方向齐全（上/下/左/右 + 四个斜向），可直接支撑 Little World 的双人走位。

## 4. 资源目录速览

```
assets/sprites/idle/            8 张 256² 待机
assets/movement/walk_*/         8 个方向，768² 精灵图 + frames.tres
assets/movement/sleep*          入睡/睡姿/翻身，1024²
assets/activities/              羽毛球、音乐、掌机、骑龙（source + packages）
assets/dog/                     小雪球（白狗）八方向与动作，**与 qianqian 独立**
assets/runtime/pet_sprite_frames.tres   19 个动画的运行时总表
assets/care|chat|music|companion/runtime/ui/   UI 素材
```

## 5. 授权与仓库注意

- DesktopPet 仓库 README 明确写着"**当前没有发布开源许可证**……获得仓库访问权限不代表获得复制、修改、再发布、商用代码或素材的许可"。
- 该仓库同时标注为 Private（徽章），但从本机可匿名克隆，说明**当前实际是公开的**，与描述不一致。
- 结论：qianqian 素材是作者（你）自己的作品，用于 Little World 私人礼物没有问题；但 **Little World 仓库是 public**，把 qianqian 精灵图提交进去等于公开发布。是否公开需要你点头，或者只放在私有分支/私有目录。

## 6. 对男方角色的硬性约束（从上面推导）

1. 画布 256×256、透明、硬边像素、无半透明。
2. 身高 194–198 px，脚底 y=225，水平中心 x≈126。
3. 描边用暖深灰（`#393834` 系），不用纯黑。
4. 主色控制在 30–45 色覆盖 90%，允许少量渐变但不要照片级噪点。
5. 头身比 2.4 左右（Q 版），肩宽约为头宽的 0.9–1.1 倍。
6. 与 qianqian 并排时，两人像素颗粒必须一致（1 像素 = 1 像素），禁止缩放模糊。
