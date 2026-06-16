---
title: RF Board & Si5351 Quadrature VFO (RFBoard)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [rf, si5351, vfo, quadrature, attenuator, tr-switch, hardware-abstraction]
source_refs: [sources/t41-ep-schematics]
related: ["[[overview]]", "[[rf-board-electronics]]", "[[tune-frequency-control]]", "[[hardware-state-machine]]", "[[filter-boards]]", "[[ssb-phasing-method]]", "[[iq-quadrature-sampling]]", "[[iq-imbalance-correction]]", "[[hardware-platform]]", "[[i2c-bus-map]]", "[[t41-ep-schematics]]"]
---

# RF Board & Si5351 Quadrature VFO (RFBoard)

**Files:** `RFBoard.cpp` (~36 KB), `RFBoard.h`, plus a vendored/modified Etherkit Si5351
driver `RFBoard_si5351.cpp` (~49 KB) / `.h`.

The hardware-abstraction layer for the RF board: a clean, mockable API (used heavily in the
unit tests) over the **Si5351 clock generator** (the quadrature LO), **step attenuators**,
**TX/RX switching**, **modulation routing**, and the **calibration loopback**. All hardware
reconfiguration is funneled through here by `HandleTuneState()`/`HandleRFHardwareStateChange()`
in [[hardware-state-machine]], using the frequency math from [[tune-frequency-control]].

## Three VFOs on one Si5351

| VFO | Si5351 outputs | PLL | Purpose |
|---|---|---|---|
| **RX** | CLK0 + CLK1 (`CLK0RX`/`CLK90RX`) | PLLA | quadrature LO for RX I/Q |
| **TX** | CLK4 + CLK5 (`CLK0TX`/`CLK90TX`) | PLLB | quadrature LO for SSB TX |
| **CW** | CLK6 (`CLKCW`); CLK2 single-VFO | — | single-output CW carrier |

RX on **PLLA** and TX on **PLLB** lets the two run at independent frequencies simultaneously
(`RFBoard.cpp:388,403`) — needed for the dual-VFO TX-spectrum monitor trick
([[tune-frequency-control]]). On single-VFO boards `SetTXVFOFrequency` just falls back to
`SetRXVFOFrequency` (`RFBoard.cpp:535`), gated by `HasDualVFOs()`.

The `HasDualVFOs()` fallback is the firmware accommodation for **older (pre-V12.8) single-Si5351
boards**, where one shared quadrature pair (CLK0/CLK1) served both RX and TX mixers (CW still on
CLK2). The chip-level board-revision detail lives in [[rf-board-electronics]].

## Quadrature generation — the heart of it

SSB by the phasing method ([[ssb-phasing-method]]) needs an LO in **two copies 90° apart**.
Phoenix produces them from two Si5351 clocks on the **same PLL**, by two different techniques
depending on frequency (`SetRXVFOFrequency`, `RFBoard.cpp:483`):

- **≥ ~3.2 MHz (multisynth divider ≤ 126):** set both clocks to the same frequency, then write
  the **phase-offset register** of CLK90 equal to the multisynth divider value. The phase
  register counts in VCO half-periods, so "phase = divider" is a quarter-cycle = **90°**
  (`set_phase(CLK90RX, rxmultiple)`, `RFBoard.cpp:501`). A `pll_reset` aligns the outputs.
- **< 3.2 MHz (divider > 126):** the phase register can't reach 90°, so Phoenix uses the
  **TJ-Labs timed-delay technique** (`RFBoard.cpp:506-523`): set both clocks **4 Hz low**,
  align them, set CLK0 to the target, wait a precise delay (~58.5 ms — a quarter of the 4 Hz
  beat period), then set CLK90 to the target. The accumulated beat is exactly 90°. Interrupts
  are disabled (`cli()/sei()`) so the delay is accurate; source:
  <https://tj-lab.org/2020/08/27/...> (cited in code).

Two important implementation details:
- **`EvenDivisor()`** forces an even integer multisynth divider — required for clean,
  glitch-free phase alignment.
- **PLL-only retune optimization:** while a frequency change stays within the same divider
  ("multiple") range, only the PLL is re-programmed, not the multisynth/phase — minimizing I²C
  traffic per encoder detent (`RFBoard.cpp:492-495`). The expensive full re-setup only happens
  when crossing a divider boundary.

## Frequency units
The VFO frequency is carried in **Hz × 100 (centi-Hz, 0.01 Hz resolution)**:
`SetRXVFOFrequency` takes `frequency_cHz` documented in the `.cpp` as "(Hz × 100)"
(`RFBoard.cpp:481`), matching the Etherkit `SI5351_FREQ_MULT = 100` and `HandleTuneState`'s
`centerFreq*100`. The `_cHz` suffix names this correctly — these are centi-Hz. (Earlier code
used a `_dHz`/"decihertz" naming, which was a misnomer; all uses have since been renamed to
`cHz`.)

> Branch note (2026-06-16): the `_dHz`→`_cHz` symbol rename is **not yet present on
> `encoder-i2c-speed`** — that working tree still shows `_dHz` identifiers (docstrings already
> flag the misnomer). This page reflects the intended/renamed state; expect it to match once the
> rename branch merges.

`SetFrequencyCorrectionFactor()` applies the crystal ppb correction (from frequency
calibration, [[hardware-state-machine]]) via the Si5351 `set_correction`.

## Other hardware controls
- **Step attenuators** — independent TX and RX, **0–31.5 dB in 0.5 dB steps**, clamped/rounded
  (`SetRXAttenuation`/`SetTXAttenuation`). `SetAttenuator` writes `round(2·atten_dB) & 0x3F`
  (the GPIO value is the **attenuation in dB × 2**) to `writeGPIOA` (RX) / `writeGPIOB` (TX)
  (`RFBoard.cpp:276,67-68,102-103`) on an **MCP23017** GPIO expander; `GetRFMCPRegisters` exposes
  the register for tests. Used for RX gain and TX power setting ([[tune-frequency-control]] power
  cal). The GPIO-bit → PE4312 wiring and per-revision designators are in [[rf-board-electronics]].
- **VFO output power / enable** — `SetRXVFOPower` etc. select Si5351 drive (2/4/6/8 mA);
  `Enable/DisableRXVFOOutput` toggle the CLK outputs.
- **TX/RX switching** — `SelectTXMode`/`SelectRXMode` drive the T/R relays.
- **Modulation routing** — `SelectTXSSBModulation`/`SelectTXCWModulation`.
- **CW keying** — `CWon`/`CWoff` key the CW carrier between Morse elements (driven by
  [[mode-state-machine]]'s keyer states via [[hardware-state-machine]]).
- **Calibration loopback** — `EnableCalFeedback` routes TX output back into the RX input for
  I/Q calibration ([[iq-imbalance-correction]]); `DisableCalFeedback` restores normal routing.

## Physical hardware
The chip-level electronics this module drives — the dual-Si5351 LO (and the single-Si5351 board
revisions), the PE4312 attenuators with their GPIO-bit → pin table and per-revision designators,
the ADT/PSA/MAR/MASWSS mixer front end, and the I/Q baseband op-amps — are documented in
**[[rf-board-electronics]]** (schematic-traced against `T41-RF-board-V012`,
[[t41-ep-schematics]]). See also the platform-level [[hardware-platform]].

## Testability
The header exposes `getRXTXState`, `getCWState`, `getCalFeedbackState`, `getModulationState`,
`GetRFMCPRegisters`, `ResetVFOState`, `SetDualVFOs` — explicit hooks so the unit tests can
assert hardware state without a physical board (the abstraction pattern CLAUDE.md cites as the
template for new hardware modules).

## Open questions
- The Si5351 driver is a modified Etherkit library — document the local modifications vs.
  upstream (the `set_freq_manual` + manual phase path).
- Exact `EvenDivisor` policy and the divider-boundary frequencies where full re-setup occurs.
- ~~Which physical attenuator part sits behind the MCP23017.~~ **Resolved:** PE4312 digital
  step attenuators on the T41_Attenuator_Daughter board ([[t41-ep-schematics]], 2026-06-14).
- ~~Which MCP23017 (U21) port bits map to the PE4312 parallel inputs.~~ **Resolved (2026-06-14/15):**
  GPIOA→RX attenuator, GPIOB→TX attenuator, low 6 bits = attenuation word (0.5 dB LSB); table +
  per-revision designators above. The **upper bits GPA6/7 and GPB6/7 are unused / not connected**
  (owner-confirmed 2026-06-15) — each bank carries only the 6-bit attenuation word.
