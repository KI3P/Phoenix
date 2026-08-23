---
title: "Dev: Manual Notch Filter (#11)"
type: roadmap
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [development, feature, notch, noise-reduction, lms, display, front-panel]
source_refs: []
related: ["[[development-backlog]]", "[[noise-reduction]]", "[[display-subsystem]]", "[[front-panel]]", "[[main-loop]]", "[[persistent-config]]"]
---

# Dev: Manual Notch Filter (#11)

**GitHub issue:** [KI3P/Phoenix#11](https://github.com/KI3P/Phoenix/issues/11) — *"Feature
request: manual notch filters"*

> Goal (from the issue): add a **manual** notch as an alternative to the autonotch. The notch
> button cycles three states — **no notch / autonotch / manual notch**. In manual mode a line
> on the audio spectrum shows the notch location, moved by turning the **filter** knob.

Scoping note — what exists vs. what's missing. Not a solution design.

## Current state — auto-notch scaffolding exists, manual mode does not

- **Engine:** the notch is the `Xanr` LMS adaptive filter ([[noise-reduction]]). Its signature
  `Xanr(data, ANR_notch)` already *names* a manual mode (`0=off, 1=auto, 2=manual` per the
  `DSP_Noise.h` doc), but **no `mode 2` / manual code path is implemented** (grep finds none).
- **State flag:** `ED.ANR_notchOn` ([[persistent-config]]) is currently **binary** — the notch
  button toggles it 0↔1 (`Loop.cpp:545-548`), and DSP only acts on `== 1`
  (`DSP.cpp:930` → `Xanr(&data, 1)`).
- **Display hook:** the home-screen settings pane already reflects `ANR_notchOn`
  (`MainBoard_DisplayHome.cpp:1404-1413`).
- **CAT:** `ANR_notchOn` is also settable over CAT (`CAT.cpp:600`, the `NT` command,
  [[cat-control]]).

So the plumbing (button, flag, display indicator, DSP entry point, CAT) is in place for
auto-notch; **manual notch is genuinely new work.**

## What remains to be done (to scope)
- **3-way state:** extend `ANR_notchOn` / the notch-button handler from 0↔1 to a 3-state cycle
  (off / auto / manual). Note this changes a persisted `ED` field's meaning — mind migration
  ([[persistent-config]]).
- **Manual DSP path:** implement the `Xanr` manual mode (a fixed-frequency notch) — the engine
  hook exists but the behaviour does not.
- **Notch-frequency control:** bind the **filter encoder** ([[front-panel]]) to a notch-freq
  parameter while in manual mode (it currently adjusts filter bandwidth — needs mode-aware
  routing).
- **Display:** draw a vertical line at the notch frequency on the **audio-spectrum pane**
  ([[display-subsystem]]); keep it in sync as the knob moves.
- **State storage:** where the manual notch frequency lives (new `ED` field?).
- **Interaction:** how manual-notch mode coexists with the filter knob's normal job, and with
  the other NR options.

## Reference
Russ's example cited in the issue:
<https://groups.io/g/SoftwareControlledHamRadio/message/35544>

## Relevant code & wiki
`DSP_Noise.cpp` (`Xanr`), `DSP.cpp:930`, `Loop.cpp:545`, `MainBoard_DisplayHome.cpp`. Wiki:
[[noise-reduction]], [[front-panel]], [[display-subsystem]], [[persistent-config]].
