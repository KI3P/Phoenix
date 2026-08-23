---
title: Noise Reduction Algorithms
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-14
tags: [noise-reduction, nr, spectral-subtraction, lms, kim, ephraim-malah, auto-notch]
source_refs: []
related: ["[[theory-overview]]", "[[agc-design]]", "[[dsp-chain]]", "[[fast-convolution-filtering]]"]
---

# Noise Reduction Algorithms

Phoenix offers **three** selectable noise-reduction algorithms plus off, dispatched by
`NoiseReduction()` (`DSP.cpp:672`) on `ED.nrOptionSelect`. They span two completely
different families: **frequency-domain spectral gain** (Kim, Spectral) and a
**time-domain adaptive filter** (LMS). Implementations live in `DSP_Noise.cpp`. In the RX
chain NR runs on the **demodulated audio**, i.e. **after** AGC and demodulation and the band
EQ: `ConvolutionFilter → AGC → Demodulate → BandEQ → NoiseReduction` (`DSP.cpp:911-927`).

| Option | Function | Family | Line |
|---|---|---|---|
| `NROff` | — | none | — |
| `NRKim` | `Kim1_NR()` | spectral subtraction (Kim & Ruwisch style), ×30 post-scale | `DSP.cpp:677` |
| `NRSpectral` | `SpectralNoiseReduction()` | decision-directed MMSE (Ephraim-Malah) | `DSP.cpp:682` |
| `NRLMS` | `Xanr()` | LMS adaptive filter / auto-notch | `DSP.cpp:685` |

## 1. Spectral-gain NR *(general DSP)*

Both `NRKim` and `NRSpectral` work the same way at a high level: take a short-time FFT of the
audio, **estimate the noise floor per frequency bin**, then apply a **real gain `0…1` per
bin** that attenuates bins dominated by noise and passes bins dominated by signal, then
inverse-FFT. They differ in how the gain is computed.

### Kim1_NR (`DSP_Noise.cpp:134`)
- **256-point FFT** (`NR_FFT_L = 256`), **128-sample frame step** → 50% overlap, **Hann
  window** (`line 192-194`), processed two half-blocks per call (`for k in 0..1`).
- Runs at the **24 kHz decimated RX rate** (raw 192 kHz ÷ DF1·DF2 = 4·2 = 8; `SDT.h:409-410`),
  so the **bin bandwidth is 24000/256 = 93.75 Hz**. The in-code comment
  `// bin BW is 46.9Hz [12000Hz / 256 bins] @96kHz` (`DSP_Noise.cpp:141,155`) is a **stale
  DD4WH heritage artifact** — those are the *old* Convolution-SDR numbers (96 kHz input,
  12 kHz NR rate). The running code is rate-agnostic: it divides by the **live**
  `data->sampleRate_Hz / NR_FFT_L` (`line 155-156`), so it is correct at 24 kHz despite the
  comment. See [[code-heritage]].
- A **VAD (voice-activity) bin range** is computed from the band's `FLoCut/FHiCut`
  (`line 143-172`) so the gain is only shaped across the actual channel passband — bins
  outside the audio filter are left alone.
- Smoothing/aggressiveness constants: `NR_alpha = 0.95`, `NR_KIM_K = 1.0`, `NR_PSI = 3.0`
  (`DSP_Noise.cpp:43-44`).
- The DSP applies a **×30 gain** to the result afterward (`DSP.cpp:678-679`) to make up the
  algorithm's level loss.

### SpectralNoiseReduction (`DSP_Noise.cpp:387`)
The sophisticated one. Its own comment cites the literature: *"Based on Romanin et al. 2009,
Schmitt et al. 2002, and Gerkmann & Hendriks 2002"* (`line 361`). That is the
**decision-directed MMSE** lineage:
- **A-posteriori and a-priori SNR** per bin (`NR_SNR_post`, `NR_SNR_prio`), the a-priori SNR
  smoothed by the decision-directed rule (`NR_alpha`).
- **Minimum-statistics / Gerkmann-Hendriks noise estimation** (`NR_Nest`).
- Per-bin gain `NR_G` with hysteresis (`NR_Hk_old`, `NR_Gts`).
This is the same family as the wdsp/DD4WH "spectral NR" and gives the least musical-noise of
the three. *(general DSP)* The classic failure mode of naive spectral subtraction is
**musical noise** — isolated random surviving bins that warble; the MMSE/decision-directed
approach exists specifically to suppress it.

## 2. LMS adaptive filter — `Xanr()` (`DSP_Noise.cpp:302`)

A single structure that does **both** noise reduction and auto-notch, selected by the
`ANR_notch` argument. It's an **adaptive line enhancer (ALE)**: predict the current sample
from a **decorrelation-delayed** copy of the signal using a 64-tap LMS filter.

```c
y     = Σ w[j]·d[idx];     // adaptive prediction  (idx = in + j + ANR_delay)
error = d - y;             // what the predictor could NOT predict
data->Q[i] = ANR_notch ? error   // NOTCH: keep the unpredictable part → tone removed
                       : y;      // NR:    keep the predictable part   → tonal/coherent kept
```

*(general DSP)* Over a decorrelation delay, **narrowband (tone-like)** components stay
correlated and are predictable; **broadband noise** is not. So `y` (prediction) isolates
tones and `error` (residual) removes them — hence the same engine is **auto-notch** for
heterodyne whistles/carriers (mode 1) and a coherent-signal enhancer (mode 0). It appears
**twice** in the RX chain: as `Xanr(data, 0)` inside `NoiseReduction()` (NR mode,
`DSP.cpp:685`), and as `Xanr(&data, 1)` (notch) at `DSP.cpp:930`, gated by `ED.ANR_notchOn`,
each followed by a copy of the result `Q → I` (see I/O contract below).

### I/O contract (important, and a little surprising)
`Xanr` **reads `data->I`, writes its output to `data->Q`, and never modifies `data->I`**. But
everything downstream (`InterpolateReceiveData`, `DSP.cpp:700`) treats **`I`** as the primary
channel. So each `Xanr` call is followed by a `Q → I` copy that also folds in a makeup gain:
- NR mode: `arm_scale_f32(data->Q, 2, data->I, N)` (`DSP.cpp:687`) — 2× makeup, Q→I.
- Notch: `arm_copy_f32(data.Q, data.I, N)` (`DSP.cpp:932`) — plain Q→I.

This *looks* like a `Q`/`I` mix-up but is correct (verified 2026-06-08): without it the
denoised signal in `Q` would be discarded. The commented-out `//arm_scale_f32(data->I, 1.5,
data->I,…)` next to line 687 is the buggy in-place variant they abandoned — it would have
scaled the *un-denoised* `I`. Treat the "Xanr outputs to Q" convention as a documented
gotcha.

Parameters (`DSP_Noise.cpp:51-59`): `ANR_taps = 64`, `ANR_delay = 16`, step
`ANR_two_mu = 1e-4`, delay-line `ANR_DLINE_SIZE = 512`. The update is a **leaky normalized
LMS**: `w[j] = c0·w[j] + c1·d[idx]` with `c0 = 1 − 2µ·ngamma` (adaptive leakage) and
`c1 = 2µ·error/σ` (normalization by input energy `σ`, `line 345-350`). The leakage `ngamma`
is driven by an adaptive index `ANR_lidx` clamped to 120…200 (`line 337-343`) — this is the
wdsp "leak" control that trades convergence speed against stability.

## Robustness notes captured in the code
- **NaN latch hazard.** `Xanr` sanitizes each input sample (`line 311-314`): a single
  non-finite sample would otherwise be baked **permanently** into the adaptive weights and
  never wash out. The spectral buffers are `DMAMEM` and `CLEAR_VAR`'d at init
  (`DSP_Noise.cpp:112`) — the same cold-boot uninitialized-DMAMEM class of bug noted for the
  convolution overlap buffers in [[fast-convolution-filtering]] (and fixed in the
  "NR audio dropout after cold power cycle" change).

## Open questions
- Where `ED.ANR_notchOn` and the manual-vs-auto notch choice are set in the UI.

_Resolved 2026-06-08: the `DSP.cpp:687` `Q→I` 2× scale is correct, not a typo (see I/O
contract above); auto-notch is `Xanr(&data,1)` at `DSP.cpp:930`._
_Resolved 2026-06-14: NR runs at the **24 kHz** decimated rate → 93.75 Hz/bin (256-pt FFT).
The `12000Hz … @96kHz` bin-BW comment is stale DD4WH text; the code uses the live sample rate
(`data->sampleRate_Hz / NR_FFT_L`) so it is correct regardless of the comment._
