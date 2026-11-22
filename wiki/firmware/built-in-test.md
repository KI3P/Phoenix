---
title: Built-In Test (BIT) — I²C hardware presence
type: module
status: draft
created: 2026-06-09
updated: 2026-11-14
tags: [bit, self-test, i2c, diagnostics, startup, hardware]
source_refs: []
related: ["[[overview]]", "[[rf-board]]", "[[filter-boards]]", "[[front-panel]]", "[[ui-state-machine]]", "[[main-loop]]", "[[persistent-config]]", "[[i2c-bus-map]]", "[[hardware-platform]]"]
---

# Built-In Test (BIT) — I²C hardware presence

Phoenix runs a lightweight **built-in self-test** at startup that probes each expected I²C
device and records whether it responded. It's a *presence/connectivity* check — "is each board
actually there and talking?" — surfaced on a diagnostic screen so a missing ribbon cable or
dead board is obvious.

## What it records — `struct BIT` (`SDT.h:334`)
One global `bit_results` (`Globals.cpp:69`) of booleans, one per I²C subsystem:

| Field | Device |
|---|---|
| `RF_I2C_present` | RF board MCP23017 ([[rf-board]]) |
| `RF_Si5351_present` | Si5351 VFO clock generator |
| `BPF_I2C_present` | BPF board MCP23017 ([[filter-boards]]) |
| `V12_LPF_I2C_present` | LPF board MCP23017 |
| `V12_LPF_AD7991_present` | LPF board AD7991 power ADC |
| `FRONT_PANEL_I2C_present` | front-panel MCP23017(s) ([[front-panel]]) |
| `AD7991_I2C_ADDR` | detected AD7991 address (uint8) |
| `ATU_MCP23017_present` / `ATU_MCP23008_present` | ATU control board expanders, 0x20 and 0x21 on Wire2 ([[atu-board]]); shown only when built with `ATU_PRESENT` |

## How it's populated
Not a separate test pass — each board's **`Initialize*` routine sets its flag** as a side
effect of trying to bring up its I²C device, e.g. `BPFBoard.cpp:30/41` set
`BPF_I2C_present` true/false on success/failure, `LPFBoard.cpp:251/734-748` set the LPF MCP and
AD7991 flags, `FrontPanel.cpp:282-292` the front-panel flag. So BIT reflects the result of the
normal hardware bring-up (the same `ENOI2C` failures the init functions return).

## How it's shown — `DrawBIT()` (`MainBoard_Display.cpp:77`)
The **"I2C Status Report"** screen (the UISm `BIT` state, [[ui-state-machine]],
[[display-subsystem]]) lists each subsystem present/absent, including the detected AD7991
address. Reached via the `BIT` menu action, which dispatches `iBITDISPLAY` from [[main-loop]] →
`UISm_EventId_BIT` (`Loop.cpp:1354`).

## Scope / limits
- It's a **connectivity** test (does the device ACK on I²C), not a functional/parametric test —
  it doesn't verify the Si5351 actually produces the right frequency or that relays switch.
- Runs once at startup; the screen reflects that snapshot.

## Open questions
- Is BIT re-runnable on demand, or only the startup snapshot?
- Could it be extended to functional checks (VFO lock, relay readback, power-sensor sanity)?
  — a possible roadmap item.
