---
title: Persistent Configuration (ED struct, Storage, ParamSave)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [config, ed-struct, littlefs, sdcard, json, persistence, globals, bands]
source_refs: []
related: ["[[overview]]", "[[main-loop]]", "[[tune-frequency-control]]", "[[hardware-state-machine]]", "[[iq-imbalance-correction]]", "[[dsp-chain]]"]
---

# Persistent Configuration

How the radio's operating state and calibration survive power cycles. The data backbone every
other module reads from.

## `ED` — the single source of truth

`ED` is one global instance of `struct config_t` (`SDT.h:264-331`). It holds **all runtime
operating configuration and calibration**, with sensible inline defaults. Field groups:

| Group | Fields (selected) |
|---|---|
| Audio / DSP | `agc`, `audioVolume`, `rfGainAllBands_dB`, `nrOptionSelect`, `ANR_notchOn`, `currentMicGain` |
| Tuning | `activeVFO`, `currentBand[2]`, `centerFreq_Hz[2]`, `fineTuneFreq_Hz[2]`, `stepFineTune`, `freqIncrement`, `freqCorrectionFactor`, `lastFrequencies[band][3]` |
| Modulation/CW | `modulation[2]`, `keyType`, `currentWPM`, `keyerFlip`, `CWToneIndex`, `CWFilterIndex`, `sidetoneVolume`, `decoderFlag` |
| Spectrum/display | `spectrum_zoom`, `spectrumScale`, `spectrumNoiseFloor[band]`, `spectrumFloorAuto`, `dbm_calibration[band]` (S-meter) |
| Equalizer | `equalizerRec[14]`, `equalizerXmt[14]` |
| Power / PA | `powerOutCW[band]`, `powerOutSSB[band]`, `PA100Wactive`, `PowerCal_20W/100W_Psat_mW[band]`, `…_kindex[band]`, `…_threshold_W`, `…_DSP_Gain_correction_dB` |
| Calibration | `IQAmp/PhaseCorrectionFactor[band]` (RX), `IQXAmp/XPhaseCorrectionFactor[band]` (TX), `DCOffsetI/Q[band]` (carrier null), `XAttenCW[band]`, `RAtten[band]`, `SWR_F/R_SlopeAdj/Offset[band]` |
| Routing | `antennaSelection[band]` |

Many fields are **per-band arrays** (`NUMBER_OF_BANDS = 13`). The calibration arrays are
written by the routines in [[hardware-state-machine]] / [[iq-imbalance-correction]] and the
power-cal tanh fit in [[tune-frequency-control]]; the tuning fields by
[[tune-frequency-control]]; the DSP fields by [[dsp-chain]].

> **Naming gotcha:** `ED` stands for "EEPROM Data" and the `.ino` says settings are saved to
> "EEPROM" — but the Teensy 4.1 has no true EEPROM and the implementation actually uses
> **LittleFS + SD card JSON** (below). The name is historical. Logged in [[documentation-todos]].

## Supporting definitions
- **`Config.h`** — compile-time constants: pin assignments, `NUMBER_OF_BANDS`, default WPM /
  band / frequencies, feature flags. (Compile-time config vs. `ED`'s runtime config.)
- **`Globals.cpp`** — definitions of everything declared `extern` in `SDT.h`: `ED` itself,
  `SampleRate`, the `hardwareRegister`, the state-machine instances (`modeSM`, `uiSM`, …), the
  `SR[]` sample-rate table, and the `bands[]` table. Also `ComputeDitDuration` etc.
- **`bands[]`** — per-band static data (edges `fBandLow/High_Hz`, filter cuts
  `FLoCut/FHiCut_Hz`, type `HAM_BAND`/etc., name). Read constantly by tuning, filtering, and
  the TX interlock (`IsTxAllowed`).

## Persistence — `Storage.cpp` (LittleFS + SD, JSON)

Not a raw struct dump — `ED` is serialized **field-by-field as JSON** via **ArduinoJson**
(`doc["agc"] = ED.agc; …`, `Storage.cpp:56+`), which is robust to struct-layout changes (fields
are matched by name, not offset). Backends:
- **`LittleFS_Program myfs`** — ~1 MB of the Teensy **program flash** (primary store).
- **`SD` card** (`BUILTIN_SDCARD`) — optional secondary/portable copy.

Public API (`Storage.h`):
- `InitializeStorage()` — mount LittleFS + (if present) SD.
- `SaveDataToStorage(bool savetosd)` — serialize `ED` → LittleFS, and to SD when `savetosd`.
- `RestoreDataFromStorage()` / `RestoreDataFromSDCard()` — deserialize back into `ED`.
- `PrintEDToSerial()` — dump for debugging.

`ED` is restored at startup and saved on graceful shutdown (`ShutdownTeensy()` in
[[main-loop]]) and on demand (e.g. after calibration).

## `ParamSave.cpp` — calibration scratch (not the main store)

A small **RAM** backup area, distinct from `Storage`: 5 variable slots + 3 array slots
(≤256 bytes each, `ParamSave.h:26-28`). `SaveVariable`/`RestoreVariable`,
`SaveArray`/`RestoreArray`. Used to **stash an `ED` field before twiddling it during
calibration** so it can be rolled back if the user cancels — a transient undo buffer, not
persistent storage.

## Built-In Test
`struct BIT` (`SDT.h:334`) holds built-in-test results, surfaced by the `iBITDISPLAY` event
([[main-loop]]) and the UISm `BIT` screen ([[ui-state-machine]]).

## Open questions
- Full JSON schema vs. struct (is every `ED` field persisted, or a subset?).
- Versioning/migration: what happens when the JSON on flash predates a new `ED` field
  (ArduinoJson leaves it at the default — confirm and document).
- LittleFS-vs-SD precedence on restore, and corruption/again handling.
- Rename note: should `ED`/"EEPROM" terminology be updated in source to reflect LittleFS.
