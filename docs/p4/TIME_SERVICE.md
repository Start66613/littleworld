# 时间服务（SNTP）

时钟是 OS V2 里最显眼的"真/假"分界线：屏幕右上角那个 `19:30`，以前是写死的演示时间，现在必须能变成真的。

结论先写：**时钟服务在模型层已经完整且通过测试；真实 SNTP 联网同步尚未在板子上验证**（因为没有开启 Wi-Fi 后端，见 `WIFI_BACKEND.md`）。

---

## 1. 五个状态

```c
typedef enum {
    LW_TIME_DEMO = 0,  /* 从未同步：显示演示时间，UI 必须标注"演示" */
    LW_TIME_SYNCING,   /* 同步进行中，20 s 超时 */
    LW_TIME_SYNCED,    /* 拿到可信 UTC */
    LW_TIME_STALE,     /* 曾经同步，现在不可信（断网或太久没同步） */
    LW_TIME_FAILED     /* 这次同步失败 */
} lw_time_state_t;
```

| 当前 | 事件 | 下一个 | 说明 |
| --- | --- | --- | --- |
| DEMO | `request_sync()` | SYNCING | 只有 Wi-Fi 连上才允许发起 |
| SYNCING | 成功 | SYNCED | 记录 `utc_seconds` 与 `synced_ms` |
| SYNCING | 失败 | FAILED | 带错误码（timeout/network/invalid） |
| SYNCING | 20 s 无结果 | FAILED | `LW_TIME_ERR_TIMEOUT` |
| SYNCED | `on_network_lost()` | STALE | **不清空时间**，只是不再可信 |
| SYNCED | 距上次同步 ≥ 6 h | SYNCING | 周期重同步 |
| STALE | 再次同步成功 | SYNCED | |
| FAILED | 再次请求 | SYNCING | 由胶水层按退避触发，不是自动风暴 |

**关键设计：DEMO 不等于 FAILED。** DEMO 是"这台机器从没连过网"，FAILED 是"连了但没成"。UI 对两者的文案必须不同，否则用户会以为时钟坏了。`lw_time_state_name()` 提供 `"demo"/"syncing"/"synced"/"stale"/"failed"`。

---

## 2. 时区放在哪

时区偏移**不放进 24 字节的偏好设置**。

理由：`lwo2.prefs` 是已经出货的格式，带 CRC-32、带版本字节。为了一个时区去改它的布局，会让老固件写的记录在新固件里 CRC 失败，用户的所有设置（主题、亮度、音量）一起丢。为一个字段冒这个险不值得。

所以时区单独一条记录 `lwo2.time`：

```c
#define LW_TIME_DEFAULT_TZ_MINUTES 480 /* UTC+08:00 */
```

范围 `-840..840`（UTC-14:00 .. UTC+14:00），越界在 `clamp_zone()` 里夹紧，不报错——用户错按"+"五十次也不该看到崩溃。

---

## 3. 时间从哪来

三层，优先级从高到低：

1. **RTC / NVS 恢复**：`lw_time_service_restore(utc_seconds, now_ms)`。恢复后状态是 **STALE**，不是 SYNCED——因为这个值是上次开机留下的，没有任何东西为这次开机作证。UI 可以显示它，但"已同步"标记必须是灰的。
2. **SNTP**：`esp_netif_sntp_init()` + `pool.ntp.org`，成功后 `utc_seconds` 覆盖。
3. **DEMO**：什么都没拿到时，shell 用固定演示时间，标签写 `DEMO 时间`。

当前 `lw_net_esp_time_request()` 的实现是"读一遍系统时间，大于 2023-11 就当作成功"。这是个**妥协**：真正的 `esp_netif_sntp_sync_wait()` 会阻塞事件循环，不能直接在定时器回调里调用。正确做法是在适配层起一个一次性任务等 SNTP 回调；这一步**没有做**，因为 SNTP 路径现在根本编不进来（`CONFIG_LW_NET_SNTP_ENABLE` 依赖 Wi-Fi 开关）。代码里用 `LW_NET_HAVE_SNTP` 明确标注了这一点，不会假装已经实现。

---

## 4. 时间怎么用

模型不返回 `struct tm`，因为 `tm` 在跨平台上有太多坑（`tm_year` 从 1900 起、`tm_wday` 语义、`time_t` 32/64 位）。对外只有三个：

```c
bool lw_time_service_clock(const lw_time_service_t *, uint32_t now_ms, lw_clock_t *out);
bool lw_time_service_date(const lw_time_service_t *, uint32_t now_ms, lw_date_t *out);
void lw_time_service_hm(const lw_time_service_t *, uint32_t now_ms, char out[6]);      /* "19:30" */
void lw_time_service_date_label(const lw_time_service_t *, uint32_t now_ms, char *out, unsigned cap);
                                                                                       /* "10月25日 周日" */
```

`lw_date_t` 复用 `lw/lw_time.h` 里已有的 Hinnant 历法换算（`lw_days_from_civil` / `lw_civil_from_days`），这套换算早就在纪念日功能里用了半年，不重新发明。

**推进靠 `now_ms`，不靠 `time()`。** `utc_seconds` 是锚点，当前时间 = 锚点 + (now_ms - synced_ms)/1000。好处：模型完全可测（传任意 now_ms 就能测"6 小时后"），坏处：`now_ms` 回绕（约 49.7 天）会算错。当前 `esp_timer` 的 microseconds 转 uint32 确实会回绕，**这是已知缺陷**，修法是把 `now_ms` 换成 uint64；本轮没改，因为要同步改所有模型签名，风险大于收益。

---

## 5. 已验证 / 未验证

**已验证**

- 主机测试覆盖：DEMO 不冒充真时间、20 s 超时、断网转 STALE、6 h 重同步、时区夹紧、RTC 恢复标记、跨日换算、`hm()` 在无时间时返回 `--:--`。
- 三套固件配置编译通过（SNTP 关闭状态，即当前默认）。

**未验证**

- 真实 SNTP 往返（需要 C6 联网）。
- 长时间漂移：`esp_timer` 的精度决定 6 小时内会漂多少，没测。
- 夏令时（DST）：不支持，时区是固定偏移。中国不用 DST，但这条要写清楚。

---

## 6. 下一步

1. Wi-Fi 后端打通后，把 `lw_net_esp_time_request()` 换成真正的 SNTP 异步等待（起任务 + 信号量），现在这版是占位。
2. 把 `now_ms` 从 uint32 升到 uint64，消除 49.7 天回绕。
3. 板级验证：开机 → 联网 → 时钟从 `--:--` 跳成真实时间 → 拔网 → 变 STALE 但时间继续走。
