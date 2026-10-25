# 音频后端（ES8311）

音乐播放是这次任务里最容易"看起来做了、其实没做"的部分。所以先把边界说清楚：

- **已经真做的**：扬声器通路探测、音量真正下发到 codec、可测的音频状态机与环形缓冲。
- **没有做的**：解码器、音乐文件播放、TTS。板子上没有可播放的音频源，也不该为了演示塞一个假波形。

---

## 1. 硬件事实

| 项 | 值 | 来源 |
| --- | --- | --- |
| Codec | ES8311 | 原理图 / BSP |
| 接口 | I2S（P4 主控） | BSP |
| 采样 | 由 BSP 默认配置决定（沿用 `bsp_audio_codec_speaker_init`） | BSP 3.0.1 |
| 音量 | 0–100 → `esp_codec_dev_set_out_vol()` | 代码 |

探测发生在 `lw_hw_begin()`：

```c
s_speaker = bsp_audio_codec_speaker_init();
if (s_speaker != NULL && esp_codec_dev_set_out_vol(s_speaker, 40) == ESP_OK) {
    s_audio = true;
} else {
    s_speaker = NULL;
    ESP_LOGW(TAG, "ES8311 speaker path unavailable; volume UI stays a preference only");
}
```

`caps.audio` 是唯一事实来源：`true` 才允许 UI 说"真的在放声音"。

---

## 2. 三层

```
音量滑块（UI）
   │  lw_system_backend_t.volume → lw_hw_volume_set(percent, muted)
   ▼
lw_hw.c ── esp_codec_dev_set_out_vol() → ES8311     ← 真实硬件，已接
   │
lw_services.c ── 能力上报（lw_services_set_audio_backend）      ← 已接
   │
lw_audio.c ── 状态机 + 4 KB PCM 环形缓冲                        ← 已测，暂无数据源
```

`lw_audio` 现在**没有任何解码器喂数据**，所以它在设备上的实际状态是 `LW_AUDIO_UNAVAILABLE`（codec 没探测到）或 `READY`（探测到但没数据）。这是诚实的：UI 应显示"暂无音频源"，而不是一个假装在跑的进度条。

---

## 3. 状态机

```
UNAVAILABLE ──set_sink(true)──► READY
READY ──start()──► PLAYING ──pause()──► PAUSED ──resume()──► PLAYING
PLAYING ──stop()──► READY（等待环形缓冲排空后 on_drained）
任意 ──on_io_error()──► FAILED
```

```c
#define LW_AUDIO_RING_BYTES 4096u
```

4 KB 不是随便挑的：16 kHz / 16 bit / 单声道 ≈ 32 KB/s，4 KB ≈ 125 ms 缓冲——足够吸收一次按帧调度抖动，又不至于让暂停到真正静音延迟可感。

错误码：`NOT_SUPPORTED`（没有 codec）、`NOT_PLAYING`（没在放却想暂停）、`FULL`（生产快过消费）、`IO`（适配层报错）。

---

## 4. 三个边界行为

**静音不是"音量设为 0"。** `lw_audio_set_muted(true)` 之后 `lw_audio_write()` 直接丢弃并计入 `dropped_bytes`，而不是继续往环形缓冲塞 0 字节。区别：解静音时不会突然涌出几秒的旧音频。

**溢出要记账，不要覆盖。** 缓冲满时 `lw_audio_write()` 返回实际接受字节数（可能小于请求），丢掉的记 `dropped_bytes`。没有"静默丢帧"，因为调试音量问题时你唯一能查的就是这个计数器。

**欠载也是一等公民。** `lw_audio_on_underrun()` 让状态回到 `READY` 并 `underruns++`。播放器上层可以据此判断"缓冲太小"或"解码太慢"。

---

## 5. 为什么没有解码器也能测

因为缓冲和状态机是纯 C，不依赖 I2S：测试直接 `lw_audio_write(4096 字节)` → 读回 → 校验字节序与计数；`muted` 时验证丢弃；`underrun` 后验证状态。

主机测试里 `lw_net_esp_stub.c` 把 `lw_net_esp_audio_available()` 固定为 `false`，所以测试环境永远走 `UNAVAILABLE` 分支——这正好覆盖了"没有 codec 时的降级"。

---

## 6. 已验证 / 未验证

**已验证**

- 主机：环形缓冲读写、静音丢弃、溢出计数、欠载回落、状态迁移、`state_name()`。
- 固件：`os` 配置（`LW_ENABLE_AUDIO=y`）编译通过，`lw_hw_begin` 里真的调用了 BSP 的 speaker init。
- `noaudio` 配置（`LW_ENABLE_AUDIO=n`）编译通过，`want_audio=false`，完全不碰 codec。

**未验证**

- 板上是否真的出声：需要 `esp_codec_dev` 写 PCM。现在没有数据源，所以"能出声"这件事**没有被证明**，只有"音量能下发"被证明。
- 音量与硬件寄存器的线性度、爆音、静音切换时的 pop 声。
- 与 RGB 屏刷新的干扰（I2S 与 PPA 带宽）。

---

## 7. 要真正播放，还差什么

1. 一个解码器：MP3/AAC 用 `esp_lv_decoder`（已在 managed components 里）或 helix；或者先用 WAV（无解码，最快能听到声音）。
2. 一个喂数任务：I2S 写回调里 `lw_audio_read()` 取数据，取空就 `on_underrun()`。
3. 音频文件从哪来：SD 卡（本板无 TF 卡槽的可靠结论，见任务约束）或 flash 分区。当前 `storage` 分区 4 MB，塞一两首 30 秒的 WAV 是够的。

这三步都是新工作，不在本轮范围内；本轮交付的是"管道、计量、降级"这三件地基。
