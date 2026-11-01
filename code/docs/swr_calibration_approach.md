# SWR bridge calibration in the firmware

How the radio should calibrate its forward-power and SWR readings on every band by itself.
The operator connects only three things to the antenna connector, in this order: a wattmeter
with its 50 Ω load, an open, and a short. The firmware does the keying, the fitting and the
checks, and stores four values per band.

This approach was tested on radio 1 on 2026-11-01. It needs firmware with the AD7991
discarded-conversion fix (commit 6217be9). With that fix, radio 1 read a 2:1 load (100 Ω) as
1.97–2.04 and a 4:1 load (200 Ω) as 3.96–4.22 on 160, 40 and 20 m. The measurements behind
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
  the forward line, s_f and o_f.
- **The open and the short** reflect all the power, so the reflected power equals the
  forward power. With the forward line known, the forward reading gives that power, and the
  reflected line, s_r and o_r, follows.
- **Wattmeter errors mostly cancel in the SWR.** A wrong wattmeter scale moves o_f and o_r
  by the same amount. The power display carries the wattmeter's error; the SWR does not.

**Calibrate every band, with its own wattmeter readings.** The following do not work, as the
2026 bench tests showed:

- **Slopes from drive steps** (DSP gain or the TX attenuator) read 2–7 % low, because the
  transmitter is not dB-linear even 20 dB below full power.
- **Carrying one band's offset to another** with the VNA path loss was off by up to 3 dB.
- **One slope for all bands** worked on radio 1 (within 2 %). Radio 2's 160 m slope was
  5–7 % below its 40 m slope.

## 2. Equipment and set-up

| Item | Requirement |
|---|---|
| Firmware | Includes the AD7991 fix: two discarded conversions after each channel switch, `AD7991::readADCsingle()` (commit 6217be9). Without it the readings compress the return loss to about 0.65 of its true value, and no calibration can recover it. |
| Wattmeter and load | A wattmeter between the antenna connector and a 50 Ω dummy load, accurate from about 0.1 W to the 20 W PA's full output (about 6 W on 10 m, 16 W on 40 m). ±5 % gives about ±0.2 dB on the power display. |
| Open | Nothing on the antenna connector. |
| Short | A low-inductance short fitted directly on the connector. |
| No cables or adapters for the open and short | A cable between the connector and a standard reduces its reflection by twice the cable's loss. Every SWR then reads high, and the checks cannot detect it. |
| PA | The 20 W PA for the whole procedure. The bridge sits after the LPF, so its calibration does not depend on the PA. |
| Antenna port | The port used in operation (ANT1 on the bench). |
| Warm-up | Calibrate after the radio has been on for some time. Calibrations moved 0.3–0.5 dB between sessions on radio 2. |

## 3. Order of operations: three connector changes

Each standard is fitted once and every band is measured with it before the next change.
The bench script `code/tools/swr/swr_bench_measure.py` uses the same order, so its sheets are
a model for the firmware's data log.

| Stage | On the antenna connector | What the firmware does on each band | Operator input |
|---|---|---|---|
| A | Wattmeter with its 50 Ω load | Key-up floor; forward sweep (5–7 CW levels); PA power curve (the existing power calibration) | Enter the wattmeter reading at each level |
| B | Open | Open sweep (5–6 levels), automatic | None |
| C | Short | Short sweep at the same levels, automatic | None |
| — | Fit, check, store | Section 6 and Section 7 for every band, then save | Accept or repeat failed bands |

Stage A must come first, because the reflected fit needs the forward line to convert the
forward readings into power. Stages B and C can come in either order. Ending with the short
means the operator then fits the antenna, which is the natural next step.

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

## 4. Taking a reading

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

## 5. The sweeps on each band

### Calibration frequencies

Use one frequency in the middle of each band. The coupling and the leaks change little
within a band.

| Band | 160 m | 80 m | 60 m | 40 m | 30 m | 20 m | 17 m | 15 m | 12 m | 10 m | 6 m |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Frequency (MHz) | 1.9 | 3.75 | 5.357 | 7.1 | 10.125 | 14.2 | 18.118 | 21.2 | 24.94 | 28.5 | 50.2 |

`BAND_4M` and `BAND_GENERAL` have no LPF of their own on the bench radios. Until that is
settled, they keep the values of the band whose LPF they use (Section 9).

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
  its reading (Section 4) when the operator enters the value.

### Stages B and C: open and short sweeps

Automatic. Use the same CW drive levels on the open and the short, so the two sweeps can be
compared level by level:

- **Levels:** 5–6 of them, from about 1.2 W of forward power down to the lowest the
  attenuator gives. Choose the attenuations from the band's PA curve (stage A) for 50 Ω. The
  PA delivers a different power into an open or a short, but V_f measures what it actually
  delivers, so the drive need not be exact.
- **Span:** at least 8 dB. Where the attenuator cannot reach 8 dB below 1.2 W, reduce the
  carrier's DSP gain as well. Its accuracy does not matter, for the same reason.
- **Protecting the PA:** key each point for no more than 2 s, and leave at least 1 s in
  receive between points. The whole short sweep on one band then puts about 12 s of full
  reflection into the PA.
- **Oscillation check (abort rule).** Radio 2's PA oscillated into a short on 10 m: the
  forward reading sat at about 2100 mV whatever the drive. After each point, compare V_f
  with the previous point. When the drive drops by Δ dB, V_f must fall by at least
  0.5 × Δ × 25 mV. Also, V_f must not exceed the stage A V_f at the same attenuation by
  more than 3 dB × 25 mV. If either test fails: unkey, discard that band's short (or open)
  sweep, record the band as failed, and move on to the next band.

## 6. The calculations

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
- **Rule for every band:** fit once without the 0 dB point and compute that point's residual.
  If it is within 0.5 dB, refit with it included. If not, leave it out and log it.

The stored values:

$$
s_f = m_f - 25, \qquad o_f = b_f + 38
$$

The 38 is 84 − 26 − 20, from the firmware's constants in Section 1.

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
- **The open–short gap,** b_o − b_s, is kept as a check (Section 7).

### Checks computed from the stage A readings

- **Forward residuals:** the rms and the worst residual of the forward fit, in dB.
- **The wattmeter load's SWR (S50).** Apply the new reflected calibration to stage A's V_r,k
  and V_f,k. This measures the bridge's leak plus the mismatch of the wattmeter and its load.
- **Drift:** the repeated point against its first reading, in dB.

### Optional: subtracting the detector floor

The key-up floor adds a small power to every reading. Converting it with the new calibration
gives P_floor,f and P_floor,r per band. Subtracting them in the power domain (in W, not mV)
before the SWR calculation removes a small high bias at low power. On radio 1 the floor is
a few µW on the line, which matters only for a well-matched load below about 50 mW.

## 7. Acceptance checks

Store a band's values only if it passes all of these. Otherwise keep its previous values and
report the failure. The thresholds come from the 2026-10/11 bench sessions.

| Check | Pass | Radio 1, 2026-11-01 | Usual cause of a failure |
|---|---|---|---|
| Forward fit rms | ≤ 0.3 dB, worst point ≤ 0.6 dB | 0.11–0.21 dB (10 m 0.13 dB without the ceiling point) | Wattmeter entry error; unsettled reading; saturation |
| Forward slope m_f | 22–28 mV/dB, and within 5 % of the median of this radio's bands | 24.1–24.9 mV/dB | Wrong entry; too small a span |
| Reflected fit rms | ≤ 0.2 dB | 0.07–0.15 dB | Unsettled readings; oscillation |
| Reflected slope against forward | \|m_r − m_f\| ≤ 1.5 mV/dB | within 0.6 mV/dB (160–20 m) | Lossy standard; oscillation |
| Open–short gap \|b_o − b_s\| | ≤ 0.5 dB on 160–20 m; ≤ 1.0 dB above 20 m | 0.02–0.04 dB (160–20 m), 0.51 dB (10 m) | Lossy short or open; cable or adapter in the way |
| S50 on the wattmeter load | ≤ 1.3 below 20 MHz; ≤ 1.5 above | not taken on the meter chain; a bare 50 Ω load read 1.01–1.06 (160–20 m) and 1.22 (10 m) | Poorly matched wattmeter or load; bridge fault |
| Drift of the repeated point | ≤ 0.2 dB | ≤ 0.08 dB | Thermal drift; loose connection |
| Oscillation | None detected (Section 5) | none on radio 1 | PA instability into the short (radio 2, 10 m) |

## 8. What to save

### Per band, in the configuration (`Storage.cpp` saves `ED` as now)

| Field | Exists | Content |
|---|---|---|
| `ED.SWR_F_SlopeAdj[band]` | yes | s_f = m_f − 25 |
| `ED.SWR_F_Offset[band]` | yes | o_f = b_f + 38 |
| `ED.SWR_R_SlopeAdj[band]` | yes | s_r = m_r − 25 |
| `ED.SWR_R_Offset[band]` | yes | o_r = b_r + 38 |
| `ED.PowerCal_20W_Psat_mW[band]`, `ED.PowerCal_20W_kindex[band]` | yes | PA curve, from stage A as now |
| `ED.SWR_F_Floor_mW[band]`, `ED.SWR_R_Floor_mW[band]` | new, optional | P_floor,f and P_floor,r on the line (Section 6) |
| `ED.SWR_CalValid[band]` | new, recommended | Set when the band passed every check; lets the display mark SWR as uncalibrated |
| `ED.SWR_CalDate[band]` | new, recommended | Time stamp of the calibration (the radio keeps time from the PJRC sync) |

For radio 1 on 40 m, the 2026-11-01 sheet gives s_f = −0.44 mV/dB, o_f = −5.51 dB,
s_r = +0.13 mV/dB and o_r = −2.71 dB (notebook Section 11.7). The offsets are negative
because the firmware's constants assume a −84 dBm intercept and the coupler and pad losses
of the original design.

### Calibration log (not in `ED`)

Write each band's raw data to a log file, LittleFS or the serial port, as rows like the bench
sheets (`code/tools/swr/swr_bench_measurements_radio1_adcfix.csv`):
- **For every point:** band, stage, attenuation, wattmeter reading, the V_f and V_r means and
  standard deviations, and the number of readings.
- **For the band:** the fit results and every check value.

The log lets a fit be redone offline, and lets calibrations be compared over time.

## 9. Notes and open questions

- **Recalibrate after any change to the bridge or its readout:** L44, the pads, the
  detector input parts, or the AD7991 read code. Radio 1's offsets moved by several dB with
  the readout fix alone.
- **The 100 W PA** uses the same bridge, so the 20 W calibration applies. Readings above about
  16 W are extrapolated: by simulation about ±0.1–0.2 dB at 75 W. The 100 W PA's own power
  calibration takes a wattmeter reading near 75 W. Logging V_f there checks the
  extrapolation for free.
- **10 m at full power reads about 2 dB high** on radio 1 since its L44 rebuild: the cause is
  open. The calibration is unaffected if the ceiling point is dropped (Section 6). The
  power display at the 10 m ceiling still reads high until the cause is found.
- **AD8307 log conformity** limits the forward accuracy to about ±0.25 dB on radio 1 and
  ±0.5 dB on radio 2 over 16 mW to full power. A straight line cannot remove it.
- **`BAND_4M` and `BAND_GENERAL`:** decide which LPF and calibration each uses. Until then,
  copy the values of the band whose LPF is selected.
- **Re-check periodically.** One stage A point per band against the wattmeter shows whether
  the offsets have drifted (radio 2 moved 0.3–0.5 dB between sessions).
