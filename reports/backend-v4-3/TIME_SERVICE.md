# 统一时间与昼夜服务（P0）

分支 `agent/dsh-v4-3-engine`。本轮无设备操作。

## 1. 要解决的问题

V3.1/V4.1 的时间是"三套各说各话"：

- 状态栏用 `lw_now()`，它内部退回 `LW_DEMO_BASE + uptime`（2026-10-25 19:30 起算）；
- 纪念日计数用同一个 `lw_now()`，但没人告诉界面它是假的；
- 主题是编译期常量，19:30 的屏幕上画着正午的房间；
- 网络时间从未真正启用（`lw_hw_time_get()` 无条件返回 false）。

结果：界面显示一个看起来很真的时间，实际上是从开机起累加的演示值。这正是
"不得把模拟时钟当真实时间"要禁止的行为。

## 2. 四态与优先级

```c
typedef enum {
    LW_DAY_SOURCE_INVALID = 0, /* 没有任何时钟：显示 --:-- */
    LW_DAY_SOURCE_DEMO = 1,    /* 固定演练时钟，永不当作真实时间 */
    LW_DAY_SOURCE_MANUAL = 2,  /* 用户手动设置，可保存 */
    LW_DAY_SOURCE_REAL = 3     /* 本次开机同步过（或来自 RTC） */
} lw_day_source_t;
```

数值顺序**就是**优先级：REAL > MANUAL > DEMO > INVALID。

- 手动设置覆盖 DEMO，并且可保存（`lw_app_state` 的偏好里）。
- 一次成功的网络同步**替换**手动设置——真实时间优于用户输入。
- `INVALID` 只出现在从未初始化时；`lw_day_hm()` 此时输出 `--:--`，
  界面不允许画一个假的 00:00。

## 3. 五时段与三套主题

```c
typedef enum {
    LW_DAY_PHASE_DAWN = 0,       /* 05:00 - 07:59 */
    LW_DAY_PHASE_DAY = 1,        /* 08:00 - 16:59 */
    LW_DAY_PHASE_DUSK = 2,       /* 17:00 - 19:59 */
    LW_DAY_PHASE_NIGHT = 3,      /* 20:00 - 22:59 */
    LW_DAY_PHASE_LATE_NIGHT = 4, /* 23:00 - 04:59 */
} lw_day_phase_t;

#define LW_DAY_THEME_MORNING 0
#define LW_DAY_THEME_NIGHT   1
#define LW_DAY_THEME_SUNSET  2
```

五个时段映射到三张已发布的场景图（这是内容决策，不是渲染决策）：
清晨/白天 → MORNING，黄昏 → SUNSET，夜晚/深夜 → NIGHT。

边界可配置（`lw_day_set_bounds()`），因为设置页允许用户调整。

## 4. 迟滞：不允许在边界上反复横跳

`LW_DAY_HYSTERESIS_MS = 3000`。时钟停在 17:59:59 时，新时段必须稳定 3 秒才提交，
否则 1 秒刷新会在两个主题之间来回切换——那才是真正的"闪烁"。

**但显式事件不走迟滞**：用户手动设置 18:00、改边界、清掉手动覆盖，都必须
**立刻**生效。这里修的是一个真实缺陷：初版把所有相位变更都送进迟滞路径，
`lw_day_set_manual(18:00)` 之后界面仍停在白天。修法是新增

```c
static void commit_now(lw_day_t *day, uint32_t now_ms);
```

在 `lw_day_set_bounds / set_manual / set_real / clear_manual` 里直接调用它，
而 `lw_day_tick / refresh` 仍走迟滞。

## 5. 信任与"需要加限定词"

```c
bool lw_day_needs_qualifier(const lw_day_t *day)
{
    return day->source != LW_DAY_SOURCE_REAL || day->stale || !day->trusted;
}
```

初版只判断 `source != REAL`，漏了 STALE：链路掉线后时间仍是 REAL，
但已经没人担保它准。现在 STALE 也要求界面标注（"时间未校时"）。

三种限定词在界面上分别是：

| 状态 | 后缀 |
| --- | --- |
| DEMO | ` · 演示` |
| MANUAL | ` · 手动` |
| REAL + stale | ` · 时间未校时` |
| REAL + trusted | 无后缀 |

## 6. 不每帧取时间

`lw_day_tick()` 有整秒门：

```c
uint32_t second = now_ms / 1000u;
if (second == day->last_eval_second) return;
day->last_eval_second = second;
```

调用方（`lw_time_boot_update()`）挂在 1 秒的 LVGL 定时器上，不是每帧。
相位变更只在整秒变化时才计算，`lw_day_until_next()` 给出到下一个边界的秒数，
供上层安排下一次主题切换，不需要轮询。

## 7. 接线

新增 `firmware/main/lw_time_boot.c/.h`：

```c
void lw_time_boot_init(void);
void lw_time_boot_update(const lw_net_status_t *status, uint32_t now_ms);
bool lw_time_boot_get_tm(struct tm *out);
const lw_day_t *lw_time_boot_day(void);
void lw_time_boot_line(char *out, size_t len);
const char *lw_time_boot_report(void);
```

- `lw_time_boot_init()` 在 `start_os()` 里、`lw_os_start()` 之前调用，起始状态是
  **明确标注的 DEMO**。
- `lw_time_boot_update()` 由 `caps_timer_cb`（1 秒）调用，读
  `lw_services_status()` 的快照，按 `time_state` 切换：
  SYNCED → `lw_day_set_real()`；STALE → `lw_day_mark_stale()`（只在跃迁时记日志）；
  SYNCING → 不变；DEMO/FAILED → 回 DEMO（除非已是 MANUAL/REAL）。
- `platform_get_time()` 改为 `return lw_time_boot_get_tm(out);`，
  并且**只在 `trusted` 时返回 true**——DEMO 故意不从这里出去，
  这样 `lw_now()` 会继续用它自己的、带标注的演示基准，而不是把演练时钟
  伪装成真实时间喂给平台层。

`lw_net_status_t` 新增 `int64_t utc_seconds;`（在 `time_valid` 之后）。理由写在
`lw_services.h` 注释里：快照是 poll 唯一写入、其他任务只读的结构，
加一个字段比让 UI 任务去读 `services->time` 安全。

## 8. UI 读取接口

`firmware/components/lw_ui/include/lw_ui_clock.h`：

```c
typedef struct {
    unsigned hour, minute;
    lw_day_source_t source;
    bool trusted, stale, manual;
    lw_day_phase_t phase;
    int theme;
    uint32_t until_next_s;
    bool needs_qualifier;
} lw_ui_clock_t;

void lw_ui_clock_attach(const lw_day_t *day);
bool lw_ui_clock_read(lw_ui_clock_t *out);
```

- 页面**只能读**，不能决定时间。
- 未 attach 时 `read()` 返回 false 并清零，页面显示 `--:--`，不画假午夜。
- `lw_ui_clock_read()` 用 `lv_tick_get()`——和精灵定时器、OS 主循环同一个毫秒时钟，
  所以时钟标签和相位切换不会各说各话。
- `lw_os_start()` 之前由 `lw_boot.c` 调用 `lw_ui_clock_attach(lw_time_boot_day())`，
  写一次、之后只读，因此不需要锁。

`lw_os.c` 里原本散在三处的取时间代码收敛为一个 `clock_fields()`：

```c
static void clock_fields(unsigned *hour, unsigned *minute, char *qualifier, size_t cap, bool *demo);
```

状态栏、锁屏大字、锁屏日期、设置页"日期与时间"都用它。没有 attach 时
（Windows 模拟器、legacy shell）退回原来的 `lw_now()` 路径，行为不变。

## 9. 诊断

`lw_time_boot_line()` 输出例如：

```
19:30 demo, not trusted, night, theme 1
```

接进 `lw_diag_report` 的 `clock` 字段（文本与 JSON 都有）。这一行是**故意**
把 "not trusted" 写出来的：诊断如果省略它，就等于替界面撒谎。

控制台命令 `clock` 输出完整版本：

```
clock 19:30 (demo) not trusted, phase night, theme 1, next in 1800s
```

## 10. 测试

`tests/host/test_lw_engine.c` 的 day 部分（7 个测试）：

| 测试 | 覆盖 |
| --- | --- |
| `test_day_defaults` | 默认边界、`bounds_valid` 拒绝乱序/越界 |
| `test_day_phases` | 五个时段的边界值（05:00/08:00/17:00/20:00/23:00） |
| `test_day_sources` | DEMO 不漂移、MANUAL 覆盖 DEMO、REAL 覆盖 MANUAL、`needs_qualifier` 三种情况、STALE 后仍为 REAL 但要限定词 |
| `test_day_hysteresis` | 3 秒内不提交、跨过后提交、回退也迟滞 |
| `test_day_bounds_change` | 改边界立即生效（不走迟滞） |
| `test_day_next_boundary` | 到下一个边界的秒数，含跨午夜 |
| `test_day_clock_fields` | `lw_day_clock()` 的年月日时分秒、`hm` 格式、INVALID 输出 `--:--` |

`test_day_sources` 里对"显式设置立即生效"的断言，就是第 4 节那个缺陷的回归锁。

## 11. 未做

- **没有**把昼夜主题真正应用到场景切换：`lw_visual_assets` 已有三张图，
  但"按 theme 换图"属于 UI 布局，本轮只提供 `theme` 字段，接线留给 UI 那一轮。
- **没有**做手动设置的 UI 输入框（引擎与 `lw_ui_set_anniversary` 已有能力，
  界面入口未做）。
- 未在硬件上验证：真实 SNTP 同步、断链后 STALE 的时序、跨午夜的长跑。
- 硬件没有 RTC 电池（`lw_hw_time_get()` 恒 false），所以断电重启后
  REAL 会退回 DEMO——这是硬件事实，不是缺陷。
