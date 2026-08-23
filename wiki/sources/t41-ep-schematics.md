---
title: T41-EP Schematics (KiCad, V012-era)
type: source
status: draft
created: 2026-06-14
updated: 2026-06-14
tags: [hardware, schematic, kicad, t41-ep, source, bom]
source_refs: []
related: ["[[hardware-platform]]", "[[i2c-bus-map]]", "[[rf-board]]", "[[filter-boards]]", "[[front-panel]]", "[[display-subsystem]]"]
---

# T41-EP Schematics (KiCad, V012-era)

KiCad schematic files for the T41-EP board set (six dropped into `raw/` on 2026-06-14; the LPF
**filter** board added 2026-06-15 — seven total). These are the **first physical-hardware
source** ingested — until now the `hardware/` pages were inferred from firmware constants.
Schematics are authoritative for what is physically present; where they disagree with code
comments, the schematic wins (and the discrepancy is logged).

> KiCad `.kicad_sch` are S-expression text. Facts below are extracted from `(property
> "Reference"/"Value" …)` symbol fields and net labels; exact pin-level nets were not fully
> traced. Component **values** (chip part numbers) are reliable; precise wiring claims that
> aren't net-confirmed are marked *(infer)*.
>
> ⚠ **Multiple board revisions exist and reference designators can differ between them**
> (e.g. the RF-board attenuators — see below). The files in `raw/` are the **V12.8 / "V012"**
> generation. The **electrical wiring and firmware are stable across revisions**, so prefer
> citing *function* (e.g. "RX attenuator") over a bare `U#`, and state the revision when a
> designator matters.

## The six files / boards

| File (`raw/…`) | Board | Role |
|---|---|---|
| `T41-main-board-V012.kicad_sch` | **Main board** | Teensy 4.1 compute, audio converters, power, all I/O fan-out |
| `T41-RF-board-V012.kicad_sch` | **RF board** | Quadrature mixer, LO + attenuator daughterboards, I/Q baseband amps |
| `T41-RF-board-Attenuator-daughterboard.kicad_sch` | **Attenuator daughterboard** | 2× PE4312 digital step attenuators |
| `DualClockLO.kicad_sch` | **Dual-clock LO daughterboard** | 2× Si5351A quadrature/independent LO |
| `T41-exciter-filter-board.kicad_sch` | **BPF / exciter-filter board** | Switched per-band band-pass filter banks |
| `K9HZ_100W_11band_LPF-Control.kicad_sch` | **LPF / 100 W PA control board** | Band decode, PA bias, antenna switching, SWR/power sensing |
| `K9HZ_100W_11band_LPF-Filter.kicad_sch` | **LPF filter board** | The actual per-band low-pass filters + their switching relays (added 2026-06-15) |

## Main board (`T41-main-board-V012`)
- **U8 Teensy 4.1** (compute). **U7 Teensy Audio Adaptor** = the lone **SGTL5000** codec
  (double-duty: mic in **+** TX I/Q out). **U6 PCM1808PW** (ADC) = **RX I/Q in**. **U5 PCM5102**
  (DAC) = **speaker out**. So the audio path is **one SGTL5000 + PCM1808 + PCM5102** (owner-
  confirmed). The firmware's `pcm5102_mainBoard` object represents U5 but is mistyped
  `AudioControlSGTL5000`; since PCM parts have no I²C, its control calls are no-ops — a code
  clarity fix, not a missing chip. → [[hardware-platform]]
- **U4 ATtiny85-20S** + **Q1/Q4 SUP90P06-09L** P-MOSFETs + **SW1 ON-OFF button** = soft
  power-switching / housekeeping.
- Regulators: **U1/U9 LM7805** (+5 V), **U3/U10 LM1117-3.3** (+3.3 V), **U2 AP7384**; TO-220
  heatsinks (EA-T220-38E). Rails seen: +12V, +5V, +3V3.
- **Three I²C buses leave the Teensy**: net labels `SDA/SCL`, `SDA1/SCL1`, `SDA2/SCL2`
  (= Wire/Wire1/Wire2 — confirms [[i2c-bus-map]]).
- Connectors: **J6 RF CONTROL** (→RF board), **J12 BANDS** (→filter boards), **J5 Front
  Panel**, **J7 Display** + **J8 Display Voltage** (the RA8875 module is *off-board* — that is
  why no RA8875 appears in the schematic), **J9 Keys/PTT**, **J16 Mic_In**, **J17 Ex_Out**
  (TX I/Q to exciter), **J14/J15 Rec_Out/In**, **J11 ENCODER**, **J24 USBHost**, **J23
  Ethernet**, **J3 Acc**, **J4 12-13.8 V**.

## RF board (`T41-RF-board-V012`)
- **U12 = T41_DualClock_Daughter** (the dual-Si5351 LO board) and **U3 + U11 =
  T41_Attenuator_Daughter** (two PE4312 attenuator boards → RX and TX paths).
- **TR1 ADT1-1, TR2 ADT2-1T** RF transformers (mixer/balun); **U2 PSA-8A+** and **U13
  MAR-3SM+** MMIC gain blocks; **U7/U9 MASWSS0179** RF switches (T/R, cal loopback);
  **U1/U4 74CBTLV3253** analog muxes (I/Q switching) *(infer: quadrature routing)*.
- I/Q baseband op-amps: **U6/U8 MC33078, U106 AD8599, U107 OPA2209, U16 LM358B**.
- **U21 MCP23017** (= 0x27, the RF-board GPIO that latches switch/attenuator control).
- Coax/headers: **J1 IQ_Out, J5 IQ_In** (to/from main-board codecs), **J2 RF_IN, J6 RF_OUT,
  J3 RFControl**.

## Attenuator daughterboard (`…Attenuator-daughterboard`)
- One **PE4312** per board (chip ref `U1` in this sheet) — 6-bit digital step attenuator,
  **31.5 dB / 0.5 dB**. The hardware behind `SetRXAttenuation`/`SetTXAttenuation` ([[rf-board]]).
- **Designators are revision-dependent** (the wiring/firmware never changed): in **V12.8** (this
  `raw/` schematic, PE4312 on daughterboards) **RX = U11, TX = U3** (U12 = dual-clock daughter);
  in **prior revisions** (PE4312 on the RF board) **RX = U3, TX = U12**.
- **Control mapping (electrical, revision-independent; owner-confirmed 2026-06-14):** MCP23017
  (U21, 0x27) **GPIOA → RX attenuator**, **GPIOB → TX attenuator**, low 6 bits = attenuation
  word: bit0→pin20 (C0.5), bit1→19 (C1), bit2→17 (C2), bit3→16 (C4), bit4→15 (C8), bit5→1 (C16);
  PE4312 pin 18 = GND. Matches firmware `round(2·dB)&0x3F` exactly. → [[rf-board]]

## Dual-clock LO daughterboard (`DualClockLO`)
- **2× Si5351A-B-GM** clock generators, **25 MHz reference** (ECS-TX053 TCXO + crystal),
  2k2 I²C pull-ups, **qwiic** connector. Confirms the dual-VFO 0x60/0x61 Si5351 hardware
  ([[rf-board]], [[i2c-bus-map]]).

## BPF / exciter-filter board (`T41-exciter-filter-board`)
- ~**22× MASWSS0179** RF switches + **SN74LVC1G04** inverters switch **per-band band-pass
  L-C filter banks** (RX image rejection + TX). **U35 MCP23017** (= 0x24) does band select.
- Connectors: **J1 TO PA, J2 FROM EXCITE, J5 TO RECEIVE, J6 FROM LPF, J4 Bands**.

## LPF / 100 W PA control board (`K9HZ_100W_11band_LPF-Control`)
- **Decode chain (owner net-trace 2026-06-15):** MCP23017 (U15, 0x25) → **U12 CD74HC4514**
  4→16 decoder (band BCD on GPIOB[0:3], A0–A3 = pins 2,3,21,22) → inverting **U9/U10 ULN2803**
  → **external** band-LPF relays via **J24/J25** (selected line driven LOW; NF=Y15 bypass).
  Antenna: GPIOB[4:5] → **U2 74HC237** → **U4 ULN2803** → **K4–K7**. 100 W PA: GPIOB[7] → spare
  U4 channel → **K2/K3**. BPF RX/TX routing: GPIOA[0:1] → **U5/U6** (BPF between J15–J18). So the
  on-board **K2–K7 are PA (K2/K3) + antenna (K4–K7)** — *not* the band LPFs (those are external).
  Full BCD table + detail in [[filter-boards]].
- **Power/SWR sensing** (net-traced + owner-confirmed 2026-06-14): the directional coupler
  feeds **two AD8307 log detectors** — **U14 = forward, U19 = reflected** — whose log outputs
  (`FWD_V`/`REF_V`) go to the **AD7991** I²C ADC (CH0/CH1). The ADC's reference is the
  **REF3440 4.096 V** on Vin3/Vref (net `REF` = the *reference*, not "reflected"). The default
  firmware path correctly converts these log voltages mV/25 − 84 → dBm → W
  (`LPFBoard.cpp:640-643`); the linear `(V·10)²/50` formula is the separate `USE_ANALOG_SWR`
  diode-detector path. → [[filter-boards]]
- PA control: **Q1 IRF630, L7808, TC4428**. Back-end connectors: **ANT-1…ANT-4**, **FROM
  100 W PA / TO 100 W PA**, **TO/FROM 20 W PA**, **TO XVTR**, **TR CTL**, **Bands**,
  **KEY OUT**.

## LPF filter board (`K9HZ_100W_11band_LPF-Filter`, added 2026-06-15)
The board holding the actual filters that the control board selects.
- **24× HF41F/12-ZS** relays = **2 per band** (input + output switch). **One dedicated LPF per
  band** (11 bands) + a **BNF straight-through bypass** — not band-groups.
- Selected by **band-named control nets** `B160M…B6M, BNF` arriving on **J26/J27** (LPF CTRL
  LOW/HI) from the control board's J24/J25. Each net appears 3× (2 relay coils + 1 connector pin).
- Each filter is a **7th-order LC ladder**: 3 series inductors (symmetric, outer pair equal +
  distinct middle) + shunt caps. **33 inductors** (3 × 11); inductance descends with frequency
  (5.21/4.73 µH at 160 m → 0.188/0.170 µH at 6 m). Caps 27 pF–2200 pF.
- Closes the band-select loop: control-board **U12 CD74HC4514 output Yn (n = band BCD)** → this
  board's **B<band>** relays → that band's filter. → [[filter-boards]]

## What this source changed
- **Resolved:** one physical SGTL5000 (not two); RA8875 is off-board; PE4312 = the 31.5 dB/
  0.5 dB attenuators; AD8307+AD7991 = the power/SWR chain; dual Si5351 confirmed.
- **Resolved (owner, 2026-06-14):** one SGTL5000 total (mic+TX I/Q); `pcm5102_mainBoard` =
  the PCM5102 speaker DAC, mistyped `AudioControlSGTL5000` (no-op control calls). Code clarity
  fix, not a bug. → [[hardware-platform]]
- **False alarm cleared (net trace, 2026-06-14):** the suspected "AD8307 log vs linear power
  math" bug was a wiki error. The default digital path already does the correct log conversion;
  the linear formula is the unrelated analog-diode path. → [[filter-boards]]
- Many component-level details added to [[rf-board]] and [[filter-boards]].
