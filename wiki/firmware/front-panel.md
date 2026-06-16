---
title: Front Panel (encoders, buttons, LEDs)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [front-panel, encoders, buttons, leds, mcp23017, i2c, interrupts, real-time, rotary]
source_refs: []
related: ["[[overview]]", "[[main-loop]]", "[[ui-state-machine]]", "[[mode-state-machine]]", "[[tune-frequency-control]]", "[[real-time-constraints]]", "[[hardware-platform]]", "[[i2c-bus-map]]"]
---

# Front Panel (encoders, buttons, LEDs)

**Files:** `FrontPanel.cpp` / `.h`, `FrontPanel_Rotary.cpp` / `.h`.

Reads the physical controls and turns them into the `InterruptType` events that the
[[main-loop]] FIFO dispatches to [[mode-state-machine]] / [[ui-state-machine]]. This page also
documents the **`encoder-i2c-speed`** branch's two-tier servicing design (the reason this
module was reworked).

## Hardware: two MCP23017 expanders on I²C (`Wire1`)

| Device | Inputs/outputs | INT pin |
|---|---|---|
| `mcp1` | 16 front-panel switches (pins 0–15) → buttons 0–15 | `INT_PIN_1` |
| `mcp2` | 6 switches (buttons 16–21), the 4 rotary encoders, 2 LEDs | `INT_PIN_2` |

Both MCP23017 INT lines are open-drain, active-LOW, wired to Teensy GPIO interrupts (FALLING).

## The four encoders

Quadrature encoders handled by **`Rotary_V12`** (`FrontPanel_Rotary.h`), a Ben Buxton-style
state machine adapted for MCP23017 pins, with per-encoder direction reversal and a `BOURN_ENCODERS`
build flag (Bourns parts have A/B swapped). `process()` returns accumulated steps
(interrupt-safe, resets on read).

| Encoder | Events emitted | Consumer |
|---|---|---|
| volume | `iVOLUME_INCREASE/DECREASE` | AGC/volume |
| filter | `iFILTER_INCREASE/DECREASE` | filter bandwidth |
| centre tune | `iCENTERTUNE_INCREASE/DECREASE` | coarse VFO ([[tune-frequency-control]]) |
| fine tune | `iFINETUNE_INCREASE/DECREASE` | fine software-mixer tune |

Buttons set `iBUTTON_PRESSED` (with debounce); `GetButton()` returns the id (0–21) for
`HandleButtonPress` in [[main-loop]], routed per UI state ([[ui-state-machine]]). LEDs are
driven by `FrontPanelSetLed(led, state)` → `mcp2.digitalWrite`.

## Two-tier servicing (the `encoder-i2c-speed` design)

The problem: MCP23017 reads are **blocking I²C** transfers. Doing them in a high-priority GPIO
ISR would stall audio; doing them only in the main loop ties encoder responsiveness to the
display/DSP cadence (10 ms+ when busy) — so fast tuning felt laggy and dropped steps. The fix
splits the work across two priority levels:

1. **GPIO ISR — flag only.** `fpInterrupt1ISR`/`fpInterrupt2ISR` (`FrontPanel.cpp:238-250`)
   are deliberately minimal: they set `interrupted1`/`interrupted2 = true` and return. **No
   I²C in the ISR.**
2. **1 ms timer — deferred I²C drain.** An `IntervalTimer fpServiceTimer` runs
   `ServiceFrontPanel()` every 1 ms at **NVIC priority 240** — *below* the OpenAudio update ISR
   at **208** (higher number = lower priority on Cortex-M), so encoder/button I²C **can never
   delay audio servicing** (`FrontPanel.cpp:340-344`). It performs the actual `interrupt1()` /
   `interrupt2()` reads when a flag is set **or the INT pin still reads LOW**.

Two robustness details:
- **Level recovery:** the MCP INT line stays asserted (LOW) until its GPIO is read, so
  `ServiceFrontPanel` also checks `digitalRead(INT_PIN) == 0` — recovering any edge the ISR
  missed while the line was already low (`FrontPanel.cpp:261-267`).
- **Single-read optimization:** `interrupt2()` calls `readGPIOAB()` once to grab both ports in
  one I²C transaction ("save an I2C instruction for speed", `FrontPanel.cpp:133`) before
  decoding all four encoders.

> Caveat in the code: all Teensy `IntervalTimer`s share the `IRQ_PIT` vector, so the
> `priority(240)` applies to every IntervalTimer, not just this one (`FrontPanel.cpp:340-342`).

`CheckForFrontPanelInterrupts()` (`FrontPanel.cpp:353`) is the **legacy main-loop poll** — it
polls the INT pins and calls the same `interrupt1()/interrupt2()` drains. It still runs from
[[main-loop]] as a backstop; the level-check makes the two paths idempotent (whoever reads the
GPIO first clears the INT line).

## Why it matters
This decoupling is the whole point of the `encoder-i2c-speed` branch (commit *"Speed up
encoder handling…"*): tuning latency/step-loss is now bounded by the 1 ms tick rather than the
main-loop budget, while audio integrity is preserved by the priority ordering. Relates to
[[real-time-constraints]] and the [[spectrum-refresh-floor]] finding (both about not coupling
responsiveness to the slow display path).

## Open questions
- Per-button id → physical-label map (0–21 across the two expanders).
- Debounce timing constants and whether long-press is detected here or in [[main-loop]].
- Whether the legacy `CheckForFrontPanelInterrupts()` main-loop call is now redundant given the
  timer, and any measured latency improvement from the branch.
