---
title: Frequency Control & Tuning Math (Tune.cpp)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [tuning, vfo, frequency, fs-over-4, cw-offset, fast-tune, power-calibration]
source_refs: []
related: ["[[overview]]", "[[hardware-state-machine]]", "[[rf-board]]", "[[mode-state-machine]]", "[[iq-quadrature-sampling]]", "[[dsp-chain]]"]
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

## The frequency functions

| Function | Returns | Notes |
|---|---|---|
| `GetTXRXFreq(vfo)` | Hz | `centerFreq − fineTune − Fs/4` for a VFO (`Tune.cpp:121`) |
| `GetTXRXFreq_dHz()` | **centi-Hz** (×100) | same, active VFO, in Si5351 resolution units (`Tune.cpp:110`) |
| `GetCWTXFreq_dHz()` | centi-Hz | TXRX ± CW tone offset; **LSB subtracts, USB adds** (`Tune.cpp:132`) |
| `ResetTuning()` | — | fold `fineTune` into `centerFreq`, zero `fineTune` (recenter, same freq) (`Tune.cpp:146`) |
| `GetBand(freq)` | band idx or −1 | linear search of `bands[]` (`Tune.cpp:159`) |

Frequencies are carried in **centi-Hz (Hz×100)** internally for Si5351 tuning resolution. (The
`_dHz` suffix and the source docstrings say "deci-Hz", but ×100 is **centi**-Hz — a historical
misnomer; see [[rf-board]] and [[documentation-todos]].)

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

## Per-mode VFO selection lives in HardwareSm
The actual "which formula → which VFO register" dispatch is `HandleTuneState()`
(`HardwareSm.cpp:639`), which also selects LPF/BPF band, antenna, PA, and refreshes the FIR
mask before programming the VFO. Highlights:
- `TuneReceive` → RX VFO = `centerFreq` (fine tune done in DSP).
- `TuneSSBTX` → TX VFO = `GetTXRXFreq_dHz()`; on dual-VFO radios the RX VFO is set to
  **freq/3** to watch the 3rd harmonic for the TX spectrum display (fundamental too strong)
  (`HardwareSm.cpp:662-666`).
- `TuneCWTX` → CW VFO = `GetCWTXFreq_dHz()` (tone offset applied).
- `TuneCalReceiveIQ` / `TuneCalTransmitIQ` → calibration-specific VFO placement (TX-IQ offsets
  the TX VFO from RX so the image sits far from DC). See [[hardware-state-machine]],
  [[iq-imbalance-correction]].

## PA power-calibration math (also in this file)
Unrelated to tuning but physically co-located. The PA output is modeled as a saturating
**tanh** curve (`Tune.cpp:168-204`):

```
Pout = Psat · tanh( k · 10^(−A/10) )      A = attenuation in dB
```

- `attenToPower_mW` / `powerToAtten_dB` — forward/inverse of the model.
- `CalculateCWAttenuation(P)` / `CalculateSSBTXGain(P)` — pick attenuation (CW) or gain (SSB)
  for a requested power, and select the 20 W vs 100 W PA at `PowerCal_20W_to_100W_threshold_W`.
- `FitPowerCurve` / `fitTanhModel` — a **Gauss-Newton least-squares** fit of `(Psat, k)` from
  measured (attenuation, power) points, used by the power-calibration routine.

These back `PowerCalSm` in [[hardware-state-machine]]; the per-band `Psat`/`k` coefficients
live in the `ED` struct ([[persistent-config]]). *(Candidate for its own
power-calibration theory page.)*

## Open questions
- Confirm the exact DSP site of the fine-tune software mix (`FreqShiftF`) and how `−Fs/4`
  composes with it.
- The SSB-TX 3rd-harmonic-monitor trick — document the dual-VFO display path that consumes it.
