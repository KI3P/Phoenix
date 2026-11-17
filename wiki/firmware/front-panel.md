---
title: Front Panel (encoders, buttons, LEDs)
type: module
status: draft
created: 2026-06-08
updated: 2026-11-17
tags: [front-panel, encoders, buttons, leds, mcp23017, i2c, interrupts, real-time, rotary]
source_refs: []
related: ["[[overview]]", "[[main-loop]]", "[[ui-state-machine]]", "[[mode-state-machine]]", "[[tune-frequency-control]]", "[[real-time-constraints]]", "[[hardware-platform]]", "[[i2c-bus-map]]", "[[rapid-tune-mute-freeze]]"]
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

Quadrature encoders handled by **`Rotary_V12`** (`FrontPanel_Rotary.h`), with per-encoder
direction reversal (`*_REVERSED` in `Config.h`). `interrupt2()` reads both contacts of the
encoder whose pin changed and calls `update(state)` (bit 0 = A, bit 1 = B, 1 = open);
`process()` returns accumulated steps (interrupt-safe, resets on read).

**Decoder (since 2026-11-17): Ben Buxton's full-step state table, per encoder.** The encoder
rests at 11; one detent runs 11→10→00→01→11 (A leads, +1 unreversed) or 11→01→00→10→11 (B
leads, −1). A step is counted only on returning to 11 after the whole sequence, so a contact
chattering between two neighbouring states cannot add steps. Two additions to Buxton's table:
- a jump where both contacts changed between readings (a fast spin against the 1 ms service)
  is resolved in the direction already in progress — single-contact chatter changes one bit
  at a time, so it can never trigger this;
- a jump from rest straight to 00 (the first contact's edge missed) resumes the direction of
  the last step. A wrong guess runs backwards through the table and counts nothing.

*Why it changed.* The previous decoder (AJ8X's May 2026 tweak of the G0ORX version) armed a
direction on the first contact, counted when the second read closed, and stayed armed while
steps kept coming (80 ms window, refreshed by every step) — so a chattering closing contact
counted once per chatter. Its flags and timer were also file statics shared by all four
encoders. An edge log on the KI3P prototype's Filter knob (2026-11-17) showed contact A
reopening up to ~90 times over tens of ms as it closed on a clockwise click: the old decoder
counted +19, +6, 0, +2, 0 for five clockwise clicks (the zeros were missed edges of contact
B) and −1 for each anticlockwise one; the new decoder counted exactly ±1 on all ten. The
recorded readings are replayed in `FrontPanel_test.cpp` (`RotaryEncoder.*`). The same log
showed this knob comes to rest after a clockwise click with B already closed (state 01),
right on B's switching point.

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

The flip side: encoders are now responsive enough that a *fast* spin enqueues a tune event on
nearly every loop, which surfaced audio glitches and a jerky spectrum sweep. The optional
[[rapid-tune-mute-freeze]] feature detects that condition and masks both.

## Open questions
- Per-button id → physical-label map (0–21 across the two expanders).
- Debounce timing constants and whether long-press is detected here or in [[main-loop]].
- Whether the legacy `CheckForFrontPanelInterrupts()` main-loop call is now redundant given the
  timer, and any measured latency improvement from the branch.
