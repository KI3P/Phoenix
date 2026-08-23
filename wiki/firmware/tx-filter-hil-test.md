---
title: Transmit Filter Hardware-in-the-Loop Test
type: decision
status: draft
created: 2026-07-29
updated: 2026-07-29
tags: [test, verification, hil, analog-discovery, filters, sample-rate, cat, transmit, ssb, hilbert]
source_refs: []
related: ["[[filter-hil-test]]", "[[runtime-filter-design]]", "[[sample-rate-switching]]", "[[cat-control]]", "[[dsp-chain]]", "[[ssb-phasing-method]]", "[[audio-equalizer]]", "[[multirate-decimation]]", "[[iq-imbalance-correction]]", "[[tx-carrier-null]]", "[[audio-io]]"]
---

# Transmit Filter Hardware-in-the-Loop Test

`code/tools/tx_filter_hil/` — the transmit counterpart to [[filter-hil-test]]. It measures the
**transmit** DSP filters on the actual radio and checks they hold their designed frequencies at
both sample rates, closing the other half of the gap [[runtime-filter-design]] left: the two
generated transmit stages were previously verified only in simulation.

Full operating instructions live in `code/tools/tx_filter_hil/README.md`. This page records the
method, the quantitative findings, and — importantly — **what this measurement cannot prove**.

## Rig

An Analog Discovery 2 drives `W1` into the microphone input and reads the exciter's **I and Q
outputs on `Ch1` and `Ch2` simultaneously**. CAT on `/dev/ttyACM1`, diagnostics on
`/dev/ttyACM0`. No PTT wiring: the radio is keyed over CAT with `TX;`/`RX;` ([[cat-control]]).

Ch1/Ch2 order does not matter — the suite detects it (see below). The exciter outputs sit on a
DC bias near **+1.6 V**, so the scope offset is centred there; the AD2's range is peak-to-peak
about that offset, giving a ±2.5 V window on the default 5 V range.

## The method: complex baseband, not a differential measurement

The exciter output *is* a complex baseband signal — a 1 kHz microphone tone emerges as a complex
exponential at ±1 kHz, one real sine on each of I and Q, 90° apart ([[iq-quadrature-sampling]]).
Capturing both channels synchronously reconstructs `I + jQ` and so gives the **two-sided**
spectrum, from which one sweep yields:

- the level at `+f` — the transmit audio response, and
- the level at `-f` — the **opposite-sideband suppression**, the headline specification of an SSB
  transmitter ([[ssb-phasing-method]]).

Both come out of the same capture. Two separate single-channel captures would have no defined
phase relationship, so suppression could not be measured at all.

⚠️ **The receive suite's differential trick is not available here.** [[filter-hil-test]] measures
every response as the difference between two captures with a filter engaged and bypassed, so the
whole analog path cancels exactly. Nothing in the transmit chain can be bypassed over CAT: there
is no command for `ED.equalizerXmt`, and the two generated FIR stages are unconditional. Every
transmit response is therefore a **composite** of the whole path, microphone input to exciter
output, including the codec front end and the equaliser bank.

That costs absolute accuracy — hence the loose absolute tolerances — but it costs the rate
comparison **nothing**: no part of the analog path changes when the sample rate does, so it
divides out of a between-rates comparison just as cleanly as a bypass difference would.

## Resolving the wiring takes two captures, not four

The receive suite brute-forces its two unknowns by trying all four combinations and keeping
whichever the SSB filter passes. That does not work here, because **every combination produces a
perfectly good tone**; all that changes is which side of DC it lands on. One capture cannot
distinguish "Ch1 on I, upper sideband" from "Ch1 on Q, lower sideband" — swapping the probes and
switching sideband both conjugate the signal.

What separates them is the radio's own sideband switch. `SidebandSelection()` negates I for USB
and leaves LSB alone (`DSP_FFT.cpp:864-869`), so commanding USB conjugates the transmitted signal
and moves the tone across DC. Nobody rewires the bench between two captures, so **the change
between LSB and USB isolates the radio's contribution from the wiring's**. Which sign LSB
produced then says which input is on I.

The suite measures the resolved sideband rather than predicting it, because an inverting stage
anywhere in the exciter's analog output would flip it without being a fault.

## Scope sample rate: 100 kHz, not 96

⚠️ A measurement trap specific to this rig. The exciter DAC leaves residual images near its own
192 kHz output rate. Sampling at 96 ksps, an image at `192000 − f` aliases to **exactly `−f`** —
straight onto the mirror frequency the suppression reading uses — and would cap the measurable
suppression at whatever the DAC's image rejection happens to be. At 100 ksps the same image lands
at `8000 + f`, clear of everything measured. Any scope rate that divides 192000 or 176400 has the
same problem; the suite warns if one is given.

## The tolerance had to be 2.5 %, not 1.5 %

⚠️ **This is the finding most likely to be misread as a fault.** The transmit corners move
**≈ +1.2 % between rates on correct firmware**, and no tightening of the test can remove it.

Both generated transmit stages are 48-tap Kaiser-Bessel windowed sincs (`CalcFIRCoeffs`,
`DSP_FIR.cpp:756`). A 48-tap design **does not scale exactly** when its normalised cutoff
changes: the taps are quantised to the same 48 positions at both rates, so the realised corner
lands in a slightly different place even though the design specification is identical. Evaluating
the two generated tap sets directly gives a cascade −3 dB corner of:

| Audio rate | Cascade −3 dB | −6 dB |
|---|---|---|
| 24 ksps (192 ksps) | **2726 Hz** | 2952 Hz |
| 22.05 ksps (176.4 ksps) | **2759 Hz** | 2969 Hz |

= **+1.2 %**. `TransmitChain176k.AudioBandwidthHoldsAcrossRates` allows 2 % for the same reason.
The bug being hunted is still **−8.125 %**, so 2.5 % leaves better than a factor of three between
correct and broken. **A tolerance tighter than the design's own reproducibility is a tolerance on
the design, not on the firmware, and would cry wolf.** Do not set this to 1.5 % to match the
receive suite.

This does not apply to the receive filters, which are IIR designs generated from prewarped analog
prototypes — those genuinely hold to 0.3 % on the bench.

## The transmit stopband profile

Also from evaluating the generated tap sets (cascade of `TX_DECIMATE3_FC_HZ` 3500 Hz and
`TX_AUDIO_LPF_FC_HZ` 3039.6 Hz, both −6 dB corners, at 24 ksps):

| Frequency | Cascade |
|---|---|
| 2760 Hz | −3.4 dB |
| 3000 Hz | −6.8 dB |
| 3500 Hz | −21.6 dB |
| 4000 Hz | **−52 dB** |
| 4500 Hz and above | better than −90 dB |

This matters for how the out-of-band test is gated. An earlier draft demanded 30 dB of rejection
at 3450 Hz, where the cascade is only ~20 dB down **and correctly so** — that is the transition
skirt, not the stopband. The stopband boundary is therefore taken as **4 kHz**
(`bandtable.TX_STOPBAND_FROM_HZ`), above the highest equaliser cell and where the cascade is
comfortably past 50 dB.

## The alias test has two regions, not one

`TXDecimateBy2Again` halves the audio rate, so its output Nyquist is `audio_rate/4` — 6 kHz at
192 ksps, 5512 Hz at 176.4 ksps. Above that point a microphone tone **cannot appear at its own
frequency at all**; it can only reappear folded, at `|audio_rate/2 − f|`. So an 8 kHz input
resurfaces at 4 kHz (192 ksps) or 3025 Hz (176.4 ksps). The two regions are checked separately:

| Probe region | What is measured |
|---|---|
| 4 kHz → fold point | direct rejection: the tone at its own frequency |
| fold point → audio Nyquist | fold-back: the product at `\|audio_rate/2 − f\|` |

This is the failure [[runtime-filter-design]] fixed — the table `TX_DECIMATE3_FC_HZ` replaced was
flat to 0.425·Fs, so 6–9.5 kHz folded into the transmitted audio unattenuated. The fold
frequencies scale with the rate, so where to look changes per rate; see
[[multirate-decimation]].

Both readings are taken against the **rig's own noise floor**, measured with the microphone
silent using the identical windowed peak search. Without that, "−33 dBc" is ambiguous between a
real product and the floor — and the difference decides whether a pass means anything. Results
sitting on the floor are reported as floor-limited, i.e. the true rejection is greater than the
rig can see.

## What this cannot prove

Three limits, all repeated in the generated report so a passing run is not over-read:

1. ⚠️ **A clean fold-back result does not on its own vindicate the decimator.** `BandEQ` runs
   *before* `TXDecimateBy2Again` (`DSP.cpp:1137` vs `:1140`) and its highest cell is 4 kHz, so it
   already attenuates an 8 kHz input substantially. A clean result proves nothing folds into the
   transmitted audio — which is what matters operationally — but
   `TransmitChain176k.DecimatorStopsBeforeTheFoldPoint` is what proves that stage's own stopband,
   by evaluating the tap set at the fold point directly.
2. **Sideband suppression is checked against a floor, not for rate invariance.** The Hilbert table
   is deliberately not regenerated per rate, and that is correct — a Hilbert transformer's usable
   band is a fraction of its sample rate, so one fixed table is the same design at any rate and
   its band edges are *meant* to move with Fs. Requiring the suppression curve to hold still in
   hertz would fail correct firmware. What is measured lumps together the Hilbert accuracy, the
   per-band [[iq-imbalance-correction]], and any gain difference between the two exciter outputs;
   from the exciter's terminals those are not separable.
3. **The transmit equaliser cells are not measured individually, deliberately.** `DSP_FFT.cpp:402-403`
   points `S_Xmt[i].pCoeffs` and `S_Rec[i].pCoeffs` at the *same* array, so the transmit cells
   carry byte-for-byte the coefficients [[filter-hil-test]] already sweeps on the receive side.
   Adding a CAT command to solo them would re-verify one table through a second biquad instance,
   whose only independent state is `pState`. They are still exercised here as part of the
   composite passband — **the low corner is theirs**, since nothing in the chain is a deliberate
   high pass and what rolls the bottom off is the 198 Hz lowest cell ([[audio-equalizer]]).

## Operating constraints

- **The radio transmits continuously for the whole sweep**, several minutes per rate at 100 %
  duty. Run with the exciter I/Q feeding the scope only, or into a dummy load. The suite reports
  total key-down time.
- ⚠️ **The power setting must not change during a run.** `TXGain` (`DSP.cpp:1101`) derives the
  exciter drive from `ED.powerOutSSB[band]` via `CalculateSSBTXGain`, so changing it moves every
  level measured. The suite reads it, reports it, and does not touch it.
- **Must be in SSB.** `TX_write` only dispatches PTT from `SSB_RECEIVE` or `CW_RECEIVE`
  ([[mode-state-machine]]), and a CW key-down transmits a sidetone-oscillator carrier rather than
  processed microphone audio — exercising none of these filters. The suite forces USB (or LSB) and
  restores the original modulation.
- **PTT is dropped around every sample-rate change.** `SR_write` returns `?;` unless the radio is
  receiving, precisely because `ChangeSampleRate()` reconfigures the I²S clock and rebuilds the
  DSP chain (`CAT.cpp:814-827`). The state guard registers "transmit off" **last** so it runs
  *first* on the way out, since every other restore assumes the radio is receiving.
- Only the microphone's **left** channel matters: `TransmitProcessing` overwrites Q with a copy of
  I immediately after the equaliser (`DSP.cpp:1139`), so a second AWG channel would contribute
  nothing ([[audio-io]]).
- Unlike [[filter-hil-test]], **AGC is irrelevant** — it is a receive-path control.

## The suite tests itself

`test_tx_filter_hil.py` — 48 hardware-free tests covering the complex-spectrum metrology, the
clip test (which must judge against the AD2's **half**-range about the offset, not the full range
from zero), the wiring-swap symmetry, and the corner extraction. **Including a check that the
comparison fails when handed a simulated −8.125 % shift, and that it names it as that specific
regression** rather than as generic drift.

The whole pipeline was additionally validated against a simulated radio whose response is the
replicated `CalcFIRCoeffs` cascade: correct firmware → PASS at +1.23 %, frozen tables → FAIL on
all three corners. That is what produced the +1.2 % figure above and drove the tolerance choice.

## Coupling to the firmware

- Needs no new CAT commands — `TX`/`RX`/`MG`/`PC`/`MD`/`SR`/`ED` already suffice, which is why
  this suite (unlike [[filter-hil-test]]) required no firmware change.
- ⚠️ `bandtable.py` **mirrors design constants from `DSP_FIR.cpp` and `DSP.cpp` by hand**, and
  `TX_AUDIO_CORNER_3DB_HZ` / `TX_STOPBAND_*` were derived by evaluating the generated tap sets.
  Nothing detects a firmware value edited without updating it.
- It shares `filter_hil.radio` (CAT plumbing, `ED;` parsing, state guard) and the scalar curve
  fitting in `filter_hil.measure` with the receive suite; the PTT, mic-gain and power helpers were
  added there.

## Open questions
- **Not yet run on the bench.** The suite is verified against simulation and self-tests; the
  measured result is still to come. `--scope-offset` defaults to 1.6 V from the older
  `transmit_test.py`, which assumed the exciter loaded by the RF board — the bias may differ with
  it disconnected, showing up as `clipped` points.
- The composite low corner mixes the 198 Hz equaliser cell with the microphone input's AC
  coupling and they cannot be separated over CAT. A CAT command for `equalizerXmt` would let the
  cells be zeroed, isolating the analog contribution — the one case where that command would earn
  its keep.
- Whether the +1.2 % tap-quantisation spread could be removed by asking `CalcFIRCoeffs` for more
  taps on the transmit stages, or whether it is small enough not to care about.
