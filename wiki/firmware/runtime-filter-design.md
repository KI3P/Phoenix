---
title: Run-Time Filter Design (rate-independent coefficients)
type: decision
status: draft
created: 2026-07-29
updated: 2026-07-29
tags: [dsp, filters, chebyshev, bilinear, prewarp, equalizer, cw, sample-rate, iir, fir]
source_refs: []
related: ["[[sample-rate-switching]]", "[[dsp-chain]]", "[[cw-processing]]", "[[audio-equalizer]]", "[[multirate-decimation]]", "[[filter-hil-test]]", "[[tx-filter-hil-test]]", "[[ssb-phasing-method]]", "[[theory-overview]]"]
---

# Run-Time Filter Design (rate-independent coefficients)

Several audio-rate filter stages shipped as **coefficient tables designed offline for one
sample rate** — 24 ksps, i.e. 192 ksps at the ADC decimated by 8. They are now **generated from
an analog design spec on every rate change**, which is what makes
[[sample-rate-switching]] possible without every labelled frequency moving.

## The bug this fixes

A frozen digital coefficient table encodes frequencies as *fractions of Fs*. Run the same table
at a different Fs and every corner scales with it:

```
176400 / 192000 = 0.91875
```

So at 176.4 ksps:

| Filter | Labelled | Actually was |
|---|---|---|
| CW audio filter | 2.0 kHz | **1.84 kHz** |
| Equaliser top cell | 4000 Hz | **3675 Hz** |

An 8.125 % error on every CW corner and every equaliser centre — not subtle, and exactly the
kind of thing a user would report as "the 2 kHz filter sounds narrow".

## Recovering the prototypes rather than guessing them

The point of pride in this change is that **the filter families are unchanged**: what was a
12-pole Chebyshev is still a 12-pole Chebyshev. The design constants were *recovered* from the
shipped tables, not re-invented:

- **CW audio filters.** The tables measure **0.0200 dB** of passband ripple, and `cheby1(12,
  0.02)` fits them to **0.013 dB RMSE**. That identifies them as Chebyshev type I, whose natural
  design parameter is the ripple edge — hence `CW_AUDIO_RIPPLE_EDGE_HZ[] = {807.1, 1038.0,
  1269.0, 1731.5, 1963.2}` (`DSP_FIR.cpp:1242-1244`), sitting a few percent below the nominal
  840/1080/1320/1800/2000 Hz labels.
- **Equaliser cells.** Every biquad has `b1 = 0` and `b2 = −b0`, which puts its zeros at
  `z = ±1`. That is precisely the bilinear image of an **analog bandpass**, so an *inverse*
  bilinear transform recovers the analog prototype exactly — four bandpass sections per cell,
  stored as (frequency-ratio, Q) pairs in `EQ_BAND_PROTO[14][4]` (`DSP_FIR.cpp:1263-1278`).

`code/tools/extract_filter_prototypes.py` performs that recovery and prints the C literals. It
reads the original tables, which are **kept verbatim as test fixtures** in
`code/test/reference_filters.cpp` — so the thing the generator is checked against is the thing
it replaced.

## Prewarping is the load-bearing part

Designing in the analog domain and bilinear-transforming is not by itself enough. The bilinear
transform **compresses the analog frequency axis** as it wraps it onto the unit circle, so a
section designed at ω lands slightly below ω once discretised — and by a rate-dependent amount.
`PrewarpRadians()` (`DSP_FIR.cpp:998`) cancels that:

```
ω_prewarped = 2·Fs·tan(π·f / Fs)
```

**This is what makes the result rate-independent rather than merely re-derived.** Without it the
corners would still drift with Fs, just by less.

## The generators

| Function | `DSP_FIR.cpp` | Produces |
|---|---|---|
| `PrewarpRadians` | `:998` | prewarped analog frequency |
| `BilinearBiquad` | `:968` | one analog 2nd-order section → one ARM biquad |
| `CalcChebyshevILowpassCoeffs` | `:1024` | Chebyshev I lowpass as a biquad cascade; poles on the s-plane ellipse, normalised to unity DC gain |
| `CalcBandpassCascadeCoeffs` | `:1086` | an equaliser cell from its 4-section analog prototype |
| `NormalizeFIRDCGain` | `:1310` | unity-DC-gain scaling for the generated FIRs |

Two entry points call them:

- **`InitializeReceiveAudioFilterCoeffs(audioFs_Hz)`** (`:1330`) — the 5 CW audio lowpasses, the
  14 equaliser cells, and the CW decoder input FIR. Called from the top of `InitializeFilters()`
  (`DSP_FFT.cpp:342`), so it runs at startup *and* on every rate change, before the ARM instances
  are bound.
- **`InitializeTransmitFilterCoeffs(audioFs_Hz)`** (`:1369`) — the two stages either side of the
  Hilbert transform. Called from `InitializeTransmitFilters()` (`DSP_FFT.cpp:447`).

Note the argument in both cases is `SR[SampleRate].rate / RXfilters.DF` — the **audio** rate,
not the raw rate. A decimating FIR filters at its input rate and an interpolating one at its
output rate; for both transmit stages that rate is the audio rate, not the 12 ksps the Hilbert
transform between them runs at (`DSP_FIR.cpp:1363-1365`).

## ⚠️ The FIR stages are only rate-independent to ≈1.2 %, the IIR ones to 0.3 %

The two generated **transmit** stages are 48-tap Kaiser-Bessel windowed sincs, and a 48-tap FIR
**does not scale exactly** when its normalised cutoff changes. The taps are quantised to the same
48 positions at both rates, so the realised corner lands in a slightly different place even though
the design specification is identical. Evaluating the generated tap sets directly (cascade of
`TX_DECIMATE3_FC_HZ` and `TX_AUDIO_LPF_FC_HZ`):

| Audio rate | Cascade −3 dB | −6 dB |
|---|---|---|
| 24 ksps (192 ksps) | **2726 Hz** | 2952 Hz |
| 22.05 ksps (176.4 ksps) | **2759 Hz** | 2969 Hz |

**= +1.2 %, on correct firmware.** `TransmitChain176k.AudioBandwidthHoldsAcrossRates` allows 2 %
for exactly this reason, and it is why [[tx-filter-hil-test]] uses a 2.5 % tolerance where
[[filter-hil-test]] uses 1.5 %.

The receive filters do not share this limitation: they are **IIR** cascades bilinear-transformed
from prewarped analog prototypes, so the corner is placed by a continuous parameter rather than
quantised onto a tap grid — which is why they measure to within 0.3 % on the bench. The prewarping
described above is what buys that.

The practical consequence: **rate invariance is not a single number across the whole firmware.**
A tolerance tight enough for the IIR stages will fail the FIR ones. Both are still an order of
magnitude clear of the 8.125 % the frozen tables produced.

The same tap-set evaluation gives the transmit cascade's stopband profile — −21.6 dB at 3.5 kHz,
**−52 dB at 4 kHz**, better than −90 dB above 4.5 kHz — which is what fixes where an out-of-band
rejection test can legitimately be gated. See [[tx-filter-hil-test]].

## Deliberately *not* generated

The decimation, interpolation, Hilbert and [[zoom-fft]] filters are left alone. Their corners
are specified **as a fraction of Fs**, and scaling with Fs is exactly what an anti-alias or
anti-image filter *should* do (`DSP_FIR.cpp:1322-1326`). Regenerating them would be a no-op at
best.

This resolves the asymmetry [[multirate-decimation]] describes: the split is not "RX runtime /
TX fixed", it is **"specified in Hz → must be generated" vs "specified as a fraction of Fs →
already correct"**.

## Three bugs found while in there

- ⚠️ **The transmit decimate-by-2 feeding the Hilbert stage had no usable stopband.** Its table
  was flat to **0.425·Fs** where a decimate-by-2 needs everything below **0.25·Fs** gone, so
  **6–9.5 kHz folded back into the transmit audio unattenuated**. Replaced with a real lowpass at
  3.5 kHz (`TX_DECIMATE3_FC_HZ`, `DSP_FIR.cpp:1296`), which sits above the 2.76 kHz the audio
  bandwidth filter passes and so costs no wanted signal. **Transmitted audio therefore differs
  from previous releases even at 192 ksps** — the one user-visible behaviour change in this work
  that is not a rate-change effect. [[tx-filter-hil-test]] measures the fold-back directly on the
  bench, with the caveat that `BandEQ` runs before this stage and attenuates high inputs on its
  own.
- **The AM DC blocker** had a fixed pole at 0.99 and, worse, its state was a **file-scope static
  shared across all three `ReceiveFilterConfig` instances**.
- **`ApplyEQBandFilter` scrubbed the receive instance's state even on the transmit path.**

## Verification

`code/test/FilterDesign_test.cpp` (683 lines) does two things:

1. **Fidelity** — measures each generated filter against the reference table it replaces:
   **CW audio within 0.05 dB, equaliser cells within 0.01 dB**.
2. **Rate independence** — checks every corner holds across both rates, where the frozen tables
   moved it by 8.125 %.

Both are simulation. The on-the-bench measurements are:

- **Receive** — [[filter-hil-test]] found **95 checks passed, 0 failed**, every CW corner and
  equaliser centre holding to **within 0.3 %** across the rate change.
- **Transmit** — [[tx-filter-hil-test]] measures the composite transmit passband corners,
  opposite-sideband suppression, and the fold-back the `TX_DECIMATE3_FC_HZ` fix addressed.
  Written and self-verified; **not yet run on the bench**.

## Open questions
- The equaliser prototypes are stored to 9 decimal places as recovered floats. Nobody has
  identified what the *original* analog design was (a specific filterbank standard?), which
  would let the table be replaced by a formula. See [[audio-equalizer]].
- Whether the AM DC blocker's pole should itself be rate-derived now that it is per-instance —
  it was made per-instance but the fixed 0.99 was not obviously revisited.
- `CW_DECODE_FIR_FC_HZ` (1749.1 Hz) is a **−6 dB** corner because that is what `CalcFIRCoeffs`
  takes, while the filter is nominally described by its −3 dB point of 1560 Hz
  (`DSP_FIR.cpp:1280-1283`). Worth a note on [[cw-processing]] so the two numbers are not read
  as a contradiction.
