# CODEX RPG HANDOFF — 无冲突合并指引

> **Phase 2 更新（2026-10-25）**：角色路线已锁定，请按下面的路径接入。
>
> | 用途 | 资产 | 说明 |
> |---|---|---|
> | 男方默认立绘 | `assets/rpg/final/male_B_gentle_final.png` | 256² RGBA，高 209，脚底 226，中心 128 |
> | qianqian 默认立绘 | `assets/rpg/final/qianqian_v2_a_final.png` | 256² RGBA，高 218，脚底 226，中心 128 |
> | 房间背景 | `assets/rpg/room/room_pixel_1024x600.png` | 1024×600，无角色/无 UI/无文字，中央地毯留空 |
>
> **不要当默认角色用**（只是表情参考）：
> `male_C_playful_expression.png`（搞怪/眨眼）、`qianqian_v2_b_expression.png`（好奇歪头）。
>
> - 两张正式立绘高度不在 192–204 区间（209 / 218），**已按用户"风格优先"的指示接受**；
>   若你的集成需要严格 198，请等我下一批按 198 重出，不要自己缩放（会破坏像素）。
> - 替换 TEST A 的合成预览在 `assets/rpg/previews/phase2/06_os_desktop_male_b.png`，
>   但那是本地 mock，**不要直接当系统截图发**；系统里替换需要你走资源加载入口。
> - 房间是 2048 母版 2× 降采样 + 顶部补墙，母版在 `assets/rpg/room/room_pixel_2048x1152_master.png`。
 > - 本轮**没有**动画帧，idle/walk 需要下一批。

版本 v0.1 · 2026-10-25 · DSH → Codex
分支：`rpg/meowa-v3-phase2`（隔离 worktree `D:\LittleWorld-RPG`，基于 `art-assets`）
**没有改动任何 OS 文件，没有动 main，没有刷机。**

## 1. 本次新增的路径（全部是新增，无修改）

```
rpg/include/lw_rpg.h          公开接口与数据结构
rpg/src/lw_rpg.c              网格/寻路/角色/热点/任务/对话运行时
rpg/src/lw_rpg_save.c         40 字节存档 + CRC
rpg/data/lw_rpg_home.c        房间、3 个热点、任务、对话（示例文本）
rpg/data/lw_rpg_demo.h
tests/host/test_rpg.c         85 项纯 C 单元测试
tools/rpg_scenario.c          脚本化场景，输出状态快照 JSONL
tools/render_rpg.py           mock 渲染（Python + Pillow），产出 1024×600 预览
docs/rpg/*.md                 角色、管线、玩法、地图、对话、存档
docs/handoff/CODEX_RPG_HANDOFF.md   本文件
reports/rpg/                  4 张 mock 截图
reports/rpg-scenario.jsonl    场景状态快照
```

与你的 `agent/pixel-duo`、`agent/littleworld-os-v2`、`host/`、`firmware/` 无重叠。

## 2. 建议的宿主 API（草案，等你确认）

```c
typedef struct {
    void (*on_exit_requested)(void *user);
    void (*on_reward_unlocked)(uint32_t reward_id, void *user);
    void (*on_save_requested)(const void *blob, size_t size, void *user);
    void (*on_sfx)(const char *id, void *user);   /* 可为 NULL */
    void *user;
} lw_rpg_host_t;

bool lw_rpg_host_init(const lw_rpg_host_t *host);
void lw_rpg_host_enter(void *lv_parent);   /* 你决定是 lv_obj_t* */
void lw_rpg_host_suspend(void);
void lw_rpg_host_resume(void);
void lw_rpg_host_exit(void);
```

这部分**尚未实现**（本轮只做纯 C 核心），需要你确认签名后我再补 `rpg/host/` 适配层。
在你确认前，`lw_rpg_*` 的纯逻辑接口已经冻结，可先用它做单元测试。

## 3. 输入与渲染的边界

| 归属 | 内容 |
|---|---|
| RPG 模块 | 网格坐标、寻路、碰撞、热点判定、任务/对话状态、存档 |
| OS 宿主 | LVGL 对象、触摸事件 → `lw_rpg_tap_tile(x, y)`、绘制、音频、页面转场 |

触摸映射：屏幕像素 → 格坐标
`tx = (sx - 32) / 44; ty = (sy - 104) / 44;`（越界直接丢弃）

提示：`lw_rpg_interact` 只在玩家距离热点 ≤1 格时成功，宿主可据此显示"点一下"提示。

## 4. 资源与内存

- 本版**没有**引用任何 256 立绘；RPG 角色精灵尚未生产（等男方向定稿）。
- 若按 256² RGB565+A8 估算：196,608 B/帧；两人 + 单帧换图峰值 ≈ 590 KB（不含背景/字体/帧缓冲）。
- 存档 40 B；房间数据 ≈ 1 KB 常量；对话文本按实际字符串计。
- 建议：RPG 使用独立的 LVGL screen，进入时创建、退出时 `lv_obj_delete`，与 OS 的页面生命周期一致。

## 5. 验证方式（你可独立复跑）

```powershell
# 纯 C 测试（MSVC）
cl /nologo /utf-8 /W4 /WX /I rpg\include /I rpg\data tests\host\test_rpg.c `
   rpg\src\lw_rpg.c rpg\src\lw_rpg_save.c rpg\data\lw_rpg_home.c `
   /Fe:build\rpg\test_rpg.exe /Fo:build\rpg\
.\build\rpg\test_rpg.exe            # 期望：85 checks, 0 failure(s)

# 场景快照 + mock 截图
cl ... /Fe:build\rpg\rpg_scenario.exe
.\build\rpg\rpg_scenario.exe | Out-File -Encoding utf8 reports\rpg-scenario.jsonl
python tools\render_rpg.py reports\rpg-scenario.jsonl reports\rpg
```

实测：85 项全部通过；场景脚本跑完三件物品、任务完成、存档解码返回 0。

## 6. 需要你决定的三件事

1. 宿主 API 的最终签名（`void*` 还是 `lv_obj_t*`；是否要 `on_sfx`）。
2. RPG 用独立 NVS 键（`lw_rpg_save`）还是并入 OS 的统一存储。
3. RPG 页面由谁创建：你的路由调用 `lw_rpg_host_enter(lv_screen_active())`，还是我提供 `lv_obj_t*` 根节点给你挂。

## 7. 明确未完成

- 没有真实 RPG 角色精灵（等男方向定稿 + 预算批准）。
- 没有 LVGL 渲染层（等你确认宿主 API）。
- mock 截图是 Python 画的示意，不是设备运行截图。
- 没有板测、没有固件集成、没有改 OS。
