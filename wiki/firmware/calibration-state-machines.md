---
title: Calibration Sub-State-Machines (the 4 generated cal SMs)
type: module
status: draft
created: 2026-06-09
updated: 2026-11-03
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
