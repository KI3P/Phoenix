---
title: Audio Equalizer (14-band parallel filterbank)
type: concept
status: draft
created: 2026-06-09
updated: 2026-07-29
tags: [equalizer, eq, filterbank, biquad, audio, tx, rx, sample-rate]
source_refs: []
related: ["[[theory-overview]]", "[[dsp-chain]]", "[[ssb-phasing-method]]", "[[display-subsystem]]", "[[persistent-config]]", "[[front-panel]]", "[[runtime-filter-design]]", "[[filter-hil-test]]", "[[cat-control]]", "[[tx-filter-hil-test]]"]
---

# Audio Equalizer (14-band parallel filterbank)

Phoenix has a **14-band graphic equalizer** applied to both receive audio and transmit audio,
letting the operator shape tone (e.g. tailor TX audio for punch, or RX audio for comfort).
Implemented in `BandEQ()` / `ApplyEQBandFilter()` (`DSP_FFT.cpp:787-836`).

## How a graphic EQ works here *(general DSP)*
This is a **parallel constant-band filterbank**, not a chain of shelving filters: the audio is
run through **14 fixed band-pass biquad filters in parallel**, each output scaled by its band's
gain "slider", and the 14 scaled outputs are **summed** back into one signal. Moving a slider
boosts/cuts only that band's contribution to the sum.

```
audio ─┬─ band1 BPF ─×g1─┐
       ├─ band2 BPF ─×g2─┤
       │      ...         ├─ Σ ─→ equalized audio
       └─ band14 BPF ─×g14┘
```

## The 14 bands

Centre frequencies, from `EQ_BAND_FC_HZ[]` (`DSP_FIR.cpp:1247-1251`):

| # | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| **Hz** | 198.4 | 250 | 315 | 400 | 500 | 630 | 793 |

| # | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|
| **Hz** | 1000 | 1259 | 1587 | 2000 | 2500 | 3150 | 4000 |

Roughly ⅓-octave spacing (the R40 series), i.e. a standard graphic-EQ layout. Each cell is
**four bandpass sections** whose resonances straddle the centre, stored as (frequency-ratio, Q)
pairs in `EQ_BAND_PROTO[14][4]` (`:1263-1278`), with a per-section gain putting the peak at
unity (`EQ_BAND_GAIN[]`, `:1254-1259`).

## Implementation
- **Band filters:** `EQ_Coeffs[14]` (`DSP_FIR.cpp:184`) — 14 biquad-cascade coefficient sets
  (`EQ_Band1Coeffs…EQ_Band14Coeffs`), `eqNumStages = 4` each, run with
  `arm_biquad_cascade_df2T_f32`.
- ⚠️ **The coefficients are generated, not stored.** They used to be frozen tables designed for
  24 ksps audio, so at 176.4 ksps the 4000 Hz cell peaked at **3675 Hz**. They are now
  regenerated from the analog prototype on every rate change
  (`CalcBandpassCascadeCoeffs`, `InitializeReceiveAudioFilterCoeffs` `:1344-1347`) —
  see [[runtime-filter-design]] for how the prototypes were recovered from the shipped tables
  (every biquad had `b1 = 0`, `b2 = −b0`, i.e. zeros at `z = ±1`, the bilinear image of an
  analog bandpass) and why prewarping is what makes them rate-independent. Verified to within
  **0.01 dB** of the original tables in simulation, and to **0.3 %** of centre across the rate
  change on the bench ([[filter-hil-test]]).
- **Separate RX/TX instances, shared coefficients:** `S_Rec[14]` and `S_Xmt[14]` filter states
  both point at the same `EQ_Coeffs[i]` (`DSP_FFT.cpp:396-397`) — same bands, independent filter
  memory for the two paths. One set of coefficients serves both because the receive and transmit
  chains run at the same audio rate. ⚠️ That separation was **broken in practice** until 2026-07:
  `ApplyEQBandFilter` scrubbed the *receive* instance's state even on the transmit path
  ([[runtime-filter-design]]).
  - *Testability corollary:* because the coefficients are literally the same array, verifying the
    transmit cells' centre frequencies independently would add nothing — the only state a transmit
    instance owns is its `pState`. This is why [[tx-filter-hil-test]] does **not** sweep them and
    why no `EQ`-equivalent CAT command for `equalizerXmt` was added ([[cat-control]]). The transmit
    cells are still exercised there as part of the composite passband: nothing in the transmit
    chain is a deliberate high pass, so **the ~200 Hz low corner of the transmitted audio is set by
    cell 0** (198.4 Hz) and moves if the cells are mis-generated.
- **Per-band gains:** `ED.equalizerRec[14]` (RX) and `ED.equalizerXmt[14]` (TX),
  [[persistent-config]]. Stored as integers, scaled `/100` (100 = unity), default all 100 (flat).
- **The summation** (`ApplyEQBandFilter`): each band filters `data->I` → `eqFiltBuffer`, scales
  by `sign × gain`, and **accumulates** into `eqSumBuffer`; `BandEQ` zeroes the accumulator,
  loops all 14 bands, then copies the sum back to `data->I`.
- **Alternating sign:** even-numbered bands are summed with `sign = −1`
  (`DSP_FFT.cpp:788-789`). This phase-alternation across adjacent overlapping band-pass filters
  is what makes the filterbank **reconstruct flat** at unity gain (adjacent bands would
  otherwise add constructively and bump the response).
- **NaN guard:** the per-band filter state is checked and zeroed if it goes non-finite
  (`DSP_FFT.cpp:797-798`) — same cold-boot/feedback robustness theme as elsewhere in the DSP.

## Where it runs in the chain
- **RX:** `BandEQ(&data, &RXfilters, RX)` runs after demodulation (`DSP.cpp:924`, the RX order
  in [[dsp-chain]]).
- **TX:** the same engine with `TXRX = TX` shapes the microphone audio in the transmit chain
  ([[ssb-phasing-method]]) using the `equalizerXmt` gains.

## User interface
Edited on the dedicated equalizer screen `MainBoard_DisplayEqualizer` (the UISm `EQUALIZER`
state, [[display-subsystem]]); the operator selects a band and adjusts its gain with the
front-panel encoder ([[front-panel]]). A toggle switches between editing the RX and TX curves.

Also settable over CAT: **`EQbbvvv;`** sets receive cell `bb` = 00–13 to level `vvv` = 000–100,
`EQbb;` reads one back ([[cat-control]]). Added for [[filter-hil-test]], which sweeps one cell at
100 with the rest at 0.

## Open questions
- Gain range of the `equalizerRec/Xmt` sliders (just 0–100, or boost above 100?). The CAT `EQ`
  command clamps to 000–100, which suggests no boost — worth confirming against the encoder path.
- Confirm the RX-vs-TX edit toggle path (`ToggleRXTXEqualizerEdit` referenced in `Loop.cpp`).
- What the *original* analog design was. The prototypes in `EQ_BAND_PROTO` are floats recovered
  by inverse bilinear transform from the shipped tables, not a formula anyone has identified;
  if it is a known filterbank standard, the 56-entry table could collapse to a generator
  ([[runtime-filter-design]]).

## Resolved
- **The 14 band centre frequencies.** Tabulated above from `EQ_BAND_FC_HZ[]`
  (`DSP_FIR.cpp:1247-1251`), which the run-time filter generation exposed as named constants —
  they were previously only implicit in the frozen `EQ_Band*Coeffs` tables. *(resolved
  2026-07-29)*
