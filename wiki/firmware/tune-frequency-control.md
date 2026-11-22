---
title: Frequency Control & Tuning Math (Tune.cpp)
type: module
status: draft
created: 2026-06-08
updated: 2026-11-03
tags: [tuning, vfo, frequency, fs-over-4, cw-offset, fast-tune, power-calibration, sample-rate]
source_refs: []
related: ["[[overview]]", "[[hardware-state-machine]]", "[[rf-board]]", "[[mode-state-machine]]", "[[iq-quadrature-sampling]]", "[[dsp-chain]]", "[[rapid-tune-mute-freeze]]", "[[sample-rate-switching]]", "[[filter-hil-test]]", "[[automatic-power-swr-calibration]]"]
---

# Frequency Control & Tuning Math (Tune.cpp)

**Files:** `Tune.cpp` (~15 KB), `Tune.h`.

> ⚠️ **Myth-buster.** `CLAUDE.md` and `PhoenixSketch.ino` describe a "TuneSm" frequency-control
> **state machine** in `Tune.cpp` with states `TuneReceive / TuneSSBTX / TuneCWTX`. That is
> wrong on both counts: **`Tune.cpp` contains no state machine** — it is a library of pure
> functions — and the `TuneState` enum that *does* have those values lives in **`HardwareSm.cpp`**
> (`static TuneState tuneState`, dispatched by `HandleTuneState()`), see
> [[hardware-state-machine]]. This page documents the real `Tune.cpp`. (Logged in
> [[documentation-todos]].)

`Tune.cpp` does two unrelated jobs: **frequency/tuning math** and **PA power-calibration
math**. They share a file but not a concept.

## Two-stage tuning architecture

Phoenix tunes in two stages — a coarse **hardware** step and a fine **software** step:

1. **Coarse:** the Si5351 hardware VFO ([[rf-board]]) is parked at the band-center frequency.
   `HandleTuneState(TuneReceive)` sets the RX VFO to `centerFreq_Hz` (`HardwareSm.cpp:651-654`).
2. **Fine:** a digital software mixer shifts within the captured passband. The displayed/operating
   frequency is

   ```
   RXTX_freq = centerFreq_Hz − fineTuneFreq_Hz − SampleRate/4      (GetTXRXFreq, Tune.cpp:121)
   ```

   The fine-tune and the **−SampleRate/4** term are applied in the DSP, not the Si5351 — the
   hardware LO stays put while you tune across the spectrum. `FreqShiftF()` in [[dsp-chain]]
   performs the digital shift.

### Why the −Fs/4 quarter-shift *(general DSP)*
Direct-conversion receivers have a **DC hole**: LO leakage, 1/f noise, and ADC DC offset all
pile up at 0 Hz, right where the signal of interest would sit. Phoenix offsets the digital
spectrum by **Fs/4** so the operating frequency lands a quarter-band away from DC, clear of
that junk, then shifts it back in software. The complex baseband ([[iq-quadrature-sampling]])
makes the negative-frequency placement meaningful. (Fs/4 is also the cheapest possible
digital mix — multiplies are just ±1, ±j.)

**Fs/4 is 48 kHz at 192 ksps and 44.1 kHz at 176.4 ksps** ([[sample-rate-switching]]), and this
has two consequences:

- **Changing the rate moves the dial** unless compensated. `ChangeSampleRate()` shifts
  `ED.centerFreq_Hz` for **both VFOs** by `Δ(Fs/4)` — 3.9 kHz between the two rates — before it
  updates `SampleRate`, so `centerFreq_Hz − Fs/4` is held constant and the operator stays on
  frequency (`MainBoard_AudioIO.cpp:523-532`).
- ⚠️ **Anything injecting a signal into the receive path must allow for the fine tune as well.**
  `ReceiveProcessing` shifts *twice* — by Fs/4 (`FreqShiftFs4`) and then by the fine tune
  (`FreqShiftF`) — so the input frequency that demodulates to DC is `|Fs/4 + fineTuneFreq_Hz|`,
  not Fs/4. `fineTuneFreq_Hz` is routinely several kHz, far wider than any passband, so omitting
  it produces **silence rather than a small error**. This is the single most expensive trap in
  [[filter-hil-test]].

## The frequency functions

| Function | Returns | Notes |
|---|---|---|
| `GetTXRXFreq(vfo)` | Hz | `centerFreq − fineTune − Fs/4` for a VFO (`Tune.cpp:121`) |
| `GetTXRXFreq_cHz()` | **centi-Hz** (×100) | same, active VFO, in Si5351 resolution units (`Tune.cpp:110`) |
| `GetCWTXFreq_cHz()` | centi-Hz | TXRX ± CW tone offset; **LSB subtracts, USB adds** (`Tune.cpp:132`) |
| `ResetTuning()` | — | fold `fineTune` into `centerFreq`, zero `fineTune` (recenter, same freq) (`Tune.cpp:146`) |
| `GetBand(freq)` | band idx or −1 | linear search of `bands[]` (`Tune.cpp:159`) |

Frequencies are carried in **centi-Hz (Hz×100)** internally for Si5351 tuning resolution. The
`_cHz` suffix and source docstrings now name this correctly; an earlier `_dHz`/"deci-Hz" naming
was a misnomer (×100 is **centi**-Hz) and was renamed across the source on 2026-06-16. See
[[rf-board]] and [[documentation-todos]].

## Fine-tune control & limits — `AdjustFineTune()` (`Tune.cpp:37`)

Each tuning-encoder detent adds `ED.stepFineTune × filter_change` to `fineTuneFreq_Hz`. Then it
clamps the result so you can't tune outside what's usable:
- **Visible-window limit:** fine tune must stay within the on-screen span, which shrinks with
  zoom — `±SampleRate/2` at zoom 0, `±(SampleRate / 2^zoom)/2` otherwise (`Tune.cpp:64-74`).
  See [[zoom-fft]].
- **Band-edge margin:** keeps the passband a filter-bandwidth away from the band edge, using
  the mode's `FLoCut_Hz`/`FHiCut_Hz` (`Tune.cpp:77-94`).
- **Fast tune** (`#ifdef FAST_TUNE`): if the encoder is spun quickly (`FT_trig = 4` steps each
  under `FT_on_ms = 100` ms apart) the step jumps to `FT_step = 1000` Hz for rapid QSY, and
  reverts after a `FT_cancel_ms = 500` ms pause (`Tune.cpp:20-57`).

`AdjustBand()` in [[main-loop]] re-derives the band from the new frequency, keeping the last
valid band if you tune outside the ham bands (so demod keeps working).

Note `AdjustFineTune()` only moves `fineTuneFreq_Hz` — it never touches `centerFreq_Hz` or
reprograms the Si5351, so a fine-tune spin leaves the spectrum **center fixed** and only slides
the on-screen marker. [[rapid-tune-mute-freeze]] relies on exactly this distinction: it fully
freezes the spectrum for Center Tune (which re-centers) but for Fine Tune holds the trace and
just redraws the moving tuning bar.

## Per-mode VFO selection lives in HardwareSm
The actual "which formula → which VFO register" dispatch is `HandleTuneState()`
(`HardwareSm.cpp:639`), which also selects LPF/BPF band, antenna, PA, and refreshes the FIR
mask before programming the VFO. Highlights:
- `TuneReceive` → RX VFO = `centerFreq` (fine tune done in DSP).
- `TuneSSBTX` → TX VFO = `GetTXRXFreq_cHz()`; on dual-VFO radios the RX VFO is set to
  **freq/3** to watch the 3rd harmonic for the TX spectrum display (fundamental too strong)
  (`HardwareSm.cpp:662-666`).
- `TuneCWTX` → CW VFO = `GetCWTXFreq_cHz()` (tone offset applied).
- `TuneCalReceiveIQ` / `TuneCalTransmitIQ` → calibration-specific VFO placement (TX-IQ offsets
  the TX VFO from RX so the image sits far from DC). See [[hardware-state-machine]],
  [[iq-imbalance-correction]].

## PA power-calibration math (also in this file)
This is unrelated to tuning but lives in the same file. Since V1.5.0 the PA output is modelled
as a **shaped tanh** curve (`Tune.cpp:152-223`):

```
Pout = Psat · tanh( (k · 10^(−A/10))^s )^(1/s)      A = attenuation in dB
```

- **The shape s** sets how sharp the knee is; s = 1 is the plain tanh used before V1.5.0. An
  s of 0 or less is taken as 1, so a configuration saved before the shape existed keeps its
  curves. The per-band, per-PA shape is stored as `PowerCal_20W_shape[]` /
  `PowerCal_100W_shape[]` ([[persistent-config]]).
- `attenToPower_mW` / `powerToAtten_dB` evaluate the model and its inverse; both take the
  shape.
- `CalculateCWAttenuation(P)` / `CalculateSSBTXGain(P)` choose the attenuation (CW) or the
  gain (SSB) for a requested power. They also select the 20 W or the 100 W PA, using
  `PowerCal_20W_to_100W_threshold_W`.
- `FitPowerCurve` (with `fitPowerModel`) is a **Levenberg–Marquardt** fit on ln Psat, ln k and
  ln s, in double precision.
  - The fit is done in dB, so every point counts by its relative error.
  - It fits the plain tanh first, then releases s. With fewer than 4 points (the manual flow's
    3) s stays 1.
  - It converges in about 20 iterations. The Gauss–Newton fit in mW that it replaced never met
    its float32 convergence test and always ran 100.

**Why the shape was added (2026-11-02, radio 2's level 2 sweeps).**

| Fit | rms miss: median band, worst band |
|---|---|
| tanh in mW, float32 (before) | 0.69 dB, 1.64 dB |
| tanh in dB | 0.35 dB, 0.62 dB |
| shaped tanh in dB (now) | 0.16 dB, 0.50 dB |

This PA's knee is sharper than a plain tanh allows (the fitted s is 0.5–0.75). 6 m is the
worst band with any model. Full detail is in `code/docs/PowerCalibration/AutoCalibrationDesign.md`.

These functions back `PowerCalSm` ([[calibration-state-machines]],
[[automatic-power-swr-calibration]]). The per-band `Psat`/`k`/`shape` coefficients live in
the `ED` struct ([[persistent-config]]).

## Open questions
- Confirm the exact DSP site of the fine-tune software mix (`FreqShiftF`) and how `−Fs/4`
  composes with it.
- The SSB-TX 3rd-harmonic-monitor trick — document the dual-VFO display path that consumes it.
