---
title: ATU Automatic Tuning (Phase 2)
type: roadmap
status: draft
created: 2026-11-14
updated: 2026-11-14
tags: [roadmap, atu, antenna-tuner, tuning-algorithm, swr, calibration]
source_refs: []
related: ["[[atu-board]]", "[[atu-control]]", "[[hardware-state-machine]]", "[[swr-bridge-measured-behaviour]]", "[[automatic-power-swr-calibration]]"]
---

# ATU Automatic Tuning (Phase 2)

Phase 2 starts once manual control ([[atu-control]]) has been proven on the bench. The
algorithm comes from §6 of `code/docs/ATU_RF_Board_Characterisation.md`, where it is simulated
in `code/tools/atu_vna/tuning_sim.py`.

## Why it needs a model
- **SWR alone can't identify a setting.** One |Γ| reading puts the antenna on a circle of
  impedances needing entirely different settings, even different K17 positions.
- **Probes recover the missing phase.** Reading |Γ| at a few *known* probe settings, fitted
  against a circuit model of the tuner, recovers the antenna impedance.
- **Select robustly.** Choosing the setting whose worst case is best over slightly perturbed
  models carries over to builds with different hand-wound coils. On 10 m that is essential:
  median SWR 1.09 robust against 1.78 plain best.

## Progress
- **Stage A done (2026-11-14): the model runs in firmware.**
  - `ATUModel.cpp` ports `model.py`. It matches the Python to 3×10⁻⁷ in Γ on 432 golden cases
    (`test/atu_golden.h`).
  - Parameters come from `fits.json` through `tools/atu_vna/export_c.py`.
  - On the Teensy (600 MHz, `AX1;`): precompute 4.9 ms, full 131,072-setting search
    17.8–18.7 ms, robust selection 0.02–0.14 ms. That is well below the report's "tens of ms"
    estimate. The engine will run the search in chunks of about 6 ms per loop pass.
  - Cross-check: for 30+j40 Ω at 7.1 MHz the firmware picks LC/15/32, the setting the report
    gives (SWR 1.17).
- **Stage B done (2026-11-14): the tune engine keys and reads.**
  - ModeSm has new states `ATU_TUNE_SPACE` / `ATU_TUNE_MARK` in an `ATU_TUNE_STATES` composite
    (`ModeSm.drawio`, regenerated with `ss.cli run -h -b --no-ask`; that reproduced the old
    output exactly before the edit).
    - Entry is from SSB or CW receive only. Digital is excluded because its enter and exit
      actions change the sample rate.
    - They return to the receive state they came from.
    - They use their own carrier events (`ATU_CARRIER_ON` / `OFF`), so an operator's PTT can't
      key them.
  - `ATUTune.cpp` runs from `loop()`. Each reading: relays (carrier off) → 15 ms → carrier → 25 ms
    → collect at least 60 ms and 5 bridge samples → carrier off → |Γ| with the band's
    calibration. The T/R relay stays in TX for the whole program.
  - Any front-panel action, CAT `TX;`/`RX;` or `AX0;` aborts. BR and the power calibration are
    locked out while it runs.
  - **Bench, dummy load, 14.2 MHz, 2 W:** the READ diagnostic (`AX2;`) took 6 readings in
    0.69 s. Repeatability ±0.001 in |Γ|; bypass |Γ| 0.047, against 0.04 on the VNA.
  - **Bridge against model** (dummy load):

    | Setting | Bridge \|Γ\| | Model \|Γ\| |
    |---|---|---|
    | empty LC | 0.101 | 0.031 |
    | L3 | 0.196 | 0.153 |
    | C4 | 0.322 | 0.259 |
    | L5 | 0.699 | 0.695 |
    | C7 | 0.968 | 0.959 |

    Above 0.6 they agree within 0.02. Near a match the bridge reads up to about 0.07 high. That
    fits the bridge leak (about −28 dB on 20 m) plus a load that isn't exactly 50 Ω through the
    output coax and TR switch, both about 0.04, adding as vectors. The tuning algorithm absorbs
    the load error into its impedance estimate. It has to tolerate about ±0.04 from the bridge
    leak near a match.

## Proposed algorithm (§6.5)
memory → bypass check (SWR ≤ 1.3 stays bypassed) → 4 probes (empty LC, one L ≈ 50 Ω, one C ≈
1/50 S, both in CL) → estimate Z_ant by least squares → robust jump (top 300 by model, 6 models
with coils ±2 %) → accept if SWR ≤ 1.5 or within 0.05 of prediction, else re-estimate (up to 4
jumps) → optional hill-climb polish → store. A median of five readings in simulation.

## Firmware plan (outline)
- **State machine:** `ATU.cpp` in the [[hardware-state-machine]] style (`HandleATUState`). It
  does one relay change or one SWR reading per loop pass, to keep the 10 ms budget
  ([[real-time-constraints]]).
- **Relays switch with the carrier off.** Hook in before `SelectTXMode()` in the `RFTransmit`
  and CW-mark paths of `HandleRFHardwareStateChange()`.
- **Band and frequency changes:** a hook next to `SelectLPFBand` in `HandleTuneState()`. Cache
  it, because it fires on every tune step. It applies the stored setting or bypasses.
- **SWR source:** the LPF board's AD7991 bridge, with the crosstalk fix and calibration (see
  [[swr-bridge-measured-behaviour]], `code/docs/swr_calibration_approach.md`).
- **Model in firmware:**
  - port `fits.json` to C tables and precompute per frequency (about 10 kB)
  - **benchmark the 131,072-setting search on the Teensy first**: estimated tens of ms, not measured
  - fallback if too slow: analytic L-network plus a local search
- **Per-build inductor calibration (§6.6):** use a dummy load, low power, one coil at a time.
  Use the band that puts each coil at 50–100 Ω.
- **Memory:** per band and antenna, stored in `ED`/LittleFS ([[persistent-config]]).
- **Controls:**
  - a Tune action in the ATU menu
  - Kenwood `AC P1P2P3;` (RX/TX AT in or through, start/stop tuning) for logging software
  - the raw `AT` command stays for testing
- **Bench trial:** known complex loads on ANT, checked with the VNA.

## Open questions
- Is a 4-probe estimate robust against the bridge's *systematic* error? The simulation
  modelled random error only.
- Do the layout parasitics hold on a second board? Only the coils were varied between
  simulated builds.
- Power handling and contact life at 100 W are untested.
