---
title: Rapid-Tune Audio Mute & Spectrum Freeze
type: decision
status: draft
created: 2026-06-16
updated: 2026-06-16
tags: [tuning, encoders, spectrum, waterfall, audio, mute, real-time, display, compile-option]
source_refs: []
related: ["[[front-panel]]", "[[tune-frequency-control]]", "[[main-loop]]", "[[display-subsystem]]", "[[spectrum-refresh-floor]]", "[[dsp-chain]]", "[[real-time-constraints]]"]
---

# Rapid-Tune Audio Mute & Spectrum Freeze

A feature on the `encoder-i2c-speed` branch that hides the artifacts produced when the
**Center Tune** or **Fine Tune** encoders are spun *fast*. Guarded by a compile-time option;
when enabled it **mutes the output audio** and **freezes/decouples the spectrum redraw** for the
duration of a fast spin, then resumes automatically.

## Why it exists
The two-tier encoder servicing in [[front-panel]] (the whole point of the `encoder-i2c-speed`
branch) made the encoders so responsive that a fast spin now enqueues a tune event on nearly
every `loop()` iteration. Each consumed Center-Tune event reprograms the Si5351 VFO and
re-centers the spectrum ([[tune-frequency-control]]), so at speed the operator heard **glitchy
audio** and saw a **jerky, half-drawn spectrum sweep**. This feature masks both without changing
the underlying per-event tuning (we still reprogram the VFO every event — we just hide the
side-effects).

## Compile-time option (`Config.h`)
```c
#define MUTE_ON_RAPID_TUNE              // comment out to disable the whole feature
#define RAPID_TUNE_ENGAGE_MS   60       // inter-event gap below this = "spinning"
#define RAPID_TUNE_RELEASE_MS  180      // no tune event for this long = "done spinning"
```
(`Config.h:51-53`.) With the define removed, all the detection/mute/freeze hooks compile out to
no-ops and the behavior is exactly as before.

## Detection — `Loop.cpp`
The detector is driven from **event consumption**, not the ISR. Each of the four HOME-state tune
cases in `ConsumeInterrupt()` calls `NoteTuneActivity(fineTune)` — `false` for the two
`iCENTERTUNE_*` cases, `true` for the two `iFINETUNE_*` cases (`Loop.cpp:1123-1146`).

- **`NoteTuneActivity(bool fineTune)`** (`Loop.cpp:372`) — if the gap since the previous tune
  event is `< RAPID_TUNE_ENGAGE_MS`, latch `rapidTuning = true`; also records which encoder
  drove it (`lastTuneWasFine`).
- **`IsRapidTuning()`** (`Loop.cpp:381`) — returns the latch, auto-clearing it once no tune
  event has arrived for `RAPID_TUNE_RELEASE_MS`. The idle subtraction is done in **`uint32_t`**
  so it wraps like the Teensy `millis()` (the host test mock returns a wider type — without the
  cast the latch cleared immediately under test).
- **`IsRapidCenterTuning()`** (`Loop.cpp:390`) = `IsRapidTuning() && !lastTuneWasFine`.
- **`IsRapidFineTuning()`** (`Loop.cpp:396`) = `IsRapidTuning() && lastTuneWasFine`.

Detection keys off consumption timing because one event is consumed per `loop()` while the FIFO
backs up during a spin (the loop runs every few ms), which is more than fast enough to measure
the gap. Declared in `Loop.h`; consumed by [[dsp-chain]] (mute) and [[display-subsystem]]
(freeze).

## Audio mute — `DSP.cpp`
`AdjustVolume()` (`DSP.cpp:721`), the final output-gain stage before `PlayBuffer()`, scales the
block by `0.0f` instead of the volume amplification while `IsRapidTuning()` is true
(`DSP.cpp:727`) — for **both** encoders. The spectrum PSD is computed upstream of this stage, so
muting the audio does **not** blank the spectrum trace.

## Spectrum behavior — `MainBoard_DisplayHome.cpp`
The two encoders are treated differently because they affect the spectrum differently
([[tune-frequency-control]]): **Center Tune reprograms the VFO and re-centers** the spectrum,
while **Fine Tune leaves the center fixed** and only slides the tuning marker (`AdjustFineTune()`
never touches `centerFreq_Hz` or calls `UpdateRFHardwareState`).

### Center Tune → full freeze
`DrawSpectrumPane()` early-returns while `IsRapidCenterTuning()`, holding the last published
trace + waterfall on screen and marking the pane stale so it gets one clean repaint on release
(`MainBoard_DisplayHome.cpp:857`).

### Fine Tune → frozen trace, *moving* tuning bar
The trace/waterfall are held frozen, but the **blue filter-bandwidth bar + cyan center marker**
keep tracking so the operator can see where they are tuning within the held spectrum
(`MainBoard_DisplayHome.cpp:868`). To make this responsive (the first attempt — throttled to the
50 ms refresh and repainting the whole ~512-line trace each update — was visibly laggy), the
redraw is **decoupled from `SPECTRUM_REFRESH_MS`** and made cheap:

1. **`StampTuningBar()`** (`MainBoard_DisplayHome.cpp:448`) — refactored out of
   `DrawBandWidthIndicatorBar()`; draws the filter highlight (`FILTER_WIN`) + cyan `drawFastVLine`
   marker at the current `GetTXRXFreq()` on whatever layer is selected, with no clear/layer
   switch.
2. **`BuildFrozenBackdrop()`** (`MainBoard_DisplayHome.cpp:802`) — once per fine-tune episode,
   paints a clean **bar-less** trace onto the **L2** back-buffer from `pixelold[]` (the
   per-column y values of the last completed sweep).
3. **`RedrawFrozenSpectrumWithBar()`** (`MainBoard_DisplayHome.cpp:822`) — per update, one
   hardware **`BTE_move` (L2→L1)** restores the clean trace (erasing the previous bar), then
   `StampTuningBar()` stamps the new bar on L1.

The pane redraws on **frequency change**, not on the refresh timer — each update is a single blit
plus a bar stamp, so the bar tracks the knob with no perceptible lag. A consequence of the
blit-then-stamp order is that the moving filter-highlight box is drawn **on top of** the frozen
trace (the trace is hidden inside the passband box while fine-tuning), rather than the normal
trace-over-highlight compositing.

### Release
When the latch clears, `PaneSpectrum.stale` (set throughout the freeze) forces a clean overlay
repaint and normal `ShowSpectrum()` sweeping resumes; the held L2 backdrop is dropped
(`fineFreezeBackdropReady = false`, `MainBoard_DisplayHome.cpp:859,878`).

## Relationship to the refresh-rate work
This is the deliberate **inverse** of [[spectrum-refresh-floor]]: there the goal was to *speed up*
the sweep; here we intentionally *pause* it during a transient (fast tuning) and replace it with a
cheap marker overlay. Both share the lesson that the expensive parts are the per-frame `drawLine`s
and the `BTE` busy-wait — which is exactly why the fine-tune fast path avoids repainting the trace
and does a single blit instead.

## Tests
`code/test/Loop_test.cpp` (host build, `#ifdef MUTE_ON_RAPID_TUNE`) covers engage on fast events,
release after idle, slow tuning not engaging, center-vs-fine classification, and switching
encoders mid-spin (6 cases). The display fast-path is exercised on hardware, not unit-tested.

## Open questions
- Bench-tune the thresholds (`RAPID_TUNE_ENGAGE_MS` / `RAPID_TUNE_RELEASE_MS`) against real
  encoder feel, and the perceived smoothness of the fine-tune bar at high spin rates.
- Whether muting audio during *Fine* tune is desirable (Fine tune is exactly when you zero-beat a
  signal); currently both encoders mute.
- Optional: composite the fine-tune bar **behind** the trace (trace-over-highlight) without
  giving up the single-blit cost.
