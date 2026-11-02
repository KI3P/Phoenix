# Automatic power and SWR calibration

The AUTO branch of the power calibration state machine (`PowerCalSm.drawio`) calibrates three
things on every band in one session:
- the PA power curve (CW P_sat and k);
- the SSB DSP gain;
- the SWR bridge (s_f, o_f, s_r, o_r).

It follows the three-level approach in `code/docs/swr_calibration_approach.md`. The manual flow
in `PowerCalibration.md` is unchanged and remains the default when power calibration starts.

**Status (2026-11-02):** the state machine is in place and unit-tested
(`PowerCalAutoTest` in `code/test/PowerCalibration_test.cpp`). Its actions in
`HardwareSm_PowerCalibration.cpp` are stubs marked `TODO`: they step through the session but do
not yet key the transmitter, measure, fit, check or store anything.

## Connector order: load, open, short

The operator fits each standard once, and every band is measured on it before the next change.
That is two connector changes at every level:

1. **50 Ω load.** At levels 2 and 3, the wattmeter goes between the connector and its load.
2. **Open.** Nothing on the connector.
3. **Short.** Fitted directly on the connector.

The load comes first even at level 1, so that level 1 can also fit the PA curve. The forward line
from the level 1 defaults converts the sweep's forward readings to power. The load stage also
gives the oscillation check a 50 Ω reference for the open and short, and the S50 check its data.
At level 1 the operator may skip the load stage (button 12) for a quick SWR-only recalibration.
The PA curves are then kept, and there is no S50 check.

## What each level does

| Stage | Level 1, no wattmeter | Level 2, one reading per band | Level 3, full |
|---|---|---|---|
| Load | Automatic CW sweep, 0 to 31.5 dB in about 3 dB steps (12 points). Forward line from the defaults. PA curve from the sweep. SSB tone point | The same sweep, then one wattmeter reading near 2 W. o_f from that reading. PA curve from the sweep, read through the new line. SSB point | A wattmeter reading at 0, 5 … 25 and 31.5 dB, plus a drift repeat (8 points). Full forward fit. PA curve from the wattmeter readings. SSB point |
| Open | 6 points from about 1.2 W down to 31.5 dB. ≤ 2 s keyed per point, then 1 s in receive. Oscillation check after each point | same | same |
| Short | The same attenuations as the open. If it fails, the band falls back to the open alone | same | same |
| Review | Reflected fits and the acceptance checks. The operator accepts the passing bands or retries the failed ones | same | same |
| Operator readings | 0 | 1 per band | 8 per band |

There is no key-up floor measurement. The floor subtraction in
`swr_calibration_approach.md` §7 is optional: it only removes a small high bias in the SWR on a
well-matched load below about 50 mW, and neither calibration needs it.

The SSB gain point is automatic at every level. The firmware keys the SSB tone into the load
and reads its power from the band's new forward line.

## State machine

```
ROOT ─► MANUAL (initial)                    the existing manual flow
│   PowerPoint1 ► PowerPoint2 ► PowerPoint3 ► PowerComplete ─CURVE_COMPLETE► SSBPoint ► MeasurementComplete
│   MANUAL: RESET ► PowerPoint1
│   MANUAL: AUTO [AutoCalAllowed()] ► AUTO
│
└─ AUTO     enter / AutoCalEnter()   exit / AutoCalExit()   do / count_ms++
    AUTO: RESET ► MANUAL                     abort from anywhere; AutoCalExit() unkeys
    SETUP ──SELECT──► PROMPT ──SELECT──► BAND ──────────────────────► POINT
                       ▲  └─SKIP (L1 load)─┘                              │
                       │                                                  ▼
    POINT:  KEYDOWN ─settle─► [meter entry?] ─► METER_ENTRY ─SELECT─► COLLECT
                                    │             │ timeout                 │
                                    │             ▼                         │
                                    │          METER_PAUSED ─SELECT─► KEYDOWN
                                    └──────────────────────────────► COLLECT
    COLLECT ─► choice:  oscillation        ► BAND_DONE (abandon this band's sweep)
                        more points, load  ► POINT  (stays keyed)
                        more points        ► REST ► POINT  (1 s in receive)
                        forward fit due    ► FIT_FORWARD ► POINT (the SSB tone point)
                        else               ► BAND_DONE
    BAND_DONE ─► choice: more bands ► BAND;  more standards ► PROMPT;  else ► FIT_ALL
    FIT_ALL ► REVIEW ─SELECT─► DONE
                     ─RETRY [a band failed]─► SETUP (failed bands only)
```

| State | What happens |
|---|---|
| SETUP | The operator chooses the level with the volume encoder. SELECT starts. |
| PROMPT | Asks for the next standard. SELECT confirms it is fitted. |
| BAND | Tunes to the band's calibration frequency and plans its points. |
| POINT | Sets the drive for one point. Its sub-states key, wait to settle, take the wattmeter entry when one is needed, and collect the averaged detector readings. |
| METER_ENTRY | Keyed. The operator dials in the wattmeter reading with the filter encoder and presses SELECT. |
| METER_PAUSED | The entry timed out (30 s). Unkeyed. SELECT re-keys the same point. |
| REST | Open and short only: 1 s in receive between points to protect the PA. |
| FIT_FORWARD | Load stage, after the CW points: fits the band's forward line. The SSB tone point follows. |
| BAND_DONE | Unkeys. Load stage: fits the PA curve and the SSB gain. |
| FIT_ALL | Reflected fits, the open-only fallback, S50 and the acceptance checks for every band. |
| REVIEW | Per-band pass or fail. SELECT stores the passing bands. RETRY repeats the failed ones. |
| DONE | Summary. ZOOM or HOME leaves. |

### Buttons and encoders in AUTO

| Control | Action |
|---|---|
| Button 1 | Start AUTO (from the manual flow, 20 W PA only) |
| SELECT (button 0) | Start, standard fitted, wattmeter reading entered, re-key after a timeout, accept |
| ZOOM (button 3) | Abort: unkey and return to the manual flow. Nothing is stored |
| Button 12 | Skip the load stage (level 1 only) |
| Button 14 | On the review screen: retry the failed bands. Elsewhere: change W/dBm |
| Filter encoder | Wattmeter reading |
| Volume encoder | Level, on the setup screen only. The firmware sets the attenuation itself |
| BAND_UP, BAND_DN, button 16 | Ignored. The firmware chooses the bands, and AUTO uses the 20 W PA |
| HOME | Aborts AUTO first (so it unkeys), then leaves calibration as before |

### Timing variables

These are set in `AutoCalEnter()`, and tests shorten them:

| Variable | Value | Purpose |
|---|---|---|
| `bandSettle_ms` | 300 | After a band change |
| `settle_ms` | 500 | Keyed, before collecting |
| `collect_ms` | 2000 | Length of the averaged reading. Use 1500 on the open and short so each point is keyed for ≤ 2 s |
| `rest_ms` | 1000 | Receive between open and short points |
| `meterTimeout_ms` | 30000 | Unkey if no wattmeter entry arrives |

## Rules for the implementation

- **Always unkey on the way out.** Every key-down path leaves through COLLECT's choice, REST,
  METER_PAUSED or AUTO's exit, and `AutoCalExit()` always unkeys. `InitializePowerCalibration()`
  clears the session, because `PowerCalSm_start()` does not run exit actions.
- **No I²C, long calculations or storage writes in the actions.** The state machine runs in the
  1 ms timer interrupt. Its actions post work with `RequestAutoService()`.
  `PowerCalAutoService()`, called from `loop()`, does the work, and the state machine waits on the
  `AutoServiceIdle()` guard. Keying goes through `SetInterrupt(iPTT_PRESSED/RELEASED)` as before.
- **Readings.** COLLECT clears the raw bridge statistics on entry.
  `PerformSWRBridgeReading()` already adds every key-down reading to them
  (`AccumulateSWRBridgeStats()`), and `AutoStorePoint()` reads them with `GetSWRBridgeStats()`.
- **Nothing reaches `ED` before the operator accepts on REVIEW.** The session results live in
  `autoCal` and the per-band buffers. `InitializePowerCalibration()` still zeroes the four SWR
  arrays on entry. That must stop before AUTO stores anything
  (`swr_calibration_approach.md` §4).
- **Operator PTT during AUTO** goes through the same interrupt as the firmware's keying. The
  implementation needs to ignore it while AUTO is active.

## Still to do

- **Point setup and keying:** band, frequency, LPF, attenuation and the CW/SSB mode, in
  `PowerCalAutoService()`.
- **Point readings:** store the means, standard deviations and counts, log CSV rows,
  and run the oscillation check (§6).
- **Fits:** the forward line per level, the PA curve, the SSB gain, the reflected lines with the
  open-only fallback, and S50 (§7).
- **Acceptance checks** (§8).
- **Storage:** commit to `ED`, add `SWR_CalLevel` and the L44 turns setting, and call
  `SaveDataToStorage()`.
- **Display:** a screen for each AUTO state, with band mask and L44 turns selection on SETUP.
- **BAND_4M and BAND_GENERAL:** copy the values of the band whose LPF they use.
