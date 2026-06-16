---
title: Synchronous AM Detection (SAM)
type: concept
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [sam, am, synchronous-detection, pll, carrier, demodulation, fading]
source_refs: []
related: ["[[theory-overview]]", "[[ssb-phasing-method]]", "[[iq-quadrature-sampling]]", "[[dsp-chain]]", "[[display-subsystem]]", "[[code-heritage]]"]
---

# Synchronous AM Detection (SAM)

Phoenix offers **SAM** as a demodulation mode alongside plain AM. `AMDecodeSAM()`
(`DSP.cpp:567`) is the classic DD4WH/wdsp synchronous detector ([[code-heritage]]), selected by
the `SAM` case in `Demodulate()` ([[dsp-chain]]).

## Why synchronous detection *(general DSP)*
Plain **envelope AM** (the `AM` case uses a magnitude estimator, `AlphaBetaMag`) distorts badly
under **selective fading**, where the carrier fades independently of the sidebands — the
recovered audio tears up as the carrier drops. **Synchronous AM** instead **regenerates a clean,
steady carrier** locked to the incoming one with a PLL, then mixes coherently. Replacing the
faded carrier with a local steady reference makes detection robust to fading and reduces
distortion — the same reason BFO/product detection beats envelope detection.

## How Phoenix does it — a 2nd-order PLL

For each sample (`DSP.cpp:585-613`):
1. **NCO:** generate `Sin`/`Cos` at the current loop phase `phzerror`
   (`arm_sin_f32`/`arm_cos_f32`).
2. **Mix** the complex baseband ([[iq-quadrature-sampling]]) against the NCO to get the in-phase
   correlation `corr[0]` and quadrature error `corr[1]`, and the coherent `audio`.
3. **Phase detector:** `det = atan2(corr[1], corr[0])` (via `ApproxAtan2`) — the phase error
   between the local NCO and the received carrier.
4. **Loop filter + NCO update** (2nd-order):
   ```
   omega2  += g2 * det;            // integrator → tracks frequency offset (clamped ±pll_fmax)
   fil_out  = g1 * det + omega2;   // proportional + integral
   phzerror += (prev fil_out);     // NCO accumulates phase, wrapped mod 2π
   ```
   The loop gains `g1`, `g2` are derived from the PLL **bandwidth `omegaN = 200`** and damping
   `zeta`, and the frequency estimate `omega2` is clamped to **±`pll_fmax = 4000 Hz`**
   (`DSP.cpp:579-582`).

When locked, `omega2` *is* the carrier frequency offset, and the coherent `audio` is the
demodulated output.

## Fade leveler
An optional **fade leveler** (`fade_leveler`) tracks two DC averages with time constants
`tauR`/`tauI` and re-inserts the difference (`DSP.cpp:594-598`) to even out amplitude as the
signal fades.

## Carrier-offset readout
The detected offset is published for the UI:
```
SAM_carrier = omega2 · fs / (2·2π);
SAM_carrier_freq_offset = 0.95·prev + 0.05·(10·SAM_carrier);   // smoothed
```
`GetSAMCarrierOffset()` returns it, and the home-screen **SAM-offset pane** displays it
([[display-subsystem]]) — so the operator can see how far the PLL has pulled and zero-beat the
tuning.

## Relationship to other modes
- **AM** (envelope) vs **SAM** (synchronous) are both AM-family demods in `Demodulate()`.
- Distinct from SSB ([[ssb-phasing-method]]), which needs no carrier recovery (the sideband
  *is* the signal).

## Open questions
- Exact `zeta` value and the `tauR`/`tauI` fade-leveler constants (and `zeta_help`).
- Why the `×10` factor in the smoothed `SAM_carrier_freq_offset` (display scaling?).
- Whether SAM supports selectable sideband (DSB vs single-sideband SAM) — check the `corr`
  combination.
