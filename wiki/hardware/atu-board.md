---
title: K9HZ Antenna Tuner (ATU) Board
type: hardware
status: draft
created: 2026-11-14
updated: 2026-11-14
tags: [hardware, atu, antenna-tuner, l-network, relays, mcp23017, mcp23008, wire2]
source_refs: []
related: ["[[atu-control]]", "[[atu-auto-tuning]]", "[[i2c-bus-map]]", "[[filter-board-electronics]]", "[[swr-bridge-measured-behaviour]]"]
---

# K9HZ Antenna Tuner (ATU) Board

The K9HZ 100 W auto-tuner is two boards: an **RF board** carrying a switched L-network, and a
**control board** that drives its relays. The firmware that drives it is [[atu-control]]. The
full measured characterisation is `code/docs/ATU_RF_Board_Characterisation.md`; this page
summarises it.

## RF board: a relay-switched L-network
- **Series inductors L1–L8** (K1–K8): 0.05, 0.1, 0.22, 0.45, 1.0, 2.2, 4.4, 8.8 µH. A
  de-energised relay shorts its coil.
- **Shunt capacitors C1–C8** (K9–K16): 10, 22, 47, 100, 220, 470, 1000, 2000 pF. C8 is two
  parts in parallel. A de-energised relay leaves the capacitor open.
- **K17 (LC/CL):** moves the capacitor bank from the antenna side of the inductors (de-energised,
  L-C) to the transmitter side (energised, C-L).
- **K18 (BAL OUT):** routes the output to a 4:1 balun instead of the RF OUTPUT coax.
- **K19 + K20 (one control line, schematic "BYPASS"):** de-energised = network **bypassed**;
  energised puts the network in line. The schematic name is the opposite of what energising
  does, so the firmware calls it `ATUNETWORKBIT`.
- **Every relay off is the safe, power-off state:** bypassed, coax output.

## Control board
- **Expanders on the BANDS connector (I²C `Wire2`):**
  - MCP23017 U5 at **0x20**: GPA → K1–K8 (L), GPB → K9–K16 (C). Close jumpers J4, J5, J6.
  - MCP23008 U6 at **0x21**: GP0 → K17, GP1 → K18, GP2 → K19/K20. Close J8, J9; leave J7 open.
  - With every jumper open, both chips sit at 0x27 and clash.
  - The front panel's expanders also use 0x20/0x21, but they are on `Wire1` (see [[i2c-bus-map]]).
- **Drivers:** UDN2987 high-side drivers, active high, put +12 V on a coil.
- **Power:** 12 V on J11.
- **Rework:** the original 0.1 µF driver-output capacitors (C1–C19) tripped the UDN2987
  overcurrent latch. They were replaced with smaller values before testing.

## Measured behaviour (VNA, 2026-11-14)
- **Elements:** every relay switches its element. C2–C8 are within 3 % of nominal and L4–L8
  within 7 %. L1–L3 add 18–28 nH less than nominal, because energising also removes the relay's
  own shorting path.
- **Parasitics that matter above about 7 MHz:**
  - a 56 pF stray at the capacitor node, which sets the minimum shunt C
  - 80–90 nH of lead inductance per capacitor, 64 nH of it shared: C8 resonates at 11.9 MHz
  - coil self-resonances: L8 at 24 MHz, L7 at 33 MHz, L6 at 49 MHz
  - about 4.2 ns of extra path with the network in line: 0.8 dB loss and 10 dB return loss on 6 m
- **Loss:** 0.1–0.3 dB dissipated at matched settings.
- **Not yet tested:** power handling, hot switching and the balun.

## Firmware register map
The relay state lives in bits 33–51 of the 64-bit `hardwareRegister` (`SDT.h`):

| Bits | Relay | Function |
|---|---|---|
| 33–40 | K1–K8 | L1–L8 (`ATULLSB`..`ATULMSB`) |
| 41–48 | K9–K16 | C1–C8 (`ATUCLSB`..`ATUCMSB`) |
| 49 | K17 | caps at input, C-L (`ATUCAPINPUTBIT`) |
| 50 | K18 | balun output (`ATUBALANCEDBIT`) |
| 51 | K19+K20 | network in line (`ATUNETWORKBIT`) |

Before the ATU went in, the LPF/BPF/attenuator setters masked the register with 32-bit
literals, which cleared bits 32–63 on every band change. That was fixed first (commit
`c25c602`); see [[filter-boards]].
