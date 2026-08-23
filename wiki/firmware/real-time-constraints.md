---
title: Real-Time Constraints & Timing Budget
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-09
tags: [real-time, timing, audio-buffers, loop-budget, spectrum]
source_refs: []
related: ["[[overview]]", "[[main-loop]]", "[[dsp-chain]]", "[[display-subsystem]]"]
---

# Real-Time Constraints & Timing Budget

Phoenix is a soft-real-time system: missing the audio deadline produces audible glitches.
The timing rules (from the `.ino` header) that constrain the whole design:

- **Main loop**: each `loop()` pass must complete in **~10 ms** or the audio buffers
  overflow. `loop()` is `FASTRUN` (RAM-resident) for speed. See [[main-loop]].
- **Spectrum refresh**: the redraw period is `≈ NCHUNKS × T_loop` (the trace is drawn one
  chunk per `loop()`), so `SPECTRUM_REFRESH_MS` is a weak lever and can even *backfire* via
  beating. Cutting the per-frame draw cost let `NCHUNKS` drop from 8 → 5 (11.7 → 18.8 fps,
  shipped 2026-05-30). Full analysis: [[spectrum-refresh-floor]].
- **Display updates**: pane-based with **stale flags** so only changed regions redraw
  ([[display-subsystem]]).
- **CW keyer**: the **1 ms `tick1ms()`** timer guarantees precise dit/dah timing
  independent of loop jitter ([[mode-state-machine]]).
- **State-machine transitions**: deterministic and low-latency by construction
  ([[state-machine-architecture]]).

## Why it matters for changes
Any new work in the loop path (DSP, display, CAT, front-panel polling) spends part of the
10 ms budget. The `encoder-i2c-speed` branch exists because I²C polling of the front panel
can eat into this budget — see [[front-panel]] (the encoder-i2c-speed servicing design).

## To flesh out
- [ ] Measure the actual per-stage time budget (there is a `timing-measurement` skill).
- [ ] Document audio buffer depth and the exact overflow threshold.
- [ ] Reconcile the 200 ms `.ino` figure with the measured spectrum floor.
