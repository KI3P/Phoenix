---
title: Spectrum Refresh Rate Floor & Optimization
type: decision
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [spectrum, waterfall, display, refresh-rate, nchunks, real-time, performance]
source_refs: []
related: ["[[real-time-constraints]]", "[[zoom-fft]]", "[[display-subsystem]]", "[[dsp-chain]]", "[[main-loop]]", "[[rapid-tune-mute-freeze]]"]
---

# Spectrum Refresh Rate Floor & Optimization

A recorded investigation (2026-05) into why the spectrum/waterfall couldn't be sped up by
tweaking `SPECTRUM_REFRESH_MS`, and what actually moved the frame rate. Source: the full study
in `code/docs/refresh_rate_study/` (`Spectrum_Refresh_Rate_Study.md`,
`Frame_Rate_Optimization_Plan.md`).

## The floor — why `SPECTRUM_REFRESH_MS` is a weak lever
`ShowSpectrum()` draws the trace in `NCHUNKS` column-groups, **one chunk per `loop()`**, and
each `loop()` is paced by the audio/DSP block rate (`T_loop ≈ 10.67 ms`,
[[real-time-constraints]]). So the realized redraw period is always
**`≈ NCHUNKS × T_loop`** — an integer multiple of the loop period.

- At the original `NCHUNKS = 8`: floor ≈ `8 × 10.67 ≈ 85 ms` (**11.7 fps**).
- `SPECTRUM_REFRESH_MS` only re-arms the redraw on a free-running grid; **below the floor it
  can't help**, and the realized rate is **non-monotonic** — beating between that grid and the
  loop cadence made 65–72 ms collapse to ~149 ms (6.7 fps), *worse* than 100 ms. (Pre-ship sweet
  spot was 75–90 ms; 80 ms was used.)

## What actually moved it — cut the fixed per-frame draw cost
`NCHUNKS` only *spreads* a **fixed ~57 ms/frame draw cost** across loops; you can't safely lower
it until that cost is cut, because each `loop()` must still finish its audio block in 10.67 ms.
Lowering `NCHUNKS` below 8 on the original code **broke the receiver** (IQ-queue overflow, SNR
negative). The dominant costs were the full-frame BTE completion blit (~13.8 ms busy-wait), the
bandwidth-bar `fillRect`, the 512 `drawLine`s, and Pass-2 (audio-spectrum bars + S-meter).

## SHIPPED outcome (2026-05-30): 11.7 → 18.8 fps
On branch `refresh_rate_update`, final config in `MainBoard_DisplayHome.cpp`:
`NCHUNKS = 5`, `SPECTRUM_REFRESH_MS = 50`, `AUDIO_SPECTRUM_DECIMATE = 2`, `WATERFALL_DECIMATE = 2`.
Three changes made `NCHUNKS = 5` safe:
1. **Generalized chunk arithmetic** to rounded boundaries so any `NCHUNKS` (incl. non-divisors of
   512 like 5/6/7) sums to 512.
2. **Throttled Pass-2** (audio-spectrum bars + S-meter, ~13 ms) to every 2nd frame.
3. **Throttled the waterfall scroll BTE** to every 2nd frame (phase-offset from the audio pass).

Trade-off: audio-spectrum, S-meter, and waterfall update at ~9.4 fps (half the trace rate), and
trace cadence is jittery (~43–63 ms). Reviewed live and approved. (Smoother 15.6 fps alternative:
`NCHUNKS = 6`, `WATERFALL_DECIMATE = 1`. Full revert: `NCHUNKS = 8`, both decimates = 1.)

## Ruled out / remaining headroom
- **Teensy framebuffer + `writeRect`** — ruled out: the RA8875 is on **SPI**, so a full-frame
  push is ~40 ms (*slower* than the current chip-accelerated `drawLine` path). A test froze the
  panel.
- **`NCHUNKS < 5`** — not reachable without a faster display bus or drawing *less* (e.g. 256
  bins).
- **Real next unlock:** make the BTE blits async (drop the `while(tft.readStatus())` busy-wait)
  — but PJRC `RA8875::BTE_move` busy-waits internally, so this needs library surgery (high risk).
  Hardware vertical-scroll registers for the waterfall would enable `NCHUNKS ≈ 4` (~23 fps).

## Rule of thumb
> When asked to speed up the spectrum/waterfall, target **`NCHUNKS` and the per-frame draw
> cost**, not `SPECTRUM_REFRESH_MS` — and warn that lowering the refresh define can *backfire*
> via beating, and that lowering `NCHUNKS` without first cutting draw cost breaks the receiver.

See [[real-time-constraints]] for the loop budget and [[zoom-fft]] for how the PSD is produced.
