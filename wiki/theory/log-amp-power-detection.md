---
title: Log-Amp Power Detection (how the SWR bridge measures power)
type: concept
status: draft
created: 2026-10-23
updated: 2026-10-24
tags: [ad8307, log-amp, detector, swr, power-sensing, calibration, theory]
source_refs: [sources/ad8307-datasheet, sources/t41-ep-schematics]
related: ["[[ad8307-datasheet]]", "[[swr-bridge-measured-behaviour]]", "[[filter-board-electronics]]", "[[filter-boards]]", "[[calibration-state-machines]]", "[[theory-overview]]"]
---

# Log-Amp Power Detection

The T41 measures forward and reflected power with two AD8307 logarithmic amplifiers behind a
directional coupler and a pad ([[filter-board-electronics]]). This page covers the theory
behind that measurement:
- which calibration errors are possible;
- which of them can vary with band;
- how a log detector misreads modulated signals.

Datasheet facts come from [[ad8307-datasheet]]. Code references are to `LPFBoard.cpp`.

## The log law and the firmware's inverse
A demodulating log amp gives an output voltage that is linear in the input level in dB:

  V = S · (P_det − P_x)

Here S is the slope (25 mV/dB nominal) and P_x is the intercept (−84 dBm nominal). The
detector sees the line power less the path loss C(f) of the coupler and pad, so
P_det = P_line − C(f). The firmware inverts this (`LPFBoard.cpp:667`):

  P_line(dBm) = mV / (25 + SlopeAdj[band]) − 84 + Offset[band] + 46

The fixed 46 dB is `PAD_ATTENUATION_DB` + `COUPLER_ATTENUATION_DB`.

## Which error goes where
**Anything linear ahead of the detector is a pure offset.** In the log domain, a gain or loss
G(f) that does not depend on signal level only adds G(f) dB to P_det. The slope multiplies
P_line, and every path term appears only in the offset. On the T41 this covers:
- the coupler (flat within 0.15 dB across HF);
- the compensated π-pad (rises 2.6 dB from 160 m to 10 m);
- the 52.3 Ω termination;
- the chip's 1.1 kΩ ∥ 1.4 pF input.

**The slope belongs to the chip.** It is the chip's 2 µA/dB output current into its own
12.5 kΩ, which varies ±20 % between parts. That gives 23–27 mV/dB, part-to-part and over
temperature, with no frequency condition. A shunt load on OUT would lower it. None is fitted
on the T41: the diode-detector option that would load OUT is absent.

The calibration terms therefore separate as follows:

| Term | Varies with | Same on every band? |
|---|---|---|
| Slope S | part, temperature | yes (except perhaps 160 m, see below) |
| Chip intercept P_x | part, temperature | yes |
| Path C(f) | band and board (mainly C147 on 10 m and 6 m) | no |

The stored per-band `Offset` = path difference + intercept error. The **band-to-band
difference in offset is the path difference alone**, so a VNA table of C(f) can carry an
absolute calibration from one band to the others.

## Why a fitted slope can still look band-dependent
1. **Conformance error depends on detector input level, not line power.** The log law is
   built from cascaded 14.3 dB limiting cells. Its error therefore ripples with a period of
   about 14.3 dB, ±0.3 dB typical. It rises toward +10 dBm and compresses near +16 dBm. On 6 m
   the path loss is 6 dB lower than on 160 m, so the same line power sits higher on the
   detector curve:

   | Band | Path loss (VNA) | 100 W at the detector | 20 W at the detector |
   |---|---|---|---|
   | 160 m | 45.9 dB | +4.1 dBm | −2.9 dBm |
   | 20 m | 45.0 dB | +5.0 dBm | −2.0 dBm |
   | 10 m | 43.3 dB | +6.7 dBm | −0.3 dBm |
   | 6 m | 39.9 dB | +10.1 dBm | +3.1 dBm |

   A slope fitted up to 100 W on 6 m includes the top-end bump. On 160 m it does not.
   - **Rule:** fit the slope over at least one ripple period (≥ 14 dB), and keep the top point
     below about +5 dBm at the detector.
   - The owner's earlier bench fit gave 22.7 mV/dB over only 10 dB
     (`T41-V12-SDT/testing/slope_and_intercept.ipynb`). A span that short can pick up a few
     percent of slope error from the ripple alone.
2. **The offset loop on 160 m.** The AD8307's offset-nulling loop gives a closed-loop
   high-pass corner at about 1.5 MHz with OFS open, as it is on the T41. 160 m (1.8–2.0 MHz)
   sits just above that corner. The datasheet has no data between 1 kHz and 10 MHz. The loop
   wraps a chain that limits at high input, so its effect may depend on level, which would
   change the slope. This is untested.
   - **Test:** compare the gain-step slope on 160 m with 40 m.
   - **Hardware fix:** a capacitor from OFS to ground (1 µF puts the corner below 10 Hz).

## SWR needs only the slope ratio and the offset difference
With |Γ|² = P_r / P_f, the SWR depends on the *difference* of the two dB readings. A common
error in both offsets cancels. What matters is:
- the slope of each channel;
- the reflected offset *relative* to the forward one, which the open and short sweeps set.

This is why the open/short procedure (`code/tools/swr/swr_calibration_50_open_short.ipynb`)
can calibrate SWR without a wattmeter. A wattmeter is needed only for absolute forward
power. The coupler's directivity leakage adds as a vector and depends on the load, so it fits
neither the slope term nor the offset term. The notebook models it separately.

## Modulated signals: the mean of the log
The AD8307 output follows the RF envelope within about 0.5 µs. The firmware averages the ADC
counts, which are linear in dB (`LPFBoard.cpp:657`, 0.1/0.9 running average). The displayed
power is therefore the **mean of the log of the envelope**, not the log of the mean power. By
Jensen's inequality it reads low for any signal whose envelope varies. *(Derived, not stated in
the datasheet.)*
- **CW or a single steady tone:** exact. This is what all calibration points must use.
- **Two equal tones:** the envelope is ∝ |cos θ|, and mean(20·log₁₀|cos θ|) = −6.02 dB. The
  reading is therefore 6 dB below PEP, which is **3 dB below the true average power**. In
  practice it is slightly less low, because the detector's floor clips the envelope nulls.
- **SSB speech:** lower still, by an amount that depends on the speech and the processing.
  Forward-power and SWR readings during speech are indicative only. The SWR survives better,
  because the forward and reflected readings share the same envelope, so the bias cancels in
  the ratio, as long as both detectors stay inside their log range.

**Measured (2026-10-24, [[swr-bridge-measured-behaviour]]).** Radio 2's detector shows the
conformance error as a smooth ±0.4–0.5 dB curve across a 30 dB fit, which a slope and offset
cannot remove; radio 1's is about half that. Radio 2's 160 m slope is 5–7 % low, consistent
with the offset loop above. Both radios read SWR far too high, from a bridge leak rather than
from any term on this page.

The datasheet's waveform note is a different, RF-scale effect: a square wave reads +6 dB and
Gaussian noise +0.5 dB against a sine. The T41's carrier is sinusoidal, so the envelope effect
above is the one that matters.

## Output ripple
The output is unbuffered, with an internal pole near 5 MHz. It therefore carries ripple at
twice the RF frequency: ±0.5 dB at 10.7 MHz, ±0.02 dB at 50 MHz, and more on 160 m and 80 m.
The AD7991 samples asynchronously, so the ripple is zero-mean noise. It averages out in the
running average and adds scatter, not bias. A capacitor from OUT to ground would remove it,
at the cost of response time.
