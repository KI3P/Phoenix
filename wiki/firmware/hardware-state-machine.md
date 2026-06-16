---
title: Hardware & Calibration Coordinator (HardwareSm)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [hardware, coordinator, relays, tr-switching, calibration, vfo, statesmith]
source_refs: []
related: ["[[overview]]", "[[state-machine-architecture]]", "[[mode-state-machine]]", "[[tune-frequency-control]]", "[[rf-board]]", "[[filter-boards]]", "[[iq-imbalance-correction]]", "[[persistent-config]]"]
---

# Hardware & Calibration Coordinator (HardwareSm)

**Files:** `HardwareSm.cpp` (~30 KB) + `HardwareSm.h`, with four calibration partials:
`HardwareSm_PowerCalibration.cpp`, `_ReceiveIQCalibration.cpp`,
`_TransmitCarrierCalibration.cpp`, `_TransmitIQCalibration.cpp`.

> ⚠️ **Not StateSmith-generated.** Unlike ModeSm/UISm, `HardwareSm.cpp` is **hand-written**
> (no `HardwareSm.drawio`, no generated banner). It is a coordinator built around two
> hand-coded state enums, not a StateSmith machine. The four *calibration sub-machines* it
> drives **are** generated (see below).

HardwareSm is the **bridge between logical mode and physical hardware**. [[mode-state-machine]]
decides *what* mode the radio is in; HardwareSm translates that into *which relays, signal
paths, filters, and VFO frequencies* — a deliberately **many-to-one** mapping.

## Two state variables

```c
enum RFHardwareState { RFCWMark, RFCWSpace, RFReceive, RFTransmit,
                       RFCalReceiveIQ, RFCalTransmitIQ, RFCalTransmitIQSingleVFO, RFInvalid };
enum TuneState       { TuneReceive, TuneSSBTX, TuneCWTX,
                       TuneCalReceiveIQ, TuneCalTransmitIQ, TuneInvalid };
```

Both are derived from `modeSM.state_id` by a `switch`. (These `TuneState` values are the real
home of the "TuneReceive/SSBTX/CWTX" that `CLAUDE.md`/`.ino` wrongly attribute to a
"TuneSm" — see [[tune-frequency-control]].)

## 1. RF hardware state — `UpdateRFHardwareState()` (`HardwareSm.cpp:531`)

Maps mode → RF board state, then `HandleRFHardwareStateChange()` does the physical switching
(relays, TX/RX path, PA select) with **settling delays** (relay ~6 ms, PIN-diode switching).

| RFHardwareState | ModeSm states that map to it |
|---|---|
| `RFReceive` | SSB_RECEIVE, CW_RECEIVE, CALIBRATE_FREQUENCY, CALIBRATE_OFFSET_SPACE, CALIBRATE_TX_IQ_SPACE |
| `RFTransmit` | SSB_TRANSMIT, CALIBRATE_OFFSET_MARK |
| `RFCWMark` (key down) | CW_TRANSMIT_MARK/DIT_MARK/DAH_MARK, CALIBRATE_POWER_MARK |
| `RFCWSpace` (key up) | CW_TRANSMIT_SPACE/KEYER_SPACE/KEYER_WAIT, CALIBRATE_POWER_SPACE |
| `RFCalReceiveIQ` | CALIBRATE_RX_IQ |
| `RFCalTransmitIQ` / `…SingleVFO` | CALIBRATE_TX_IQ_MARK (dual-VFO vs. external-analyzer) |

Key points:
- **CW QSK** uses the distinct `RFCWMark`/`RFCWSpace` pair so key-down/key-up toggles the TX
  path quickly without a full T/R relay cycle.
- **Calibration reuses the normal TX/RX hardware** — the cal MARK/SPACE mode states fold onto
  the same RFReceive/RFTransmit/RFCWMark states.
- A `previousRadioState` guard skips the (slow) relay work when the RF state is unchanged but
  still calls `UpdateTuneState()` (frequency may have moved). `ForceUpdateRFHardwareState()`
  bypasses the guard when a non-mode parameter (e.g. PA selection) changes
  (`HardwareSm.cpp:598`).

## 2. Tune state — `UpdateTuneState()` → `HandleTuneState()` (`HardwareSm.cpp:639,715`)

Maps mode → `TuneState`, then `HandleTuneState()` programs **band-dependent hardware** in
order — PA (100 W vs bypass), `SelectLPFBand`, `SelectBPFBand`, `SelectAntenna`,
`UpdateFIRFilterMask` — and finally the **VFO frequency** via the [[rf-board]], using the math
in [[tune-frequency-control]]. The per-state frequency formulas and the SSB-TX 3rd-harmonic
monitor trick are documented there. Calibration tune states place the VFOs specially (e.g.
`TuneForReceiveIQCalibration` puts the test tone Fs/4 from the LO, `HardwareSm.cpp:605`).

## 3. Calibration orchestration

Four routines, each a hand-written **partial that wraps a StateSmith-generated sub-machine**
(state maps documented in [[calibration-state-machines]]):

| Partial file | Sub-machine (generated) | Calibrates | Theory |
|---|---|---|---|
| `HardwareSm_PowerCalibration.cpp` | `PowerCalSm` | PA output power curve | tanh fit, [[tune-frequency-control]] |
| `HardwareSm_ReceiveIQCalibration.cpp` | `ReceiveIQCalSm` | RX I/Q balance | [[iq-imbalance-correction]] |
| `HardwareSm_TransmitIQCalibration.cpp` | `TransmitIQCalSm` | TX I/Q balance | [[iq-imbalance-correction]] |
| `HardwareSm_TransmitCarrierCalibration.cpp` | `TransmitCarrierCalSm` | TX carrier null (LO leakage) | (distinct from image) |

Each partial: starts its sub-SM (`PowerCalSm_start`, …), dispatches events to step it
(`…_EventId_AUTO`, `NEXT_POINT`, `RECORD_DATA_POINT`, `RESET`, …), and bridges results back
into the RF/tune state and the `ED` calibration arrays ([[persistent-config]]). The
**power-cal** routine records (attenuation, power) points and fits the tanh PA model
(`CalculatePowerCurveFit` → `FitPowerCurve` in `Tune.cpp`); the **IQ** routines run the
sideband-separation sweep documented in [[iq-imbalance-correction]].

Calibration is **entered** when [[mode-state-machine]] takes a `CALIBRATE_*` event into its
`CALIBRATION_STATES` branch; the sub-SMs are **driven** from [[main-loop]] (front-panel cal
actions, `Loop.cpp:720-795`) and from these partials.

## Invocation
`UpdateRFHardwareState()` is called by `UpdateHardwareState()` (`Mode.cpp:24`) on every ModeSm
state entry, and directly from [[main-loop]] on frequency/band/mode changes
(`Loop.cpp:438-578`). This is the single funnel through which all hardware reconfiguration
flows.

## Open questions
- ~~Full state map of each generated cal sub-SM~~ → done in [[calibration-state-machines]]
  (`PowerCalSm` POWERPOINT1/READ_AND_ADJUST/
  POWERCOMPLETE, etc.) — only partially traced.
- `RFCalTransmitIQSingleVFO` external-analyzer flow vs. the dual-VFO self-cal path.
- Exact relay/PIN-diode settling delays and their T/R timing budget vs. [[real-time-constraints]].
- Whether a `TX carrier-null` theory page should be split out of [[iq-imbalance-correction]].
