---
title: Filter Boards — Electronics (BPF / LPF / PA back-end, decoders, SWR sensing)
type: hardware
status: draft
created: 2026-06-16
updated: 2026-06-16
tags: [hardware, bpf, lpf, relays, decoders, ad8307, ad7991, swr, mcp23017, pa, antenna]
source_refs: [sources/t41-ep-schematics]
related: ["[[hardware-platform]]", "[[i2c-bus-map]]", "[[filter-boards]]", "[[rf-board-electronics]]", "[[t41-ep-schematics]]"]
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

An optional **`USE_ANALOG_SWR`** build instead uses **linear diode detectors** on Teensy ADC
pins 26 (FWD) / 27 (REV) — different hardware, different (square-law) math, off by default.

## Open questions
- Per-band exact L/C → cutoff frequency table for the external LPF board.
- AD7991 `REGISTER_SETUP` channel-select bits vs. the "CH0+CH1" comment (a firmware detail —
  see [[filter-boards]]).
