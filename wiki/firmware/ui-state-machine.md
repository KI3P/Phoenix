---
title: UI State Machine (UISm)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [state-machine, ui, menus, screens, splash, statesmith]
source_refs: []
related: ["[[overview]]", "[[state-machine-architecture]]", "[[display-subsystem]]", "[[front-panel]]", "[[main-loop]]", "[[mode-state-machine]]"]
---

# UI State Machine (UISm)

**Files:** `UISm.cpp` (~33 KB, **generated**), `UISm.h`, source diagram `UISm.drawio`.
StateSmith algorithm *Balanced2*. Do **not** hand-edit the `.cpp` — edit `UISm.drawio` and
regenerate ([[state-machine-architecture]]).

## Role
UISm owns **which screen is shown and how menus navigate**. It decides *what* to display; the
[[display-subsystem]] (`MainBoard_Display*`) owns *how* to draw it, reading global state
read-only. UISm and [[mode-state-machine]] are siblings driven from the same [[main-loop]]
event stream — notably both receive the `CALIBRATE_*` events (UISm shows the cal screen,
ModeSm sequences the cal hardware).

## States (`UISm.h:35-50`)

13 states under `ROOT`:

| State | Screen | Renderer |
|---|---|---|
| `SPLASH` | startup splash (timed) | — |
| `HOME` | main operating screen (spectrum, S-meter, VFO) | `MainBoard_DisplayHome` |
| `MAIN_MENU` | top-level menu | `MainBoard_DisplayMenus` |
| `SECONDARY_MENU` | sub-menu | `MainBoard_DisplayMenus` |
| `UPDATE` | parameter-adjust editor | (per-parameter) |
| `FREQ_ENTRY` | **Direct Frequency Entry (DFE)** | `MainBoard_DisplayDFE` |
| `EQUALIZER` | 14-band TX/RX EQ editor | `MainBoard_DisplayEqualizer` |
| `BIT` | Built-In Test results | — |
| `CALIBRATE_FREQUENCY` | frequency cal screen | `MainBoard_DisplayCalibration_Frequency` |
| `CALIBRATE_POWER` | power cal screen | `…_Power` |
| `CALIBRATE_RX_IQ` | RX I/Q cal screen | `…_RXIQ` |
| `CALIBRATE_TX_IQ` | TX I/Q cal screen | `…_TXIQ` |

> Resolves the [[display-subsystem]] open question: **DFE = Direct Frequency Entry** (the
> `UISm_EventId_DFE` → `FREQ_ENTRY` state, `MainBoard_DisplayDFE`).

## Events (`UISm.h:15-28`) and sources
All dispatched from [[main-loop]] (`UISm_dispatch_event(&uiSM, …)`):

| Event | Source |
|---|---|
| `DO` | 1 ms `TimerInterrupt()` (`Loop.cpp:356`) — splash timeout |
| `MENU` / `SELECT` / `HOME` | button presses in `HandleButtonPress` (`Loop.cpp:419/413/425…`) |
| `DFE` | button (`Loop.cpp:564`) |
| `EQUALIZER`, `BIT`, `CALIBRATE_FREQUENCY/POWER/RX_IQ/TX_IQ` | menu actions (`Loop.cpp:1350-1386`) |

## Transition map (verified from `UISm.cpp`)

```
SPLASH ──[DO: splashCount_ms ≥ splashDuration_ms]──▶ HOME

HOME ──MENU──▶ MAIN_MENU ──SELECT──▶ SECONDARY_MENU ──SELECT──▶ UPDATE ──SELECT──▶ HOME
   │   (HOME event from MAIN_MENU / SECONDARY_MENU / UPDATE returns to HOME)
   │
   ├─DFE───────────▶ FREQ_ENTRY ─┐
   ├─EQUALIZER─────▶ EQUALIZER  ─┤
   ├─BIT───────────▶ BIT        ─┼─ each returns to HOME on the HOME event
   ├─CALIBRATE_FREQUENCY ▶ … ────┤
   ├─CALIBRATE_POWER     ▶ … ────┤
   ├─CALIBRATE_RX_IQ     ▶ … ────┤
   └─CALIBRATE_TX_IQ     ▶ … ────┘
```

Two distinct shapes:
- **Menu drill-down** — a 3-level stack `HOME → MAIN_MENU → SECONDARY_MENU → UPDATE`, each
  level entered by `SELECT`; `UPDATE`'s `SELECT` commits and returns to `HOME`. `HOME` event
  pops straight back to `HOME` from any level.
- **Tool screens** — DFE, EQUALIZER, BIT and the four CALIBRATE screens hang directly off
  `HOME` and are leaf states that only respond to `HOME` (return). They take no part in the
  menu stack.

## State variables (`UISm.h:63-73`)
- `splashDuration_ms` / `splashCount_ms` — splash timing (the `DO` tick counts up to the
  duration, then → HOME).
- `mainMenuSelection` / `mainMenuLength`, `secondMenuSelection` / `secondMenuLength` — menu
  cursor position and item count (the encoder moves the selection; `SELECT` descends).
- `clearScreen` — set true on `UPDATE` entry to force a full redraw.
- `uiUp` — opaque pointer to the current UI context (back-navigation target).

## Interactions
- **Button routing** is state-aware: `HandleButtonPress` switches on `uiSM.state_id`
  (`Loop.cpp:406,1003`) so the same physical button means different things per screen, and is
  ignored entirely while [[mode-state-machine]] is transmitting (`Loop.cpp:395-403`).
- **Calibration** is dual-dispatched: the `CALIBRATE_*` event drives UISm (screen) **and**
  ModeSm (into `CALIBRATION_STATES`, hardware) — see [[mode-state-machine]],
  [[hardware-state-machine]].

## Open questions
- How encoder rotation maps to `mainMenuSelection`/`secondMenuSelection` movement (selection
  is updated outside UISm, in the front-panel/menu code).
- What `uiUp` actually points at and how deep back-navigation works.
- The menu *content* model (where the menu item lists live) vs. UISm's structural states.
