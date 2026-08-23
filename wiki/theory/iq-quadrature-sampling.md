---
title: I/Q Quadrature Sampling & Complex Baseband
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [iq, quadrature, complex-baseband, image, analytic-signal]
source_refs: []
related: ["[[theory-overview]]", "[[ssb-phasing-method]]", "[[iq-imbalance-correction]]", "[[fast-convolution-filtering]]", "[[rf-board]]", "[[dsp-chain]]"]
---

# I/Q Quadrature Sampling & Complex Baseband

The foundation of the whole radio. Phoenix is a **direct-conversion quadrature receiver**:
it mixes the RF signal down to (near) 0 Hz using two local-oscillator copies 90° apart,
producing two real streams **I** (in-phase) and **Q** (quadrature) that together form one
**complex** baseband signal `z[n] = I[n] + jQ[n]`.

## Why complex? Sign of frequency

*(general DSP)* A single real mixer folds frequencies symmetrically about the LO: a signal
at LO+5 kHz and one at LO−5 kHz both land at 5 kHz and become inseparable (the **image**
problem). Sampling in **quadrature** keeps the two apart because the complex exponential
`e^{jωn}` distinguishes positive from negative ω, whereas a real cosine cannot.

Concretely, for a tone at offset `f` from the LO:
- `I[n] = cos(2π f n / fs)`
- `Q[n] = sin(2π f n / fs)`

The **sign of `f`** survives only in the *relative phase* of Q vs. I (Q leads or lags I by
90°). A real-only receiver throws that away; the quadrature receiver keeps it, so signals
above and below the LO occupy **positive vs. negative frequencies** of the complex spectrum
and can be filtered independently. This is exactly what [[ssb-phasing-method]] exploits to
pick one sideband, and what the asymmetric filter mask in
[[fast-convolution-filtering]] does in the frequency domain.

## How Phoenix represents it in code

The unit of work is a `DataBlock` (`SDT.h:365`):
```c
struct DataBlock { uint32_t N; uint32_t sampleRate_Hz; float32_t *I; float32_t *Q; };
```
I and Q are carried as **separate parallel float arrays** throughout the chain (not an
interleaved complex array) until the FFT, where they are interleaved `[re,im,re,im,…]` for
the CMSIS-DSP complex FFT (`ConvolutionFilter`, `DSP_FFT.cpp:707-718`).

- The hardware quadrature LO is the Si5351 on the [[rf-board]]; the codec digitizes I and Q
  at **192 kHz** (`Globals.cpp:106`).
- A block is **2048 samples** (`READ_BUFFER_SIZE`, `SDT.h:175-178`), ≈10.7 ms — matching the
  loop's [[real-time-constraints]].
- After decimation to 24 kHz the notional sampled span is **−12 kHz … +12 kHz** around the
  LO (`DSP_FFT.cpp:737`); the negative half is real RF, not a mathematical artifact.

## The catch: imperfect quadrature

The above assumes I and Q have exactly equal gain and exactly 90° separation. Real mixers
don't — amplitude/phase imbalance lets a fraction of the unwanted sideband leak through as
an **image**. Phoenix corrects this per band before filtering (`ApplyIQCorrection`,
`DSP.h:111`). See [[iq-imbalance-correction]].

## Spectral bookkeeping (worth memorizing)
At 24 kHz over a 512-point FFT, each bin is **24000/512 = 46.875 Hz** (`DSP_FFT.cpp:740`).
Positive frequencies occupy bins 1…129, negative frequencies bins 1023…895
(`DSP_FFT.cpp:743-744`). The display plots DC…6 kHz = ¼ of the span = 128 bins.

## See also
- [[ssb-phasing-method]] — using the sign-of-frequency property to select a sideband.
- [[fast-convolution-filtering]] — the complex frequency-domain filter that does it.
- [[zoom-fft]] — zooming the complex baseband for the spectrum display.
