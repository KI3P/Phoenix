---
title: Audio Equalizer (14-band parallel filterbank)
type: concept
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [equalizer, eq, filterbank, biquad, audio, tx, rx]
source_refs: []
related: ["[[theory-overview]]", "[[dsp-chain]]", "[[ssb-phasing-method]]", "[[display-subsystem]]", "[[persistent-config]]", "[[front-panel]]"]
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

## Implementation
- **Band filters:** `EQ_Coeffs[14]` (`DSP_FIR.cpp:475`) — 14 fixed biquad-cascade coefficient
  sets (`EQ_Band1Coeffs…EQ_Band14Coeffs`), `eqNumStages = 4` each, run with
  `arm_biquad_cascade_df2T_f32`.
- **Separate RX/TX instances, shared coefficients:** `S_Rec[14]` and `S_Xmt[14]` filter states
  both point at the same `EQ_Coeffs[i]` (`DSP_FFT.cpp:396-397`) — same bands, independent filter
  memory for the two paths.
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

## Open questions
- The 14 band centre frequencies / Q (from `EQ_Band*Coeffs`) — tabulate them.
- Gain range of the `equalizerRec/Xmt` sliders (just 0–100, or boost above 100?).
- Confirm the RX-vs-TX edit toggle path (`ToggleRXTXEqualizerEdit` referenced in `Loop.cpp`).
