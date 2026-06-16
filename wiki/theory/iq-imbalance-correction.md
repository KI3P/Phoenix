---
title: I/Q Imbalance Correction & Image Rejection
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-14
tags: [iq, imbalance, image-rejection, calibration, amplitude, phase]
source_refs: []
related: ["[[theory-overview]]", "[[iq-quadrature-sampling]]", "[[ssb-phasing-method]]", "[[hardware-state-machine]]", "[[dsp-chain]]", "[[persistent-config]]"]
---

# I/Q Imbalance Correction & Image Rejection

Quadrature SDR assumes I and Q have **equal gain** and are **exactly 90° apart**. Real
mixers and codecs aren't perfect, so a fraction of the *unwanted* sideband leaks through as
an **image** — a mirror-frequency ghost. Since Phoenix selects sidebands purely by phase
cancellation ([[ssb-phasing-method]]), image suppression *is* sideband suppression, and it
lives or dies on how accurately I/Q is balanced. Phoenix corrects the residual error in
software, per band.

## Why imbalance creates an image *(general DSP)*

Write the complex baseband ([[iq-quadrature-sampling]]) as `z = I + jQ`. With a gain error
`g` on Q and a phase error `φ`, the measured signal is a mix of the true signal `z` and its
conjugate `z*`. The conjugate term is the **image**: a tone meant to sit at `+f` also appears
at `−f`. The leakage power relative to the wanted signal — the **image-rejection ratio
(IRR)** — depends on both errors. Rules of thumb:

| Want IRR | Need amplitude error ≲ | Need phase error ≲ |
|---|---|---|
| ~40 dB | ~1 % (0.09 dB) | ~1° |
| ~50 dB | ~0.3 % | ~0.3° |
| ~60 dB | ~0.1 % | ~0.1° |

Either error alone caps the IRR, so both must be corrected. The errors drift with
**frequency** (mixer/codec response), which is why Phoenix calibrates **per band**.

## The correction model in code

`ApplyIQCorrection()` (`DSP.cpp:206`) is deliberately a *simplified single-axis* model — its
own comment admits "to be honest: we only correct the amplitude of the I channel":

```c
arm_scale_f32(data->I, amp_factor, data->I, data->N);   // 1) amplitude: scale I only
IQPhaseCorrection(data->I, data->Q, phs_factor, data->N); // 2) phase: cross-mix channels
```

`IQPhaseCorrection()` (`DSP.cpp:186`) applies a small-angle **shear** — adding a fraction of
one channel into the other rotates the relative phase:
```c
if (factor < 0)  Q += factor * I;   // mix a bit of I into Q
else             I += factor * Q;   // mix a bit of Q into I
```
So `amp_factor` (nominal **1.0**) trims gain, `phs_factor` (nominal **0.0**) trims phase. For
small errors this is a good linear approximation of the exact 2×2 correction matrix.

Applied early in the chain, right after RF gain and before decimation/filtering
(`ReceiveProcessing`, `DSP.cpp:787`).

## Per-band storage

Four arrays in the `ED` struct ([[persistent-config]], `SDT.h:302-305`), one slot per band:
- **RX**: `IQAmpCorrectionFactor[]` (init 1), `IQPhaseCorrectionFactor[]` (init 0)
- **TX**: `IQXAmpCorrectionFactor[]`, `IQXPhaseCorrectionFactor[]`

They survive power cycles and are restored at startup.

## How calibration estimates the factors

The RX routine (`HardwareSm_ReceiveIQCalibration.cpp`, driven by `ReceiveIQCalSm`
([[calibration-state-machines]]) in
[[hardware-state-machine]]) sets the **RX LO to the band center** and injects a **CW test
tone exactly `SampleRate/4` above it** via the cal loopback:
```c
SetRXVFOFrequency( band_center*100 );                       // HardwareSm.cpp:609 — LO = band center
SetCWVFOFrequency( (band_center + SR[SampleRate].rate/4)*100 );  // line 613 — tone = LO + Fs/4
```
With the default raw rate `SampleRate = SAMPLE_RATE_192K` (`Globals.cpp:106`,
`SR[].rate = 192000`), `Fs/4 = 48 kHz` — **this is what "48 kHz above/below the LO" in the
code comment means**; it is not arbitrary, it is the [[iq-quadrature-sampling]] Fs/4 offset.

The measurement uses the **512-bin spectrum-display FFT** (`psdnew[SPECTRUM_RES]`,
`SPECTRUM_RES = 512`), *not* the 24 kHz baseband. Cal forces **`spectrum_zoom = 0` (1×)**
(`HardwareSm.cpp:498`) so the FFT spans the full raw **192 kHz** → bin width = 192000/512 =
**375 Hz**, and the display is fftshifted with **bin 256 = LO** (confirmed by the TX routine
reading the carrier at `psdnew[256]`). The tone at LO+Fs/4 therefore lands at
bin 256 + 48000/375 = **bin 384** (the *wanted* product); its conjugate image mirrors to
bin 256 − 128 = **bin 128**. The objective maximized is the **sideband separation**:
```c
float32_t upper = psdnew[384];   // wanted tone (LO + Fs/4)   — line 195
float32_t lower = psdnew[128];   // image       (LO − Fs/4)   — line 196
sideband_separation = (upper - lower) * 10;                  // line 197
```
The `×10` converts the metric to **decibels**: `psdnew` already holds a log10 magnitude
(`DSP_FFT.cpp:144,186` write `log10f_fast(...)`), so `(upper − lower)` is a base-10
log *ratio* and ×10 turns it into dB (power-style 10·log₁₀). Maximizing it drives the image
(bin 128) down relative to the wanted tone (bin 384).
It then runs a **3-pass coordinate search** (`AdjustRXIQCalSetting`, line 142), alternating
amplitude (even iterations) and phase (odd):

| Pass | Iterations | Step `Delta` | Range |
|---|---|---|---|
| Coarse | 0 (amp), 1 (phase) | 0.02 / 0.01 | full sweep |
| Medium | 2, 3 | 0.01 / 0.01 | ± a few steps about the coarse optimum |
| Fine | 4, 5 | 0.001 / 0.001 | ± steps about the medium optimum |

Each pass **re-centers** on the previous optimum (`center[nextIndex] = maxSBS_parameter`,
line 154) and shrinks the step — a classic narrowing grid/coordinate descent. During the
search, measurements are IIR-smoothed (0.5/0.5) to fight noise; a final un-smoothed
measurement is taken at the optimum (line 198-204). The machine **auto-advances through all
bands** (`AdjustRXIQBand`, line 112), ~60 ms acquisition per point (line 98).

The TX routine (`HardwareSm_TransmitIQCalibration.cpp`) is structurally the same, maximizing
sideband separation and writing the `IQX*` arrays.

## Distinct from carrier null

Image (sideband) correction is **not** the same as **carrier null / LO leakage**, which is a
DC-offset / leakage problem handled separately by `TransmitCarrierCalSm`
([[hardware-state-machine]]). Image = conjugate term; carrier = additive DC/LO feedthrough.

## Open questions
- Whether the single-axis (I-only amplitude) model leaves a residual vs. a full 2×2 matrix,
  and the achieved IRR per band (needs hardware measurement).

## Resolved
- **Tone offset = Fs/4, not "~6 kHz".** The tone is placed `SR[SampleRate].rate/4` above the
  band-center LO = 48 kHz at the default 192 kHz raw rate (`HardwareSm.cpp:613`). The earlier
  "bins 128/384 at 24 kHz ⇒ ±6 kHz" reasoning was wrong: the PSD here is the 192 kHz raw,
  1×-zoom 512-bin display FFT (375 Hz/bin, bin 256 = LO), so ±Fs/4 = ±128 bins = bins 128/384.
  The code comment is correct. *(resolved 2026-06-14)*
- **`×10` and `psdnew` units.** `psdnew` is `log10f_fast(magnitude)` (`DSP_FFT.cpp:144,186`),
  so the bin difference is a log₁₀ ratio; ×10 expresses sideband separation in **dB**.
  *(resolved 2026-06-14)*
- **Wanted/image bin direction.** Wanted product = **bin 384** (tone is Fs/4 *above* the LO),
  image = **bin 128**; `(upper−lower)` is maximized to suppress the image. *(resolved 2026-06-14)*
