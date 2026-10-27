# 合并记录

## 基线（`git fetch origin --prune` 后实测）

| 分支 | commit | 说明 |
| --- | --- | --- |
| `agent/visual-v4-2-polish` | `2900569e381f1e06ecf88d720a3f032a9bd97db6` | Codex 视觉 V4.3 正式美术 |
| `agent/dsh-v4-3-engine` | `05bfc36b335a5a4636f38d641a0fefde4dbd6599` | DSH 引擎 |
| `agent/v43-integration-support` | `9fb60456922b51f8616a2960c219dfbb2c1cfa0e` | Codex 追加的接线说明与测试（只读参考，未合并） |
| `integration/os-v4-p4-unified` | `3827040f5cd4706861017d41e1e6694f928aa117` | 旧统一版 |
| `main` | `8af0637c3ce72844e9dfec828ea1eb1bd9e194bb` | |
| `release/os-v3.1-stable` | `d95b806635659a1b3cf0282e306c9e603e7c734a` | 可恢复稳定版 |

两条 V4.3 分支的 merge base 是 `94efbf590e5b7b2a51827edc46c03cb3c3a5237c`。

## 集成工作区

```
git worktree add D:\LittleWorld-V43-Board -b integration/os-v4-3-board-smoke 05bfc36
```

用**新的** worktree，没有动 Codex 的 `D:\LittleWorld-V43`（`agent/dsh-v4-3-engine`）、
`D:\LittleWorld-V4-2`（`agent/v43-integration-support`）或任何其他工作区；
全程没有 `reset --hard`、没有 `clean -fdx`、没有覆盖过别人的未提交文件。

## 合并

```
git merge --no-commit --no-ff origin/agent/visual-v4-2-polish
```

Codex 侧 477 文件 / +65,690 / −58,197。自动合并以外的冲突**只有四处**，
全部逐段保留双方功能，**没有用 `-X ours` / `-X theirs`**：

### 1. `.gitignore`

两侧忽略项取并集。DSH 侧保留 `/reports/logs/flash/`、`/reports/logs/v4/`、`/reports/diagnostics/`、
`/content/private/`、`/content/*.local.lwcx`、`/dist/release-v4-3/`；
Codex 侧保留 `/reports/ui-v2/v42-*/`、`/assets/visual-v4-2/generated/`、
`/reports/visual-v4-2/preflight|contact-review|scale-review|interface-check|meowa-first|meowa-second/`、
`/reports/logs/meowa-skill-review/`、`/reports/logs/v42-custom-workflow-catalog.json`。
另加 `/reports/ui-v2/v43-*/`（本轮联合版截图目录）。

### 2. `firmware/components/lw_ui/CMakeLists.txt`

重写为 **41 个源文件的并集**：DSH 侧 `lw_pack.c`、`lw_ui_asset_view.c`、`lw_ui_clock.c` 与
Codex 侧 `lw_polish.c`、`lw_ambience.c`、`lw_skin.c` 都在。
`REQUIRES` 取 DSH 的超集 `lvgl__lvgl lw_core arcade_core lw_duo lw_system lw_rpg lw_storage lw_engine log`，
保留 BSP 的 `PRIV_REQUIRES esp32_p4_wifi6_touch_lcd_7b`。

> 这一条是本轮最容易被做错的地方：只取一侧的源文件列表不会报错，
> 而是让另一侧的组件在链接期静默缺席。41 个源文件是逐个核对 `git show` 两侧列表后写的。

### 3. `firmware/components/lw_ui/src/lw_visual_assets.c`

两个 include 都保留：`#include "lw_path.h"`（`scene_path()` 用 `lw_path_join`）与
`#include "lw_ambience.h"`（`lw_visual_daily_image()` 用 `lw_ambience_phase`）。

### 4. `firmware/components/lw_ui/src/lw_os.c`（两处）

**`refresh()`** —— 一次时钟快照同时喂状态栏和锁屏，保留 Codex 的皮肤选择：

```c
unsigned hour=0, minute=0; char qualifier[24]; bool demo=false;
clock_fields(&hour,&minute,qualifier,sizeof(qualifier),&demo);
lw_ui_clock_t clock; bool have_clock=lw_ui_clock_read(&clock);
lw_date_t d=have_clock ? (lw_date_t){clock.year,(int)clock.month,(int)clock.day} : lw_today();
if (s->clock) lv_label_set_text_fmt(s->clock,"%02u:%02u   %d 月 %d 日%s",
                                   hour,minute,d.month,d.day,qualifier);
```
锁屏日期后缀 `(demo || !have_clock) ? " · 演示日期" : ""`；`OC_DND`/`OC_MUTE` 仍走 `lw_skin_select()`。

**`tick()`** —— 顺序：DSH 的 `CONFIG_LW_DIAG_STATIC_UI` 早退块 → Codex 的 Dock 自动收起
（`lv_indev_get_next`/`lv_indev_get_state`，`dock_idle>4500`）→ 每秒块同时刷新时钟、氛围层与时段，
**只在时段跨越时重绘**：

```c
s->last_clock+=dt; if (s->last_clock>=1000) {
    s->last_clock=0; refresh(); lw_ambience_refresh(s->light);
    lw_day_phase_t phase=lw_ambience_phase();
    if(s->phase!=phase && s->overlay==OS_OVER_NONE && !s->boot &&
       (s->page==OS_DESKTOP||s->page==OS_WORLD||s->page==OS_PET)){s->phase=phase;render();}
}
```

## 两套昼夜真值的统一（合并之外必须做的修正）

合并后出现了**两个同名枚举**：`lw_day.h` 的 `lw_day_phase_t`（`LW_DAY_PHASE_*`，五态）
与 Codex `lw_ambience.h` 自带的 `lw_day_phase_t`（`LW_DAWN…LW_DEEP_NIGHT`，同样五态）。
同一编译单元里两个类型同名，是编译错误——但更危险的是"能编过但语义不同"的版本。

处理：`lw_ambience.h` 改为 `#include "lw_day.h"`，把 `LW_DAWN` 等**定义为 `LW_DAY_PHASE_*` 的别名**，
`phase >= LW_NIGHT` 的比较含义保持不变。
`lw_ambience.c` 重写为**优先读 `lw_ui_clock_read()`**（DSH 的有效时钟，带可信状态），
`lw_ambience_clock_valid()` 只在 `source == LW_DAY_SOURCE_REAL && trusted` 时为真，
平台钩子降级为 Windows 桌面构建的兜底。

这样：真实时间可信时按真实时间切换；未同步/演示时钟会带"· 演示"后缀；手动预览走 `lw_ambience_preview()`，
三者不再各说各话。**这一点有截图证据**：`windows/v43-time-settings.png`（时间段预览）、
`windows/v43-home-night.png`、`windows/v43-daily-night.png`（夜间场景与主题一致）。

## 提交

| commit | 内容 |
| --- | --- |
| `e70042b` | 合并提交（两父：`05bfc36` + `2900569`），479 文件 / +65,810 / −58,204 |
| `041bf46` | LWPX 动态几何 + 全量资源注册 + TF 卡打包器重写 |

两次都已推送 `origin/integration/os-v4-3-board-smoke`，`origin/HEAD` 与本地一致。
