---
title: Display Subsystem (RA8875, panes, menus, calibration screens)
type: module
status: draft
created: 2026-06-08
updated: 2026-07-29
tags: [display, ra8875, ui, spectrum, waterfall, menus, panes, stale-flags, sample-rate]
source_refs: []
related: ["[[overview]]", "[[ui-state-machine]]", "[[dsp-chain]]", "[[zoom-fft]]", "[[hardware-state-machine]]", "[[persistent-config]]", "[[spectrum-refresh-floor]]", "[[rapid-tune-mute-freeze]]", "[[sample-rate-switching]]", "[[cat-control]]"]
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

### The filter bar draws on the *effective* passband
`EffectivePassbandEdges_Hz()` (`MainBoard_DisplayHome.cpp:457`) exists because the stored
`FLoCut_Hz`/`FHiCut_Hz` follow the sign convention of the **band's default mode**
(`bands[].mode`), which is not necessarily the current `ED.modulation`: the front-panel
DEMODULATION toggle changes the modulation without converting the stored cuts. `InitFilterMask()`
applies the same normalization, so display code that positions the markers must too — otherwise
a USB-over-an-LSB-band bar draws on the **wrong sideband**, and an AM-over-SSB marker lands on
the wrong edge. The AM/SAM case is made symmetric about the carrier. This is the display-side
counterpart of the `MD` CAT bug described in [[cat-control]]: same normalization rule, two
places that had to learn it.

### Rate-dependent axes
Three things that used to assume 192 ksps now derive from `SR[SampleRate].rate`
([[sample-rate-switching]]):
- **Spectrum frequency ticks** are positioned by `FreqToBin()` at round frequencies
  (`:550-571`) rather than from a fixed pixel table with a non-round step, so labels stay
  aligned with the trace at any rate; the clear strip was widened so old ticks are wiped on
  zoom/tune changes.
- **Audio-spectrum span** is `Fs/32` (`:1301-1303`) — 6.0 kHz at 192 ksps, 5.5 kHz at 176.4 ksps
  — not a hard-coded 0–6000 Hz.
- **Settings pane** gained a `"Rate:"` row (`:1654`); Key Type moved down one.

## Parameter editing & menus
- **`VariableParameter`** (`MainBoard_Display.h:103`) — a type-safe editor (union over i8/i16/
  i32/i64/f32/bool/KeyTypeId with per-type min/max/step). Drives the `UPDATE` state: the encoder
  adjusts a bounded `ED` field, `DrawParameter()` overlays the value on the home screen.
- **Menus** — `PrimaryMenuOption primaryMenu[9]` (`MainBoard_DisplayMenus.cpp:602`,
  `MainBoard_Display.h:371`) and nested `SecondaryMenuOption`s. A secondary option is either a
  `variableOption` (→ UPDATE editor) or a function pointer (→ runs an action, e.g. start a
  calibration). This is the data model behind the [[ui-state-machine]] menu stack. It grew
  8 → 9 with the **"Sample Rate"** menu (`:580-611`, [[sample-rate-switching]]).
- A secondary option may also carry a **`postUpdateFunc`**, run after the value changes — e.g.
  "Sidetone volume" calls `UpdateSidetoneOscillator` so the change is applied to the running
  oscillator rather than only stored.

## Open questions
- Waterfall scroll/colour-map implementation and how `psdnew` bins map to pixel columns.
- Touch input: does the RA8875 touchscreen drive any UI, or is navigation encoder/button-only?
  *(Partly answered: AGC mode is touchscreen-only — see [[filter-hil-test]], which cannot set
  it over CAT.)*
- The full `primaryMenu[9]` contents (top-level menu items).
