---
title: Memory Layout, Stack Headroom and Build Settings
type: decision
status: stable
created: 2026-11-11
updated: 2026-11-11
tags: [memory, stack, itcm, dtcm, dmamem, optimiser, build, reboot, teensy]
source_refs: []
related: ["[[spi-display-hang]]", "[[hardware-platform]]", "[[zoom-fft]]", "[[iq-imbalance-correction]]", "[[real-time-constraints]]", "[[dsp-chain]]", "[[main-loop]]", "[[og-startup-hang]]", "[[build-toolchain]]"]
---

# Memory Layout, Stack Headroom and Build Settings

Some Arduino **Optimize** settings made the radio reboot roughly 8 s after every start, over
and over. The cause was a **stack overflow**. Two things combined: the receive path put 16 KB
of scratch arrays on the stack, and the Teensy 4.1 sizes the stack in 32 KB steps depending
on how much code the optimiser emits. The arrays moved to `DMAMEM` in commit `7e3c8d9`
(2026-11-11). A second, unrelated problem made the radio freeze rather than reboot: see
[[spi-display-hang]].

## How the Teensy 4.1 divides its fast RAM

The i.MX RT1062 has 512 KB of FlexRAM in **16 banks of 32 KB**. Each bank is either ITCM
(instruction memory) or DTCM (data memory). Teensyduino gives code as many whole banks as it
needs. On Teensy 4 all code runs from ITCM unless it is marked `FLASHMEM`. The remaining banks
are DTCM. DTCM holds initialised and zeroed variables (`.data`/`.bss`), and **the stack is
whatever is left of DTCM**. The build output prints that remainder as
*"free for local variables"*.

The core places a 32-byte no-access MPU region at `_ebss`, the bottom of the stack
(`cores/teensy4/startup.c:307`). A stack overflow writes into it and causes a MemManage fault.
The core's fault handler keeps USB running, then **reboots after 8 s**
(`startup.c:647`). `CrashReport` on the next boot reads
`(MMARVALID) Accessed Address: <_ebss> (Stack problem)`.

So one setting decides the stack size, in 32 KB steps: the number of ITCM banks the code
needs. With ~146 KB of variables:

| ITCM banks | Stack available | Optimize settings that land here (V1.5.0 code) |
|---|---|---|
| 8 | 119 KB | Smallest Code with LTO (`oslto`) |
| 9 | 87 KB | Smallest Code (`osstd`) |
| 10 | 50 KB | Fast with LTO (`o1lto`) |
| 11 | **~17.5 KB** | Fast (`o1std`), Faster (`o2std`), Faster with LTO (`o2lto`), Debug (`ogstd`) |
| 13 | −48 KB (does not link) | Fastest, with and without LTO (`o3std`/`o3lto`) |

Any change in code size can move a build across a bank boundary. The step is a whole 32 KB
of stack in either direction.

## What used the stack

The stack was painted at boot and its low-water mark read back every second. Peak use was
**17.1–17.6 KB**, and almost all of it came from one function:

- `ZoomFFTExe()` (`DSP_FFT.cpp`) declared `float32_t x_buffer[data->N]` and `y_buffer[data->N]`.
  These are variable-length arrays with N = `READ_BUFFER_SIZE` = 2048, so **16 KB** of stack on
  every receive block whenever the spectrum zoom is not 1× ([[zoom-fft]]).
- `IQPhaseCorrection()` (`DSP.cpp`) declared an 8 KB VLA. It runs *before* `ZoomFFTExe()`,
  not inside it, so it did not add to the peak ([[iq-imbalance-correction]]).

Every 11-bank build was therefore at the limit. Faster with LTO (`o2lto`) inlines more and
had 256 B less stack: it needed 17,560 B against 17,376 B available and **rebooted every
time**. Fast and Faster survived with about 550 B spare. The CPU speed (528/600/720/816 MHz)
did not change the memory layout, and changed peak use by only ~50 B (interrupt nesting).
CPU speed therefore did not decide which settings rebooted.

## The fix (commit `7e3c8d9`)

Both arrays are now `static DMAMEM` arrays of `READ_BUFFER_SIZE` floats in OCRAM (RAM2), with
an early return if a block is larger. Both functions run only from the main loop, so shared
static scratch is safe. **Measured peak stack use dropped from 17,560 to 3,552 bytes.**

Why `DMAMEM` and not plain `static`: a static array in DTCM adds 16 KB to the variables. That
takes 16 KB away from the stack, so the margin stays the same. Only RAM2 actually frees
stack space.

Speed: OCRAM sits behind the 32 KB data cache. The receive path already streams its main
buffers from `DMAMEM` (`float_buffer_L/R`, the noise-reduction arrays). The new buffers start
and end on 32-byte boundaries, so they share no cache line with a DMA buffer. The extra cost
is estimated at well under 1 % of a block, but it has not been measured.

## Results after both fixes (528 MHz, 2026-11-11)

Each build was flashed and polled with the read-only CAT `FA;` every 2 s for 45 s:

| Setting | Result |
|---|---|
| `oslto`, `osstd`, `o1lto`, `o1std`, `o2std`, `o2lto` | pass (`oslto` also ran 10 min with no missed reply) |
| `o3std`, `o3lto` | do not fit in RAM1; cannot be flashed |
| `ogstd` (Debug) | **never starts; USB never appears.** Not a stack problem: it hangs the same way with 50 KB of stack. It is a Teensyduino 1.59 core startup bug, fixed in 1.60: see [[og-startup-hang]]. Passes on 1.62. |

These results are for Teensyduino 1.59. For 1.62, at 528 and 600 MHz, see [[build-toolchain]].

The "pass" results need **both** this fix and the SPI clock fix in [[spi-display-hang]].
Earlier checks that only watched for USB re-enumeration passed `o1std` and `oslto`, because
the SPI hang does not reboot.

## Diagnosing this class of problem again

- A **reboot ~8 s after a fault** is the core fault handler. Print `CrashReport` early in
  `setup()` to read the fault type and address.
- **Check "free for local variables" first** in the build output. If it is under ~20 KB, look
  for large local arrays or VLAs on the main-loop path.
- To measure peak use, fill the region from `_ebss + 32` up to the current SP with a pattern
  in `setup()`, then scan for the first overwritten word. A diagnostic build can itself move
  across a bank boundary and change the result. One way around this is to move `TXfilters`
  (35 KB, in DTCM) to `DMAMEM` in the diagnostic build only.
