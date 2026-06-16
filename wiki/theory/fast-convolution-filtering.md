---
title: Fast-Convolution (FFT Overlap) Filtering
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [fft, convolution, overlap-save, filtering, channel-filter]
source_refs: []
related: ["[[theory-overview]]", "[[iq-quadrature-sampling]]", "[[ssb-phasing-method]]", "[[multirate-decimation]]", "[[zoom-fft]]", "[[dsp-chain]]", "[[code-heritage]]"]
---

# Fast-Convolution (FFT Overlap) Filtering

This is the radio's main **channel filter** and the reason the ancestor project was called
the *Teensy Convolution SDR* ([[code-heritage]]). Instead of a long time-domain FIR, Phoenix
filters by **multiplying in the frequency domain** — cheap, and trivially reconfigurable
(change the mask, not the taps).

Implementation: `ConvolutionFilter()` (`DSP_FFT.cpp:685`), mask built by `InitFilterMask()`
/ `UpdateFIRFilterMask()` (`DSP_FFT.cpp:334`).

## Why FFT filtering *(general DSP)*

Convolution in time = multiplication in frequency. Filtering a block with an `N`-tap FIR
costs `O(N·L)` per `L` samples directly, but `O(L log L)` via FFT → multiply → IFFT. For the
sharp, narrow SSB/CW channel filters a radio wants (hundreds of taps), the FFT route wins
and lets the passband shape be an arbitrary, **complex, asymmetric mask** — which is what
makes single-sideband selection fall out for free ([[ssb-phasing-method]]).

The equivalent FIR length here is `m_NumTaps = FFT_LENGTH/2 + 1 = 257` (`SDT.h:423`).

## Overlap-save, as implemented

Phoenix uses **50%-overlap overlap-save** with a 512-point complex FFT over 256-sample
blocks (block size = `READ_BUFFER_SIZE/DF = 2048/8 = 256`, at 24 kHz). Per block
(`DSP_FFT.cpp:705-772`):

1. **Assemble 512-sample buffer**: first half = *previous* block's I/Q
   (`last_sample_buffer_L/R`), second half = current block. The overlap carries the filter's
   memory across block boundaries so there's no edge discontinuity.
2. **Forward FFT**: `FFT512Forward()` in place, interleaved `[re,im,…]`.
3. **Apply mask**: `arm_cmplx_mult_cmplx_f32(buffer, FIR_filter_mask, iFFT_buffer, 512)` —
   one complex multiply per bin. The mask encodes bandwidth, low/high cut, **and** sideband.
4. **(Tap the spectrum)**: the magnitude² of the masked bins feeds the audio spectrum
   display (`DSP_FFT.cpp:745-761`).
5. **Inverse FFT**: `FFT512Reverse()`.
6. **Discard-and-keep**: throw away the first 256 complex samples (the time-aliased part),
   keep the last 256 as clean output (`DSP_FFT.cpp:768-772`). This discard is the defining
   move of overlap-**save**.

## The filter mask = the radio's selectivity

`FIR_filter_mask` is a length-512 **complex** array. Because the baseband is complex
([[iq-quadrature-sampling]]), the mask need not be symmetric about DC, so:
- **Bandwidth / passband** comes from which bins are non-zero (driven by the band's
  `FHiCut_Hz` / `FLoCut_Hz`, `DSP_FFT.cpp:362-364`).
- **Sideband (USB/LSB)** comes from *which half* (positive vs. negative bins) is passed.
- Recomputed only when filter settings change (`UpdateFIRFilterMask`), not per block — the
  efficiency payoff over time-domain variable filters.

## Gotchas captured in the code
- The overlap history buffers `last_sample_buffer_L/R` are `static DMAMEM`, which the Teensy
  4.x does **not** zero at cold boot. Uninitialized NaN/Inf there poisons the first FFT
  frame and the NR feedback state, so they are explicitly `CLEAR_VAR`'d in
  `InitializeFilters()` (`DSP_FFT.cpp:380-387`). This is a real bug that was fixed — see the
  cold-power-cycle NR dropout history.

## Connections
- Runs at the decimated 24 kHz rate — see [[multirate-decimation]] for how we get there and
  back.
- The same FFT machinery, with extra IIR decimation up front, drives the [[zoom-fft]]
  spectrum display.
