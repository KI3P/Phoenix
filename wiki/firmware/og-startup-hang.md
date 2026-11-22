---
title: Debug (-Og) Startup Hang (Flash Clock Gated)
type: decision
status: stable
created: 2026-11-11
updated: 2026-11-11
tags: [optimiser, debug, startup, teensyduino, flexspi, pll, hardfault, teensy, build]
source_refs: []
related: ["[[memory-layout-and-stack]]", "[[spi-display-hang]]", "[[hardware-platform]]", "[[documentation-todos]]", "[[build-toolchain]]"]
---

# Debug (-Og) Startup Hang (Flash Clock Gated)

With the Arduino Optimize setting **Debug** (`opt=ogstd`, `-Og`), the radio never starts. The
Teensy never attaches to USB, and recovering it needs the PROGRAM button. The cause is a bug
in the **Teensyduino 1.59 core's startup code**, not in Phoenix: no Phoenix code runs before
the hang. PJRC fixed it upstream in **Teensyduino 1.60** (core tagged 2026-01-15). 1.62 also
has the fix. Found 2026-11-11. Nothing in the repo changed.

## Symptom

- After the upload, the kernel log shows the bootloader (`16c0:0478`) disconnecting, and then
  **nothing**: no new USB device and no failed enumeration. On 2026-11-11 the log was silent
  for 32 minutes until PROGRAM was pressed.
- There was no `CrashReport` and no reboot loop.
- This is different from the two faults that looked similar in
  [[memory-layout-and-stack]] (an ~8 s reboot) and [[spi-display-hang]] (a freeze with USB
  still alive). It is **not** a stack problem: it hung the same way with 50 KB of stack.

The core starts USB in `ResetHandler2()` (`cores/teensy4/startup.c`) before running C++
constructors or `setup()`. So if the board never appears on USB at all, the hang must be in
the core's own startup, before `usb_init()`.

## Cause

`reset_PFD()` in Teensyduino 1.59's `startup.c` is `FLASHMEM` and runs from flash. It is
called right after memory initialisation and again after `usb_pll_start()`. To change the
PLL3 PFD dividers it does two things in order:

1. It writes `CCM_ANALOG_PFD_480_SET` with the four clock-gate bits, **stopping all four PLL3
   PFD outputs**.
2. It writes `CCM_ANALOG_PFD_480 = 0x13110D0C`, which sets the dividers and **ungates** the
   outputs again.

**FlexSPI, the program-flash interface, is clocked from PLL3 PFD0.** Read on the running chip:
`CCM_CSCMR1` = `0x66130040`, so FLEXSPI_CLK_SEL = 3 (PLL3 PFD0) and PODF = 4, giving
720 MHz ÷ 5 = 144 MHz. Between steps 1 and 2 the flash has no clock, so nothing may be
fetched from it.

- **At `-O2`/`-Os`** (with or without LTO), `reset_PFD()` is inlined and both constants are
  already in registers, so the gap contains only stores.
- **At `-Og`** the function stays out of line and loads the second constant from a **literal
  pool in flash**, between the two stores (`ldr r2, [pc, #12]`). That read cannot complete,
  so the CPU takes a **HardFault**. The fault handler then cannot run normally either, since
  the flash clock is still gated, so the chip stays dead without the 8 s reboot.

The same core already has comments such as "Some optimization with LTO won't start without
this delay, but why?". Startup code that depends on how the compiler happens to lay it out is
a known fragility.

## How it was found

1. **Kernel log.** The board never attached to USB, which put the hang before `usb_init()`.
2. **A wrong lead, ruled out.** A forum thread (PJRC thread 61664, "No USB serial with
   Teensy 4.x compiled Optimize Debug") blamed the `SCB_CCR` cache-enable write in
   `configure_cache()` sitting near a cache-line end. Our hung builds did have it at line
   offsets 0x1c and 0x18, against 0x04–0x14 for the working builds. But neither change
   helped: adding `dsb`/`isb` after the write, and aligning it to a line start with
   `.balign 32`, both still hung.
3. **Breadcrumbs.** In a scratchpad copy of the core, progress markers were written at each
   startup step, and the fault handler recorded IPSR and the faulting PC. They were stored at
   `0x2027FF40` (top of OCRAM, just below CrashReport's `0x2027FF80`), which survives reset and
   reflash. Each write was cleaned from the D-cache with `SCB_CACHE_DCCMVAC`. The next PROGRAM
   press flashed a small reader sketch that printed them:
   `stage 3, boots 1, IPSR 3 (HardFault), PC 0x60002694`.
   That PC is the literal-pool `ldr` inside `reset_PFD()`.
4. **Fix confirmed.** The same core copy with `reset_PFD()` changed to `FASTRUN` (so it runs
   from ITCM with its literals beside it, and the first call is after ITCM is loaded) booted
   `-Og` at 528 MHz. It passed the 45 s CAT liveness check with 23 of 23 replies.

## Fix and status

- **Upstream:** PaulStoffregen/cores fixed it in commits `2b87070` (2025-04-27, removes one
  `reset_PFD()` call), `f4d7aa6` ("Be more careful in PLL PFD setup") and `a78bc7c` (both
  2025-07-05). The first release tag containing them is **1.60**.
  - **1.60** sets the dividers directly from constants. It only gates first if you build
    with `GATE_PFD_WHILE_CHANGE`.
  - **1.62** (2026-06-07) drops the gating code altogether.
- **This machine:** upgraded to 1.62 on 2026-11-11. Debug builds now pass the liveness check
  at 528 and 600 MHz ([[build-toolchain]]).
- **On 1.59:** either change `FLASHMEM void reset_PFD()` to `FASTRUN` in the installed core,
  or don't use the Debug setting.
- **Before upgrading elsewhere:** re-run the per-setting liveness checks, as
  [[build-toolchain]] did. The core's startup timing, program-flash clock and PSRAM handling
  changed between 1.59 and 1.62.

## Diagnosing a pre-USB hang again

- Read `journalctl -k` around the upload. "No device at all" (pre-`usb_init`) and "device
  present but silent" (later) point to different parts of the code.
- **Breadcrumb storage:** use OCRAM at `0x2027FF40`, cleaned to memory after each write.
  `SNVS_LPGPR0..3` ignore writes on this board, and `SRC_GPR6/7` accept writes but a reflash
  clears them.
- **The PROGRAM-button trap.** The Teensy Loader app stays open in Auto mode after an upload.
  A PROGRAM press re-flashes the hex it last uploaded, from the same path. If that hex hangs,
  the press just re-flashes the hang, which looks like "PROGRAM needs two presses". Before
  asking for a recovery press, copy a known-good hex over that path. The same trick flashes
  any hex in one press.
- **Testing a core change** without touching the installed core: copy the platform
  (`boards.txt`, `platform.txt`, `cores/teensy4`; symlink `libraries`) into
  `<dir>/hardware/teensytest/avr`. Build with
  `arduino-cli --config-file <yaml whose directories.user is <dir>>`, the FQBN
  `teensytest:avr:teensy41:...` and `--libraries ~/Arduino/libraries`. Upload the hex with
  the normal `teensy:avr` FQBN.
