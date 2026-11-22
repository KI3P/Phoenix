---
title: Automatic Power and SWR Calibration
type: module
status: stable
created: 2026-11-03
updated: 2026-11-03
tags: [calibration, power, swr, pa, ad8307, ad7991, state-machine, statesmith]
source_refs: []
related: ["[[calibration-state-machines]]", "[[hardware-state-machine]]", "[[tune-frequency-control]]", "[[filter-boards]]", "[[swr-bridge-measured-behaviour]]", "[[log-amp-power-detection]]", "[[persistent-config]]", "[[display-subsystem]]", "[[cat-control]]"]
---

# Automatic Power and SWR Calibration

One session calibrates three things on every band from 160 m to 6 m (V1.5.0):
- the PA power curve, for CW (P_sat, k and the knee shape s);
- the SSB DSP gain correction;
- the SWR bridge: the forward and reflected lines (s_f, o_f, s_r, o_r).

The operator fits three standards to the antenna connector in turn (a 50 Ω load, an open and a
short). Depending on the level, they may also type in a few wattmeter readings. The firmware
does the rest. It has been run and validated on two radios (2026-11-02 and 11-03).

**Authoritative documents** (in the repository, not the wiki):
- `code/docs/PowerCalibration/AutoCalibration.md` is the operator's guide, with screenshots.
- `code/docs/PowerCalibration/AutoCalibrationDesign.md` is the developer's design: every
  rule, limit, log line and timing.
- `code/docs/swr_calibration_approach.md` sets out the measurement approach the design
  follows: the three levels, the open/short reflected fit and the acceptance checks.

This page summarises them and links them to the rest of the wiki.

## Code

| File | Role |
|---|---|
| `PowerCalSm.drawio` → `PowerCalSm.cpp` | The generated state machine. Its AUTO branch sits beside the old MANUAL flow ([[calibration-state-machines]]) |
| `HardwareSm_PowerAutoCalibration.cpp` | The session: planning the points, the hardware service, the fits, the checks, storage and the log |
| `SWRCalibration.cpp/.h` | Pure calculations: line fits, the level 1 defaults, the reflected fit, the oscillation and load-present rules, the median |
| `Tune.cpp` | `FitPowerCurve()`, the shaped PA curve fit ([[tune-frequency-control]]) |
| `MainBoard_DisplayCalibration_PowerAuto.cpp` | The AUTO screen ([[display-subsystem]]) |

**Tests:**
- `SWRCalibration_test.cpp` covers the calculations.
- `PowerCalAutoTest`, in `PowerCalibration_test.cpp`, runs whole sessions against a simulated
  radio.
- `PowerAutoCalibration_E2E_test.cpp` runs the firmware as it runs on the radio. The real
  `tick1ms()` runs on a timer thread, and a simulated bench answers the AD7991 from the
  hardware state and the standard on the connector. After every `loop()` a monitor checks that
  nothing transmits outside a point, the band never changes while keyed, and the settle and
  rest times hold.
- `AutoCalScreenshots_main.cpp` renders the operator guide's screenshots through the SDL
  display.

## The three levels

| Level | Equipment | Operator readings | Power display accuracy |
|---|---|---|---|
| 1 | 50 Ω load, short | none | about ±1.5 dB |
| 2 (recommended) | wattmeter + its load, short | 2 per band (near 2 W and at 0 dB) | about ±0.3 dB, worst ±0.8 dB |
| 3 | wattmeter + its load, short | 8 per band | about ±0.2 dB |

The SWR is almost the same at every level. The reflected line is fitted from the open and the
short against the forward line, so an error in the forward line cancels
([[log-amp-power-detection]], [[swr-bridge-measured-behaviour]]). Level 1 takes its forward
line from per-band defaults that depend on the number of turns on L44 (`ED.SWR_CouplerTurns`,
set on the setup screen).

## Session order: load, open, short

Each standard is fitted once, and every band is measured on it before the next change:
1. **Load.** A CW sweep keyed through the attenuator at 0, 3 … 30 and 31.5 dB (12 points, each
   keyed for about 1.5 s). Level 2 adds its two wattmeter points; level 3 replaces the sweep
   with 8 wattmeter points. The forward line is fitted next (`FIT_FORWARD`), then an SSB tone
   point. From these come the PA curve and the SSB gain correction. Every load point must show
   V_f − V_r ≥ 100 mV, or the load was not really fitted and the band fails. At level 1,
   button 12 skips the load stage for an SWR-only recalibration.
2. **Open, then short.** Six points of the SSB tone, starting at about 1.2 W and stepping down
   2 dB in DSP gain. Each point is keyed for at most 2 s, then 1 s in true receive (T/R relay in
   RX). After each point an oscillation check compares V_f with the previous point's. A rise,
   a jump in spread, or three short falls running abandon that standard's sweep. A single short
   fall is taken as compression: the previous point is left out and the sweep extended.
3. **Review.** The firmware fits the reflected line jointly from the open and the short
   (`FIT_ALL`), or from either one alone if the other was abandoned (`SWR_CAL_OPEN_ONLY` /
   `SWR_CAL_SHORT_ONLY`). It then runs the acceptance checks, including S50, the SWR read on
   the load. SELECT stores the bands that passed. Button 14 repeats the session for the failed
   bands only.

The open and short use the SSB tone rather than the CW carrier for two reasons. The tone steps
in exact dB with no limit on the span. And its key-up state is true receive, which takes the PA
out of circuit; CW's key-up state leaves the T/R relay in TX. Radio 2's PA oscillated into a
10 m short whatever the drive, which is what made this matter.

4 m and GENERAL have no LPF of their own, so they are skipped and keep their values. AUTO runs
with the 20 W PA only.

## How it drives the hardware

- **The state machine's actions ask; the main loop acts.** `PowerCalSm` runs in the 1 ms timer
  interrupt. Its actions only record what they want in `autoCal`. `PowerCalAutoService()`,
  called from `loop()`, then works in a fixed order: unkey, change mode, change band, clear
  the statistics, store the point, and key last. Keying is done by dispatching ModeSm's
  `PTT_PRESSED`/`PTT_RELEASED` directly.
- **The timers count from the hardware change.** `AutoServiceIdle()` holds the machine until
  ModeSm really is keyed or unkeyed, in CW or SSB, as requested. It then restarts `count_ms`,
  so the settle, collect and rest times start when the hardware has changed.
- **Every path unkeys.** ZOOM (RESET) unkeys ModeSm directly in `AutoCalExit()`. HOME aborts
  AUTO before leaving calibration. The physical PTT is ignored during AUTO.
- **Readings.** `PerformSWRBridgeReading()` adds every key-down bridge reading to running
  statistics (`AccumulateSWRBridgeStats()`), and each point takes their mean and standard
  deviation. CAT `BR` shares these statistics, so it is refused during AUTO ([[cat-control]]).
- **Nothing reaches `ED` until the operator accepts on the review screen.** A band that fails
  keeps its previous calibration.

## What is stored

For each band that passes, the session stores:
- `SWR_F/R_SlopeAdj/Offset`;
- `SWR_CalLevel`, with the one-standard flags;
- `SWR_CalDate`;
- if the load stage ran: `PowerCal_20W_Psat_mW`, `_kindex`, `_shape` and
  `_DSP_Gain_correction_dB`.

It then calls `SaveDataToStorage()` ([[persistent-config]]).

**The log.** Every point (`AUTOCAL,…`), every band result (`AUTOCAL-BAND,…`), and every
decision to leave something out (`AUTOCAL-SKIP,…`) goes through `Debug()`, so a session can
be refitted offline. `PrintAutoCalTable()` prints the review table. `Debug()` is silent in the
USB-audio build. There, Diagnostics → Print stored data prints the stored calibration over
Serial instead.

## Uncalibrated defaults

Since V1.5.0, a radio with no saved configuration starts on 160–6 m from measured values rather
than zeros:
- **Forward offsets:** the level 1 defaults for 10 turns.
- **Reflected line:** radio 2's, kept in its place relative to the forward line.
- **PA curve:** radio 2's shaped fit (P_sat 1.7–17.5 W, s 0.52–0.71).

The SSB gain correction stays 0. The load stage keys its tone at the stored correction, and it
rejects a new correction more than 10 dB away from it. Before this change, an uncalibrated
radio's power display read about 4 dB high, and its PA curve missed by up to 13 dB on 6 m.

## Open items

- **`SWR_CalLevel` in the display:** use it to mark an uncalibrated band's SWR, and to show the
  power reading's expected accuracy.
- **100 W PA:** its power curve still comes from the manual flow.
- **Bench tools:** the scripts, notebooks and CSV data behind the defaults and limits stay on
  the `swr_cal_redux` branch under `code/tools/swr/`. They were not merged into `nightly`.
  References to them on other pages are provenance.
