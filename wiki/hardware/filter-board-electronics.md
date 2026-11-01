---
title: Filter Boards — Electronics (BPF / LPF / PA back-end, decoders, SWR sensing)
type: hardware
status: draft
created: 2026-06-16
updated: 2026-11-01
tags: [hardware, bpf, lpf, relays, decoders, ad8307, ad7991, swr, mcp23017, pa, antenna]
source_refs: [sources/t41-ep-schematics, sources/ad8307-datasheet]
related: ["[[hardware-platform]]", "[[i2c-bus-map]]", "[[filter-boards]]", "[[rf-board-electronics]]", "[[t41-ep-schematics]]", "[[ad8307-datasheet]]", "[[log-amp-power-detection]]"]
---

# Filter Boards — Electronics

The *physical* RF back-end boards that the [[filter-boards]] firmware module (`BPFBoard.cpp`,
`LPFBoard.cpp`, `LPFBoard_AD7991.cpp`) drives. This page owns the **electronics** — chips,
decoders, relays, and the schematic-traced decode chain; the firmware page owns the **code**
(the shared `hardwareRegister` model, the band-encoding macros, the SWR/power math). Three
separate physical boards back this one module ([[t41-ep-schematics]]).

## BPF / exciter-filter board (`T41-exciter-filter-board`)
- **~22× MASWSS0179** RF switches + **SN74LVC1G04** inverters select the per-band band-pass
  **L-C filter banks** (RX/TX image rejection).
- Band select via **MCP23017 (U35, 0x24)** ([[i2c-bus-map]]).
- Sits between exciter/receiver and the PA (J2 FROM EXCITE, J1 TO PA, J5 TO RECEIVE, J6 FROM LPF).

## LPF / 100 W PA control board (`K9HZ_100W_11band_LPF-Control`)
**MCP23017 (U15, 0x25)** GPIO bits are decoded by dedicated logic — the firmware never
addresses relays directly, it just writes the encoded `hardwareRegister` bits ([[filter-boards]]):

- **LPF band select** — **GPIOB[0:3]** (4-bit BCD) → **U12 CD74HC4514** 4→16 decoder (inputs
  A0–A3 = pins 2,3,21,22): one of Y0–Y15 goes **HIGH**. Those 16 lines pass through the
  **inverting** relay drivers **U9 + U10 (2× ULN2803)**, so every output is **HIGH except the
  selected band's, which is pulled LOW**. The band-LPF relays are on an **external board**
  reached via **J24 (LPF CTRL HI) / J25 (LPF CTRL LOW)**; a relay engages when its control pin
  is driven **LOW**. **NF = 0b1111** (Y15) is the *No-Filter* bypass. The BCD codes
  (`SDT.h:95-106`): 60 m=0000, 160 m=0001, 80 m=0010, 40 m=0011, 30 m=0100, 20 m=0101,
  17 m=0110, 15 m=0111, 12 m=1000, 10 m=1001, 6 m=1010, NF=1111.
- **Antenna select** — **GPIOB[4:5]** (2-bit) → **U2 74HC237** 3→8 decoder → **U4 ULN2803** →
  **relays K4–K7** (on-board) → antenna ports ANT-1…4.
- **100 W PA route** — **GPIOB[7]** drives a spare **U4** channel directly → **relays K2/K3**,
  switching the optional 100 W amplifier into the TX path.
- **BPF path routing** — **GPIOA[0:1]** (TXBPF/RXBPF) → **U5/U6** route the RX vs TX signal
  through the external band-pass filter (between J15 and J18).

So the on-board **HF41F relays K2–K7 are *not* the band LPFs** — K2/K3 = 100 W PA, K4–K7 =
antenna. The per-band LPF relays live on a separate external filter board (below). Also on this
board: 20 W and 100 W PA paths, transverter out, **KEY OUT**; PA control via
**IRF630 / L7808 / TC4428**.

## External LPF filter board (`K9HZ_100W_11band_LPF-Filter`)
Receives the decoded band lines on **J26/J27** (LPF CTRL LOW/HI ← the control board's J24/J25)
as **band-named control nets** `B160M, B80M, B60M, B40M, B30M, B20M, B17M, B15M, B12M, B10M,
B6M` and `BNF`. Each net drives **2 of the 24 HF41F relays** (input + output switch), so there
is **one dedicated low-pass filter per band** — *not* shared band-groups. Each filter is a
**7th-order** LC ladder: **3 series inductors** (symmetric, outer pair equal + distinct middle)
plus shunt capacitors; **33 inductors total** (3 × 11 filtered bands; **BNF** has no inductors —
a straight-through bypass). Inductances descend with frequency, **5.21 µH / 4.73 µH** (160 m)
down to **0.188 µH / 0.170 µH** (6 m).

The full band-select path therefore closes as:
```
band BCD (GPIOB[0:3]) → U12 CD74HC4514 Yn (n = BCD) → U9/U10 ULN2803 (invert)
  → J24/J25 → J27/J26 → B<band> net → 2× HF41F relays → that band's 7-element LPF
```
i.e. **decoder output Yn directly selects the band whose BCD = n** (Y0=60 m … Y10=6 m, Y15=NF).
*(Per-band exact L/C → cutoff table not yet tabulated; assignment by descending inductance vs.
band frequency.)*

## Power / SWR sensing hardware
A directional-coupler transformer feeds **2× AD8307** *log* power detectors (**U14 = forward,
U19 = reflected**), read by **1× AD7991** 12-bit I²C ADC (**U20, 0x28/0x29**; **CH0 = forward,
CH1 = reflected**) against a **REF3440 4.096 V** reference on the ADC's Vin3/Vref pin
(schematic-confirmed, net-traced 2026-06-14). Because the AD8307 is **logarithmic**, the
default firmware applies the AD8307 transfer law (≈25 mV/dB, −84 dBm intercept) — the
conversion math lives in [[filter-boards]].

**Placement (owner-confirmed 2026-10-22):** the coupler sits **after the band LPF**, so PA
harmonics do not reach the broadband AD8307s. Bridge readings near PA saturation therefore see
the same signal as a wattmeter at the antenna connector.

**Detector front end (schematic net trace + owner confirmation, 2026-10-23).** Both channels
are identical. The forward channel's references are listed first, and the reflected
channel's follow in brackets.
- **Coupler L44 (winding owner-described 2026-10-25; the schematic does not show it).** A
  current transformer and a voltage transformer on one BN-43-202 binocular core. Hole 1 carries
  the line (pin 1 from J23 to pin 2 toward the T/R relays) and 10 turns from pin 3 to pin 5.
  Hole 2 carries 10 turns from pin 1 to pin 5 and a straight wire from pin 3 to pin 4. Pin 5
  is grounded. Pin 3 is the forward port and pin 4 the reflected port. The couplers are hand
  wound and differ (owner, 2026-10-31): radio 1's has 10 turns with the core 20 mm above the
  board, radio 2's 20 turns flush (coupling −26 dB). Thick wires 0.55 mm insulated, windings
  0.3 mm enamelled. See [[swr-bridge-measured-behaviour]].
- **Coupler output.** Each port is loaded only by its pad's input, 50.8 Ω. The schematic's
  52.3 Ω R20 [R28] belong to the diode-detector option below and are not fitted. That load
  matters: the reflected port nulls a forward wave only when the *forward* port is loaded
  by 50 Ω × n_ct/n_vt, and vice versa. A 26 Ω load (R20 fitted) would cut the directivity to
  about 7 dB. Model and leak budget: [[swr-bridge-measured-behaviour]].
- **Pad (values owner-confirmed; the schematic leaves them blank).** A π-pad with 499 Ω
  series (R17 [R22]) and 56.2 Ω shunts (R16/R18 [R24/R25]). It is frequency-compensated:
  10 pF (C147 [C154]) bridges the series arm, and each shunt arm returns to ground through
  22 nH (L38/L40 [L42/L43]).
- **Detector input.** The pad is terminated in 52.3 Ω (R13 [R21]) and drives INP through
  10 nF (C144 [C155]). INM is decoupled to ground by 0.1 µF (C139 [C152]), so the drive is
  single-ended. **OFS (pin 3) and INT (pin 5) are not connected.** ENB is tied to VPS (+5 V
  via a ferrite bead).
- **OUT.** OUT goes to the AD7991 (CH0 [CH1]) and, through a 0 Ω link (R30 [R23]), to
  `FWD_V` [`REF_V`] on J21 Bands. There is no buffer or capacitor between OUT and the ADC,
  so the ADC sees the AD8307's 12.5 kΩ output resistance ([[ad8307-datasheet]]).
- **Readout crosstalk (measured 2026-11-01, radio 1).** With an AD2 on U20 pins 3 and 4, the
  radio's ADC readings fit ADC_F = 0.83·Vf + 0.14·Vr + 31 mV and ADC_R = 0.18·Vf + 0.80·Vr
  + 22 mV over 40 keyed points. Each reading is pulled toward the other channel, so Vf − Vr
  passes with a gain of 0.65 while common changes pass at 0.98. That compressed SWR on both
  radios and was mistaken for a bridge leak ([[swr-bridge-measured-behaviour]]).
  - Cause: each read selects the channel and converts it in the same I²C transaction, so the
    sampling capacitor still holds about 15–19 % of the previous channel's voltage. Each
    discarded conversion cuts the crosstalk by that factor again.
  - Firmware fix, verified 2026-11-01: two discarded conversions after each channel switch.
    The cross terms fall to 0.007/−0.008 ([[filter-boards]]).
  - Hardware fallback: about 10 nF from each OUT to ground at U20, or a buffer.
- **Diode-detector stuffing option, not populated (owner-confirmed 2026-10-25).** The
  schematic also places a diode peak detector on each coupled port and OUT node: R20 [R28],
  D1 [D2], C149 [C156], RV19 [RV27] and R26 [R29]. It is the hardware for the
  `USE_ANALOG_SWR` build, an alternative to the AD8307s, and none of it is fitted. The owner's
  earlier bench fit, 22.7 mV/dB (`T41-V12-SDT/testing/slope_and_intercept.ipynb`), agrees
  with an unloaded OUT.

**Frequency response (owner's VNA measurements of one board, `T41 V012/Testing/LPF_testing/
directional_coupler/*.s2p`).**
- The coupler alone couples −20.0 dB, flat within 0.15 dB from 160 m to 10 m (−19.4 dB at
  6 m).
- The as-built path from J23 to the forward detector input is *not* flat:

  | Band | 160 m | 40 m | 20 m | 10 m | 6 m |
  |---|---|---|---|---|---|
  | J23 → forward detector input | −45.9 dB | −45.7 dB | −45.0 dB | −43.3 dB | −39.9 dB |

The pad explains the shape completely. A model of the π-pad between 50 Ω terminations, with
the values above, plus the measured in-circuit coupler, matches the J23 sweep within 0.1 dB on
every band:
- **160 m:** pad −25.88 dB, total −45.90 dB, measured −45.89 dB.
- **10 m:** pad −23.38 dB, total −43.24 dB, measured −43.33 dB.
- **6 m:** pad −20.54 dB, total −39.96 dB, measured −39.93 dB.

The pad's DC loss of 25.9 dB matches the firmware's `PAD_ATTENUATION_DB` = 26. The rise
comes from **C147 across the 499 Ω series arm, a zero at 32 MHz**. The 22 nH shunt inductors
change the response by under 0.2 dB even at 6 m. The as-built sweep also matches the
`…after_attenuator_no_L` file within 0.1 dB. The `…after_attenuator` file (with L), by
contrast, reads 6–15 dB *less* pad loss, which no 22 nH inductor can explain. It probably
records an earlier build. *(infer)*

The firmware assumes a fixed 46 dB (`PAD_ATTENUATION_DB` + `COUPLER_ATTENUATION_DB`) and lets
the per-band `SWR_F_Offset` absorb the difference. The forward offset therefore differs by
about 2.6 dB from 160 m to 10 m, and about 6 dB to 6 m.

**Board-to-board spread (modelled, 2026-10-23).** The resistors barely matter: 1 % on the
499 Ω moves the pad by ≤ 0.08 dB on any band. The bridging capacitance dominates on the high
bands. Each extra 1 pF across R17 (capacitor tolerance plus layout parasitics) shifts the pad
by:

| Band | 160 m | 40 m | 20 m | 10 m | 6 m |
|---|---|---|---|---|---|
| Shift per extra 1 pF | 0.00 dB | 0.04 dB | 0.14 dB | 0.38 dB | 0.59 dB |

A VNA-derived band-to-band transfer of the forward offset is therefore robust from 160 m to
20 m, but on 10 m and 6 m it needs a check on each board, such as one wattmeter reading.
**Measured, 2026-10-24: not borne out.** Carried from 40 m, the transfer was off by up to
2.1 dB on 20 m and 3.3 dB on 10 m on radio 2 ([[swr-bridge-measured-behaviour]]). Calibrate
each band with a wattmeter.

**Detector error terms.** The pad, coupler and termination are linear, and nothing loads OUT.
The bridge's calibration terms therefore split as follows (theory in
[[log-amp-power-detection]], data in [[ad8307-datasheet]]):
- **Slope:** set per chip and shared by every band. The datasheet gives 23–27 mV/dB with no
  frequency condition. *Measured: true on radio 1, within 2 %. On radio 2, the 160 m slope is
  5–7 % below 40 m and 10 m is 3 % above ([[swr-bridge-measured-behaviour]]).*
- **Offset:** the per-band path above plus a per-chip intercept that is the same on every
  band.

There are two exceptions, both covered on the theory page:
- **160 m, and possibly 80 m.** These sit just above the 1.5 MHz high-pass corner that the
  open OFS pin gives the AD8307's offset loop.
- **Conformance error depends on the detector input level.** The path loss differs by 6 dB
  across the bands, so the same line power lands at different points on the detector curve.
  A slope fitted up to 100 W on 10 m or 6 m reaches the conformance bump near +10 dBm.

Nothing filters OUT, so it carries ripple at twice the RF frequency. This is largest on the
low bands; it adds scatter, not bias.

**The bridge in the radio leaks far more than the VNA measured on the bare board.** Bench
tests on two radios found an effective directivity of only about −9 to −19 dB, against
−17 to −36 dB on the bare board. Known loads therefore read 30–90 % high, and no slope or
offset calibration can fix that. The cause is open. See [[swr-bridge-measured-behaviour]].

An optional **`USE_ANALOG_SWR`** build instead reads the diode detectors described above on
Teensy ADC pins 26 (FWD) / 27 (REV), with square-law math. It is off by default, and those
parts are not fitted on the owner's board. *(The J21 → Teensy pin 26/27 routing is inferred,
not traced.)*

## Open questions
- Does the AD8307's open-OFS offset loop (corner about 1.5 MHz) change the slope or offset on
  160 m? *Partly answered:* radio 2's wattmeter slope on 160 m is 5–7 % below 40 m in two
  sessions; radio 1's is not ([[swr-bridge-measured-behaviour]]). A capacitor on OFS would test it.
- ~~Why does the bridge leak 10–20 dB more in the radio than on the VNA bench?~~ Answered
  2026-11-01: mostly not a leak, but the AD7991 readout crosstalk above. At the pins, radio
  1's rebuilt bridge leaks −40 to −42 dB on 160/40 m, −31 dB on 20 m and −22 dB on 10 m
  ([[swr-bridge-measured-behaviour]]).
- Board-to-board spread of the 10 m and 6 m path, set mainly by C147 [C154] and the layout
  parasitics. A second board's VNA sweep would bound it.
- Per-band exact L/C → cutoff frequency table for the external LPF board.
- AD7991 `REGISTER_SETUP` channel-select bits vs. the "CH0+CH1" comment (a firmware detail —
  see [[filter-boards]]).
