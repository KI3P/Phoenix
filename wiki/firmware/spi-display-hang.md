---
title: SPI Display Hang (LPSPI Clock Root)
type: decision
status: stable
created: 2026-11-11
updated: 2026-11-11
tags: [spi, lpspi, ra8875, display, hang, clock, teensy, cat]
source_refs: []
related: ["[[memory-layout-and-stack]]", "[[display-subsystem]]", "[[hardware-platform]]", "[[main-loop]]", "[[cat-control]]", "[[real-time-constraints]]"]
---

# SPI Display Hang (LPSPI Clock Root)

The radio could **freeze without rebooting**, anywhere from a few seconds to a couple of
hours after start. CAT stopped answering, but USB stayed enumerated and audio interrupts kept
running. The main loop was stuck inside an RA8875 display call: the SPI peripheral had lost
the byte that call was waiting for. The cause was the LPSPI clock root running at 240 MHz.
Commit `fbdd06f` (2026-11-11) moves it to 132 MHz. This is separate from the boot-loop stack
overflow in [[memory-layout-and-stack]], although both first showed up as "some optimiser
settings don't work".

## Symptom

- CAT read commands (`FA;`) get no reply, and USB writes stall once the receive buffers fill.
- `/dev/ttyACM0` stays present and there is no re-enumeration. So it is not a fault: the
  core's fault handler would reboot within 8 s.
- An upload still works **without the PROGRAM button**, because the USB interrupt is still
  serviced.
- It occurred with every Optimize setting tested. Each setting was simply more or less likely
  to hit it within a short test (at 528 MHz: Smallest Code with LTO hung in 3 of 3 one-minute
  boots, both before and after the stack fix).

## How it was located

A temporary diagnostic used a spare hardware timer, GPT2, firing every 10 ms at NVIC priority
144: above the audio (208) and PIT/1 ms tick (240) interrupts, below USB (128). It ran a
`naked` handler that read the interrupted context's exception frame. When `loop()` had not
advanced for 2 s, it printed the stuck PC/LR, then walked return addresses up the stack.

Every hang was in thread mode, in the same place:
`DrawHome()` → `RA8875::writeTo()` / `_writeData()` / `drawPixels()` →
`RA8875::writeCommand()` → **`SPIClass::transfer()`**. On Teensy 4 that function writes `TDR`,
then spins with no timeout until the RX FIFO count is non-zero (`SPI.h:1248-1252`).

At the hang, LPSPI4 read: `CR=1` (enabled), `TCR=C0000007` (mode 3, 8-bit, receive not masked),
`FSR=0` (TX and RX FIFOs empty). **Pushing one extra byte into `TDR` from the timer interrupt
made the main loop resume every time.** The peripheral was healthy; exactly one received
byte had gone missing.

## Cause: the LPSPI clock root

Teensyduino 1.59's `SPI.begin()` sets `CCM_CBCMR` to LPSPI_CLK_SEL = 1 (PLL3 PFD0, 720 MHz),
LPSPI_PODF = 2 (÷3). That gives a **240 MHz** LPSPI functional clock; the source comment
mentions 132 MHz (`SPI.cpp:1274-1281`). The decisive measurement used the same diagnostic
build (Smallest Code with LTO, 528 MHz), 10 minutes each, counting hangs that the probe
recovered:

| LPSPI clock root | Hangs in 10 min |
|---|---|
| PLL3 PFD0 ÷ 3 = 240 MHz (Teensyduino default) | 8 |
| PLL2 ÷ 4 = 132 MHz | **0** |

With an unchanged rate of 8 per 10 minutes, zero has a probability of about e⁻⁸ ≈ 0.03 %. The
datasheet's LPSPI_CLK_ROOT limit has not been checked here. The working assumption is that
240 MHz exceeds it, and that an occasionally lost word is how this silicon fails when
overclocked. That would also explain why the failure looks random and timing-dependent. It
may depend on CPU speed through the core voltage that `set_arm_clock()` selects, but this
was not tested.

## Hypotheses ruled out by measurement

- **The stack fix.** The hang rate was the same with and without commit `7e3c8d9`.
- **Teensy's per-transaction disable/enable.** `beginTransaction()` writes `CR=0`, reconfigures,
  then `CR=MEN` on every call. A patched SPI library that only does this when `CCR` changes
  still gave 8 hangs in 10 min.
- **The `REF` (receive FIFO overflow) flag.** It is set every time the LPSPI is disabled and
  re-enabled after a transfer, about once per display transaction (~130,000/s). This is
  harmless. It was sticky in every hang dump, which made it look guilty.
- **Synthetic stress.** About 4.5 M transfers with the display's chip-select high lost no
  bytes. Variants included random gaps, zero gap between enable and `TDR`, alternating
  20/4 MHz settings, and a GPIO write in between. Losses needed real display traffic.
- **A second SPI user or display access from an interrupt.** Nothing else uses LPSPI4. No
  state-machine action reachable from `tick1ms()` draws on the display. DMA requests were off
  (`DER=0`).

## The fix (commit `fbdd06f`)

`SetSPIClockRootInSpec()` in `MainBoard_Display.cpp` runs right after `tft.begin()`, which
calls `SPI.begin()`, the function that sets `CBCMR`. With interrupts off, it disables LPSPI4,
switches the root to PLL2 ÷ 4 = 132 MHz, and re-enables it. It then runs a 1 MHz dummy
transaction, because `SPIClass` caches the divider for the last clock it was asked for and
would otherwise keep a divider computed for 240 MHz. The function is guarded by
`__IMXRT1062__`, so the host unit tests skip it.

The display divider changes accordingly. The RA8875 asks for 20 MHz writes and 4 MHz reads,
and writes now run at **18.9 MHz** (132 ÷ 7) instead of 20 MHz. The screen was checked by eye
and looks normal.

After both fixes, every buildable Optimize setting at 528 MHz passed a 45 s CAT liveness
check. Smallest Code with LTO ran 10 min with no missed reply (see
[[memory-layout-and-stack]] for the table).

## Notes for next time

- **Liveness tests must poll the firmware**, for example with read-only CAT `FA;`. Watching
  only for USB drops misses this hang entirely.
- Any other LPSPI user (`SPI1`/`SPI2`) shares the same clock root and gets the fix
  automatically. A future `SPI.begin()` call after display init would restore 240 MHz.
- Upstream: this probably affects any Teensy 4 SPI user. It has not been reported to PJRC.
