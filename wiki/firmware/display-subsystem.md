---
title: Display Subsystem (RA8875, panes, menus, calibration screens)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [display, ra8875, ui, spectrum, waterfall, menus, panes, stale-flags]
source_refs: []
related: ["[[overview]]", "[[ui-state-machine]]", "[[dsp-chain]]", "[[zoom-fft]]", "[[hardware-state-machine]]", "[[persistent-config]]", "[[spectrum-refresh-floor]]", "[[rapid-tune-mute-freeze]]"]
---

# Display Subsystem (RA8875)

The largest UI area by code volume. Renders to an **RA8875**-driven TFT (800×480). **Read-only**
w.r.t. global state by design — it draws what other modules compute, never mutating `ED` or DSP
state.

## Files
- `MainBoard_Display.cpp` / `.h` — core: the `Pane` model, `DrawDisplay()` router, the
  `VariableParameter` editor, menu structures
- `MainBoard_DisplayHome.cpp` (~66 KB) — the home screen and its panes
- `MainBoard_DisplayMenus.cpp` — main/secondary menu rendering
- `MainBoard_DisplayDFE.cpp` — Direct Frequency Entry pad ([[ui-state-machine]])
- `MainBoard_DisplayEqualizer.cpp` — 14-band TX/RX equalizer screen ([[audio-equalizer]])
- Calibration screens: `MainBoard_DisplayCalibration_Frequency/Power/RXIQ/TXIQ.cpp`
- Fonts: `FreeSansBold18pt7b.h`, `FreeSansBold24pt7b.h`

## The pane model — dirty-flag rendering

The efficiency mechanism that keeps drawing inside the ~10 ms loop budget
([[real-time-constraints]]). Each screen region is a `Pane` (`MainBoard_Display.h:32`):
```c
struct Pane { int x,y,w,h; void (*DrawFunction)(void); bool stale; };
```
A pane redraws **only when its `stale` flag is set**; modules flip `stale = true` when the
underlying data changes (e.g. `PaneVFOA.stale = 1` on a VFO switch). The home screen iterates
its `WindowPanes[NUMBER_OF_PANES]` array and redraws just the dirty panes, then clears the flags.

### The home screen panes (`MainBoard_DisplayHome.cpp:80-92`)
13 panes (the `.ino`'s "12-pane layout" is approximate):

| Pane | Rect (x,y,w,h) | Shows |
|---|---|---|
| VFO A | 5,5,280,50 | active VFO A frequency |
| VFO B | 300,5,220,40 | VFO B frequency |
| Freq/Band/Mod | 5,60,310,30 | band + modulation |
| **Spectrum** | 5,95,520,345 | main spectrum + waterfall |
| State of Health | 5,445,260,30 | status indicators |
| Time | 270,445,260,30 | RTC clock |
| SWR | 535,15,150,40 | SWR (TX) |
| TX/RX Status | 710,20,60,30 | T/R indicator |
| S-meter | 515,60,260,50 | signal strength |
| Audio Spectrum | 535,115,260,150 | demod audio spectrum |
| Settings | 535,270,260,170 | live settings summary |
| Name Badge | 535,445,260,30 | callsign/name |
| SAM Offset | 320,60,180,30 | synchronous-AM carrier offset ([[synchronous-am-detection]]) |

## Screen routing — `DrawDisplay()` (`MainBoard_Display.cpp:244`)

The top-level render dispatch is a `switch` on **`uiSM.state_id`** — i.e. [[ui-state-machine]]
chooses the screen, the display draws it:

```
SPLASH→DrawSplash · HOME→DrawHome · MAIN_MENU→DrawMainMenu · SECONDARY_MENU→DrawSecondaryMenu
UPDATE→DrawHome + DrawParameter (variable edit overlay)
EQUALIZER→DrawEqualizerAdjustment · FREQ_ENTRY→DrawFrequencyEntryPad · BIT→DrawBIT
CALIBRATE_{FREQUENCY,RX_IQ,TX_IQ,POWER}→DrawCalibrate*
```
Each non-home screen defines **its own pane set** (e.g. the frequency-cal screen has freq-plot /
factor / error / instructions panes). So "panes" is a per-screen concept; the table above is the
home screen specifically.

## Spectrum / waterfall
The main spectrum pane is fed by the **zoom FFT** display path ([[zoom-fft]], `psdnew` → pixel
columns); the audio-spectrum pane by the channel-filter FFT in [[dsp-chain]] (`audioYPixel`).
Refresh period is `≈ NCHUNKS × T_loop` — see [[spectrum-refresh-floor]] (governed by `NCHUNKS`
and per-frame draw cost, not `SPECTRUM_REFRESH_MS`; shipped at 18.8 fps) and
[[real-time-constraints]]. `DrawBandWidthIndicatorBar()` draws the blue filter-bandwidth bar +
cyan tuned-frequency marker over the trace; the bar-stamping math is factored into
`StampTuningBar()` so it can be reused. While a tune encoder is spun fast,
[[rapid-tune-mute-freeze]] hijacks `DrawSpectrumPane()` to freeze the sweep (and, for Fine Tune,
keep only the bar moving via a cheap L2→L1 blit of a held bar-less backdrop).

## Parameter editing & menus
- **`VariableParameter`** (`MainBoard_Display.h:103`) — a type-safe editor (union over i8/i16/
  i32/i64/f32/bool/KeyTypeId with per-type min/max/step). Drives the `UPDATE` state: the encoder
  adjusts a bounded `ED` field, `DrawParameter()` overlays the value on the home screen.
- **Menus** — `PrimaryMenuOption primaryMenu[8]` and nested `SecondaryMenuOption`s. A secondary
  option is either a `variableOption` (→ UPDATE editor) or a function pointer (→ runs an action,
  e.g. start a calibration). This is the data model behind the [[ui-state-machine]] menu stack.

## Open questions
- Waterfall scroll/colour-map implementation and how `psdnew` bins map to pixel columns.
- Touch input: does the RA8875 touchscreen drive any UI, or is navigation encoder/button-only?
- The full `primaryMenu[8]` contents (top-level menu items).
