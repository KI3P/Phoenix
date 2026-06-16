---
title: Mode State Machine (ModeSm)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [state-machine, ssb, cw, transmit, keyer, iambic, calibration, statesmith]
source_refs: []
related: ["[[overview]]", "[[state-machine-architecture]]", "[[tune-frequency-control]]", "[[main-loop]]", "[[hardware-state-machine]]", "[[ssb-phasing-method]]", "[[rf-board]]", "[[audio-io]]"]
---

# Mode State Machine (ModeSm)

**Files:** `ModeSm.cpp` (~69 KB, **generated**), `ModeSm.h`, source diagram
`ModeSm.drawio`; hand-written actions/guards in `Mode.cpp` / `Mode.h`. The largest state
machine in the project. StateSmith algorithm: *Balanced2*. Do **not** hand-edit the `.cpp` —
edit `ModeSm.drawio` and regenerate ([[state-machine-architecture]]).

> ⚠️ The `PhoenixSketch.ino` header lists ModeSm's states as "SSB_RECEIVE, SSB_TRANSMIT,
> CW_RECEIVE, CW_TRANSMIT, TUNE". That is **out of date**: there is **no `TUNE` state**, and
> CW transmit is actually six sub-states. The authoritative list is `ModeSm.h` (below).

## Role
Owns the radio's **operating mode and TX/RX switching**, the **CW keyer**, and the entry
into **calibration**. State entry/exit actions reconfigure hardware via
`UpdateHardwareState()` → `UpdateRFHardwareState()` + `UpdateAudioIOState()` (`Mode.cpp:23`),
so every mode change deterministically reprograms the [[rf-board]] and audio I/O.

## State hierarchy (`ModeSm.h:43-65`)

```
ROOT
├── NORMAL_STATES
│   ├── SSB_RECEIVE          ← default operating state
│   ├── SSB_TRANSMIT
│   ├── CW_RECEIVE
│   ├── CW_TRANSMIT_MARK         ┐ straight key (carrier on/off)
│   ├── CW_TRANSMIT_SPACE        ┘
│   ├── CW_TRANSMIT_DIT_MARK     ┐
│   ├── CW_TRANSMIT_DAH_MARK     │ iambic keyer
│   ├── CW_TRANSMIT_KEYER_SPACE  │ (auto-timed elements)
│   └── CW_TRANSMIT_KEYER_WAIT   ┘
└── CALIBRATION_STATES
    ├── CALIBRATE_FREQUENCY
    ├── CALIBRATE_OFFSET_MARK / CALIBRATE_OFFSET_SPACE
    ├── CALIBRATE_POWER_MARK  / CALIBRATE_POWER_SPACE
    ├── CALIBRATE_RX_IQ
    └── CALIBRATE_TX_IQ_MARK  / CALIBRATE_TX_IQ_SPACE
```

`MARK` = transmitter/carrier keyed on, `SPACE` = off — the calibration MARK/SPACE pairs let
the routine toggle a TX tone for measurement.

## Events (`ModeSm.h:18-36`) and where they originate

All dispatched from [[main-loop]] (`ModeSm_dispatch_event(&modeSM, …)`):

| Event | Source | Meaning |
|---|---|---|
| `DO` | **1 ms timer** `TimerInterrupt()` (`Loop.cpp:355`) | keyer/timeout tick |
| `PTT_PRESSED` / `PTT_RELEASED` | PTT handler (`Loop.cpp:1318/1322`) | SSB TX key |
| `KEY_PRESSED` / `KEY_RELEASED` | straight-key handler (`Loop.cpp:1332/1340`) | straight-key |
| `DIT_PRESSED` / `DAH_PRESSED` | paddle decode (`Loop.cpp:920-930`) | iambic paddles |
| `TO_SSB_MODE` / `TO_CW_MODE` | mode toggle (`Loop.cpp:479/484`) | SSB↔CW |
| `CALIBRATE_FREQUENCY/RX_IQ/TX_IQ/POWER` | cal triggers (`Loop.cpp:1359-1379`) | enter cal |
| `CALIBRATE_EXIT` | (`Loop.cpp:1388`) | leave cal |
| `OFFSET_START` / `OFFSET_END` | freq-cal offset (`Loop.cpp:857/872`) | offset measurement |

## Normal-mode transition map (`ModeSm.cpp:330-458`)

- **SSB_RECEIVE** → `PTT_PRESSED` ⇒ SSB_TRANSMIT; `TO_CW_MODE` ⇒ CW_RECEIVE.
- **SSB_TRANSMIT** → `PTT_RELEASED` ⇒ SSB_RECEIVE.
- **CW_RECEIVE** → `KEY_PRESSED` ⇒ CW_TRANSMIT_MARK (straight key); `DIT_PRESSED` ⇒
  CW_TRANSMIT_DIT_MARK; `DAH_PRESSED` ⇒ CW_TRANSMIT_DAH_MARK (keyer); `TO_SSB_MODE` ⇒
  SSB_RECEIVE.
- Transmit transitions are guarded by **`IsTxAllowed()`** = current band is a `HAM_BAND`
  (`Mode.cpp:39`) — the safety interlock that blocks TX out of band.

### Straight-key CW (semi-break-in)
`CW_TRANSMIT_MARK` (carrier on) →`KEY_RELEASED`→ `CW_TRANSMIT_SPACE` (carrier off). SPACE
runs a `DO` countdown against `waitDuration_ms` (= `CW_TRANSMIT_SPACE_TIMEOUT_MS`,
`Globals.cpp:547`); a new `KEY_PRESSED` returns to MARK, otherwise it times out back to
CW_RECEIVE. On entering SPACE, `ModeCWTransmitSpaceEnter()` re-checks the key lines and
re-queues a press if still held (`Mode.cpp:28`).

### Iambic keyer (auto-timed)
Driven by the `DO` tick incrementing `markCount_ms` / `spaceCount_ms` (`ModeSm_Vars`):

| State | `DO` action | Transition |
|---|---|---|
| `CW_TRANSMIT_DIT_MARK` | `markCount_ms++` | `≥ ditDuration_ms` ⇒ KEYER_SPACE |
| `CW_TRANSMIT_DAH_MARK` | `markCount_ms++` | `≥ 3·ditDuration_ms` ⇒ KEYER_SPACE |
| `CW_TRANSMIT_KEYER_SPACE` | `spaceCount_ms++` | `≥ ditDuration_ms` ⇒ KEYER_WAIT |
| `CW_TRANSMIT_KEYER_WAIT` | timeout | `DIT_PRESSED`⇒DIT_MARK, `DAH_PRESSED`⇒DAH_MARK |

This is standard Morse timing — **dit = 1 unit, dah = 3 units, inter-element space = 1 unit**
— with `ditDuration_ms = 60000/(50·WPM) = 1200/WPM` (`Globals.cpp:131`, the PARIS formula),
`WPM = ED.currentWPM` (also settable over CAT, `CAT.cpp:475`).

### Iambic paddle memory
Because element timing runs to completion, a paddle squeezed *during* an element must be
remembered. [[main-loop]] handles this (`Loop.cpp:914-947`): in CW_RECEIVE / KEYER_WAIT a
paddle press is dispatched immediately; in DIT_MARK / DAH_MARK / KEYER_SPACE it is
**re-queued to the front of the FIFO** (`PrependInterrupt`) so it fires the instant the keyer
reaches KEYER_WAIT — classic iambic dot/dash memory. `ED.keyerFlip` swaps which physical
paddle (KEY1/KEY2) is dit vs. dah (`Loop.cpp:919-930`).

## Calibration branch
The four `CALIBRATE_*` events are handled by the `NORMAL_STATES` ancestor from **any** normal
state (`ModeSm.cpp:339-342` etc.), transitioning into `CALIBRATION_STATES`; `CALIBRATE_EXIT`
returns. ModeSm here owns the **TX keying/sequencing** for calibration (the MARK/SPACE
toggling, `PTT_PRESSED` advancing within e.g. `CALIBRATE_POWER_SPACE`), while the actual
measurement/optimization algorithms run in the dedicated sub-machines —
[[hardware-state-machine]] (PowerCalSm, ReceiveIQCalSm, TransmitCarrierCalSm,
TransmitIQCalSm) and the IRR sweep documented in [[iq-imbalance-correction]].

## Interactions
- **Button lockout:** `HandleButtonPress()` ignores all front-panel buttons while ModeSm is
  in any TX/keyed state or `CALIBRATE_*_MARK` (`Loop.cpp:395-403`).
- **RX CW decode:** in `CW_RECEIVE` the RX chain runs the CW audio filter + adaptive Morse
  decoder — see [[cw-processing]].
- **RX DSP:** the RX chain checks `modeSM.state_id == CW_RECEIVE` to switch in CW audio
  processing (`DSP.cpp:935`, see [[dsp-chain]]).
- **VFO setup** for each mode is delegated to `HandleTuneState()` in
  [[hardware-state-machine]] (which uses [[tune-frequency-control]] math) via the hardware
  update.

## Open questions
- Exact `OFFSET_START`/`OFFSET_END` flow in frequency calibration (only partially traced).
- Whether `CW_TRANSMIT_KEYER_WAIT` timing out to CW_RECEIVE uses `waitDuration_ms` too.
- Update the stale `PhoenixSketch.ino` state list (TUNE/CW_TRANSMIT) — tracked in
  [[documentation-todos]].
