---
title: Zoom FFT (Spectrum Display)
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-14
tags: [fft, zoom, spectrum, waterfall, decimation, display, psd]
source_refs: []
related: ["[[theory-overview]]", "[[fast-convolution-filtering]]", "[[multirate-decimation]]", "[[display-subsystem]]", "[[dsp-chain]]", "[[real-time-constraints]]"]
---

# Zoom FFT (Spectrum Display)

The main spectrum/waterfall can **zoom** to show a narrower span at finer resolution without
a bigger FFT. The trick: low-pass and decimate the complex baseband *before* the FFT, so the
same 512 bins cover proportionally less bandwidth. Implementation: `ZoomFFTExe()`
(`DSP_FFT.cpp:581`), set up by `ZoomFFTPrep()` (`DSP_FFT.cpp:546`); output goes to the global
`psdnew` array consumed by the [[display-subsystem]].

## Two different spectra — don't confuse them
- **Band spectrum / waterfall** — `ZoomFFTExe` → `psdnew`. Computed from the **raw 192 kHz
  I/Q** (full RF span), zoomable. *This page.*
- **Audio-passband spectrum** — computed inside `ConvolutionFilter` → `audioYPixel`, always at
  the 24 kHz processing rate (46.875 Hz/bin). See [[fast-convolution-filtering]].

They run at different rates and serve different display panes.

## Why decimate to zoom *(general DSP)*

A 512-point FFT always produces 512 bins; the bin width is `Fsample/512`. To get *finer* bins
without a larger (slower) FFT, **lower the sample rate** feeding the FFT — but first low-pass
to prevent aliasing ([[multirate-decimation]]). Decimating by `2^zoom` divides both the span
and the bin width by `2^zoom`: you see less of the band, in more detail.

## Zoom levels (`DSP_FFT.cpp:562-568`, `SDT.h:138-144`)

`zoom_M = 1 << spectrum_zoom` (`DSP_FFT.cpp:548`):

| `spectrum_zoom` | `zoom_M` | Effective Fsample | PSD bin width |
|---|---|---|---|
| 0 (`ZOOM_1`) | 1 | 192 kHz | **375 Hz** |
| 1 (`ZOOM_2`) | 2 | 96 kHz | 187.5 Hz |
| 2 (`ZOOM_4`) | 4 | 48 kHz | 93.75 Hz |
| 3 (`ZOOM_8`) | 8 | 24 kHz | 46.875 Hz |
| 4 (`ZOOM_16`) | 16 | 12 kHz | 23.4375 Hz |

Note the base (zoom 1) resolution is **375 Hz/bin** = 192000/512, *not* the 46.875 Hz of the
audio FFT — a common point of confusion since both use a 512-point transform.

## How it's implemented

1. **Zoom 1 fast path**: no decimation, `CalcPSD512(I, Q)` directly (`DSP_FFT.cpp:583-586`).
2. **Otherwise**: anti-alias **IIR biquad** low-pass (`biquadZoomI/Q`, 4 stages) then
   `decimate_f32` by `zoom_M` (`DSP_FFT.cpp:591-595`). The code notes it deliberately uses an
   **IIR** filter, not the FIR decimator used in the audio chain — for a *magnitude* PSD
   display, the IIR's non-linear phase doesn't matter, and it's cheaper. Per-zoom cutoff
   coefficients come from `mag_coeffs[spectrum_zoom]` (`DSP_FFT.cpp:552`).
3. **Amplitude compensation**: each decimated sample is scaled by
   `zoomMultiplierCoeff[spectrum_zoom]` (`DSP_FFT.cpp:604`) to keep PSD amplitude stable as
   zoom changes (decimation + filtering otherwise alter the level).
4. **Ring-buffer accumulation for high zoom**: at zoom ≥ 8 a single 2048-sample call yields
   `2048/zoom_M < 512` decimated samples, so they are accumulated into `FFT_ring_buffer_x/y`
   across multiple calls; the PSD is computed only when 512 have collected
   (`DSP_FFT.cpp:605-617`). `ZoomFFTExe` returns `false` on calls where no PSD was produced.

## Timing interaction
Because high zoom needs several loop passes to fill the ring buffer before a PSD appears, the
**effective spectrum update rate slows with zoom** — directly relevant to the
[[spectrum-refresh-floor]] finding (redraw period ≈ `NCHUNKS × T_loop`, governed by `NCHUNKS`
and the per-frame draw cost, not `SPECTRUM_REFRESH_MS`). See [[real-time-constraints]].

## Defaults
Power-on zoom is governed **solely** by `ED.spectrum_zoom`, which defaults to **1** (`ZOOM_2`,
2× → 96 kHz span / 187.5 Hz bins; `SDT.h:274`), unless a saved value is restored from JSON
config (`Storage.cpp:276,501`, `… | ED.spectrum_zoom` fallback).

`Config.h:78 #define ZOOM 3` is **unrelated** — it is the **front-panel button ID** for the
ZOOM button (button function index 3, under the "Front panel button functions" block,
`Config.h:74-96`), consumed as `case ZOOM:` in the event dispatch (`Loop.cpp:457,818`) where
pressing it does `ED.spectrum_zoom++`. It is not a zoom *level*. So there is no real
discrepancy — the earlier TODO conflated a button identifier with the zoom default.

## Open questions
- Confirm how `mag_coeffs` cutoffs are chosen per zoom (passband vs. the new Nyquist).
- Exact relationship between `psdnew` bins and on-screen pixels in the [[display-subsystem]].

## Resolved
- **`Config.h ZOOM 3` vs `ED.spectrum_zoom = 1`.** Not a conflict: `ZOOM` is a button-ID
  macro (`Config.h:74-96`, used in `Loop.cpp:457`), `ED.spectrum_zoom` is the actual zoom
  level (default 1 = 2×). Power-on zoom = 1. *(resolved 2026-06-14)*
