---
title: Multirate Decimation & Interpolation
type: concept
status: draft
created: 2026-06-08
updated: 2026-07-29
tags: [decimation, interpolation, multirate, polyphase, fir, sample-rate]
source_refs: []
related: ["[[theory-overview]]", "[[fast-convolution-filtering]]", "[[iq-quadrature-sampling]]", "[[dsp-chain]]", "[[real-time-constraints]]", "[[sample-rate-switching]]", "[[runtime-filter-design]]", "[[tx-filter-hil-test]]"]
---

# Multirate Decimation & Interpolation

Phoenix digitizes at **192 or 176.4 kHz** ([[sample-rate-switching]]) but does its channel
filtering at a much lower rate, then
brings audio back up to the output rate. Working at the lower rate is what makes the sharp
SSB/CW channel filter affordable: a narrow filter needs a transition band that is a *large
fraction* of Nyquist, which costs few taps at low fs and many at high fs.

## Decimation = anti-alias filter + downsample *(general DSP)*

Dropping the sample rate by `M` folds everything above the new Nyquist (`fs/M / 2`) back into
band. So you must **low-pass first** to kill anything that would alias onto the signal of
interest, *then* keep every `M`-th sample. Interpolation is the mirror image: insert `M−1`
zeros (which creates spectral **images**) then low-pass to remove them.

CMSIS's `arm_fir_decimate_f32` / `arm_fir_interpolate_f32` are **polyphase**: they compute
only the samples that survive, so cost ∝ `numTaps × outputRate`, not input rate.

## What Phoenix does

| Path | Stages | Net | Result @192k | @176.4k | Where |
|---|---|---|---|---|---|
| **RX** | ÷4 then ÷2 | **÷8** | 192 → 24 kHz | 176.4 → 22.05 kHz | `DecimateRxStage1/2`, `SDT.h:407-411` |
| RX out | ×2 then ×4 | ×8 | 24 → 192 kHz | 22.05 → 176.4 kHz | `FIR_int1/2`, `InterpolateReceiveData` `DSP.cpp:697` |
| **TX** | ÷4·÷2·÷2 | **÷16** | 192 → 12 kHz | 176.4 → 11.025 kHz | `TransmitFilterConfig`, `SDT.h:537-553` |
| TX out | ×2·×2·×4 | ×16 | 12 → 192 kHz | 11.025 → 176.4 kHz | same struct |

RX decimation filters are **designed at runtime** (`InitializeDecimationFilter`,
`DSP_FIR.cpp:1284`) to a 9 kHz passband and **90 dB** stopband (`SDT.h:412-413`). TX instead
uses fixed precomputed 48-tap coefficient sets (`coeffs192K_10K_LPF_FIR`, etc.,
`SDT.h:528-533`, `DSP_FFT.cpp:425-442`) — an asymmetry explained below.

### Why RX designs taps at runtime but TX uses fixed tables
This is **primarily heritage / an incremental refactor, not a deliberate determinism or
latency choice.** The evidence:

- **The fixed tables are legacy, and shared.** The arrays are named and commented for the
  *receive* path — `// 48 Tap Kaiser 192KHz 8KHz Fc RXfilters Dec and Interpolation`
  (`DSP_FIR.cpp:174-175`) — yet they are consumed by the **TX** chain
  (`coeffs192K_10K_LPF_FIR` at `DSP_FFT.cpp:425`, etc.). They are the original DD4WH
  Convolution-SDR coefficient sets ([[code-heritage]]). The RX *decimation* path was later
  re-parametrized into `InitializeDecimationFilter` (harris tap-count + Kaiser design,
  heap-allocated `malloc`, `DSP_FIR.cpp:1295-1300`); the TX path was built by **reusing the
  legacy fixed tables wholesale**.

- **The runtime path's structural benefit — rate independence — is now live.** ⚠️ *Corrected
  2026-07-29:* this section previously said the benefit was "dormant" because `SampleRate` was
  "set once to `SAMPLE_RATE_192K` and **never reassigned** anywhere in the firmware". **That is
  no longer true.** `SampleRate` is a reference to the persisted `ED.sampleRate`
  (`Globals.cpp:106-109`) and is reassigned by `ChangeSampleRate()`
  (`MainBoard_AudioIO.cpp:519`) from a menu or over CAT — see [[sample-rate-switching]].
  `InitializeDecimationFilter` derives both `n_dec_taps` and the coefficients from
  `SR[SampleRate].rate`, so RX genuinely does retune, and the whole DSP chain is rebuilt on
  each change. `InitializeDecimationFilter` now also **frees its previous allocations**, which
  it did not need to do while this path ran once per boot.

- **Net trade-off as it stands:** TX = no heap, fully deterministic; RX = small heap
  allocations, recomputed on every rate change rather than to a constant result.

**Nuance:** "RX designs at runtime, TX is fixed" over-simplifies, and after
[[runtime-filter-design]] it is the wrong axis entirely. RX is itself a hybrid — only RX
*decimation* is fully runtime (harris-sized); RX *interpolation* uses **fixed tap counts (48 and
32)** with coefficients still computed at runtime by `CalcFIRCoeffs` (`DSP_FFT.cpp:401-403`).

The framing that actually predicts behaviour is **how a stage's corner is specified**:

| Specification | Behaviour under a rate change | Action taken |
|---|---|---|
| **Fraction of Fs** — decimation, interpolation, Hilbert, [[zoom-fft]] | corner scales with Fs, which is *correct* for anti-alias/anti-image | left alone |
| **Absolute Hz** — CW audio, equaliser cells, CW decoder FIR, the two TX stages around the Hilbert | corner drifts by the rate ratio, which is a **bug** | regenerated per rate ([[runtime-filter-design]]) |

⚠️ "Regenerated per rate" does not mean "identical at every rate". The two Hz-specified **TX**
stages are 48-tap FIRs, and a fixed tap count cannot place a corner exactly when the normalised
cutoff moves — they hold to **≈1.2 %**, against 0.3 % for the IIR receive stages. See
[[runtime-filter-design]].

### Where the TX fold point lands, per rate

`TXDecimateBy2Again` is the stage whose stopband matters most, because it halves the audio rate
and so folds everything above a quarter of it back into the transmitted audio:

| Fs | Audio rate | Output Nyquist (fold point) | An 8 kHz mic tone reappears at |
|---|---|---|---|
| 192 ksps | 24 kHz | **6 kHz** | 4000 Hz |
| 176.4 ksps | 22.05 kHz | **5512 Hz** | 3025 Hz |

The fold frequency scales with Fs, so a bench test has to recompute where to look at each rate —
which is what [[tx-filter-hil-test]] does. The pre-2026-07 table was flat to 0.425·Fs, i.e. it
did not attenuate that region at all.

## The filter-length formula (and why two stages)

`InitializeDecimationFilter` sizes each filter with the fred-harris estimate *(general DSP)*:
```c
n_fpass  = BW / fs;                       // normalized passband edge
n_fstop  = (fs/M - BW) / fs;              // normalized stopband edge (protect BW past new Nyquist)
n_dec_taps = 1 + att_dB / (22 * (n_fstop - n_fpass));   // N ≈ A_stop / (22·Δf)
```
The tap count is inversely proportional to the **normalized transition width** `Δf`. That is
the whole argument for splitting the rate change:

Working it for Phoenix's RX (BW = 9 kHz, att = 90 dB):

| Approach | M, fs in | Δf (norm.) | ~taps | runs at | cost ∝ taps×outRate |
|---|---|---|---|---|---|
| Stage 1 | 4, 192 kHz | 0.156 | ≈27 | 48 kHz out | ≈1.3 M |
| Stage 2 | 2, 48 kHz | 0.125 | ≈33 | 24 kHz out | ≈0.8 M |
| **Two-stage total** | | | ≈60 | | **≈2.1 M** |
| Single ÷8 | 8, 192 kHz | 0.031 | ≈132 | 24 kHz out | ≈3.2 M |

*(Worked at 192 ksps. The normalized quantities `Δf` and the tap counts are unchanged at
176.4 ksps — BW is absolute so `n_fpass` shifts slightly, but the argument and the ≈5× ratio
hold.)*

A single ÷8 stage needs a far sharper (5×) filter because its transition band is squeezed
against the *final* 12 kHz Nyquist while still running at 192 kHz. Splitting lets the first
stage be lazy (wide transition, the new Nyquist is still 24 kHz) and only the last stage be
sharp — fewer taps, less memory, lower MAC load. *(Tap counts above are computed from the
formula, not read from code; exact values depend on integer rounding.)*

## Implementation conventions
- **State-vector size = `numTaps + blockSize − 1`** (a CMSIS requirement) appears everywhere
  here; see the annotated allocations in `SDT.h:494-508` and `DSP_FIR.cpp:1295-1296`.
- After ÷8, the RX block is 2048/8 = **256 samples** at 24 kHz — exactly the block size the
  [[fast-convolution-filtering]] FFT expects.
- The whole cascade's group delay counts against the ~10 ms loop budget
  ([[real-time-constraints]]).

## Open questions
- Measured group delay of the RX decimate→filter→interpolate round trip.

## Resolved
- **TX fixed taps vs RX runtime harris.** Primarily **heritage**, not a deliberate
  determinism/latency choice: the fixed 48-tap tables are legacy DD4WH coefficients (the arrays
  are even named/commented "RXfilters", `DSP_FIR.cpp:174-175`) reused wholesale by the TX path,
  while RX *decimation* was refactored into the parametrized harris-formula design. RX is
  itself a hybrid (decimation runtime-sized; interpolation fixed 48/32 taps,
  `DSP_FFT.cpp:401-403`). *(resolved 2026-06-14)*
  - ⚠️ **Superseded in part 2026-07-29.** The original resolution added that the rate-independence
    was "dormant — `SampleRate` is fixed at 192 kHz, so RX recomputes the same coeffs every
    boot". `rx-dsp-176k-stage-test` made the rate switchable at run time
    ([[sample-rate-switching]]) and regenerated the Hz-specified filters
    ([[runtime-filter-design]]), so that clause no longer holds. The heritage explanation for
    the *asymmetry* still stands; the "dormant" characterisation does not.
