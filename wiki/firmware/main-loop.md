---
title: Main Loop & Event Dispatch (Loop.cpp)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [loop, events, interrupts, real-time, cw-keyer]
source_refs: []
related: ["[[overview]]", "[[mode-state-machine]]", "[[ui-state-machine]]", "[[front-panel]]", "[[real-time-constraints]]", "[[cat-control]]", "[[tune-frequency-control]]", "[[rapid-tune-mute-freeze]]"]
---

# Main Loop & Event Dispatch (Loop.cpp)

**Files:** `Loop.cpp` (~62 KB), `Loop.h`. The heart of the firmware's control flow: it
owns `loop()`, the interrupt event FIFO, and the CW-key/PTT input plumbing.

## `loop()` execution sequence

`loop()` is marked `FASTRUN` (placed in RAM for speed) and must complete each pass in
**~10 ms** to avoid audio buffer overflow ([[real-time-constraints]]). Per-iteration order
(from `Loop.h`):

1. Shutdown check (power-off request → [[persistent-config]] save, then `ShutdownTeensy()`)
2. CW key debounce
3. Poll front panel (encoders + buttons) → [[front-panel]]
4. Poll CAT serial → [[cat-control]]
5. Consume queued interrupt events → dispatch to [[mode-state-machine]] / [[ui-state-machine]]
6. Update hardware (VFO, filters, attenuators)
7. Run DSP → [[dsp-chain]]
8. Refresh display (spectrum rate-limited) → [[display-subsystem]]

## Event-driven input: the interrupt FIFO

Interrupt handlers don't act directly — they **enqueue** an `InterruptType` into a 16-slot
FIFO that the main loop drains. This decouples ISR timing from processing.

API (in `Loop.h`):
- `SetInterrupt(i)` — append event (drops oldest if full)
- `PrependInterrupt(i)` — push to front; used by the **iambic keyer paddle-memory** feature
- `GetInterrupt()` — peek head without consuming
- `ConsumeInterrupt()` — pop + dispatch
- `GetInterruptFifoSize()` — depth, for overflow diagnostics

### `InterruptType` events
PTT (`iPTT_PRESSED/RELEASED`), CW keys (`iKEY1_PRESSED/RELEASED`, `iKEY2_PRESSED`),
encoders (volume / filter / center-tune / fine-tune ± ), buttons (`iBUTTON_PRESSED`),
VFO/mode/power changes, tune update requests, equalizer, BIT display, and the five
calibration triggers (`iCALIBRATE_POWER/FREQUENCY/RX_IQ/TX_IQ`, `iCALIBRATE_EXIT`) that
drive the [[hardware-state-machine]].

The four HOME-state center/fine tune cases in `ConsumeInterrupt()` also call
`NoteTuneActivity()`, the entry point for the [[rapid-tune-mute-freeze]] detector (which then
mutes audio and freezes/decouples the spectrum while a tune knob is spun fast).

## CW key / PTT handling

`SetupCWKeyInterrupts()` configures KEY1, KEY2, PTT with pull-ups and attaches handlers
(KEY1 on CHANGE, KEY2 on FALLING, PTT on CHANGE). Keyer behavior is configurable:
- `SetKeyType(KeyTypeId_Straight | KeyTypeId_Keyer)` — straight key vs. iambic keyer
- `SetKey1Dit()` / `SetKey1Dah()` — paddle assignment (`ED.keyerFlip`) for handedness

Precise dit/dah timing comes from the **1 ms `tick1ms()` timer** (in `PhoenixSketch.ino`),
which feeds DO events to ModeSm.

## Other responsibilities

- `CheckForSerialTimeSync()` — accepts a PJRC `T<unix-utc>` packet on USB serial to set the
  Teensy RTC + TimeLib clock.
- `ShutdownTeensy()` — graceful power-off: persists state to LittleFS + SD, signals the
  ATTiny power-management circuit, then lets power be cut.

## Open questions / TODO
- Document the exact hardware-update step (what gets pushed each pass vs. only on change).
- Confirm the exact hardware-update step; spectrum-refresh timing is documented in
  [[spectrum-refresh-floor]] (redraw ≈ `NCHUNKS × T_loop`).
