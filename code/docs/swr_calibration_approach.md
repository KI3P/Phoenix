# SWR bridge calibration in the firmware

How the radio should calibrate its forward-power and SWR readings on every band by itself.
The operator connects only a few things to the antenna connector, in this order: a wattmeter
with its 50 Ω load (when one is available), an open, and a short. The firmware does the
keying, the fitting and the checks, and stores four values per band.

There are three levels, depending on the test equipment (Section 2). All three calibrate the
SWR almost equally well from the open and the short. The wattmeter only sets how accurately
the radio reads absolute power: within about ±1.5 dB without one, within ±0.8 dB with one
reading per band, and about 0.2 dB rms with a full sweep.

This approach was tested on radio 1 on 2026-11-01 and on radio 2 on 2026-11-02, both in the
same bridge build: C147 and C154 removed, 100 pF across the inputs (pins 1 and 8) of U14 and U19, and L44 flush to the board with screened through lines. It needs firmware with the AD7991
discarded-conversion fix (commit 6217be9). With that fix and a level 3 calibration, the two
radios read a 2:1 load (100 Ω) as 1.97–2.08 and a 4:1 load (200 Ω) as 3.87–4.31 on 160, 40
and 20 m. The measurements behind
every rule below are in `wiki/hardware/swr-bridge-measured-behaviour.md` and the SWR
technical report (`code/tools/swr/swr_bridge_technical_report.md`).

## 1. What is calibrated

Each detector (U14 forward, U19 reflected) gives a voltage that is a straight line in dB of
the power on the line. The firmware converts a raw ADC reading V (in mV) to line power with
(`LPFBoard.cpp`):

$$
P_\text{line}\,[\text{dBm}] = \frac{V}{25 + s} - 84 + o + 26 + 20
$$

Here 26 dB is the pad (`PAD_ATTENUATION_DB`), 20 dB is the coupler (`COUPLER_ATTENUATION_DB`),
s is a slope correction in mV/dB and o is an offset correction in dB. Each channel has its
own s and o on each band, so there are four values per band:

| Value | Meaning | Stored in |
|---|---|---|
| s_f | forward slope correction (mV/dB) | `ED.SWR_F_SlopeAdj[band]` |
| o_f | forward offset correction (dB) | `ED.SWR_F_Offset[band]` |
| s_r | reflected slope correction (mV/dB) | `ED.SWR_R_SlopeAdj[band]` |
| o_r | reflected offset correction (dB) | `ED.SWR_R_Offset[band]` |

SWR then follows from |Γ| = √(P_r/P_f).

What each standard fixes:

- **The wattmeter** is the only absolute power reference. Readings at several levels fix
  the forward line, s_f and o_f. Neither the open, the short nor a 50 Ω load can tell the
  radio how much power it makes.
- **The open and the short** reflect all the power, so the reflected power equals the
  forward power. With the forward line known, the forward reading gives that power, and the
  reflected line, s_r and o_r, follows.
- **Forward errors cancel in the SWR.** The reflected line is fitted against the forward
  line, so an error in o_f moves o_r by the same amount, and the ratio of the two powers is
  unchanged. An error in the forward slope scales the measured return loss by the same
  fraction, a few percent. This is why the SWR hardly depends on the calibration level.

What does not work, as the 2026 bench tests showed:

- **Slopes from drive steps** (DSP gain or the TX attenuator) read low: by 1.8–4.0 % on
  160–20 m and 4.5–6.3 % on 10 m on both radios with the fixed firmware. The transmitter is not
  dB-linear even 6 dB below its top drive. The nominal 25 mV/dB serves as well (Section 2).
- **Carrying one band's offset to the others by the VNA path loss** of the L44 fixture
  missed by 0.2–0.6 dB on 160 and 20 m and 1.3–1.6 dB on 10 m on both radios. Carrying it
  by another radio's measured band-to-band differences works better, provided both radios
  have the same detector input network (level 2, Section 2). Before radio 2's C147 and C154
  came out it left 10 m up to 3.5 dB out.
- **SWR without the open and short.** With nominal values on both detectors, radio 1 read a
  4:1 load as 3.1–3.4, because its two detectors' offsets differ. Radio 2 happened to read
  within 0.1 of its calibrated values; a radio cannot rely on that.

One slope per radio for all bands does hold within about 3 %: radio 1 24.1–24.9 mV/dB,
radio 2 25.0–25.2 mV/dB. The two radios' slopes differ by up to 4 %, which is
the error the nominal 25 mV/dB brings at levels 1 and 2.

## 2. Three calibration levels

The levels trade test equipment against the accuracy of the power display. All three use the
same open and short sweeps, so the SWR is almost equally good at each. The level is chosen
per calibration session and stored per band (Section 9), so a radio calibrated without a
wattmeter can be raised to level 2 or 3 later.

| Level | On the antenna connector, in order | Forward line from | Reflected line from |
|---|---|---|---|
| 1, no wattmeter | 50 Ω load (may be skipped); open; short | Per-band defaults for the radio's L44 turns, nominal slope (s_f = 0) | Open and short |
| 2, one reading per band | Wattmeter with its 50 Ω load; open; short | One wattmeter reading per band near 2 W, nominal slope (s_f = 0). Cheaper variant: one reading on 40 m, carried with the level 1 defaults | Open and short |
| 3, full | Wattmeter with its 50 Ω load; open; short | A sweep of 5–7 wattmeter readings per band | Open and short |

### Expected accuracy

Measured on radios 1 and 2 with the fixed firmware, both in the same bridge build
(2026-11-01 and 2026-11-02), on 160, 40, 20 and 10 m. The level 1 figures, and those of the one-reading
variant of level 2, are leave-one-out: each radio's defaults came from the other radio only,
so they show the build-to-build spread. As a guide, 1 dB is +26 %/−21 % in watts.

**Power**

| Quantity | Level 1 | Level 2, one reading per band | Level 2 variant, one 40 m reading | Level 3 |
|---|---|---|---|---|
| Forward power, 160–20 m | within ±1.5 dB (mean 0.9–1.2 dB) | mean within ±0.3 dB, worst point ±0.8 dB | mean within ±0.2 dB, worst point ±0.6 dB | 0.10–0.21 dB rms, worst point 0.56 dB |
| Forward power, 10 m | within ±0.9 dB (mean 0.4 dB) | mean within ±0.35 dB, worst point ±0.8 dB | mean ±0.5–0.8 dB, worst point ±1.0 dB | 0.13–0.19 dB rms, worst point 0.26 dB (ceiling point dropped) |
| Reflected power in W | Same error as forward | Same error as forward | Same error as forward | Same error as forward |
| PA output setting (power calibration fitted from the forward line) | Same error as forward | Same error as forward | Same error as forward | Same, or better if the PA curve uses the wattmeter readings directly |

**SWR**, read on known loads directly on the connector. Level 2 reads as level 1, since the
offset does not affect the SWR:

| Load (true SWR) | Radio 1, level 1 | Radio 1, level 3 | Radio 2, level 1 | Radio 2, level 3 |
|---|---|---|---|---|
| 100 Ω, 160–20 m (2.03) | 1.98–2.09 | 1.97–2.04 | 1.98–2.09 | 1.97–2.08 |
| 200 Ω, 160–20 m (4.04–4.06) | 4.02–4.31 | 3.96–4.22 | 3.87–4.32 | 3.87–4.31 |
| 50 Ω, 160–20 m (1.00–1.01) | 1.01–1.07 | 1.01–1.06 | 1.02–1.03 | 1.02–1.03 |
| 100 Ω, 10 m (2.03) | 2.20–2.36 | 2.13–2.28 | 1.97–1.99 (open only) | 1.98–2.00 (open only) |
| 200 Ω, 10 m (4.08) | 4.16–4.58 | 4.02–4.43 | 3.72–3.99 (open only) | 3.74–4.01 (open only) |
| 50 Ω, 10 m (1.02) | 1.20–1.21 | 1.18–1.19 | 1.06 (open only) | 1.06 (open only) |

Radio 2's PA oscillates into the short on 10 m, so its 10 m reflected line comes from the
open alone (Section 6). Radio 2's bridge is now the better of the two above 20 m: its return
loss on the 50 Ω load is 37 dB on 20 m and 30 dB on 10 m, against radio 1's 31 and 22 dB.

What sets each level's accuracy:

- **Level 1: build spread.** Nothing on the connector measures absolute power, so the forward
  line comes from defaults measured on other radios. After allowing for the L44 turns, radio
  2's offsets are 0.9–1.2 dB higher than radio 1's on 160–20 m and 0.4 dB higher on 10 m.
  Before radio 2's C147 and C154 came out, the gap swung from +1.0 dB on 160 m to −2.2 dB on
  10 m. The remaining difference is
  close to a constant offset per radio (pads, detector intercepts), which one wattmeter
  reading removes. These are two hand-built radios; revise the figures as more radios are
  calibrated at level 3.
- **Level 2: the nominal slope.** One reading fixes the offset exactly at about 2 W. Away from
  that point the error grows by the difference between the true slope (24.1–25.2 mV/dB) and
  25 mV/dB, up to about 0.8 dB at 16 mW or at full power.
- **Level 2 variant: one 40 m reading.** Carried to the other bands with the level 1
  defaults' band-to-band differences, it holds 160–20 m to ±0.6 dB, but 10 m only to
  ±1.0 dB. It relies on all radios having the same detector input network: before radio 2's
  C147 and C154 came out it left 10 m up to 3.5 dB out.
- **Level 3: detector log conformity,** about ±0.25 dB on these radios (Section 10).
- **The SWR at levels 1 and 2** reads at most 0.1 above level 3 on a 4:1 load on 160–20 m,
  and 0.15 on radio 1's 10 m. The nominal 25 mV/dB differs from these detectors' true slopes
  by up to 4 %, which scales the return loss by the same fraction.

### Level 1 defaults

The forward offset o_f for an L44 with 10 turns per core, with s_f = 0. For each radio, o_f is
chosen so that its wattmeter-fitted line reads correctly at V_f = 1700 mV, mid-range for these
detectors. Both radios are in the same build: C147 and C154 removed, 100 pF across the inputs (pins 1 and 8) of U14 and U19, and L44 flush to the board with screened through lines.

| Band | 160 m | 40 m | 20 m | 10 m |
|---|---|---|---|---|
| Radio 1 (10 turns) | −4.56 | −4.28 | −5.20 | −4.64 |
| Radio 2 (20 turns, moved to 10) | −3.50 | −3.08 | −4.26 | −4.20 |
| **Default o_f, 10 turns** | **−4.03** | **−3.68** | **−4.73** | **−4.42** |

- **Turns.** For an L44 with N turns per core, add 20·log₁₀(N/10) dB to o_f: +6.02 dB for
  radio 2's 20 turns. The firmware's `COUPLER_ATTENUATION_DB` (20 dB) assumes 10 turns. The
  turns need to become a configuration value (Section 9) so level 1 can pick the right
  defaults.
- **Other bands.** Interpolate linearly in log frequency between the measured bands: 80 and
  60 m between 160 and 40 m, 30 m between 40 and 20 m, 17, 15 and 12 m between 20 and 10 m.
  6 m takes the 10 m value, unverified.
- **Reflected line.** No level 1 default: level 1 always measures it from the open and short.
  An uncalibrated radio starts from radio 2's reflected line, kept at its place against these
  forward offsets (`PowerCalibration/AutoCalibration.md`, "Uncalibrated defaults").
- **The build matters.** C147 bridges the pad's series arm with a zero at 32 MHz, so it sets
  the forward offset on the upper bands. Removing C147 and C154 and adding the 100 pF moved
  radio 2's offsets by 3.1 dB on 10 m and 0.9 dB on 20 m. Its L44 rebuild then moved them by
  0.4 dB at most, while removing its leak. A radio without the detector-input changes should
  not rely on level 1 above 20 m.

## 3. Equipment and set-up

| Item | Requirement |
|---|---|
| Firmware | Includes the AD7991 fix: two discarded conversions after each channel switch, `AD7991::readADCsingle()` (commit 6217be9). Without it the readings compress the return loss to about 0.65 of its true value, and no calibration can recover it. |
| Wattmeter and load | Levels 2 and 3 only. A wattmeter between the antenna connector and a 50 Ω dummy load. Level 3 needs it accurate from about 0.1 W to the 20 W PA's full output (about 6 W on 10 m, 16 W on 40 m); level 2 needs it only near 2 W. ±5 % gives about ±0.2 dB on the power display. |
| 50 Ω load | Level 1: a dummy load on the connector for stage A, the first standard (Section 4). It gives the PA curve and the S50 check (Section 8). |
| Open | Nothing on the antenna connector. |
| Short | A low-inductance short fitted directly on the connector. |
| No cables or adapters for the open and short | A cable between the connector and a standard reduces its reflection by twice the cable's loss. Every SWR then reads high, and the checks cannot detect it. |
| PA | The 20 W PA for the whole procedure. The bridge sits after the LPF, so its calibration does not depend on the PA. |
| Antenna port | The port used in operation (ANT1 on the bench). |
| Warm-up | Calibrate after the radio has been on for some time. Calibrations moved 0.3–0.5 dB between sessions on radio 2. |

## 4. Order of operations: three connector changes at most

Each standard is fitted once and every band is measured with it before the next change.
The bench script `code/tools/swr/swr_bench_measure.py` uses the same order, so its sheets are
a model for the firmware's data log.

| Stage | On the antenna connector | What the firmware does on each band | Operator input |
|---|---|---|---|
| A | Wattmeter with its 50 Ω load | Key-up floor; forward sweep (5–7 CW levels); PA power curve (the existing power calibration) | Enter the wattmeter reading at each level |
| B | Open | Open sweep (5–6 levels), automatic | None |
| C | Short | Short sweep at the same levels, automatic | None |
| — | Fit, check, store | Section 7 and Section 8 for every band, then save | Accept or repeat failed bands |

Stage A must come first, because the reflected fit needs the forward line to convert the
forward readings into power. Stages B and C can come in either order. Ending with the short
means the operator then fits the antenna, which is the natural next step.

**By level (Section 2):**
- **Level 3:** the table as it stands.
- **Level 2:** stage A takes two wattmeter readings per band (Section 6), near 2 W and at the
  PA's ceiling. The connector changes are the same as level 3; the operator enters 22 readings
  instead of about 88.
- **Level 1:** stage A on a bare 50 Ω load, with no wattmeter readings. The forward line comes
  from the defaults, and the firmware sweeps the attenuator automatically. The sweep gives the
  PA power curve through the default forward line, the S50 check and the 50 Ω reference for
  the oscillation check. The connector order is the same as at levels 2 and 3: two changes and
  no operator readings. The operator may skip stage A for a quick SWR-only recalibration. The
  stored SSB gain correction then sets the open and short drive, and there is no S50 check.
  The firmware does not take the key-up floor (it is only needed for the optional
  subtraction in Section 7). (Decided 2026-11-02, replacing the earlier open, short, optional
  load order. See `code/docs/PowerCalibration/AutoCalibration.md`.)

**The existing power calibration belongs inside stage A.**
- **Use its readings twice.** It already takes wattmeter readings with the PA on its 50 Ω
  load. Recording both raw detector readings at the same time gives the forward sweep for
  free.
- **Fit the PA curve from the wattmeter, not the forward detector.** Its automatic mode
  (`AdjustAttenuation()`) currently builds the PA curve from the uncalibrated forward
  detector, scaled to one wattmeter reading. It should use the calibrated forward line
  instead, or the wattmeter readings directly.
- **Stop zeroing the SWR values.** `InitializePowerCalibration()` currently zeroes all four
  SWR arrays on every band. It must not, or a power calibration on one band erases the SWR
  calibration of all of them.

## 5. Taking a reading

Every calibration reading is the mean of raw ADC readings collected while keyed. The
display's 0.1/0.9 running average must not be used: its lag biases readings taken soon after
a level change.

1. Set the drive, key, and wait 0.5 s for the PA and the detectors to settle.
2. Clear the raw statistics (`ClearSWRBridgeStats()`).
3. Collect for 2 s. With the AD7991 fix, a bridge update takes about 3 ms and comes about
   every 16 ms, so 2 s gives about 120 readings.
4. Read the mean and standard deviation of each channel (`GetSWRBridgeStats()`). Readings
   from failed I²C reads are already left out.
5. Unkey, except when the next point follows at once on the same load.

**Key-up floor (stage A, each band).** In receive, take 50 single readings (`SampleSWRBridge()`)
and keep their means. With C147/C154 removed, radio 1 read about 230 mV forward and 355 mV
reflected.

## 6. The sweeps on each band

### Calibration frequencies

Use one frequency in the middle of each band. The coupling and the leaks change little
within a band.

| Band | 160 m | 80 m | 60 m | 40 m | 30 m | 20 m | 17 m | 15 m | 12 m | 10 m | 6 m |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Frequency (MHz) | 1.9 | 3.75 | 5.357 | 7.1 | 10.125 | 14.2 | 18.118 | 21.2 | 24.94 | 28.5 | 50.2 |

`BAND_4M` and `BAND_GENERAL` have no LPF of their own on the bench radios. Until that is
settled, they keep the values of the band whose LPF they use (Section 10).

### Stage A: forward sweep into the wattmeter

CW carrier, set by the TX attenuator (`ED.XAttenCW`, `SetTXAttenuation()`):
- **Levels:** at least 5, and better 6–7. Space them by 3–4 dB, from the PA's ceiling (0 dB
  attenuation) down to the lowest level the attenuator reaches (31.5 dB). The span must be
  at least 15 dB. On radio 1 the attenuator reached 0.10–0.21 W on 10 m and 40 m, and the
  ceiling was 6.6 W on 10 m and 15 W on 40 m, so the span is about 18 dB.
- **At each level:** record the operator's wattmeter reading P_k (in W), the attenuation, and
  both detector means V_f,k and V_r,k.
- **Drift check:** repeat the first level below the ceiling at the end of the band.
- **Settling:** the operator holds the key while the wattmeter settles. The firmware takes
  its reading (Section 5) when the operator enters the value.
- **Level 2:** one reading at the attenuation that gives about 2 W (1–3 W will do). That is
  well below the 10 m ceiling, where radio 1 reads high, and far above the detector floor.
  Choose the attenuation from the PA curve, or with the level 1 defaults (Section 2), which are
  close enough for this. Then a second reading at 0 dB, for the PA curve's ceiling.
  - **Why (2026-11-02):** the firmware's level 2 PA curve otherwise comes from the forward
    detector, which reads radio 1's 10 m ceiling about 2 dB high. The 0 dB reading puts the
    ceiling right, and no sweep point may read above it.

### Stages B and C: open and short sweeps

Automatic. Use the same drive levels on the open and the short, so the two sweeps can be
compared level by level. The firmware drives them with the SSB tone and sets its level with DSP
gain, because the CW carrier's only level control is the 31.5 dB attenuator, and that may not
give the 8 dB span below (`code/docs/PowerCalibration/AutoCalibration.md`). The tone also leaves
the radio in true receive between points:

- **Levels:** 5–6 of them, from about 1.2 W of forward power down in DSP-gain steps. Choose
  the first gain from the band's SSB point (stage A) for 50 Ω. The PA delivers a different
  power into an open or a short, but V_f measures what it actually delivers, so the drive need
  not be exact. If stage A was skipped, use the stored SSB gain correction, or step the gain
  on the open until V_f, read with the level 1 defaults, shows about 1.2 W.
- **Span:** at least 8 dB. DSP gain has no limit here, unlike the CW attenuator. Its accuracy
  does not matter, for the same reason.
- **Protecting the PA:** key each point for no more than 2 s, and leave at least 1 s in
  receive between points. The whole short sweep on one band then puts about 12 s of full
  reflection into the PA.
- **Oscillation check (abort rule).** Radio 2's PA oscillates into a short on 10 m: in
  October, and in all four runs of 2026-11-01 and 11-02, whatever the bridge build. Before the
  rebuild the forward reading locked at 1700–2100 mV whatever the drive; on 2026-11-02 it jumped
  52 mV up at the sweep's fourth point. After each point, compare V_f with the previous point.
  When the drive drops by Δ dB:
  - **V_f falls by at least 0.5 × Δ × 25 mV:** keep the point.
  - **V_f's spread jumps above 20 mV and to more than 4 times the previous point's:** the
    oscillation started during the point (radio 2's 10 m short at level 2: 4 → 56 mV), whatever
    its mean did. Treat it as below.
  - **V_f rises by more than 25 mV (1 dB):** the PA is oscillating. Unkey, discard that band's
    short (or open) sweep, and move on to the next band.
  - **Anything in between (a short fall):** the PA is most likely compressing near its ceiling.
    Leave out the previous point, which was the compressed one, keep this one, and add a point at
    the bottom of the sweep (at most 3 per sweep). Only the third short fall running counts as
    an oscillation: a locked oscillation never falls again, compression does a few dB lower.
  - The sweep's first point has no previous point: its V_f must not exceed the 50 Ω V_f at the
    same drive by more than 6 dB × 25 mV, or the sweep is discarded at once.
  - **Why only the first point, and why 6 dB (2026-11-02):** on the bench sheets the PA puts
    −3.3 to +4.9 dB into an open or short against 50 Ω at the same drive, and the short reads
    up to 7 dB above the open (radio 2, 160 m). A 3 dB margin against 50 Ω, or against the
    open, would have failed radio 2's 20 m open and abandoned five real short sweeps.
  - **The fall test is the dependable one:** real sweeps fall 17–28 mV per dB of drive.
  - **Why a short fall is not an oscillation (2026-11-02):** in radio 2's first level 1 run,
    the 1.2 W start point put the PA at its ceiling into the open on 20, 17 and 12 m. V_f fell
    only 21, 14 and 5 mV for 2 dB, with spreads of 1–5 mV, and the shorts on the same bands
    fell normally. Taking each short fall as an oscillation failed all three bands. The reading
    spread does not separate the two cases: radio 2's locked 10 m oscillation before the rebuild
    had a spread of 1–2 mV.
- **Falling back to one standard.** If only the short failed, fit the reflected line to the
  open alone (b_r = b_o, Section 7) and log the band as calibrated from the open only. If only
  the open failed, fit it to the short alone in the same way. The leak is then not cancelled,
  which costs up to half the open–short gap: about 0.25 dB on radio 1's 10 m, where the gap was
  0.51 dB, and at most 0.2 dB on radio 2's 160–20 m. If both failed, the band's SWR is
  uncalibrated.

## 7. The calculations

All fits are ordinary least squares in closed form, done per band.

### Forward line

Take x_k = V_f,k (mV) and y_k = 10·log₁₀(1000·P_k) (dBm on the line), and fit y = k_f·x + b_f:

$$
k_f = \frac{\sum_k (x_k - \bar x)(y_k - \bar y)}{\sum_k (x_k - \bar x)^2}, \qquad
b_f = \bar y - k_f\,\bar x, \qquad m_f = \frac{1}{k_f}
$$

**Saturation at the ceiling (10 m).**
- **The problem:** on radio 1's 10 m band since its L44 rebuild, the readings at the PA's
  ceiling (0 dB attenuation, about 6.6 W) sit 2.0–2.3 dB above the line through the other
  points. The cause is not known. With them in, the 10 m fit is 0.69 dB rms; without them,
  0.13 dB.
- **Rule for every band:** treat as ceiling points the 0 dB points and every point whose
  wattmeter reading is within 1 dB of the band's highest. Fit once without them, and compute
  each one's residual. Refit with those within 0.5 dB; leave the others out and log it.
  - **Why more than the 0 dB point (2026-11-02):** with the measured PA curves (tanh k =
    16–36), the firmware's 5 dB point is saturated too. On radio 1's 10 m the forward detector
    over-reads all through saturation, by about 0.9 dB at 6 dB and 2.3 dB at 0 dB
    (`code/tools/swr/ten_m_ceiling/`).

The stored values:

$$
s_f = m_f - 25, \qquad o_f = b_f + 38
$$

The 38 is 84 − 26 − 20, from the firmware's constants in Section 1. The fit above is level
3.

### Forward line at levels 1 and 2

Both keep the nominal slope, m_f = 25 mV/dB (s_f = 0), and set only the offset.

- **Level 1:** o_f is the band's default for this radio's L44 (Section 2), with
  20·log₁₀(N/10) added for N turns per core. Then b_f = o_f − 38.
- **Level 2:** from the band's one reading, P_1 (W) at V_f,1 (mV):

$$
b_f = 10\log_{10}(1000\,P_1) - \frac{V_{f,1}}{25}, \qquad o_f = b_f + 38
$$

The reflected line then follows exactly as below, from the forward line so defined.

### Reflected line

Each open and short point j has a reflected power equal to its forward power, which the
forward line gives: y_j = V_f,j / m_f + b_f, against x_j = V_r,j. Fit one slope to both
standards, with a separate offset for each:

$$
k_r = \frac{\sum_{\text{open}} (x_j - \bar x_o)(y_j - \bar y_o) + \sum_{\text{short}} (x_j - \bar x_s)(y_j - \bar y_s)}
           {\sum_{\text{open}} (x_j - \bar x_o)^2 + \sum_{\text{short}} (x_j - \bar x_s)^2}
$$

$$
b_o = \bar y_o - k_r\,\bar x_o, \qquad b_s = \bar y_s - k_r\,\bar x_s, \qquad
b_r = \tfrac{1}{2}(b_o + b_s), \qquad m_r = \frac{1}{k_r}
$$

$$
s_r = m_r - 25, \qquad o_r = b_r + 38
$$

- **Why average the two offsets.** The bridge's small leak adds to the reflected wave in
  phase for one standard and in antiphase for the other. The mean of the two offsets cancels
  it to first order.
- **Why not pool the points.** For the same drive, the short and the open draw different
  forward power, so pooling the points into one line would tilt the slope.
- **The open–short gap,** b_o − b_s, is kept as a check (Section 8).
- **Open only** (Section 6, when the short failed): fit y = k_r·x + b_o to the open points
  alone, and take b_r = b_o.

### Checks computed from the stage A readings

- **Forward residuals (level 3):** the rms and the worst residual of the forward fit, in dB.
- **The wattmeter load's SWR (S50).** Apply the new reflected calibration to stage A's V_r,k
  and V_f,k. This measures the bridge's leak plus the mismatch of the wattmeter and its load.
  At level 1 stage A is on a bare 50 Ω load. If stage A was skipped, there is no S50.
- **Drift (level 3):** the repeated point against its first reading, in dB.
- **Level 2 against the defaults:** the difference between o_f from the reading and the
  level 1 default, in dB.

### Optional: subtracting the detector floor

The key-up floor adds a small power to every reading. Converting it with the new calibration
gives P_floor,f and P_floor,r per band. Subtracting them in the power domain (in W, not mV)
before the SWR calculation removes a small high bias at low power. On radio 1 the floor is
a few µW on the line, which matters only for a well-matched load below about 50 mW.

## 8. Acceptance checks

Store a band's values only if it passes all of these. Otherwise keep its previous values and
report the failure. The thresholds come from the 2026-10/11 bench sessions.

| Check | Pass | Radio 1, 2026-11-01 | Usual cause of a failure |
|---|---|---|---|
| Forward fit rms (level 3) | ≤ 0.3 dB, worst point ≤ 0.6 dB | 0.11–0.21 dB (10 m 0.13 dB without the ceiling point) | Wattmeter entry error; unsettled reading; saturation |
| Forward slope m_f (level 3) | 22–28 mV/dB, and within 5 % of the median of this radio's bands | 24.1–24.9 mV/dB | Wrong entry; too small a span |
| Reflected fit rms | ≤ 0.2 dB | 0.07–0.15 dB | Unsettled readings; oscillation |
| Reflected slope against forward | \|m_r − m_f\| ≤ 1.5 mV/dB | within 0.6 mV/dB (160–20 m) | Lossy standard; oscillation |
| Open–short gap \|b_o − b_s\| | ≤ 0.5 dB on 160–20 m; ≤ 1.0 dB above 20 m | 0.02–0.04 dB (160–20 m), 0.51 dB (10 m) | Lossy short or open; cable or adapter in the way |
| S50 on the stage A load (level 1: a bare 50 Ω load) | ≤ 1.3 below 20 MHz; ≤ 1.5 above | not taken on the meter chain; a bare 50 Ω load read 1.01–1.06 (160–20 m) and 1.22 (10 m) | Poorly matched wattmeter or load; bridge fault |
| Drift of the repeated point (level 3) | ≤ 0.2 dB | ≤ 0.08 dB | Thermal drift; loose connection |
| Level 2 offset against the level 1 default | within ±3 dB on 160–20 m, ±4 dB above 20 m | −0.6 to −0.2 dB against the defaults in Section 2 (−1.2 to −0.4 dB against radio 2's values alone) | Wattmeter entry error (2 W entered as 20 W is 10 dB); wrong L44 turns setting |
| Oscillation | None detected on at least one of the open and short (Section 6) | none on radio 1 | PA instability into the short (radio 2, 10 m) |
| Load present (stage A) | every load point V_f − V_r ≥ 100 mV | 166 mV the smallest on any bench sheet; 486–1100 mV on radio 2, 2026-11-02 | Load not fitted, or its connector loose (radio 2, 2026-11-02: V_r read within 4 mV of V_f) |

## 9. What to save

### Per band, in the configuration (`Storage.cpp` saves `ED` as now)

| Field | Exists | Content |
|---|---|---|
| `ED.SWR_F_SlopeAdj[band]` | yes | s_f = m_f − 25 |
| `ED.SWR_F_Offset[band]` | yes | o_f = b_f + 38 |
| `ED.SWR_R_SlopeAdj[band]` | yes | s_r = m_r − 25 |
| `ED.SWR_R_Offset[band]` | yes | o_r = b_r + 38 |
| `ED.PowerCal_20W_Psat_mW[band]`, `ED.PowerCal_20W_kindex[band]`, `ED.PowerCal_20W_shape[band]` | yes (the shape from 2026-11-02) | PA curve P = P_sat·tanh((k·x)^s)^(1/s), from stage A, fitted in dB (`PowerCalibration/AutoCalibration.md`) |
| `ED.SWR_F_Floor_mW[band]`, `ED.SWR_R_Floor_mW[band]` | new, optional | P_floor,f and P_floor,r on the line (Section 7) |
| `ED.SWR_CalLevel[band]` | yes (2026-11-02) | 0 if the band is uncalibrated, otherwise the level it passed (1, 2 or 3, Section 2), with a flag for a reflected line from the open alone (`SWR_CAL_OPEN_ONLY`) or the short alone (`SWR_CAL_SHORT_ONLY`). Lets the display mark the SWR as uncalibrated and show the power display's expected accuracy |
| `ED.SWR_CalDate[band]` | yes (2026-11-02) | Time stamp of the calibration (the radio keeps time from the PJRC sync) |

Per radio, not per band:

| Field | Exists | Content |
|---|---|---|
| `ED.SWR_CouplerTurns` | yes (2026-11-02), set on the AUTO setup screen | L44 turns per core (10 on radio 1, 20 on radio 2). Selects the level 1 defaults (Section 2). Could instead be a build constant beside `COUPLER_ATTENUATION_DB` |

For radio 1 on 40 m, the 2026-11-01 sheet gives s_f = −0.44 mV/dB, o_f = −5.51 dB,
s_r = +0.13 mV/dB and o_r = −2.71 dB (notebook Section 11.7). The offsets are negative
because the firmware's constants assume a −84 dBm intercept and the coupler and pad losses
of the original design.

### Calibration log (not in `ED`)

Write each band's raw data to a log file, LittleFS or the serial port, as rows like the bench
sheets (`code/tools/swr/swr_bench_measurements_radio1_adcfix.csv`):
- **For every point:** band, stage, attenuation, wattmeter reading, the V_f and V_r means and
  standard deviations, and the number of readings.
- **For the band:** the level, the fit results and every check value.

The log lets a fit be redone offline, and lets calibrations be compared over time. It also
lets a band be raised to a higher level without refitting the open and the short: a later
wattmeter session gives a new forward line, and the reflected line is refitted from the
logged open and short means (Section 7).

## 10. Notes and open questions

- **Recalibrate after any change to the bridge or its readout:** L44, the pads, the
  detector input parts, or the AD7991 read code. Radio 1's offsets moved by several dB with
  the readout fix alone.
- **The 100 W PA** uses the same bridge, so the 20 W calibration applies. Readings above about
  16 W are extrapolated: by simulation about ±0.1–0.2 dB at 75 W. The 100 W PA's own power
  calibration takes a wattmeter reading near 75 W. Logging V_f there checks the
  extrapolation for free.
- **10 m at full power reads about 2 dB high** on radio 1 since its L44 rebuild: the cause is
  open. The calibration is unaffected if the ceiling point is dropped (Section 7). The
  power display at the 10 m ceiling still reads high until the cause is found.
- **AD8307 log conformity** limits the forward accuracy to about ±0.25 dB over 16 mW to full
  power on both radios in the current build (radio 2 read ±0.5 dB before its rework). A
  straight line cannot remove it.
- **`BAND_4M` and `BAND_GENERAL`:** decide which LPF and calibration each uses. Until then,
  copy the values of the band whose LPF is selected.
- **Re-check periodically.** One stage A point per band against the wattmeter shows whether
  the offsets have drifted (radio 2 moved 0.3–0.5 dB between sessions).
- **More radios for Section 2.** The level 1 defaults and the leave-one-out accuracy rest on
  two radios, revised on 2026-11-02 after radio 2's rework. Each further radio
  calibrated at level 3 should be added to the default table.
