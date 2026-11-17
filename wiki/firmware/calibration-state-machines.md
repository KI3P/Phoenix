---
title: Calibration Sub-State-Machines (the 4 generated cal SMs)
type: module
status: draft
created: 2026-06-09
updated: 2026-11-16
tags: [state-machine, calibration, statesmith, iq, carrier, power, sweep]
source_refs: []
related: ["[[overview]]", "[[hardware-state-machine]]", "[[state-machine-architecture]]", "[[iq-imbalance-correction]]", "[[tx-carrier-null]]", "[[tune-frequency-control]]", "[[persistent-config]]", "[[log-amp-power-detection]]", "[[automatic-power-swr-calibration]]"]
---

# Calibration Sub-State-Machines (the 4 generated cal SMs)

The four **StateSmith-generated** calibration machines, each wrapped by a hand-written partial
in [[hardware-state-machine]] and entered from ModeSm's `CALIBRATION_STATES`:

| SM | File | Optimizes | `ED` output |
|---|---|---|---|
| `ReceiveIQCalSm` | `ReceiveIQCalSm.cpp` | RX sideband separation | `IQAmp/PhaseCorrectionFactor` |
| `TransmitIQCalSm` | `TransmitIQCalSm.cpp` | TX sideband separation | `IQXAmp/XPhaseCorrectionFactor` |
| `TransmitCarrierCalSm` | `TransmitCarrierCalSm.cpp` | carrier suppression (dBc) | `DCOffsetI/Q` |
| `PowerCalSm` | `PowerCalSm.cpp` | PA power curve fit (manual) and the automatic power + SWR calibration | `PowerCal_*`, `SWR_*` |

All persist per-band results to [[persistent-config]] and auto-advance through the bands. The
underlying theory: [[iq-imbalance-correction]] (the two I/Q ones), [[tx-carrier-null]],
[[tune-frequency-control]] (the shaped-tanh power model), [[automatic-power-swr-calibration]].

## Three of them are the *same* machine

`ReceiveIQCalSm`, `TransmitIQCalSm`, and `TransmitCarrierCalSm` have **identical** state and
event sets — they are one "step-a-parameter, measure, keep-the-best" sweep instantiated for
three different metrics/parameters.

**States:** `STANDBY · BAND_ADJUST · FIND_MINIMUM · ADJUST · WAIT · READ` (+ `ROOT`).
**Events:** `DO · AUTO · AUTO_COMPLETE · FIND_MINIMUM · MIN_EXIT · NEXT_POINT · READ_DELTA`.

Flow (cross-referenced with the partial code in [[iq-imbalance-correction]]):
```
STANDBY ──AUTO──▶ BAND_ADJUST            // set up the current band's hardware
   ▲                 │ FIND_MINIMUM
   │ AUTO_COMPLETE   ▼
   │              FIND_MINIMUM ──▶ ADJUST ──READ_DELTA──▶ WAIT ──(settle)──▶ READ
   │                                 ▲                                        │
   │                                 └──────────── NEXT_POINT ────────────────┘
   └──────────────── MIN_EXIT ◀── (band done) ─────────────────────────────────
```
- **ADJUST** sets the next parameter value (amplitude/phase or DC-offset step).
- **WAIT** lets the signal settle (`acquisitionDuration_ms`, ~60 ms) before measuring.
- **READ** reads the metric (sideband separation / dBc) from the RX PSD and keeps the value if
  it beats the running best (`maxSBS`/`maxDBC`). `NEXT_POINT` loops to the next step; when the
  multi-pass coarse→medium→fine sweep is exhausted the band's optimum is written to `ED`.
- **BAND_ADJUST** ↔ `MIN_EXIT` advances to the next band; `AUTO_COMPLETE` ends the run.

This is the narrowing **coordinate-descent sweep** documented in detail (the amp/phase passes)
in [[iq-imbalance-correction]]; the carrier version sweeps `DCOffsetI`/`DCOffsetQ` instead.

**A pass with no valid reading keeps its setting.** READ keeps a value only if it beats the
running best, which starts each pass at 0 dB. Each pass now also starts its "best parameter"
at the setting it sweeps around (`AdjustRXIQCalSetting` & co., at `step == 0`), so a pass in
which no reading beats 0 dB (a broken or mirrored loopback) leaves the setting alone. Before
2026-11-15 such a pass wrote the best value of the *previous* pass (a phase into the amplitude
or vice versa, e.g. amplitude −0.049), silently corrupting every band.

**Leaving with HOME.** HOME on the Receive IQ or Transmit IQ screen calls
`AbortRXIQCalibration()` / `AbortTXIQCalibration()` / `AbortTXCarrierCalibration()` before
saving. If an automatic run is in progress (state not `STANDBY`), the band being swept gets back
the settings it had when the run reached it (snapshotted in `Adjust*Band`), bands already
completed keep their results, and the TX runs queue the `iPTT_RELEASED` they owe. ModeSm's
`CALIBRATE_TX_IQ_MARK` handles `CALIBRATE_EXIT` itself, so HOME with the PTT held also returns
the mode machine to receive. `TXIQCalibrationAutoActive()` / `RXIQCalibrationAutoActive()`
report whether a run is in progress.

**Every automatic run starts from the first band.** The run's band counter is reset to
`FIRST_BAND` whenever the machine enters `STANDBY` (`Reset*CalBand`: on entering the screen,
after a finished run and after an abort). Until 2026-11-16 the Transmit IQ screen's Band +/−
handlers also set it (`SetTXIQCurrentBand`, now removed), so a TX run started from the band on
screen; it now covers every band like the RX run.

**The transmit runs skip the bands without transmit.** Transmit IQ and the carrier null move
between bands with `NextTXCalBand()`, which skips every band whose `band_type` is not
`HAM_BAND`, the same test as `IsTxAllowed()`: general coverage, today. Their settings would never
be used, and the loopback there read about 2 dB IRR, so the run stored the edge of its sweep
(amplitude ≈ 0). Receive IQ still does every band. Since 2026-11-16.

**Carrier null performance log.** Each step of the carrier null prints a `CARRCAL` line through
`Debug()` (the diagnostics port of a Dual Serial build; silent with `AUDIO_INTERFACE`): band
start, every reading (offsets, raw tone and carrier bins, raw and smoothed dBc, best so far),
each pass's kept value and whether any reading was valid, the band result, aborts and the end.
`code/tools/carrier_cal_log.py` records and summarises it, to judge the algorithm on a real
radio.

**Carrier null algorithm (since 2026-11-16).** The first run on the KI3P prototype showed the
original sweep failing: each band zeroed Q first, but that radio's Q null is near −1000, so the
coarse I pass read the carrier 9 dB or more above the tone everywhere and, with "valid" meaning
above 0 dB, was thrown away on 8 of 12 bands; 6 m found nothing and lost its stored Q; one I/Q
pass per step size let noise pick the medium I when Q was still far off. Now each band starts
from its stored offsets; a pass is valid when its best reading is at least 3 dB above the pass's
median (`CARRIER_MIN_CONTRAST_DB`; on the radio the coarse passes stood 8–12 dB out, noise picks
0.9–2 dB); a band with no valid pass gets its old offsets back; the medium and fine passes run
twice each (ten passes); the first reading of each pass (after a large jump, smoothing not
caught up) is not used. Separately, some bands have a floor the offsets cannot move (30 m about
22 dB: ±12 counts on either offset changed the reading by about 2 dB). The KI3P has no 4 m
filter and 6 m is limited by its hardware, so those bands' results mean little there.

**Checking it all.** `code/test/CalWalkthrough_main.cpp` (targets `cal_walkthrough`,
`cal_walkthrough_dct`) plays an operator through every calibration in the simulator, single and
dual VFO, and checks the results, including each fix above.

**Single vs dual VFO.** Only Transmit IQ differs. Dual-VFO boards loop the TX back into the
receiver (`RFCalTransmitIQ`, 31.5 dB) and measure IRR/dBc themselves; button 16 runs the sweep,
and on `DIRECT_COUPLED_TX` builds button 14 the carrier null. Single-VFO boards
(`RFCalTransmitIQSingleVFO`) transmit through the PA to the antenna connector, read nothing,
and are adjusted by hand against an external analyser; every key-down applies the screen's
attenuation (`GetTXIQAttenuation()`, starts at 31.5 dB), and buttons 14/16 do nothing.

The operator's procedure for all of this is the calibration guide,
`code/docs/CalibrationGuide/Calibration_Guide.html`.

## PowerCalSm is different: curve fitting, not minimum-finding

Power calibration records several (drive, power) points and fits the PA curve
([[tune-frequency-control]]). Its states therefore collect data points rather than search for
a single minimum. Since V1.5.0 the machine has two branches under `ROOT`.

**Events:** `DO · AUTO · RECORD_DATA_POINT · CURVE_COMPLETE · RESET · RETRY · SKIP`.

**MANUAL (the initial state): the original operator-driven flow.**
- **States:** `POWERPOINT1 · POWERPOINT2 · POWERPOINT3 · POWERCOMPLETE · SSBPOINT ·
  MEASUREMENTCOMPLETE`.
- `POWERPOINT1/2/3` key the CW carrier at three attenuations. The operator types in the
  wattmeter reading and presses SELECT, which dispatches `RECORD_DATA_POINT`.
- `CURVE_COMPLETE` fits the PA curve from those three points (`FitPowerCurve`; with only 3
  points the shape stays s = 1, the plain tanh).
- `SSBPOINT` then measures the SSB tone to set the band's SSB gain correction. Since 2026-10-20
  that correction includes the DSP gain in force during the measurement (commit 277f820).
  Before, a radio set to 3 W SSB transmitted about 5 W.

**AUTO: the automatic power and SWR calibration.** See [[automatic-power-swr-calibration]].
- **Entry:** button 1 dispatches `AUTO` (`AutoCalAllowed()`: 20 W PA only).
- **States:** `SETUP · PROMPT · BAND · POINT{KEYDOWN, METER_ENTRY, METER_PAUSED, COLLECT} ·
  STORE · REST · FIT_FORWARD · BAND_DONE · FIT_ALL · REVIEW · DONE`.
- **Events:** `SKIP` (button 12) skips the level 1 load stage. `RETRY` (button 14 on REVIEW)
  repeats the failed bands. `RESET` (ZOOM) aborts from anywhere and returns to MANUAL.
- **Hardware:** the actions run in the 1 ms timer and only record requests. The main loop's
  `PowerCalAutoService()` keys, unkeys, changes band and stores each point, in a safe order.

The session also calibrates the bridge's per-band `SWR_F/R_SlopeAdj/Offset`. For which of
those terms really varies by band, and why every point must be a steady carrier or tone rather
than a modulated signal, see [[log-amp-power-detection]].

> **Superseded (2026-11-01):** earlier bench tests concluded that the SWR reading could not be
> corrected by calibration. The cause turned out to be crosstalk in the AD7991 readout, fixed
> by discarding two conversions after each channel switch ([[filter-boards]]). With that fix
> the open/short calibration works ([[swr-bridge-measured-behaviour]]).

## Generated-code rule
All four are StateSmith output — edit the `.drawio` and regenerate, never the `.cpp`
([[state-machine-architecture]]). Diagrams: `PowerCalSm.drawio`, `ReceiveIQCalSm.drawio`,
`TransmitCarrierCalSm.drawio`, `TransmitIQCalSm.drawio`.

## Open questions
- The exact `Vars` each SM carries (acquisition duration, counters) — only partially traced.
- Whether the three identical SMs are generated from one shared diagram or three copies (DRY
  opportunity if the latter).
