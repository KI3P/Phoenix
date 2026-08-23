---
title: Calibration Sub-State-Machines (the 4 generated cal SMs)
type: module
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [state-machine, calibration, statesmith, iq, carrier, power, sweep]
source_refs: []
related: ["[[overview]]", "[[hardware-state-machine]]", "[[state-machine-architecture]]", "[[iq-imbalance-correction]]", "[[tx-carrier-null]]", "[[tune-frequency-control]]", "[[persistent-config]]"]
---

# Calibration Sub-State-Machines (the 4 generated cal SMs)

The four **StateSmith-generated** calibration machines, each wrapped by a hand-written partial
in [[hardware-state-machine]] and entered from ModeSm's `CALIBRATION_STATES`:

| SM | File | Optimizes | `ED` output |
|---|---|---|---|
| `ReceiveIQCalSm` | `ReceiveIQCalSm.cpp` | RX sideband separation | `IQAmp/PhaseCorrectionFactor` |
| `TransmitIQCalSm` | `TransmitIQCalSm.cpp` | TX sideband separation | `IQXAmp/XPhaseCorrectionFactor` |
| `TransmitCarrierCalSm` | `TransmitCarrierCalSm.cpp` | carrier suppression (dBc) | `DCOffsetI/Q` |
| `PowerCalSm` | `PowerCalSm.cpp` | PA power curve fit | `PowerCal_*` |

All persist per-band results to [[persistent-config]] and auto-advance through the bands. The
underlying theory: [[iq-imbalance-correction]] (the two I/Q ones), [[tx-carrier-null]],
[[tune-frequency-control]] (the power tanh model).

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

## PowerCalSm is different — curve fitting, not minimum-finding

Power calibration **records multiple (attenuation, power) data points** and fits the saturating
tanh PA model ([[tune-frequency-control]]), so its states reflect data-point collection rather
than a single-minimum sweep:

**States:** `POWERPOINT1 · POWERPOINT2 · POWERPOINT3 · SSBPOINT · ACQUISITION ·
MEASUREMENTCOMPLETE · READ_AND_ADJUST · POWERCOMPLETE` (+ `ROOT`).
**Events:** `DO · AUTO · RECORD_DATA_POINT · NEXT_POINT · CURVE_COMPLETE · RESET`.

- The `POWERPOINT1/2/3` (and `SSBPOINT`) states drive the PA to set attenuations and
  `RECORD_DATA_POINT` captures the measured output power at each.
- `ACQUISITION` → `MEASUREMENTCOMPLETE` gate the settle/measure.
- `READ_AND_ADJUST` / `POWERCOMPLETE` finish a band; `CURVE_COMPLETE` triggers the
  Gauss-Newton tanh fit (`FitPowerCurve` in [[tune-frequency-control]]) yielding the per-band
  `Psat`/`k`.

## Generated-code rule
All four are StateSmith output — edit the `.drawio` and regenerate, never the `.cpp`
([[state-machine-architecture]]). Diagrams: `PowerCalSm.drawio`, `ReceiveIQCalSm.drawio`,
`TransmitCarrierCalSm.drawio`, `TransmitIQCalSm.drawio`.

## Open questions
- The exact `Vars` each SM carries (acquisition duration, counters) — only partially traced.
- Whether the three identical SMs are generated from one shared diagram or three copies (DRY
  opportunity if the latter).
- `PowerCalSm` SSB vs CW point handling (`SSBPOINT` vs the three `POWERPOINT`s).
