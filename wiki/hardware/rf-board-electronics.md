---
title: RF Board — Electronics (Si5351 LO, PE4312 attenuators, mixer front end)
type: hardware
status: draft
created: 2026-06-16
updated: 2026-06-16
tags: [hardware, rf, si5351, pe4312, attenuator, mixer, mcp23017, board-revisions]
source_refs: [sources/t41-ep-schematics]
related: ["[[hardware-platform]]", "[[i2c-bus-map]]", "[[rf-board]]", "[[filter-board-electronics]]", "[[t41-ep-schematics]]"]
---

# RF Board — Electronics

The *physical* RF board: the silicon, part numbers, and schematic-traced wiring that the
[[rf-board]] firmware module (`RFBoard.cpp`) drives. This page owns the **electronics**; the
firmware page owns the **API and algorithms** (quadrature generation, the PLL-only retune
optimization, the unit-test hooks). Confirmed against `T41-RF-board-V012` + daughterboards
([[t41-ep-schematics]]).

## Local oscillator — Si5351 quadrature VFO
- The **T41_DualClock_Daughter** board carries **2× Si5351A-B-GM** off a shared **25 MHz**
  reference (TCXO). On these newer (V12.8-era) boards the two chips provide **independent RX and
  TX quadrature LOs** plus a CW carrier — the "three VFOs" the firmware addresses:

  | VFO | Si5351 outputs | PLL | Purpose |
  |---|---|---|---|
  | **RX** | CLK0 + CLK1 (`CLK0RX`/`CLK90RX`) | PLLA | quadrature LO for RX I/Q |
  | **TX** | CLK4 + CLK5 (`CLK0TX`/`CLK90TX`) | PLLB | quadrature LO for SSB TX |
  | **CW** | CLK6 (`CLKCW`); CLK2 single-VFO | — | single-output CW carrier |

  RX on **PLLA** and TX on **PLLB** lets the two run at independent frequencies simultaneously
  (needed for the dual-VFO TX-spectrum monitor trick). Dual-VFO hardware is selected in firmware
  by `SI5351_DUAL_VFO_ADDR 0x61` / `HasDualVFOs()`.

### Board-revision history ⚠️
RF board revisions **prior to 12.8** carried a **single** Si5351 with only **one** set of
quadrature VFO outputs. On those boards the single quadrature pair (**CLK0/CLK1**) was
**shared** between the RX and TX mixers — the RX and TX LO could **not** run at independent
frequencies. They still provided the separate **CW carrier on CLK2**. The firmware's
`HasDualVFOs()` fallback (`SetTXVFOFrequency → SetRXVFOFrequency`) exists precisely to
accommodate these single-VFO boards. The dual-Si5351 arrangement above is V12.8-and-later only.

## Step attenuators — 2× PE4312
Two **PE4312** 6-bit digital step attenuators (**31.5 dB in 0.5 dB steps**), one per chain,
driven in parallel by the **MCP23017 (U21, 0x27)** ([[i2c-bus-map]]). The **electrical mapping
is revision-independent and matches the (unchanged) firmware**: **GPIOA → RX attenuator**,
**GPIOB → TX attenuator**; the low 6 bits = the attenuation word wired to the PE4312 control
pins:

| GPIO bit | Weight | PE4312 pin | Ctrl |
|---|---|---|---|
| 0 | 0.5 dB | 20 | C0.5 |
| 1 | 1 dB | 19 | C1 |
| 2 | 2 dB | 17 | C2 |
| 3 | 4 dB | 16 | C4 |
| 4 | 8 dB | 15 | C8 |
| 5 | 16 dB | 1 | C16 |

(PE4312 pin 18 = GND, hence the gap.) The GPIO value is the **attenuation in dB × 2** — the
firmware writes `round(2·atten_dB) & 0x3F` (`SetAttenuator`, [[rf-board]]). The **upper bits
GPA6/7 and GPB6/7 are unused / not connected** (owner-confirmed 2026-06-15) — each bank carries
only the 6-bit attenuation word.

### Reference designators differ by board revision
(Wiring/firmware unchanged — a packaging-convenience change. A bare "U3" is ambiguous across
revisions — always state the revision. Owner-clarified 2026-06-14.)

| Revision | RX atten | TX atten | Packaging |
|---|---|---|---|
| **V12.8** (current; the `raw/` schematic) | **U11** | **U3** | PE4312 on removable daughterboards (chip = `U1` in the daughter sheet; U12 = dual-clock LO daughter) |
| **prior revisions** | **U3** | **U12** | PE4312 placed directly on the RF board |

## Mixer / RF front end
- **Mixer transformers:** **ADT1-1 / ADT2-1T**.
- **Gain blocks:** **PSA-8A+** and **MAR-3SM+** MMICs.
- **RF switches:** **MASWSS0179** (T/R + cal loopback).
- **I/Q routing:** **74CBTLV3253** analog muxes.

## I/Q baseband amplifiers
**AD8599, OPA2209, MC33078, LM358B** op-amps buffer/scale the I and Q channels to/from the
main-board converters: **J1 IQ_Out → PCM1808** (RX I/Q in), **J5 IQ_In ← SGTL5000 TX I/Q**
(see [[hardware-platform]] for the converter map).

## Open questions
- PA bias / supply rails feeding the front-end gain blocks.
- Exact divider-boundary frequencies where the firmware does a full multisynth/phase re-setup
  vs. PLL-only retune (a firmware concern — see [[rf-board]]).
