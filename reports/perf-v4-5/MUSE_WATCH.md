# Muse watch

A running log of what the cloud agent (Muse Code) pushes to
`https://github.com/Start66613/littleworld`, and what each branch is worth.

The agent works on a remote machine, so the only shared ground truth is GitHub.
`tools/watch_muse.ps1` fetches, prints the commits that appeared since its last
run, and remembers the heads in `reports/perf-v4-5/muse-watch.json`.

## Branches seen so far

| Branch | Head | Push | Base | Verdict |
|---|---|---|---|---|
| `perf-optimization-2026-11-08` | `d3d44af` | 11-08 21:49 | `main`@8af0637c (10-25) | 13 commits / 16 files / +606 −122. Sound micro-optimisations, but branched from the old main, so it cannot merge — only cherry-pick. Two items taken (pet decay fractions, top-bar text guards) in `0e06ae6`. |
| `backup/round2-final-2026-11-08` | `3d86d8d` | 11-08 | old main | Snake drawn on the playfield instead of per-segment objects. Backup branch, not a delivery. |
| `agent/muse-cloud-audit` | `6444638` | 11-09 00:05 | `perf/v4-5-render-engine`@`fa1a25a` | `CLOUD_AUDIT.md` (350 lines, 16 findings with file:line) + `CLOUD_PATCHES.md` (182 lines, 10 patches) + `tools/test_panel_bake_sampling.py` (121 lines, 5 cases, all pass here). No firmware change. |
| `agent/muse-night-2026-11-09` | `8250cef` | 11-09 11:23 | same | 11 perf commits + one 8-file batch. Host `752/1` (it fixed two music checks), pixels `131/156`. On the board the control centre got *slower* than this branch's own base (`bc529237`): first open `85/214/356/102` vs `130/256/89`, reopen `86/103/184/175` vs `61/77/83/97/88`. Recorded in `reports/perf-v4-5/FINAL_BENCHMARK.md` §21; cause not established. |
| `agent/muse-wifi-landing` | `23c71a4` | 11-09 19:04 | night | System↔services Wi-Fi bridge, availability from the service layer, password truncation fix, 289-line host test. **Verified on the board**: control centre shows the Wi-Fi tile dimmed with `无线驱动尚未接入`, taps are inert (`capabilities.wifi != LW_SERVICE_UNAVAILABLE` gates the handler), no crash. Host `150/0`, resource `261/0`, core green, backend green, pixels `131/156`. |
| `agent/muse-ambient-home` | `562e846` | 11-09 21:58 | wifi-landing | **Standby lock screen**: sofa couple clip, 8 s photo carousel, auto-return to the lock screen after 30 s idle. |

## 2026-11-09 22:05 — `agent/muse-ambient-home` is red as pushed

Built `origin/agent/muse-ambient-home` + the diag-stub fix (`b0a3ed4`).

- Host `--os-v2-test`: **749 checks, 28 failures**. `HOST TESTS PASSED`,
  `BACKEND TESTS PASSED`, `V4 RESOURCE TESTS OK`, `CORE REGRESSION PASSED` all
  stay green — only the OS contract suite sees it.
- Single-variable test: disabling the one line
  `if(s->ambient_idle>=30000) { ... show_overlay(OS_OVER_LOCK); }` restores
  **752 checks / 1 failure** (the pre-existing `greeting naturally returns to
  idle clips`). So the 30 s auto-lock alone accounts for the 27 extra failures:
  the harness idles past 30 s in most scenarios and the screen quietly becomes
  the lock overlay, which invalidates `control exists`, `touch center within
  screen`, `20 page cycles are stable`, `top pull creates a real panel`,
  `desktop idle really schedules eye blinking`, `duo approaches and enters
  greeting`, `both actors actually select wave clips`, …
- Pixels vs `shot-skin2`: **46 identical / 110 differ** as pushed, **127/29**
  with the auto-lock disabled. The remaining 29 are the intended lock-screen
  redesign (couple sprite, photo frame) plus the book-close sequence.
- Ask: gate the auto-lock behind an explicit setting that defaults to off (and
  keep it off in host builds), then re-run. An automatic lock screen that
  nobody asked for should not be able to turn the suite red.

## 2026-11-09 22:30 — the standby screen is kept and fixed

The auto-lock was a user request, so it stays. What changed (`e2b4951`):

- `lw_os_ambient_autolock(bool)` added. Firmware leaves it armed; the host
  harness disarms it in `lw_os_host_run` and exercises the timer in one explicit
  block (lock appears after 31 simulated seconds, the unlock control exists,
  unlocking returns to the desktop). **749/28 -> 756/1** (same single
  pre-existing failure).
- Photo frame matte: black -> card colour. The scene thumbnails are 1024x600 and
  the frame is 248x196, so a fitted image letterboxes ~25 px top and bottom; on
  black that read as a broken picture.
- Carousel: six photos over three scenes, so two of every three 8 s ticks now
  reuse the picture already on screen instead of re-running the pool lookup.
- `LW_MOVE_LOCK` no longer fades the whole lock container in (layer-per-frame
  trap); slide only.

Board (COM5, 4,135,184 B, hash verified): the standby screen arrives with a
20 ms build and one 98 ms frame; in a 22 s window the carousel produced no frame
over 25 ms; unlocking returns to the desktop with both sprites animating.
