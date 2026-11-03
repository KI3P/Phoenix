---
title: SWR Bridge — Measured Behaviour (bench tests, Oct–Nov 2026)
type: hardware
status: draft
created: 2026-10-24
updated: 2026-11-02
tags: [swr, ad8307, ad7991, bridge, directivity, calibration, power-sensing, pa, measurement]
source_refs: [sources/ad8307-datasheet, sources/t41-ep-schematics]
related: ["[[filter-board-electronics]]", "[[log-amp-power-detection]]", "[[filter-boards]]", "[[calibration-state-machines]]", "[[cat-control]]"]
---

# SWR Bridge — Measured Behaviour

**The SWR bridge works; its readings were wrong because of the radio's ADC.** The AD7991
pulled each detector reading about 15 % toward the other, which made SWR read 30–90 % high
on both radios. Since 2026-11-01 the firmware discards two conversions after each channel
switch ([[filter-boards]]). With that fix and a calibration against a wattmeter, an open and
a short, radio 1 reads a 2:1 load as 1.97–2.04 and a 4:1 load as 3.96–4.22 on 160 m to
20 m. Radio 2 had a real leak as well, which its L44 rebuild removed on 2026-11-02; it now
reads 1.97–2.08 and 3.87–4.31. The October 2026 investigation, which blamed a leak in the bridge, is kept at the end
as history. The full write-up is the SWR technical report
(`code/tools/swr/swr_bridge_technical_report.md`, the local copy of the Claude Doc).

## Summary
- **The readout was at fault.** At U20's pins, radio 1's detectors read 100 Ω and 200 Ω
  within a few percent. The ADC mixed the channels, ADC_F = 0.83·Vf + 0.14·Vr and
  ADC_R = 0.18·Vf + 0.80·Vr, which cut the reflected-to-forward gap to 0.65 of its true value.
- **Cause:** the AD8307 outputs (12.5 kΩ) feed the AD7991 directly, and each read selected a
  channel and converted it at once, so the sampling capacitor kept part of the other
  channel's voltage. Each discarded conversion cut the crosstalk by the same factor again.
- **Fix:** two discarded conversions after each channel switch (commit 6217be9). A bridge
  update now takes about 3 ms instead of 1 ms.
- **SWR calibrates:** with the 50 Ω/open/short calibration, every 100 Ω and 200 Ω reading on
  radio 1 falls inside the bench-VNA range on all four bands tested. The firmware procedure
  is `code/docs/swr_calibration_approach.md`.
- **The bridge's own leak is small once L44 is rebuilt:** radio 1 −42/−40/−31/−22 dB on
  160/40/20/10 m at the pins, radio 2 −40/−41/−37/−30 dB from its fixed-firmware readings,
  against −35/−37/−27/−17 dB for the bench board. Both L44s are now flush with screened
  through lines grounded at one end (radio 1 on 2026-11-01, radio 2 on 2026-11-02).
- **Radio 2's original L44 did leak:** its 50 Ω load read SWR 1.11/1.23/1.48 on 40/20/10 m
  on the fixed firmware. Removing C147/C154 did not help; the L44 rebuild did (Radio 2's
  rework).
- **Forward power** calibrates to 0.1–0.2 dB rms per band against a wattmeter. Drive-step
  slopes read 2–6 % low. With both radios in the same build, their forward offsets differ by
  an almost constant 1 dB, so per-band defaults work to about ±1.5 dB without a wattmeter
  (`code/docs/swr_calibration_approach.md`, Section 2).
- **Open:** 10 m reads about 2 dB high at the PA's ceiling on radio 1 since its rebuild, and
  about 0.55 dB low on radio 2 since its capacitor change. Radio 2's PA oscillates into a
  short on 10 m in every build.
- **Withdrawn:** the October in-radio leak and radio 1's −9 dB extra leak, both fitted to
  compressed readings (History). Radio 2's leak, put down then to capacitance from the through
  line to its windings, was real but smaller; screening the through line removed it, which
  fits that explanation.

## The readout error and its fix

**How it showed up (2026-11-01).** Radio 1 was measured again after the owner rebuilt its L44.
C147/C154 were already out, with 100 pF on both detector inputs ([power-sensing lab book]
entries 29–31). The bench series was `code/tools/swr/swr_bench_measurements_radio1_l44_flush.csv`:
144 rows, loads directly on ANT1, open/short/loads added on 160 m, and no oscillation into the
10 m short.
- The bare 50 Ω load read 1.08–1.10 on 160/40 m, 1.21 on 20 m and 1.46 on 10 m.
- 100 Ω and 200 Ω read as before the rebuild, 2.8–3.3 and 5.9–7.1 against 2.03 and 4.06. A leak
  small enough for 1.10 on the 50 Ω load cannot move them that far.
- One factor fits all 24 load readings on four bands to 0.12 dB rms: measured return loss =
  0.666 × true, plus a small vector leak per band (`code/tools/swr/readout_compression_fit.py`).
  It does not depend on band or level, so it is not RF.
- The external coupler with its dummy load is not neutral on radio 1: 19 dB return loss on
  40 m through it, against 26.5 dB with the load directly on the connector.

**The detector pins against the ADC** (`code/tools/swr/detector_readout/`). An AD2 on U20 pin 3
(FWD) and pin 4 (REF), x10 probes, was keyed into open, short, 50, 100 and 200 Ω at −11 and
−20 dBFS on four bands. The radio's own `BR` readings came from the same key-downs. SWR
relative to each band's open/short, pins / radio ADC, original firmware:

| Band | 100 Ω (true 2.03) | 200 Ω (true 4.04–4.08) | 50 Ω (true 1.00–1.02) | Leak at the pins |
|---|---|---|---|---|
| 160 m | 1.94–1.96 / 2.82–2.86 | 3.90–3.98 / 6.26–6.31 | 1.01–1.02 / 1.08 | −42 dB |
| 40 m | 1.98–1.99 / 2.88–2.91 | 3.88–4.28 / 6.39–6.80 | 1.02 / 1.09 | −40 dB |
| 20 m | 2.02 / 2.93–2.94 | 3.90–4.26 / 5.77–6.62 | 1.06 / 1.20–1.21 | −31 dB |
| 10 m | 2.18–2.20 / 3.14–3.28 | 4.23–4.39 / 6.48–6.67 | 1.17 / 1.46 | −22 dB |

- **The ADC mixes the channels.** Over all 40 key-downs it fits, to 2.6 and 3.1 mV rms:
  ADC_F = 0.83·Vf + 0.14·Vr + 31 mV and ADC_R = 0.18·Vf + 0.80·Vr + 22 mV.
  - The mix passes Vf − Vr with a gain of 0.65, but common changes with a gain of 0.98.
  - Forward power and the open/short fit, where both channels move together, are barely
    affected. SWR, which is the gap between them, is compressed by a third.
- **The probes did not disturb it.** The radio's readings with them on matched the bench
  sheet within 1–2 mV.
- **Radio 2** fits the same compression with a factor of 0.53–0.60, leaving leaks of about
  −27 to −30 dB on 40 m and −20 dB on 20 m *(from `readout_compression_fit.py`; its pins have
  not been probed)*. Its fixed-firmware readings later confirmed a leak of that size before
  its L44 rebuild (Radio 2's rework).

**Mechanism** (supported by the fix). The firmware selects a channel and converts it in one
I²C transaction ([[filter-boards]]). The AD8307 output (12.5 kΩ source) feeds the AD7991
directly, and the sampling capacitor still holds about 15–19 % of the previous channel's
voltage ([[filter-board-electronics]]). The cross terms, FWD-from-REF / REF-from-FWD, were:
- none discarded: 0.144 / 0.186;
- one discarded conversion: 0.022 / 0.042, about their squares;
- two discarded: 0.007 / −0.008. The Vf − Vr gain (0.972) now equals the common-mode gain
  (0.971).

**Fix, verified 2026-11-01.** The firmware discards two conversions after each channel
switch. Radio 1's own ADC then reads the loads as the pins do (open/short reference;
`code/tools/swr/detector_readout/results_fixed2.json`):

| Band | 50 Ω (true 1.00–1.02) | 100 Ω (true 2.03) | 200 Ω (true 4.04–4.08) |
|---|---|---|---|
| 160 m | 1.01 | 2.01–2.03 | 3.88–3.90 |
| 40 m | 1.02 | 2.04–2.05 | 3.99–4.28 |
| 20 m | 1.06 | 2.08–2.09 | 3.96–4.32 |
| 10 m | 1.18 | 2.17–2.26 | 4.07–4.22 |

10 m carries the bridge's own −22 dB leak. A bridge update now takes about 3 ms instead of
1 ms. For a future board, about 10 nF from each OUT to ground, or a buffer, would make the
discards unnecessary.

## SWR calibration on the fixed firmware
The full 144 rows were repeated on 2026-11-01 (`code/tools/swr/swr_bench_measurements_radio1_adcfix.csv`).
With the wattmeter forward fit and the open/short reflected fit, every 100 Ω and 200 Ω point
falls inside the bench-VNA range:

| Band | 50 Ω | 100 Ω (true 2.03) | 200 Ω (true 4.04–4.08) | Forward fit rms (worst) |
|---|---|---|---|---|
| 160 m | 1.01 | 1.97–2.03 | 4.04–4.22 | 0.21 dB (0.57) |
| 40 m | 1.02 | 2.00–2.02 | 3.96–4.21 | 0.12 dB (0.19) |
| 20 m | 1.06 | 2.01–2.04 | 3.96–4.20 | 0.11 dB (0.18) |
| 10 m | 1.22 | 2.26–2.42 | 4.29–4.73 | 0.69 dB (1.15) |

- **Open and short:** they agree within 0.04 dB on 160 m to 20 m.
- **Drift checks:** each repeated its band's first reading within 0.08 dB.
- **10 m forward fit:** 0.13 dB rms without the ceiling points (Open issues).
- **Stored values:** the notebook's Section 11.7 gives the per-band values from this sheet;
  they have not been loaded into radio 1 yet.
- **Procedure:** `code/docs/swr_calibration_approach.md` sets out how the firmware should run
  this calibration on every band with three connector changes at most, and what it should
  store. It defines three levels: open and short only (power within about ±1.5 dB), one
  wattmeter reading per band (±0.8 dB), and the full sweep above (0.1–0.2 dB rms). The SWR is
  almost the same at all three, because the reflected line is fitted against the forward one.

## Radio 2's rework (2026-11-01 and 11-02)
Radio 2 got radio 1's changes in two steps, with the full 144-row series on the fixed
firmware before and after each (`code/tools/swr/swr_bench_measurements_radio2_adcfix.csv`,
`_radio2_c154_out.csv`, `_radio2_l44_shielded.csv`):
1. **Detector inputs:** C147 and C154 removed, and 100 pF across pins 1 and 8 of U14 and U19.
2. **L44:** screened through lines, grounded at one end. Still 20 turns per core, flush.

Loads directly on ANT1, read against each band's open (uncalibrated, nominal 25 mV/dB):

| Build | 50 Ω on 40 / 20 / 10 m | 100 Ω (true 2.03) on 20 / 10 m | Key-up floor, FWD / REF |
|---|---|---|---|
| Original | SWR 1.11 / 1.23 / 1.48 (return loss 25 / 19.5 / 14.2 dB) | 2.38–2.42 / 2.93–3.12 | 395 / 705 mV |
| Detector inputs changed | 1.13 / 1.30 / 1.58 (24 / 17.7 / 12.9 dB) | 2.41–2.50 / 3.06–3.14 | 327 / 370 mV |
| L44 rebuilt as well | 1.02 / 1.03 / 1.07 (41 / 37 / 30 dB) | 2.00–2.03 / 2.00–2.03 | 315 / 369 mV |

- **The leak was in L44.** The detector-input step lowered the floors and flattened the open
  across bands (10 m had read 11 mV apart from the other bands), but left the leak as it was,
  1–2 dB worse above 20 m. The L44 rebuild removed it: radio 2 now reads the 50 Ω load
  better than radio 1 above 20 m (31 and 22 dB on 20 and 10 m).
- **The forward offsets moved with the detector inputs, not with L44.** On 10 m the offset
  rose 3.1 dB and on 20 m 0.9 dB at the capacitor step, consistent with C147's zero at
  32 MHz ([[filter-board-electronics]]); the L44 rebuild moved them by 0.4 dB at most. With
  the turns allowed for (radio 2 has 20, radio 1 has 10), radio 2's offsets are now 0.9–1.2 dB
  above radio 1's on 160–20 m and 0.4 dB above on 10 m. Before, the gap swung from +1.0 dB
  on 160 m to −2.2 dB on 10 m.
- **Calibrated** (wattmeter forward fit, open/short reflected fit), the final build reads:

  | Band | 50 Ω | 100 Ω (true 2.03) | 200 Ω (true 4.04–4.08) | Forward fit rms |
  |---|---|---|---|---|
  | 160 m | 1.02 | 1.97–2.06 | 3.95–4.23 | 0.12 dB |
  | 40 m | 1.02 | 2.01–2.08 | 3.87–4.31 | 0.13 dB |
  | 20 m | 1.03 | 2.03–2.06 | 4.16–4.25 | 0.13 dB |
  | 10 m (open only) | 1.06 | 1.98–2.00 | 3.74–4.01 | 0.14 dB |

  The forward rms leaves out the ceiling points and the closing checks (below). 10 m has no
  short (the PA oscillates into it), so its reflected line comes from the open alone.
- **Open–short gap:** 0.12, 0.18 and 0.38 dB on 160, 40 and 20 m, larger than radio 1's
  0.04 dB, although the loads read correctly. Not explained.
- **Closing checks:** in the last run the CMU-200 read 0.2–1.0 dB lower at the end than at
  the start of each band, while the radio's forward detector moved 0.3 dB at most. Most likely
  the 60 dB coupler or its coupled-port cable reseated differently; those four rows are left
  out of the fits above.

## Open issues
- **10 m reads about 2 dB high at the PA's ceiling** (radio 1, since the L44 rebuild).
  - **The symptom:** keyed at 0 dB attenuation (about 6.6 W), the forward reading sits 2.0–2.3 dB
    above the fit through the other points, and the reflected one 1.4–2.2 dB. The AD2 shows the
    same rise at the pins, so it is not the readout.
  - **When it appears:** before the rebuild (2026-10-27) these points fitted within 0.2 dB. Other
    bands' ceilings fit within 0.25 dB.
  - **The output is clean:** nothing from 10 MHz to 2.7 GHz above −62 dBm (about 40 dB below the
    carrier), and nothing within ±2 MHz above −82 dBm.
  - **The forward detector over-reads.** Stepping the attenuation from 6 dB to 0 dB, the forward
    pin kept rising by 84 mV in all, while the reflected pin levelled off and the wattmeter
    stayed at 6.6 W.
  - **Next:** look below 10 MHz, repeat with the load directly on the connector, and check what
    the rebuild changed near the PA and the 10 m LPF. Data: `code/tools/swr/ten_m_ceiling/`.
- **200 Ω reads higher at the higher drive** at the pins on 40 m and 20 m: 4.26–4.28 at
  −11 dBFS against 3.88–3.90 at −20 dBFS. 100 Ω does not change with level.
- **Radio 2's 10 m ceiling reads about 0.55 dB low** since its capacitor change; before, it
  sat on the line. Radio 1's reads 2 dB high.
- **Radio 2's open–short gap** (0.12–0.38 dB on 160–20 m) is larger than radio 1's, although
  its loads read correctly.
- **Radio 2's pins have not been probed.** It has the new firmware; its stored calibration
  is out of date, like radio 1's.
- **Radio 1's stored calibration is out of date.** The fix moved the forward readings by up to
  about 110 mV at the same power.
- **The 60 dB external coupler** has never been swept from 1.8 to 30 MHz, and its readings
  moved 0.2–1.0 dB on reconnection in radio 2's last run.
- **What triggers the stuck-DIGITAL fault** (radio 2), and is it firmware?
- **Is radio 2's 10 m oscillation into a short** a PA stability problem common to the design?
  It persisted through both bridge changes, so it is in the PA, not the bridge.
- **Does the calibration hold with the 100 W PA?** Readings above about 16 W are extrapolated.

## Transmitter behaviour seen during the tests
- **PA oscillation into a short on 10 m (radio 2).** Keyed into a short directly on the
  connector, the forward detector reads 1700–2100 mV whatever the tone level, with or
  without the tone. It happened in October and in all three runs of 2026-11-01/02, whatever
  the bridge build. That is well above the 1848 mV of full 10 m output into 50 Ω, which
  suggests a VHF oscillation, where the coupler and detector respond more. Through the
  external coupler's line, the PA oscillated only at tones of −17 dBFS and below. A
  mismatched 10 m antenna could trigger it in normal use.
- **DIGITAL transmit stuck without the tone (radio 2).** The radio keyed and the bridge
  collected readings, but the output did not depend on the tone: about 640 mV forward on
  40 m, against a 466 mV floor. It survived leaving and re-entering DIGITAL mode and
  restarting the PC's audio stream, and cleared only with a power cycle. CW transmit worked
  meanwhile. It first appeared after several band changes made while in DIGITAL mode; the
  trigger is not established.
- **Reboot on static discharge (radio 1).** The Teensy reset (USB disconnect and
  re-enumeration) when a load was fitted by hand. The radio came back with its stored
  settings.
- **20 W PA power ceilings** (CW at 0 dB attenuation, into 50 Ω):

  | Band | 160 m | 40 m | 20 m | 10 m |
  |---|---|---|---|---|
  | Radio 1 | 11.7 W | 15.0 W | 10.4 W | 6.0 W |
  | Radio 2 | 12.6–12.7 W | 16.6–16.9 W | 9.4–9.9 W | 5.3–5.7 W |

  Set-points above these, such as 10 W on 10 m, give the ceiling, and the attenuator sits at
  0 dB.

## Implications
- **Firmware calibration:** follow `code/docs/swr_calibration_approach.md`.
  - Always calibrate the reflected line from an open and a short (or the open alone where the
    PA oscillates into the short).
  - For power, a wattmeter sweep per band is best; one reading per band gives ±0.8 dB; per-band
    defaults alone give about ±1.5 dB, but only on radios with the same bridge build.
  - Never take the slope from drive steps or the offset from the VNA path model.
  - Drop a ceiling point that sits off the line.
  - Re-check between sessions, or accept about 0.5 dB of drift.
- **Before modelling a bridge leak,** probe the detector outputs at U20's pins with the AD2
  (`code/tools/swr/detector_readout/`). The radio's ADC readings are not the detector voltages.
- **Measurement method** (for future bench work):
  - verify every DIGITAL row actually carries the tone (the script's tone check);
  - fit loads directly on the connector, above all on 10 m;
  - do not key repeatedly into a short on 10 m.

## History: the October 2026 investigation
From 2026-10-24 to 2026-10-31 the high SWR readings were taken to be a leak in the bridge,
10–20 dB worse inside the radio than on the bench. The sections below record that work. Every
radio reading in them was taken through the mixing readout. So their load readings, leak
figures and mechanism fits are superseded, and their slopes, offsets and floors are pre-fix
values.

What still holds:
- **The forward-channel conclusions.** Log conformance limits the accuracy. Gain-step slopes
  read low: still 0.5–1.0 mV/dB low on 160–20 m after the fix. Offsets cannot be carried
  between bands by the VNA path model, and calibrations drift 0.3–0.5 dB between sessions.
  (Carrying them by another radio's measured offsets works once the builds match: Radio 2's
  rework.)
- **The bench measurements and models of L44:** the VNA sweeps, the coupler models and the
  re-measured fixture.

### How it was measured
The script `code/tools/swr/swr_bench_measure.py` ran each radio under CAT control. Its
`README.md` and the notebook `swr_calibration_50_open_short.ipynb` (Section 11) give the
method and analysis. The firmware commands it relies on are in [[cat-control]].
- **Frequencies:** 1.9, 7.1, 14.2 and 28.5 MHz. Every row used the 20 W PA and ANT1.
- **Bridge readings:** both detectors were read raw, before the display's running average,
  over 2 s of each key-down (`BR`).
- **Forward power:** a CMU-200 on a 60 dB directional coupler between the radio and a 50 Ω
  dummy load, at 300 kHz bandwidth, averaging 10 readings per row.
- **Standards and loads:** an open, a short, 100 Ω and 200 Ω ceramic resistors, and the
  dummy load. Each load's Γ came from its VNA sweep.
  - The reflected channel was driven by DIGITAL-mode tones from −11 to −26 dBFS, about 1 W
    and below.
  - Forward rows were CW at fixed set-points plus DIGITAL tones from −6 to −30 dBFS.

| Session | Radio | Loads fitted | Notes |
|---|---|---|---|
| Radio 1 | 1 | On the external coupler's output | Rebooted once (static discharge); rows after it were re-run |
| Radio 2, session 1 | 2 | On the external coupler's output | Four 10 m short rows oscillating, excluded |
| Radio 2, session 2 ("radio 3" sheet) | 2 | Directly on the radio's connector | The DIGITAL tone was lost until a power cycle; those rows were re-run. The 10 m short was skipped (oscillation) |

**The external coupler barely matters on 40 m and 20 m.** Radio 2 was measured both ways. On
40 m and 20 m its loads read nearly the same either way, for example 100 Ω at 3.3–3.4 through
the coupler and 3.1–3.2 direct on 40 m. On 10 m the extra line length changes the readings
substantially. Loads for 10 m must therefore go directly on the connector.

### Forward channel (power)
Per-band fit of the detector reading to the CMU-200:

| Band | Radio 1 slope | Radio 2 S1 slope | Radio 2 S2 slope | Fit residual rms (worst), dB |
|---|---|---|---|---|
| 160 m | 24.61 | 23.42 | 22.96 | 0.09 (0.14) / 0.23 (0.39) / 0.21 (0.42) |
| 40 m | 24.63 | 24.62 | 23.95 | 0.10 (0.23) / 0.23 (0.52) / 0.19 (0.41) |
| 20 m | 24.14 | 25.03 | 25.04 | 0.11 (0.21) / 0.08 (0.14) / 0.11 (0.24) |
| 10 m | 24.77 | 25.35 | 25.48 | 0.16 (0.26) / 0.12 (0.23) / 0.15 (0.23) |

Slopes are in mV/dB. "S1" and "S2" are radio 2's two sessions.

- **Log-law curvature limits accuracy.** Radio 2's residuals curve smoothly: about −0.5 dB at
  the bottom of the range, +0.4 dB in the middle, −0.35 dB near the top. This is the AD8307's
  conformance error, within the datasheet's ±1 dB ([[ad8307-datasheet]]). A slope and offset
  cannot remove it. Radio 1's chip is about twice as linear.
- **One slope does not fit every band on radio 2.** Its 160 m slope is 5–7 % below 40 m in
  both sessions, which fits the open-OFS offset loop near 1.5 MHz
  ([[log-amp-power-detection]]). Radio 1 shows no 160 m effect. Its slopes agree within 2 %.
  The "one slope per chip" rule therefore holds on some chips and not others.
- **Per-radio calibration is required.**
  - The two radios' offsets differ by about 6–7 dB on 40 m. Most of it is the coupler: radio
    2's L44 has 20 turns per core, so its forward sample is −26 dB instead of −20 dB (owner,
    2026-10-31). The rest is within the AD8307's ±5 dB intercept spread.
  - Their key-up floors differ to match: 557–570 / 587–608 mV (forward / reflected) on
    radio 1, against 452–466 / 637–650 mV on radio 2.
- **Calibrations drift between sessions.** On radio 2, session 1's calibration applied to
  session 2's readings is off by +0.27 to +0.53 dB (mean), against 0.11–0.21 dB rms for
  session 2's own calibration. Whether the radio or the reconnected measurement chain moved is
  not known.
- **Gain steps (DIGITAL tone steps of 3 dB) cannot replace the wattmeter for the slope.** The
  CMU-200 shows each 3 dB tone step moving the output by only 2.5–2.97 dB (0.83–0.99 dB/dB),
  even 20 dB below the top level. The gain-step slope therefore reads 0.4–1.7 mV/dB (2–7 %)
  low on every band of both radios.
- **Carrying the forward offset between bands with the VNA path loss fails.** This
  contradicts the robustness expected in [[filter-board-electronics]]. The error against the
  40 m calibration, in dB:

  | Band | Radio 1 | Radio 2 S1 | Radio 2 S2 |
  |---|---|---|---|
  | 160 m | −0.33 | +0.34 | +0.16 |
  | 20 m | −0.43 | −1.98 | −2.13 |
  | 10 m | −2.57 | −3.0 | −3.3 |

  The 60 dB external coupler was assumed flat and has not been swept. Part of the 10 m error
  may therefore be the coupler's, but the 20 m difference between the radios is the boards'.
  Each band needs its own wattmeter point.
- **Detector floor.** The key-up floor is about −62 dBm at the detector by the nominal law,
  roughly 16 dB above the AD8307's own −78 dBm floor, so something is picked up. It equals
  about 25 µW on the line and only affects reflected readings of good loads below about
  0.1 W. The firmware should subtract it in the power domain.

### Reflected channel and SWR
The reflected slope and offset from the open and short fit well: 0.02–0.12 dB rms, with the
reflected slope within about 1 mV/dB of the forward slope. The open and short read alike
within 0.02 dB on 40 m. Even so, known loads read far too high. SWR read with the wattmeter
forward calibration plus the open/short reflected calibration:

| Band | Load (true SWR) | Radio 1 | Radio 2 S1 | Radio 2 S2 (direct) | VNA-predicted range |
|---|---|---|---|---|---|
| 40 m | 50 Ω (1.01) | 1.35† | 1.37–1.39 | 1.33–1.36 | 1.05–1.06 |
| 40 m | 100 Ω (2.03) | 2.84–3.01 | 3.29–3.35 | 3.09–3.24 | 1.91–2.17 |
| 40 m | 200 Ω (4.06) | 5.86–6.09 | 6.54–6.67 | 6.84–6.87 | 3.68–4.53 |
| 20 m | 50 Ω (1.01) | 1.26† | 1.80–1.81 | 1.67 | 1.09–1.11 |
| 20 m | 100 Ω (2.03) | 2.68–2.70 | 3.64–3.71 | 3.75–3.89 | 1.82–2.29 |
| 20 m | 200 Ω (4.06) | 5.37–5.77 | 6.56–7.63 | 7.74–8.19 | 3.43–4.92 |
| 10 m | 50 Ω (1.02) | 1.83† | 2.56–2.64 | 2.07–2.13‡ | 1.28–1.33 |
| 10 m | 100 Ω (2.03) | 3.43–3.84 | 2.18–2.23 | 4.42–4.69‡ | 1.53–2.84 |
| 10 m | 200 Ω (4.08) | 6.66–7.32 | 3.97–4.01 | 9.94–10.11‡ | 2.81–7.00 |

† Radio 1's 50 Ω reading is the dummy load through the external coupler. On radio 2, that
chain read within 0.1 of the bare load on 40 m and 20 m.
‡ No short on 10 m in session 2 (oscillation), so this is an open-only reflected calibration.

**The leak implied by the 50 Ω reading** is the bridge's effective directivity:

| Band | Radio 1 | Radio 2 | VNA, bare board as built |
|---|---|---|---|
| 40 m | about −16.5 dB | about −16.8 dB | −36.5 dB |
| 20 m | about −18.8 dB | about −12 dB | −26.5 dB |
| 10 m | about −10.7 dB | about −9 dB | −17.0 dB |

The VNA figure is J23→reflected detector over ANT4→reflected detector, from the board sweeps
in [[filter-board-electronics]]. A usable bridge needs better than about −25 dB.

**What this rules out:**
- **Not a calibration error.** A wrong reflected offset would scale every load's |Γ| by the
  same factor. Here the factor depends on the load: on 40 m, 1.44 for 100 Ω and 1.18 for
  200 Ω, while the open and short read 1.00.
- **Not the external coupler** (above), except on 10 m.
- **Not a shifted balance impedance.** A bridge that balances at some Z0 ≠ 50 Ω fits the open,
  short, 100 Ω and 200 Ω readings on radio 1. But it mispredicts radio 2's bare 50 Ω load:
  2.0 predicted against 1.33 measured on 40 m, and 1.5 against 2.6 on 10 m. The fitted Z0 also
  wanders between bands and radios.
- **~~Not correctable in firmware.~~** *(Withdrawn 2026-11-01: the error is in the readout.)* The detectors measure magnitude only, so a load's reading
  depends on its unknown phase relative to the leak. The leak also differs between the two
  radios: on 20 m a good load reads 1.26 on radio 1 and 1.67–1.81 on radio 2. On radio 2 at
  10 m, the 100 Ω and 200 Ω loads happened to read correctly in session 1 while the 50 Ω load
  read 2.6. That shows how a correct reading can be a coincidence of phase.

**Superseded 2026-11-01: the cause is mostly the ADC readout** (see "The readout error and its fix"). The
leak figures above were read through the ADC's channel mixing; the paragraphs from here to
the end of this section are kept as the record of the October reasoning.

**Cause (updated 2026-10-31): radio 2 capacitive, radio 1 unknown.** See "L44 with its real
wiring" below. The original reasoning follows. The VNA measured a bare board driven by the VNA. In the radio the PA
drives it, and the bridge sits with the rest of the hardware. Candidates:
- stray coupling from the PA or its wiring into the reflected detector, which bypasses the
  coupler (the high key-up floor shows the detectors do pick up stray signals);
- how L44 is wired into its surroundings in the radio (leads, stray coupling from the line,
  return path and ground at pad 5), which the model below shows matters more than its windings.

**L44 modelled from its winding (2026-10-25).** L44 is a 1:10 current transformer (hole 1)
and a 10:1 voltage transformer (hole 2) on one BN-43-202 binocular, described in
[[filter-board-electronics]]. `code/tools/swr/l44_coupler_model.py` solves it by nodal
analysis. It treats the core as a three-limb magnetic circuit, so the two holes couple
through the centre web. *(infer: AL, ferrite dispersion and leakage are assumed values.)*
- **The balance rests on the port loads.** The reflected port nulls a forward wave only when
  the forward port is loaded by 50 Ω × n_ct/n_vt, and vice versa. The pads give 50.8 Ω.
  R20/R28 are not fitted; fitted, they would give 26 Ω and about 7 dB directivity.
- **A correctly built L44 leaks about −30 dB on 160 m and −30 to −37 dB on 40–10 m.** The
  160 m leak comes from the core: magnetising current and the coupling between the holes.
  This is close to the board as built, which leaks more on 20 m and 10 m.
- **Flat errors:** one turn wrong, or a port load 10 % off, leaks about −25 dB on every band.
- **Rising errors:** to give radio 2's leak on its own, an imperfection would need 12–16 pF
  from the line to a coupled port, or 280–350 nH in series with the line after the voltage
  tap. Only these two hold roughly across bands, and both far exceed what L44's windings and
  leads supply.
- **~~The out-of-circuit sweep matches the radios.~~ Withdrawn 2026-10-31.** The original
  sweep of L44 alone in its perfboard fixture (`RF_in_to_*.s2p`) leaked −16.4/−11.3/−7.4 dB,
  like radio 2. Re-measured with all four ports terminated, the same fixture leaks
  −29.8/−25.0/−19.5 dB, and its forward coupling is flat where the original rose 3 dB by
  28.5 MHz. The original was taken in some other state. Its "2.8 dB rms" fit to radio 2's load
  readings becomes 7.6 dB rms with the re-measured sweep.

**L44 with its real wiring (2026-10-31).** Construction (owner): 0.55 mm insulated thick wires
(no screen), 0.3 mm enamelled windings over just over half of each hole's wall, every winding
from the pin 1/3 end to pin 5. Radio 1: 10 turns, core 20 mm up on 25 mm leads. Radio 2: 20
turns, flush, 5 mm leads. Fixture: 10 turns, flush on perfboard, 20 mm leads.
`code/tools/swr/l44_peec_model.py` models the core by its turns (closed ferrite rings link a
circuit only by its passes through each hole, so lead dress can change only the coupling through
air), every wire in air by the Neumann integral (radio board plane by images), and one
capacitance per turn to the thick wire in its hole.
- **Fixture:** 0.215 pF per turn fits the re-measured forward leak within 0.4 dB and 4°; the
  reverse leak, not fitted, agrees within 0.4 dB.
- **Radio 2:** 0.38–0.42 pF per turn (crowded turns) fits its 50 Ω leak within 0.8 dB rms and,
  through the bridge calibration, all its 40 m and 20 m load readings to 1.3–1.4 dB rms. Its leak
  is capacitive. Sized with 20 turns, it needs 6–8 pF from the line to the REF wire.
- **Bench board:** 0.34 pF per turn fits within 2.5 dB rms.
- **Radio 1: not explained.** Its readings fit a constant extra leak of about −9 dB at −84°
  (bridge_mechanism_model H3, 0.3 dB rms), which neither the board nor its coupler as modelled
  supplies.
- Inductive (air) and capacitive leaks have opposite phases and partly cancel, so the leak is
  very sensitive to construction. Fix if confirmed: a coax thick wire with its shield grounded at
  one end (tandem-match practice), or 10 spaced turns; flush mount, short leads. Check first:
  voltage winding to REF thick wire, about 7–8 pF expected on radio 2's coupler.

The discriminating test is to transmit into a good 50 Ω load and look at the reflected
detector's input, with the coupler's reflected output connected and then disconnected. A VNA
sweep of the bridge as installed in a radio would also locate it.

[power-sensing lab book]: ../../code/tools/swr/power_sensing_noise_lab_book.md
