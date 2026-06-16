---
title: DSP / SDR Theory — Overview
type: overview
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [dsp, sdr, theory, iq, ssb, filtering]
source_refs: []
related: ["[[iq-quadrature-sampling]]", "[[ssb-phasing-method]]", "[[fast-convolution-filtering]]", "[[multirate-decimation]]", "[[iq-imbalance-correction]]", "[[agc-design]]", "[[zoom-fft]]", "[[dsp-chain]]"]
---

# DSP / SDR Theory — Overview

This is the **theory** spine for Phoenix: version-independent signal-processing concepts,
constructed from the codebase plus standard DSP fundamentals. (We do **not** have an
electronic copy of the Purdum & Peter T41 book; nothing here is sourced from it.)

**Convention.** Claims that come from the code are cited as `file:line`. Claims that are
general DSP theory are marked *(general DSP)*. Where the two meet, the code citation pins
the specific choice and the theory explains why it works.

## The signal path, end to end

Phoenix is a **direct-conversion quadrature SDR**. The Si5351 produces a local oscillator at
(near) the operating frequency in two copies 90° apart; mixing the antenna signal against
them yields a complex **I/Q baseband** signal that is digitized and processed entirely in
software. Defaults (`Globals.cpp:106`, `SDT.h`):

| Quantity | Value | Where |
|---|---|---|
| I/O sample rate | **192 kHz** | `SampleRate = SAMPLE_RATE_192K`, `Globals.cpp:106` |
| Block size | **2048** samples (≈10.7 ms) | `READ_BUFFER_SIZE = 128×16`, `SDT.h:175-178` |
| RX decimation | ÷4 then ÷2 = **÷8** → 24 kHz | `ReceiveFilterConfig`, `SDT.h:409-411` |
| TX decimation | ÷4·÷2·÷2 = **÷16** → 12 kHz | `TransmitFilterConfig`, `SDT.h:537-553` |
| FFT length | **512** | `FFT_LENGTH = SPECTRUM_RES`, `SDT.h:145-146` |

This block size is exactly the ~10 ms real-time budget the firmware is built around — see
[[real-time-constraints]].

## Core concepts (each its own page)

1. **[[iq-quadrature-sampling]]** — why complex baseband; how I/Q encodes both magnitude and
   *sign* of frequency offset, enabling a single receiver to separate signals above and
   below the LO. The foundation everything else rests on.
2. **[[ssb-phasing-method]]** — Phoenix demodulates and generates SSB by the **phasing
   (Hilbert)** method, *not* Weaver or the filter method. RX `Demodulate()` and TX's ±45°
   Hilbert FIR pair (`DSP_FFT.cpp:851`).
3. **[[fast-convolution-filtering]]** — the main channel filter is **FFT overlap fast
   convolution** (the "Convolution SDR" namesake). `m_NumTaps = FFT_LENGTH/2+1` (`SDT.h:423`).
4. **[[multirate-decimation]]** — two-stage polyphase decimation/interpolation, and why
   dropping to 24 kHz makes the channel filter cheap.
5. **[[iq-imbalance-correction]]** — per-band amplitude/phase correction for image rejection;
   the theory behind RX/TX I/Q calibration.
6. **[[agc-design]]** — the wdsp-derived AGC (attack/decay, hang).
7. **[[zoom-fft]]** — spectrum-display zoom via IIR decimation before the FFT.
8. **[[noise-reduction]]** — Kim / MMSE-spectral / LMS-notch noise reduction.
9. **[[synchronous-am-detection]]** — SAM: a PLL recovers the carrier for fade-robust AM.
10. **[[audio-equalizer]]** — 14-band parallel filterbank shaping RX and TX audio.

Plus CW receive (filter + adaptive Morse decoder) in [[cw-processing]] (a firmware page).

## How theory maps to firmware

The implementation lives in [[dsp-chain]] (`DSP.cpp`, `DSP_FFT.cpp`, `DSP_FIR.cpp`,
`DSP_Noise.cpp`, `DSP_CWProcessing.cpp`). The RX entry point is `ReceiveProcessing()`
(`DSP.h:63`); TX is `TransmitProcessing()` (`DSP.h:83`).

## Heritage note
The FFT-convolution core descends from DD4WH's Teensy Convolution SDR — see
[[code-heritage]]. The AGC notes "taken from wdsp" (`SDT.h:589`), i.e. Warren Pratt NR0V's
DSP library, a common lineage in this community.
