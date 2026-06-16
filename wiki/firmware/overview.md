---
title: Firmware Architecture Overview
type: overview
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [architecture, state-machines, dsp, teensy]
source_refs: []
related: ["[[main-loop]]", "[[mode-state-machine]]", "[[ui-state-machine]]", "[[tune-frequency-control]]", "[[hardware-state-machine]]", "[[rf-board]]", "[[filter-boards]]", "[[dsp-chain]]", "[[display-subsystem]]", "[[front-panel]]", "[[cat-control]]", "[[persistent-config]]", "[[code-heritage]]", "[[hardware-platform]]", "[[i2c-bus-map]]"]
---

# Firmware Architecture Overview

Phoenix is the firmware for a **Teensy 4.1-based amateur radio transceiver** (the T41-EP
SDR). It is a complete software-defined radio: RF hardware control, real-time DSP, a
touchscreen UI, and CAT computer control, all running on a single ARM Cortex-M7.

The canonical hand-written architecture note lives in the source at
`code/src/PhoenixSketch/PhoenixSketch.ino` (header comment). This page is the wiki's
navigable summary; module pages drill into each subsystem.

## Design philosophy

The codebase is organized around five recurring principles (stated in the `.ino` header):

1. **Separation of concerns** — hardware control, signal processing, UI, and display are
   isolated modules with clear interfaces.
2. **State-machine control** — all hardware state changes flow through state machines, so
   behavior is deterministic and predictable. See [[state-machine-architecture]].
3. **Real-time constraints** — the main loop must complete within ~10 ms to avoid audio
   buffer overflows; display work is rate-limited. See [[real-time-constraints]].
4. **Read-only display** — display code reads global state but never mutates it.
5. **Hardware abstraction** — board interfaces (RF/LPF/BPF/Main) present clean APIs so
   hardware can be mocked in unit tests.

## Layered structure

```
        Front panel (encoders, buttons, PTT, CW key)   CAT (USB serial)
                         │                                    │
                  interrupt events (FIFO)              command parser
                         │                                    │
        ┌────────────────▼────────────────────────────────────▼──────────┐
        │                     Main Loop (Loop.cpp)                        │
        │   poll input · dispatch events · update HW · run DSP · draw     │
        └───┬───────────────┬───────────────┬───────────────┬────────────┘
            │               │               │               │
       State machines   RF / filter     DSP chain        Display
       ModeSm UISm      hardware        (OpenAudio)      (RA8875)
       HardwareSm(+cal) RFBoard/si5351  DSP_FFT/FIR/     MainBoard_Display*
                        LPFBoard BPFBoard  Noise/CW
            │
       Persistent config (ED struct in SDT.h · Storage · ParamSave)
       Tuning math: Tune.cpp (frequency/power functions, not a state machine)
```

## State machines

Phoenix is unusually state-machine-heavy: **six StateSmith-generated** machines (each with a
`.drawio` source) plus a **hand-written** hardware coordinator.

| Machine | File | Generated? | Role |
|---|---|---|---|
| ModeSm | `ModeSm.cpp/h` | ✔ StateSmith | Operating mode: SSB/CW × RX/TX, iambic keyer, calibration entry |
| UISm | `UISm.cpp/h` | ✔ StateSmith | Screen/menu navigation (SPLASH→HOME→menus) |
| PowerCalSm | `PowerCalSm.cpp/h` | ✔ StateSmith | Power-amplifier calibration sub-routine |
| ReceiveIQCalSm | `ReceiveIQCalSm.cpp/h` | ✔ StateSmith | RX I/Q balance calibration |
| TransmitCarrierCalSm | `TransmitCarrierCalSm.cpp/h` | ✔ StateSmith | TX carrier-null calibration |
| TransmitIQCalSm | `TransmitIQCalSm.cpp/h` | ✔ StateSmith | TX I/Q balance calibration |
| **HardwareSm** | `HardwareSm.cpp/h` | ✘ hand-written | Maps mode→RF/tune hardware state; drives the 4 cal sub-machines |

> Two `CLAUDE.md`/`.ino` myths corrected here: there is **no "TuneSm"** (`Tune.cpp` is a
> function library, [[tune-frequency-control]]; the `TuneReceive/SSBTX/CWTX` states live in
> the hand-written HardwareSm), and **HardwareSm itself is not StateSmith-generated**.

See [[mode-state-machine]], [[ui-state-machine]], [[tune-frequency-control]],
[[hardware-state-machine]].

## Execution flow

- **`setup()`** (in `Globals.cpp`) — initialize display, audio, RF/filter boards, and
  front panel; load settings from persistent storage; start the state machines; arm a
  1 ms timer for state-machine DO events.
- **`loop()`** (in [[main-loop]], `Loop.cpp`) — runs continuously, ~10 ms/iteration:
  shutdown check → CW key debounce → poll front panel → poll CAT → consume queued events →
  update hardware → run DSP → refresh display (spectrum rate-limited).
- **`tick1ms()`** (in `PhoenixSketch.ino`) — 1 ms interrupt dispatching DO events to
  ModeSm (CW keyer timing) and UISm (menu timeouts).

Input is **event-driven**: interrupt handlers enqueue `InterruptType` events into a 16-slot
FIFO that the main loop drains via `ConsumeInterrupt()`. See [[main-loop]] for the event
list and queue semantics.

## Module map

**Hardware control** — [[rf-board]] (Si5351 VFO, TX/RX switching, attenuators),
[[filter-boards]] (LPF/BPF relay banks, AD7991 power sensing), [[front-panel]]
(encoders/buttons).

**Signal processing** — [[dsp-chain]] (DSP.cpp routing/AGC, DSP_FFT spectrum, DSP_FIR
filters, DSP_Noise NR, DSP_CWProcessing), plus MainBoard_AudioIO codec interface.

**User interface** — [[display-subsystem]] (RA8875 12-pane layout, home/menus/calibration
screens), [[ui-state-machine]].

**Radio control** — [[mode-state-machine]], [[tune-frequency-control]], [[cat-control]]
(Kenwood TS-480-style emulation).

**Configuration & persistence** — [[persistent-config]] (the `ED` EEPROM-data struct in
`SDT.h`, `Storage.cpp`, `ParamSave.cpp`, `Config.h`).

## Hardware target

- **MCU**: Teensy 4.1 (ARM Cortex-M7, 600 MHz)
- **RF**: Si5351 clock generator for quadrature VFO; relay-switched BPF/LPF banks
- **Audio**: real-time 48/96/192 kHz via the OpenAudio_ArduinoLibrary
- **Display**: RA8875-driven TFT
- **Front panel I/O**: MCP23017 I²C GPIO expanders; rotary encoders

## Heritage

Phoenix descends from Frank Dziock DD4WH's **Teensy Convolution SDR**, heavily reworked by
the T41-EP community. See [[code-heritage]] for the lineage and the multi-author history.

## Testing

Unit tests use **Google Test + CMake** under `code/test/`, with Arduino, Si5351, GPIO, and
audio interfaces mocked so logic and state transitions run on a host machine. See
`wiki/CLAUDE.md` for how the wiki tracks design decisions discovered while testing.
