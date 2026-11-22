---
title: AD8307 Datasheet (Analog Devices, Rev. F)
type: source
status: draft
created: 2026-10-23
updated: 2026-10-23
tags: [hardware, ad8307, log-amp, detector, swr, power-sensing, calibration, source]
source_refs: []
related: ["[[filter-board-electronics]]", "[[filter-boards]]", "[[log-amp-power-detection]]", "[[t41-ep-schematics]]", "[[calibration-state-machines]]"]
---

# AD8307 Datasheet (Analog Devices, Rev. F)

`raw/AD8307.pdf`, "Low Cost, DC to 500 MHz, 92 dB Logarithmic Amplifier", 24 pages, Rev. F
(12/2019). The T41's forward (U14) and reflected (U19) power detectors are AD8307s
([[filter-board-electronics]]). Rev. F changed only the ordering guide relative to Rev. E.
The specification table is identical to Rev. B (2003). Figure numbers below are Rev. F's;
Rev. B numbered the typical-performance plots TPC 1–18 instead.

## Specifications (Table 2; VS = 5 V, 25 °C, RL ≥ 1 MΩ)
| Parameter | Min | Typ | Max | Note |
|---|---|---|---|---|
| Log slope, unadjusted | 23 | 25 | 27 mV/dB | Also 23–27 over temperature |
| Log intercept, unadjusted | −87 | −84 | −77 dBm | −88 to −76 over temperature; 20 µV sine amplitude |
| Log conformance, f ≤ 100 MHz, central 80 dB | | ±0.3 | ±1 dB | ±0.5 dB typ at 500 MHz, central 75 dB |
| Input range, ±1 dB error | | 88 | dB | −72 to +16 dBm re 50 Ω (Rev. B wording) |
| Operating noise floor (RS = 25 Ω) | | −78 | dBm | |
| Output resistance (pin 4) | 10 | 12.5 | 15 kΩ | Sets the slope with the 2 µA/dB output current |
| Input resistance / capacitance | | 1.1 kΩ / 1.4 pF | | Differential / either pin to ground |
| Cell gain / cell bandwidth | | 14.3 dB / 900 MHz | | |
| Upper usable frequency | | 500 | MHz | |
| Response time, small / large signal | | 400 / 500 | ns | 10–90 % |

The slope and intercept have **no frequency condition**. Their spread is part-to-part ("the
AD8307 is not laser trimmed").

## What the document says that matters for the T41
- **Frequency dependence (Figs 7, 33, 34).** The 10 MHz and 100 MHz transfer curves overlap.
  At 300 and 500 MHz the line moves down by a few dB but stays nearly parallel, so the change
  is mostly in the intercept. There is no plotted data between 1 kHz (Fig 8, taken with an
  OFS capacitor) and 10 MHz.
- **Offset-nulling loop and the low-frequency limit.** A feedback loop around the amplifier
  chain nulls input offset. With OFS (pin 3) open, the loop gives "a closed-loop high-pass
  corner at about 1.5 MHz". The on-chip filter allows operation "above 1 MHz". An external
  capacitor on OFS lowers the corner, to below 10 Hz with 1 µF. The T41 leaves OFS open, so
  160 m sits just above the corner.
- **Output interface.** The output is 2 µA/dB into an on-chip 12.5 kΩ (±20 %), unbuffered.
  - A shunt load lowers the slope: 50 kΩ gives 20 mV/dB.
  - The INT pin shifts the intercept by 8 dB/V.
  - Internal capacitance makes a low-pass pole near 5 MHz. The 2f ripple is ±0.5 dB at
    10.7 MHz and ±0.02 dB at 50 MHz. A CFLT capacitor from OUT to ground lowers it.
- **Conformance ripple and its period.** The log law comes from 6 cascaded 14.3 dB limiting
  cells, plus 3 top-end detectors on a tapped attenuator that extend the range by 27 dB. The
  conformance error therefore ripples with a period of about one cell gain, ±0.3 dB typ.
  Above about +10 dBm it rises, then compresses near +16 dBm (Figs 5, 6, 34).
- **Temperature.** The intercept is temperature-shaped on chip. Fig 6 and Fig 11 (−40, +25,
  +85 °C) show the curves nearly overlapping across the central range.
- **"Log amps do not respond to power, but to the voltage applied to their input."** dBm
  figures assume a 50 Ω termination. The effective intercept depends on the waveform: a square
  wave reads 6 dB above a sine of the same amplitude, and Gaussian noise 0.5 dB above a sine of
  the same rms. The output follows the RF envelope within about 0.5 µs. A modulated signal
  therefore averages as the *mean of the log* of its envelope (see [[log-amp-power-detection]]).
- **Input.** 1.1 kΩ ∥ 1.4 pF, AC-coupled; a 52.3 Ω shunt terminates a 50 Ω source (Fig 32).
  The usual drive is single-sided, into one input with the other AC-coupled to ground, and on
  5 V it handles up to +16 dBm. The T41 drives it this way. Mismatched coupling capacitors can
  cause a power-up transient after a long disable. The T41's are mismatched (10 nF and 0.1 µF),
  but ENB is tied to VPS, so the chip is never disabled.
- **Calibration guidance (Fig 37).** Set the slope with two levels spaced across the central
  range, for example −60 and 0 dBm. Set the intercept with a known CW level near the bottom of
  the range, to minimise the effect of residual slope error.
- **The "1 µW to 1 kW 50 Ω power meter" application (Fig 41).** A 158:1 (44 dB) resistive tap
  moves the intercept to −40 dBm, so that P(dBm) = 40·(VOUT − 1). This is the same idea as
  the T41's 46 dB coupler-plus-pad. The datasheet's coupling capacitors are rated for
  f ≥ 10 MHz.

## What this source changed in the wiki
- Established which bridge error terms can vary with frequency, recorded in
  [[filter-board-electronics]]:
  - **Slope:** set per chip.
  - **Offset:** the per-band path C(f) plus a per-chip intercept that is the same on every
    band.
  - **Exception:** 160 m, and possibly 80 m, sit near the open-OFS loop corner.
- A level-dependent conformance error moves the *apparent* slope by band, because each band's
  path loss puts a given line power at a different detector input level.
- Prompted a full net trace of the detector front end. It found:
  - the frequency-compensated π-pad;
  - the open OFS and INT pins;
  - the BAS70H diode-detector stuffing option sharing the OUT node, which is the hardware for
    `USE_ANALOG_SWR` and is **not fitted** on the owner's board.

  It also corrected the earlier claim that OUT is unloaded. → [[filter-board-electronics]],
  [[filter-boards]]
- New concept page: [[log-amp-power-detection]].
