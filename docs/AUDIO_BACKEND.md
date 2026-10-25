# Audio backend

Status: **pipeline core implemented and tested on the host; the ES8311 output
path is still not playing sound.** This document says exactly what is proven
and what is not, so nobody mistakes the model for a speaker.

## What exists now

`lw_audio.c` is the part of audio that must be correct regardless of the codec:
a bounded PCM ring with explicit back-pressure and a state machine the UI can
trust.

```
UNAVAILABLE ──sink appears──> READY ──start──> PLAYING ──drained──> DONE
                                  PLAYING <──resume── PAUSED <──pause──┘
                                  PLAYING ──io error──> FAILED
```

Properties the tests pin down:

- `write()` refuses until `start()`, returns the number of bytes accepted, and
  reports `LW_AUDIO_ERR_FULL` when the ring is full. A partial accept counts the
  lost bytes; a zero-byte accept does not (the producer retries, nothing is
  lost yet).
- `read()` only drains while PLAYING; pauses are clean.
- **Muted still consumes the stream** (bytes are counted as dropped), so timing
  stays realistic and the UI can unmute mid-track without a stall.
- `stop()` flushes; `on_drained()` reaches DONE; `on_io_error()` reaches FAILED
  and keeps the error for the UI.
- Volume clamps to 0..100.

## Why there is no sound yet

The pipeline is not the reason. The speaker path needs:

1. BSP codec init (`bsp_audio_codec_speaker_init` / `esp_codec_dev`), which the
   BSP only exposes when the ES8311 answers on I2C;
2. an I2S TX channel with the right sample rate and slot config;
3. a producer feeding real PCM — and there is no audio file in the repo, so
   "play music" is still a simulated transport in the UI.

`lw_hw_begin(CONFIG_LW_ENABLE_AUDIO)` already probes the codec and reports
`caps.audio`. When it is false the OS keeps the volume as a stored preference
and the settings page says so — the honest behaviour, not a fake equaliser.

## Next steps

1. Open I2S TX + ES8311 with `esp_codec_dev` in `lw_net_audio_esp.c`.
2. Feed `lw_audio_read()` from the I2S write callback (or push into the driver
   from a task that drains the ring).
3. Add one short WAV to the storage partition and wire the music app's
   transport to `lw_audio_start/write/read`.
4. Verify on hardware: a 440 Hz tone, then a song, then mute/unmute and volume.

Everything above the codec boundary is already covered by
`tests/host/test_lw_net.c` (`audio/*`): unavailable refusal, ordering, pause
semantics, overflow accounting, mute, underrun, drain and IO failure.
