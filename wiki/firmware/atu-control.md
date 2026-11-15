---
title: ATU Control (manual, Phase 1)
type: module
status: draft
created: 2026-11-14
updated: 2026-11-15
tags: [firmware, atu, antenna-tuner, cat, menu, config-flag]
source_refs: []
related: ["[[atu-board]]", "[[atu-auto-tuning]]", "[[cat-control]]", "[[built-in-test]]", "[[hardware-state-machine]]", "[[display-subsystem]]"]
---

# ATU Control (manual, Phase 1)

Phase 1 of ATU support gives **manual control only**. You set the relays from a menu or over
CAT, key up, and read the SWR. Nothing switches the tuner automatically; that is
[[atu-auto-tuning]]. The hardware is described in [[atu-board]].

**Verified on hardware 2026-11-14.** With the ATU installed in the radio and `ATU_PRESENT`
enabled, both expanders were found at boot, `AT;` read the bypassed boot state with readback
equal to the register, and every menu item switched its relays as expected. `atu_cat.py`
then walked K1–K20 one at a time, plus full masks and a combined LC/CL setting: 49 writes,
readback equal to the register on every one. The readback also confirms the real MCP23017
returns GPIOA in the low byte of `readGPIOAB()`, so the test mock is the one that is wrong.
With a dummy load on the KI3P prototype, keying with `TX;` made `AT` writes return `?;`, and the
relay state held through RX→TX→RX and a band change up and back.

## Build flag
- **`Config.h`:** `//#define ATU_PRESENT`, commented out by default. Uncomment it when the ATU
  control board is on the BANDS connector.
- **What it compiles in:**
  - the driver body (`ATUBoard.cpp`)
  - the init call in `InitializeRFHardware()`
  - two BIT-screen rows
  - the ATU menu and its decoding in the register print
  - a working `AT` CAT command
- **With it off:** `AT` replies `?;` and the firmware is otherwise unchanged.
- **Declarations:** `ATUBoard.h` declares the API unconditionally, so a call left unguarded
  fails at link time rather than silently doing nothing.

## Driver: `ATUBoard.cpp`
- **Pattern:** follows the `LPFBoard` conventions: state kept in `hardwareRegister`, `SET_*`
  macros with 64-bit masks, and expanders written only when a port's value changes.
- **API:**
  - `InitializeATUBoard()`
  - `SetATUInductors` / `SetATUCapacitors` / `SetATULC`
  - `SetATUState(network, balun, capsAtInput, L, C)`, which sets everything with one update
  - `ATUEngageNetwork` / `ATUBypassNetwork`
  - `ATUSelectCapsAtInput` / `…AtOutput`
  - `ATUSelectBalancedOutput` / `…Unbalanced`
  - `SetATURelay(k)` for K1–K20
  - `ResetATUBoard()` (safe state)
  - `GetATUMCPRegisters()` (I²C readback)
- **Boot:** the tuner starts **bypassed**. `InitializeATUBoard()` runs from
  `InitializeRFHardware()`. A missing tuner is not added to that function's error sum; it is
  reported on the [[built-in-test]] screen as two rows (MCP23017 0x20, MCP23008 0x21).
- **TX interlock:** `ATUChangeAllowed()` is true only in `RFReceive`. The menu and CAT paths
  check it, because relays must not switch with RF present. The low-level setters do not
  check it, so tests and the future tuning state machine can drive them. The relay state
  persists through RX↔TX.
- **Standalone copy:** `code/src/BoardTest/TestATUBoard` keeps a copy of the driver for
  bench-testing without Phoenix. The PhoenixSketch copy is the master.

## Menu: Main menu → ATU (last entry)
Simplified on 2026-11-15 from eleven bench items. Everything acts on the antenna selected for
the current band (RF Options → Antenna).

| Item | Effect |
|---|---|
| **Bypass ATU** / **Enable ATU** | bypass the ATU on this antenna, or enable it again (`ED.ATU_Enabled[ant]`, saved); the label says what selecting it will do |
| **Tuning status table** | full-screen page (UI state `ATU_STATUS`): this antenna's output and enable state, then one row per amateur band with the stored frequency, setting (topology and nominal µH/pF, or bypass) and SWR, or `--`; the current band in yellow; HOME returns |
| **Clear memory, ant N** | forget every band's setting for this antenna. Press twice: the first press re-opens the menu on the item, now reading "Press again to clear"; moving off it or waiting 10 s cancels. The home screen then shows `MEM` / `CLEARED`. |
| **Tune** | retune, as button 16 |
| **Tune power (W)** | 0.5–10 W, default 2 |
| **Output: coax** / **Output: balun** | which ATU output this antenna is on (`ED.ATU_BalunOut[ant]`, K18, saved) |

- **Toggles:** they are function items with changing labels (`ATUMenuRefreshLabels()`),
  because bool items display "true"/"false". A function item closes the menu when selected.
- **Not while transmitting:** the toggles are refused while keyed.
- **Manual relay control** is no longer in the menu. It stays on CAT `AT` for bench work;
  forgetting a single band is `AX5;`.
- **Sizing:** `NUM_PRIMARY_MENUS` in `MainBoard_Display.h` is 9 or 10 depending on the flag;
  `ATU_MENU_ITEMS` is 6. A mismatch with the table is a compile error. Tests rely on RF Options
  staying at index 0 and Calibration at index 6.
- **Labels:** keep them to about 20 characters. "Clear memory, antenna 0" overflowed the menu
  outline on the radio.

## Per-antenna settings
- **Bypass / enable** (`ED.ATU_Enabled[ant]`, default enabled): a bypassed antenna stays
  bypassed across band changes, and its memory is kept. A retune enables it again.
- **Output** (`ED.ATU_BalunOut[ant]`, default coax): K18 sits after the bypass relays, so it
  belongs to the antenna. It is kept when bypassed, in every stored setting
  (`ATUSettingFor()`), and in every setting the tuner reads. Before 2026-11-15 the tuner
  always forced coax.
- **Memory:** one entry per band and antenna (13 × 4).

## Automatic tuning and memory (Phase 2)
See [[atu-auto-tuning]] for the algorithm and the bench results. In brief:
- **Selecting a band or antenna** puts the setting stored for that band and antenna on the
  relays, in receive only. With nothing stored, or with the ATU bypassed on the antenna, the
  network is bypassed. Settings are kept per band *and* antenna (13 × 4), so a setting tuned
  on one antenna is never used on another.
- **Tuning is always a retune:** a fresh search from bypass at **Tune power (W)** (default
  2 W), whose result is stored for the band and antenna. It does not re-read the stored
  setting first: that is already on the relays, and asking for a tune means it no longer
  suits. A tune takes about 30–75 readings (3–6 s at 99 ms each).
- **Starting a tune:**
  - **Front-panel button 16** (`ATU_TUNE_BUTTON`, formerly `SPARE`) on the home screen.
    Pressing any button while it runs stops it and restores the previous setting.
  - **Menu:** ATU → **Tune**.
  - **CAT:** `AC001;` (Kenwood) or `AX3;`.
  - A retune enables the ATU on the antenna if it was bypassed.
- **Refusals:** the tuner only starts from SSB or CW receive, on a band where transmit is
  allowed and the SWR bridge is calibrated.
- **Search:** the tuner is a measured search with no model or calibration
  (`ATUTuneAlgo.cpp`, branch `ATU_search`).

### Home screen
With the board detected, two lines sit to the right of SWR and PWR (`DrawSWRPane`, x0 + 88;
the pane is 170 px wide). `ATUStatusText()` in `ATUTune.cpp` builds them:

| State | Shows | Colour |
|---|---|---|
| Network bypassed | `ATU BYP`, or `ATU OFF` if the ATU is bypassed on this antenna; line 2 `BALUN` on the balun output | white |
| 3 s after clearing an antenna's memory | `MEM` / `CLEARED` | white |
| Network in line | topology and **nominal** values: `CL 0.82uH` / `157pF` | white |
| Tuning | `TUNING` / `n 23` (readings so far) | yellow |
| 3 s after a refusal or failure | `TUNE` / `MODE`, `NO TX`, `NO CAL`, `BUSY`, `NO RF`, `ANT FAULT`, `TIMEOUT` … | red |
| 3 s after a poor match (`ATU_FAIL_NO_MATCH`) | the setting kept | yellow |

- **Nominal values:** the design values summed by `ATUNominalInductance_uH()` /
  `ATUNominalCapacitance_pF()` (`ATUBoard.cpp`): L1–L8 = 0.05 … 8.8 µH, C1–C8 = 10 … 2000 pF.
  A hand-wound build differs by 10–15 %, so they are a guide, not a measurement.

### CAT: `AC` (Kenwood TS-480)
| Form | Meaning |
|---|---|
| `AC;` | read → `AC0<P2><P3>;`: P2 = 1 if the ATU is enabled on this antenna, P3 = 1 while a tuner program runs |
| `ACp1p2p3;` with P3 = 1 | start a retune (as button 16; enables the ATU); `?;` if refused |
| `ACp1p2p3;` with P3 = 0 | stop a running tune, then P2 = 0 bypasses the ATU on this antenna, P2 = 1 enables it (the stored setting goes on the relays), as the menu's Bypass / Enable ATU |

- **P1** (RX-AT) is ignored.
- **Errors:** `?;` when a relay change isn't allowed (transmitting) or for anything other than
  0/1.
- **Without `ATU_PRESENT`:** `AC;` reads `AC000;` and settings are accepted and ignored, like a
  radio with no tuner.

### CAT diagnostics (`AX` and `AY`, not Kenwood)

  | Command | Effect |
  |---|---|
  | `AX3;` | tune |
  | `AX0;` | abort |
  | `AX;` | result (and the first 24 \|Γ\| readings) |
  | `AYpp;` | readings page pp (two digits), 12 a page: `AYpp,<n>,<nbsLLLCCC>:<g×1000>,…;` |
  | `AX2;` | READ diagnostic (bypass ×3, then the present setting ×3) |
  | `AX5;` | forget the memory entry |
- **Superseded:** the model-based tuner on branch `ATU_support` also had `AX1` (model
  benchmark), `AX4` (settings of the first 24 readings) and `AX6`–`AX9` (coil and bridge-scale
  calibration). The search tuner needs none of them.

## CAT: `AT` (not a Kenwood command)
| Form | Meaning |
|---|---|
| `ATnbsLLLCCC;` | set: n = network in line, b = balun, s = caps at input (0/1); LLL/CCC = masks 000–255. Writes the expanders once. `AT000000000;` = safe state |
| `AT;` | read → `ATnbsLLLCCC,RRRRR,HHHHHH,p;`: RRRRR = register field (hex, bit 0 = L1 … bit 18 = network); HHHHHH = I²C readback (bits 0–7 L, 8–15 C, 16–23 mode); p = both expanders present |

- **Errors:** `?;` for malformed input, a mask above 255, while transmitting, or without
  `ATU_PRESENT`.
- **Readback in the reply:** it goes in the reply, not through `Debug()`, because `Debug()` is
  silent in USB-audio builds (see [[cat-control]]).
- **Automatic tuner:** Kenwood's own tuner command, `AC`, drives it (above).
- **Script:** `code/tools/atu_vna/atu_cat.py` takes the same command vocabulary as `atu.py`
  (`ALL0 IN "LM 32" "CM 16"` …). It applies each command with one `AT` write and checks
  readback = register.

## Tests
- **Target:** `all_ATU_tests` (`code/test/ATUBoard_test.cpp`) is the only target built with
  `ATU_PRESENT`. It covers the driver, `AT`, the menu and the TX interlock.
- **Mock:** `test/Adafruit_MCP23X08.h`.
- **Known mock issue:** the `Adafruit_MCP23X17` mock returns GPIOA in the *high* byte of
  `readGPIOAB()`. The real library returns it in the low byte, which `FrontPanel.cpp` relies
  on. `CheckThatHardwareRegisterMatchesActualHardware()` in `Radio_test.cpp` and
  `Calibration_test.cpp` encodes the mock's order. The ATU tests avoid depending on it. Listed
  in [[documentation-todos]].
