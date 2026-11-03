# Automatic power and SWR calibration: design

How the automatic calibration works inside, for developers. For the operator's step-by-step
instructions, see [AutoCalibration.md](AutoCalibration.md).

The AUTO branch of the power calibration state machine (`PowerCalSm.drawio`) calibrates three
things on every band in one session:
- the PA power curve (CW P_sat and k);
- the SSB DSP gain;
- the SWR bridge (s_f, o_f, s_r, o_r).

It follows the three-level approach in `code/docs/swr_calibration_approach.md`. The manual flow
in `PowerCalibration.md` is unchanged and remains the default when power calibration starts.

**Status (2026-11-02):** complete and unit-tested, but not yet run on a radio.
- **Code:** the session and its hardware are in `HardwareSm_PowerAutoCalibration.cpp`, the
  calculations in `SWRCalibration.cpp`, and the screen in
  `MainBoard_DisplayCalibration_PowerAuto.cpp`.
- **Tests:**
  - `SWRCalibration_test.cpp` covers the calculations.
  - `PowerCalAutoTest` in `PowerCalibration_test.cpp` runs whole sessions against a simulated
    radio and checks what is stored.
  - `PowerAutoCalibration_E2E_test.cpp` runs the firmware as it runs on the radio. The real
    `tick1ms()` runs on a wall-clock timer thread, `loop()` on the test thread, and the operator
    acts only through the menus, buttons and encoders. A simulated bench answers the AD7991
    from the hardware state and the standard on the connector. After every `loop()` a monitor
    checks:
    - no transmitting outside a point;
    - true receive while resting and while changing the connector;
    - the attenuator, the DSP gain and the LPF as commanded;
    - no band change while transmitting;
    - the settle, collect, rest and key-down times.

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
The PA curves and SSB gains are then kept, the open and short start from the stored SSB gain,
and there is no S50 check.

The session covers the 11 bands with an LPF, 160–6 m. 4 m and GENERAL have no LPF of their own
(`BAND_NF_BCD`), so they are skipped and keep their values.

## What each level does

| Stage | Level 1, no wattmeter | Level 2, two readings per band | Level 3, full |
|---|---|---|---|
| Load | Automatic CW sweep, 0 to 31.5 dB in about 3 dB steps (12 points, 1.5 s keyed each). Forward line from the defaults. PA curve from the sweep. SSB tone point. Every point must show the load fitted | The same sweep, then wattmeter readings near 2 W and at 0 dB. o_f from the 2 W reading. PA curve from the sweep, read through the new line, with its ceiling from the 0 dB reading. SSB point | A wattmeter reading at 0, 5 … 25 and 31.5 dB, plus a drift repeat (8 points). Full forward fit. PA curve from the wattmeter readings. SSB point |
| Open | 6 points of the SSB tone, from about 1.2 W down in DSP-gain steps, ≥ 8 dB span. ≤ 2 s keyed per point, then 1 s in true receive. Oscillation check after each point; up to 3 more points for compressed ones left out. If it fails, the band falls back to the short alone | same | same |
| Short | The same DSP gains as the open, and the same checks. If it fails, the band falls back to the open alone; if both fail, the band fails | same | same |
| Review | Reflected fits and the acceptance checks. The operator accepts the passing bands or retries the failed ones | same | same |
| Operator readings | 0 | 2 per band | 8 per band |
| Stored for a band that passes | The four bridge values, the PA curve and the SSB gain, level 1 | the same, level 2 | the same, level 3 |

### The points

| Stage | Drive |
|---|---|
| Load, CW, levels 1 and 2 | Attenuation 0, 3, 6 … 30, 31.5 dB (12 points). Level 2 adds two wattmeter points: the sweep's attenuation whose reading, through the level 1 defaults, was nearest 2 W, then 0 dB |
| Load, CW, level 3 | 0, 5, 10, 15, 20, 25, 31.5 dB, then 5 dB again for the drift check, each with a wattmeter reading |
| Load, SSB tone | DSP gain = the stored SSB correction, which gives about 3 W (`SSB_20W_CAL_POWER_POINT_W`) |
| Open and short | DSP gain = correction + 10·log10(1.2/3), then 2 dB lower at each of 6 points (10 dB span). The correction is the band's new one, or the stored one if the load stage was skipped |

The level 2 and 3 wattmeter entry starts at the power the stored PA curve predicts for that
attenuation, so the operator only has to adjust it.

### What is fitted and checked

- **Forward line (FIT_FORWARD).**
  - Level 1: the §2 defaults for the L44 turns (`ED.SWR_CouplerTurns`), nominal slope.
  - Level 2: the offset from the wattmeter point, nominal slope.
  - Level 3: a least-squares fit to the 7 wattmeter points with §7's ceiling rule, plus the drift
    of the repeated point. The ceiling rule covers every point within 1 dB of the band's highest
    wattmeter reading, not only 0 dB. With the measured PA curves (k = 16–36), the 5 dB point
    is saturated as well, and radio 1's 10 m forward detector over-reads all through saturation.
- **PA curve and SSB gain (BAND_DONE on the load).** Only believed if every load point had
  enough bridge readings, P_sat is 1–60 W, and the new SSB correction is within 10 dB of the
  stored one. The open and short are driven from this correction, so a dead bridge readout must
  not set it: fitted to no readings, it once came out at +77 dB.
  - The PA curve is fitted with `FitPowerCurve()`. Levels 1 and 2 use the sweep read through the
    forward line; level 3 uses the wattmeter readings.
  - `FitPowerCurve()` fits P = P_sat·tanh((k·x)^s)^(1/s), x = 10^(−att/10), in dB:
    Levenberg–Marquardt on ln P_sat, ln k and ln s, in double precision, so every point counts
    by its relative error. It fits the plain tanh (s = 1) first, then releases the shape s, which
    sets how sharp the knee is. With fewer than 4 points (the manual calibration's 3) s stays 1.
    The shape is stored as `ED.PowerCal_20W_shape` / `ED.PowerCal_100W_shape`; a configuration
    saved before it existed loads s = 1, its old curve.
  - Why (2026-11-02, radio 2's level 2 sweeps, `code/tools/swr/pa_curve_fits.ipynb`):

    | Fit | rms miss (median, worst band) | worst miss (median, worst band) |
    |---|---|---|
    | tanh in mW, float32 (before) | 0.69, 1.64 dB | 1.28, 2.59 dB |
    | tanh in dB | 0.35, 0.62 dB | 0.59, 1.31 dB |
    | tanh with shape s, in dB (now) | 0.16, 0.50 dB | 0.29, 0.74 dB |

    The plain tanh's knee is too soft for this PA (fitted s = 0.5–0.75): in mW it let the low end
    go by up to 1.6 dB (6 m 2.6 dB); in dB it read up to 0.7 dB high through the knee and 0.7 dB
    low at full power. The old fit's convergence test (1e-6 mW in float32) never fired, so it
    always ran 100 iterations; the new one converges in about 20. 6 m is the worst band with any
    model, because its PA's output does not follow the drive smoothly.
  - Level 2 takes the PA's ceiling from its 0 dB wattmeter reading: that point is the
    wattmeter's, and no sweep point may read above it. Level 1 has no wattmeter. On a band whose
    forward detector over-reads in saturation, such as radio 1's 10 m, its P_sat comes out
    about 2 dB high.
  - SSB correction = gain used + 10·log10(3 W / tone power).
- **Reflected line and checks (FIT_ALL).**
  - The joint open/short fit of §7, or the open alone if the short's sweep was abandoned, or the
    short alone if the open's was.
  - S50: the median SWR of the load's CW points of 0.1 W or more.
  - The §8 checks. The failures are bit flags (`AUTO_FAIL_*` in `HardwareSm.h`), shown in hex on
    the review table.
- **Load present (STORE, load).** Every load point must read V_f − V_r ≥ 100 mV
  (`SWRCAL_LOAD_MIN_GAP_MV`). An open, a short or a loose connector reflects everything, so V_r
  reads like V_f (within about 10 mV). The smallest gap into 50 Ω on any bench sheet is 166 mV
  (radio 2 before its rebuild, 10 m); on 2026-11-02 the smallest was 486 mV (radio 2, 6 m). A
  band with any such point fails (`AUTO_FAIL_NO_LOAD`). On 2026-11-02 the load came off part-way
  through radio 2's 160 m sweep, and the session went on to fit every band's PA curve from the
  mismatch.
- **Oscillation (STORE, open and short).** After each point, compare V_f with the previous point's,
  for the 2 dB drive step (`SWRCalOscillationCheck()`, `SWRCalSweepStep()`):
  - **Fell by at least half the step × 25 mV/dB (25 mV):** keep the point.
  - **Rose by more than 1 dB × 25 mV/dB (25 mV):** the PA is oscillating. Abandon the sweep.
    Radio 2's 10 m short rose 52 mV on 2026-11-02 (its spread from 5 to 70 mV), and 355 mV
    before its rebuild. The PA's fold-back near its ceiling rises 10 mV at most.
  - **Spread jumped, above 20 mV and more than 4 times the previous point's:** an oscillation
    that started part-way through the point. Abandon the sweep there. At level 2 on 2026-11-02
    radio 2's 10 m short fell only 5.5 mV with a spread of 56 mV (4 mV before), then rose
    259 mV; taken for compression, the onset point was kept and the good point before it left
    out. Real sweeps' spreads grow at most about 1.5 times a step, to about 27 mV at low drive.
  - **Anything in between, a short fall:** usually the PA compressing near its ceiling. Leave the
    *previous* point out (it was the compressed one), keep this one, and add a point at the end of
    the sweep (at most 3 per sweep). Radio 2's 20, 17 and 12 m open fell 21, 14 and 5 mV on
    2026-11-02, with spreads of 1–5 mV, while the short on the same bands fell normally. The old
    rule took these for oscillations and failed the three bands.
  - **The third short fall running:** a locked oscillation, which never falls. Before its rebuild
    radio 2's 10 m short locked at 1930 mV with a spread of 1–2 mV, whatever the drive, so the
    spread does not tell compression from oscillation; only whether V_f falls again does. Abandon
    the sweep. A sweep that starts locked is keyed for 4 points, 8 s, before it is abandoned.

  A sweep's first point has nothing to fall from. It must stay within 6 dB of the load's SSB point
  scaled to its gain, or the sweep is abandoned at once.
  - The margin is wide because the PA puts several dB more or less into an open or a short
    than into 50 Ω: −3.3 to +4.9 dB on the bench sheets.
  - The short isn't compared with the open: on 160 m it reads up to 7 dB above it.
  - The 3 dB margin first proposed would have failed radio 2's 20 m open and abandoned five
    real short sweeps.
  - A failed short leaves the band calibrated from the open alone (`SWR_CAL_OPEN_ONLY` in
    `ED.SWR_CalLevel`), and a failed open from the short alone (`SWR_CAL_SHORT_ONLY`). Either
    costs up to half the open–short gap, since the leak is not cancelled.
  - If both fail, the band fails.
- **Storage (SELECT on REVIEW).** For each band that passed: `SWR_F/R_SlopeAdj/Offset`,
  `SWR_CalLevel`, `SWR_CalDate`, and, if the load stage ran, `PowerCal_20W_Psat_mW`,
  `PowerCal_20W_kindex` and `PowerCal_20W_DSP_Gain_correction_dB`. Then `SaveDataToStorage()`.
  Bands that failed keep their previous values. Leaving with ZOOM or HOME stores nothing, except
  that a SELECT on REVIEW is always completed first.
- **Exit.** The operator's band, frequency, mode and CW attenuations are restored.

### The log

Every point and every band's result is written through `Debug()`, so a session can be refitted
offline:
```
AUTOCAL,<band>,<load|open|short>,<cw|meter|ssb|tone|oscillation>,<drive dB>,<wattmeter W>,<Vf>,<Vf sd>,<Vr>,<Vr sd>,<n>
AUTOCAL-BAND,<band>,level <n>,fail 0x<bits>,m_f …,o_f …,m_r …,o_r …,gap …,rms_r …,S50 …,Psat …,k …,s …,ssb …,<open+short|open only|short only>
```

Every decision to leave a point or a result out, or to fail a band, gets its own line. It
gives the measured value and the rule's limit, with their units in the reason:
```
AUTOCAL-SKIP,<band>,<load|open|short|band>,<what>,<reason (units)>,<value>,<limit>
```

| What is left out | Reason in the log |
|---|---|
| Bridge readings within a point | rejected as failed I²C reads (count) |
| A point | too few bridge readings, so the band fails (count against the minimum) |
| A load point | no load: V_f minus V_r below the limit, so the band fails (mV against 100; logged at the band's first such point) |
| An open or short point | compression: the next point fell too little, so this one is left out (mV against the needed fall) |
| An open or short point, and the rest of that sweep | oscillation: V_f rose as the drive fell, V_f unsteady as its spread jumped, V_f fell too little at 3 points running, or V_f above the 50 Ω reference (mV against the limit) |
| The rest of that sweep | abandoned: the reflected line comes from the short alone (open), or from the open alone (short) |
| A level 3 wattmeter point | no wattmeter reading; or a ceiling point off the line (dB against 0.5) |
| A level 2 sweep point | the PA curve uses the wattmeter's ceiling instead (detector minus wattmeter, dB) |
| The PA curve and SSB gain | no forward line; SSB point missing or short of readings; a load point short of readings; too few points; P_sat out of range; SSB correction more than 10 dB from the stored one |
| Load points from S50 | below 0.1 W (count) |
| The band | each failed check, with its value and limit |
| The band at storage | failed its checks, so the previous calibration is kept (fail bits) |

Once the checks have run on every band (`FIT_ALL`, so on entering the review, and again after
a retry), `PrintAutoCalTable()` prints the review's table through `Debug()` for reading by eye:
the screen's columns with the slope adjustments to 3 decimals, the SSB gain correction, the
standards the reflected line came from, and every fail reason in full:
```
Auto calibration results, level 1:
Band  Status    o_f     s_f    o_r     s_r  Psat W       k  shape   S50  SSB dB  Reflected   Fails
40M   FAIL    -3.68   0.000      -       -       -       -      -     -       -  -           open and short abandoned
20M   pass    -4.73   0.000  -2.14   0.711   12.70    6.33  0.857  1.07    1.63  open+short
o: offset dB; s_f, s_r: slope adjustment mV/dB; P = Psat tanh((k x)^shape)^(1/shape), x = drive
```
`Debug()` is silent in a build with USB audio (`usb=serialmidiaudio`). In any build,
Diagnostics → Print stored data (or EEPROM → Print data to Serial) prints every stored field
over Serial, the calibrations to 3 decimals; in the audio build that is the CAT port.

### The screen

The AUTO screen replaces the manual one while AUTO runs:
- **Status:** level, standard, band, point, drive, and KEYED while transmitting.
- **Readings:** the last point's V_f and V_r with their standard deviations, and n.
- **Table:** one row per band with status, o_f, s_f, o_r, s_r, P_sat, k, the shape s and S50. A
  failed band's status is its first fail reason in red, with a + if there are more; the review
  lists them all.
- **Prompt:** what to do now.

On SETUP, the volume encoder sets the level and the filter encoder sets the L44 turns.

There is no key-up floor measurement. The floor subtraction in
`swr_calibration_approach.md` §7 is optional: it only removes a small high bias in the SWR on a
well-matched load below about 50 mW, and neither calibration needs it.

The SSB gain point is automatic at every level. The firmware keys the SSB tone into the load
and reads its power from the band's new forward line.

### Why the open and short use the SSB tone

The CW carrier comes straight from the CW VFO, which already runs at the Si5351's minimum
2 mA drive (`SI5351_DRIVE_CURRENT_CW`). Its only level control is the 31.5 dB attenuator. On
radio 1 that reached 0.10–0.21 W, only 7.6–10.8 dB below the open and short sweeps' 1.2 W start,
which is short of the 8 dB span §6 asks for.

The SSB tone (`CALIBRATE_OFFSET_MARK`) is set by DSP gain, through `SetSSBGainOverride()`. That
gives exact dB steps and no span limit. Unkeyed, `CALIBRATE_OFFSET_SPACE` is true receive
(`RFReceive`): T/R relay in RX, PA out of circuit. So REST, the oscillation abort and every
connector change leave the PA undriven *and* switched out. `CALIBRATE_POWER_SPACE`, CW's unkeyed
state, leaves the T/R relay in TX with only the carrier off. That is not enough if a PA oscillates
into the short whatever its drive, as radio 2's did on 10 m.

## State machine

```
ROOT ─► MANUAL (initial)                    the existing manual flow
│   PowerPoint1 ► PowerPoint2 ► PowerPoint3 ► PowerComplete ─CURVE_COMPLETE► SSBPoint ► MeasurementComplete
│   MANUAL: RESET ► PowerPoint1
│   MANUAL: AUTO [AutoCalAllowed()] ► AUTO
│
└─ AUTO     enter / AutoCalEnter()   exit / AutoCalExit()   do / count_ms++
    AUTO: RESET ► MANUAL                     abort from anywhere; AutoCalExit() unkeys
    SETUP ──SELECT──► PROMPT ──SELECT──► BAND ──[service idle, settled]──► POINT
                       ▲  └─SKIP (L1 load)─┘                              │
                       │                                                  ▼
    POINT:  KEYDOWN ─settle─► [meter entry?] ─► METER_ENTRY ─SELECT─► COLLECT
                                    │             │ timeout                 │
                                    │             ▼                         │
                                    │          METER_PAUSED ─SELECT─► KEYDOWN
                                    └──────────────────────────────► COLLECT
    COLLECT ─► STORE ─[service idle]─► choice:
                        oscillation        ► BAND_DONE (abandon this band's sweep)
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
| POINT | Sets the drive for one point: CW on the load, the SSB tone for the load's last point and for the open and short. Its sub-states key, wait to settle, take the wattmeter entry when one is needed, and collect the averaged detector readings. |
| STORE | The main loop keeps the point's readings and runs the oscillation check. The choice that follows needs its results. |
| METER_ENTRY | Keyed. The operator dials in the wattmeter reading with the filter encoder and presses SELECT. |
| METER_PAUSED | The entry timed out (30 s). Unkeyed. SELECT re-keys the same point. |
| REST | Open and short only: 1 s in true receive between points to protect the PA. |
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
| `collect_ms` | 1000 on the load, 1500 on the open and short | Length of the averaged reading: about 75 and 110 readings. The load stays keyed through its sweep and runs hot, so its points are kept short (1.5 s keyed with the settle; 2.5 s before 2026-11-02); each open and short point is keyed for ≤ 2 s |
| `rest_ms` | 1000 | Receive between open and short points |
| `meterTimeout_ms` | 30000 | Unkey if no wattmeter entry arrives |

## How it works with the hardware

- **Actions ask, the main loop does.** The state machine runs in the 1 ms timer interrupt. Its
  actions only record what they want in `autoCal`: keyed or not (`keyed`), CW or SSB
  (`ssbMode`), the band, a statistics clear, a point to store. `PowerCalAutoService()`, called
  from `loop()`, does it in a safe order:
  1. unkey, when asked to, or before a mode or band change;
  2. switch the mode (`OFFSET_START`/`OFFSET_END`);
  3. change band with `SetCalibrationBand()`;
  4. clear the statistics;
  5. store the point;
  6. key last.

  The keying dispatches the ModeSm `PTT_PRESSED`/`PTT_RELEASED` events directly.
- **`AutoServiceIdle()` means the hardware is there.** The service stays pending until ModeSm is
  keyed or not, in CW or SSB, as requested. When it is done, it restarts `count_ms`, so the
  settle, collect and rest times count from the actual change.
- **Always unkey on the way out.** Every key-down path leaves through STORE's choice, REST,
  METER_PAUSED or AUTO's exit. RESET is only dispatched from the main loop, so `AutoCalExit()`
  unkeys ModeSm directly, without waiting for the service. `InitializePowerCalibration()` clears
  the session, because `PowerCalSm_start()` does not run exit actions.
- **Operator controls while keyed.** `HandleButtonPress()` normally ignores every button in the
  MARK states. During AUTO it lets SELECT, ZOOM and HOME through. The physical PTT
  (`iPTT_PRESSED`/`iPTT_RELEASED`) is ignored during AUTO.
- **One dispatcher at a time.** Main-loop events go through `PowerCalDispatch()`, and the timer
  skips its DO event while one is in progress (`PowerCalSmBusy()`). Interrupts stay enabled,
  because the manual flow's actions use I²C and fit the power curve.
- **Readings.** COLLECT's statistics clear is done by the service.
  `PerformSWRBridgeReading()` adds every key-down reading in either MARK state
  (`AccumulateSWRBridgeStats()`), and the service reads them for STORE with
  `GetSWRBridgeStats()`. CAT `BR` is refused during AUTO, because it shares these statistics.
- **Nothing reaches `ED` before the operator accepts on REVIEW.** The session results live in
  `autoCal` and the per-band buffers. `InitializePowerCalibration()` still zeroes the four SWR
  arrays on entry. That must stop before AUTO stores anything
  (`swr_calibration_approach.md` §4).

## Still to do

- **Run it on a radio.** At level 1 on a dummy load, then the open and the short, check:
  - the ZOOM abort while keyed;
  - the T/R relay in RX during REST on the open;
  - the stored values against the 2026-11 bench sheets;
  - the SWR read on the 100 Ω and 200 Ω loads afterwards.
- **Use `SWR_CalLevel` in the display:** mark an uncalibrated band's SWR, and show the power
  reading's expected accuracy.
- **100 W PA:** AUTO runs with the 20 W PA only. The bridge calibration applies to both, but the
  100 W PA's power curve still comes from the manual flow.
