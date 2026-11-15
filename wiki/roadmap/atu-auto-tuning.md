---
title: ATU Automatic Tuning (Phase 2)
type: roadmap
status: draft
created: 2026-11-14
updated: 2026-11-15
tags: [roadmap, atu, antenna-tuner, tuning-algorithm, swr, calibration, search]
source_refs: []
related: ["[[atu-board]]", "[[atu-control]]", "[[hardware-state-machine]]", "[[swr-bridge-measured-behaviour]]", "[[automatic-power-swr-calibration]]"]
---

# ATU Automatic Tuning (Phase 2)

Phase 2 starts once manual control ([[atu-control]]) has been proven on the bench.

**The tuner is now a measured search with no model or calibration** (branch `ATU_search`; see
the next section). The first approach, from §6 of `code/docs/ATU_RF_Board_Characterisation.md`,
fitted |Γ| readings to a circuit model of the board. It is kept below as history; its code
stays on branch `ATU_support`.

## Change of approach (2026-11-15): a measured search
**Why.** The model-based tuner needed a VNA fit of one board plus a per-band bridge calibration
before it worked, and the bench showed where that breaks:
- the CL model is wrong above 15 MHz (15 m into 200 Ω stopped at SWR 5.1)
- it reported a coverage gap on 20 m that turned out not to exist (the search reaches 1.25)
- elements past resonance sent jumps to settings that read 0.995
- the bridge |Γ| error is a mix of scale and leak, and calibrating it out depended on the
  model (stage E)

Other builders' hand-wound coils make a per-board characterisation unshippable. Tuners of this
kind use only forward and reflected power and search by measurement; the bridge then only has
to *rank* settings near the match, and its leak only sets a floor.

**Branches.** `ATU_search` starts from stage B (`f41cb04`):

| Commit | What |
|---|---|
| `eb9fe1b` | the model leaves the firmware; it stays as the host-test plant (`code/test/atu_plant/`) |
| `a2f0676` | stage D memory, cherry-picked |
| `421815e` | the search planner, `AY` paging, host simulation |
| `abe7327` | refine LC at once when its valley already reaches the target |
| `114bbd5` | readings shortened from 114 to 99 ms |

`ATU_support` keeps stages A, C and E (`4073db3`, `a41b311`, `8ff4d8a`, `f577329`, `7e78214`,
`8d8b150`).

**The algorithm** (`ATUTuneAlgo.cpp`):
1. **Memory:** a stored setting within 1 % of the frequency is read first, and kept if within
   0.1 of its stored SWR and within 1.5.
2. **Bypass:** SWR ≤ 1.3 stays bypassed; |Γ| > 0.98 is an antenna fault.
3. **Valley walk in LC:** for each single-element L code (0, 1, 2, …, 128), find the best
   single-element C. The first L gets a sweep; each later L gets a descent from the previous
   best C. Stop once the valley floor has risen twice. On any build the single elements step
   by about 2.3× in value, so these codes form a log-spaced grid without knowing the values.
4. **If LC's valley reaches SWR 1.5, refine it at once.** Otherwise, or if that refine ends
   above 1.5, walk CL and refine the lower valley.
5. **Refine** is a pattern search in code space:
   - read the four axis steps, then the four diagonals if none gains
   - repeat a gaining move with a doubling step while it gains
   - halve the steps when nothing gains (starting at half each code's top bit, minimum 2)
6. **Stop** at SWR 1.2, when refining is done, or after 120 readings. The tuner never reads a
   setting twice and leaves the best one it read. It reports `ATU_OK` at or below 1.5,
   otherwise `ATU_FAIL_NO_MATCH`, and stores the result.

**What mapping |Γ| over every setting showed** (the model, `code/tools/atu_vna/tuning_sim.py`):
- **One element at a time misses the match.** The valley is narrow and diagonal: it starts
  near small L and climbs. With C fixed, L alone often makes things worse.
  - Example: 25+j100 Ω on 80 m matches at LC L88 C43 (|Γ| 0.007). But at C = 0, |Γ| rises
    steadily with L (0.81 → 0.95), so an L sweep at C = 0 points the wrong way.
  - What falls steadily towards the match is the valley floor (the best C at each L), so the
    coarse stage walks that.
- **Code space has gaps and ridges.** The elements step by more than 2× (for example L4 > L1 +
  L2 + L3), so some neighbouring codes are far apart in value.
  - On 20 m, L23 → L24 takes |Γ| from 0.45 to 0.88.
  - Basins are separated by ridges, so the coarse walk has to find the right basin before the
    local refine.
- **Plateaus.** At some points a small element changes nothing (C1 is 8.5 pF), and a descent
  stops there. Searching on through plateaus cost far more readings for no net gain in
  simulation, so the tuner doesn't.
- **10 m.** The model's best settings use C7 + C8 far past resonance, where the model is known
  to be wrong. 10 m is judged on the bench.

**Simulation.** Every element was perturbed, with 15 loads per band up to SWR 20, compared with
the best of all 131,072 settings:

| Build | 80–15 m loads within SWR 1.5 (of those reachable) | Median readings |
|---|---|---|
| 5 %, clean bridge | 50/51 (host test), 54/55 (`search_sim.py`) | 56 / 52 |
| 12 %, bridge leak 0.03 (0.1 on 10 m) | 46/51, 48/51 | 55 / 51 |

- **10 m:** never worse than bypass.
- **Reading noise:** at 0.5 % of |Γ| it costs about one load in 55.
- **`code/tools/atu_vna/search_sim.py`** runs the same search on the Python model in seconds,
  for trying changes before porting them.

**Bench** (KI3P prototype, 2 W, loads on the antenna connector, so seen through the output coax
and TR switch). Each cell is SWR, readings and time:

| Load | 80 m | 40 m | 20 m | 15 m | 10 m |
|---|---|---|---|---|---|
| 50 Ω dummy | bypass, 1 | bypass, 1 | bypass, 1 | bypass, 1 | 1.18, 14, 1.5 s (bypass reads 1.32) |
| 100 Ω (114 ms readings) | 1.08, 32, 3.3 s | 1.19, 16, 1.7 s | 1.15, 26, 2.7 s | 1.15, 43, 4.4 s | 1.41, 43, 4.4 s |
| 200 Ω (99 ms readings) | 1.05, 47, 4.2 s | 1.17, 36, 3.2 s | 1.25, 73, 6.4 s | 1.37, 58, 5.1 s | 1.31, 45, 4.0 s |
| Model tuner, 100 Ω | 1.08 | 1.18 | 1.21 | — | 1.42 |
| Model tuner, 200 Ω | 1.21 | 1.06 | 1.6–2.0 | 5.1 | 1.30 |

- **Memory:** a re-tune takes 1 reading (0.2 s), and band changes re-apply stored settings.
- **Trade-off:** a first tune takes 3–6 s, against 0.7 s for the model tuner on 80/40 m. In
  return there is no characterisation and no calibration, results are better on 20 and 15 m,
  and repeat tunes come from memory.

**Reading time** (into 200 Ω on 14.2 MHz, 15 readings each):

| Collect, settle | Bypass \|Γ\| mean / sd | Tuned \|Γ\| mean / sd | Per reading |
|---|---|---|---|
| 60 ms / 5 samples, 25 ms | 0.5799 / 0.0017 | 0.1141 / 0.0002 | 114 ms |
| **40 ms / 4 samples, 15 ms** (adopted) | 0.5809 / 0.0029 | 0.1144 / 0.0006 | 99 ms |
| 30 ms / 3 samples, 25 ms | 0.5800 / 0.0035 | 0.1141 / 0.0008 | 86 ms |

**Open:**
- No complex (reactive) load has been tested.
- Power handling and contact life at 100 W are untested.
- Switching relays with the carrier on at 2 W would cut a reading to about 40 ms; that is a
  relay-wear decision.
- Stage F: the Kenwood `AC` command and a status line.

## Why the report proposed a model (superseded)
- **SWR alone can't identify a setting.** One |Γ| reading puts the antenna on a circle of
  impedances needing entirely different settings, even different K17 positions.
- **Probes recover the missing phase.** Reading |Γ| at a few *known* probe settings, fitted
  against a circuit model of the tuner, recovers the antenna impedance.
- **Select robustly.** Choosing the setting whose worst case is best over slightly perturbed
  models carries over to builds with different hand-wound coils. On 10 m that is essential:
  median SWR 1.09 robust against 1.78 plain best.

## Progress (stages A–E)
Stages A, C and E are superseded by the measured search and stay on branch `ATU_support`.
Stages B (engine) and D (memory) carry over to `ATU_search`.

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
- **Stage C done in firmware (2026-11-14): the tuning algorithm.**
  - `ATUTuneAlgo.cpp` implements §6.5 as a resumable planner: bypass check, 4 probes,
    Levenberg–Marquardt load estimate in (ln R, X) from 20 starts (5 per call), chunked search
    and robust pick (3 calls of about 6 ms), accept or re-estimate (≤ 4 jumps), and a polish or
    fallback hill-climb.
  - Thresholds: stay bypassed at SWR ≤ 1.3; accept at SWR ≤ 1.5 or within 0.05 of prediction;
    polish above 1.3 (not 1.2, because of the bridge leak); antenna fault if every probe reads
    |Γ| > 0.95.
  - **Host simulation:** a plant with coils 5 % off the model and a 0.03 bridge-leak vector,
    12 loads to SWR 7.6 per band.

    | Band | Median SWR above best | Worst excess | Median readings (max) |
    |---|---|---|---|
    | 80 m | 0.02 | 0.20 | 6 (46) |
    | 40 m | 0.03 | 0.13 | 6 (46) |
    | 20 m | 0.04 | 0.25 | 6 (22) |
    | 10 m | 0.04 | 0.13 | 6 (6) |
  - **Bench:** into the dummy load, `AX3;` stays bypassed after one reading (|Γ| 0.047, 190 ms).
- **Bench with resistive loads (KI3P prototype, 2 W, 2026-11-14).** The loads were on the antenna
  connector, so the ATU sees them through its output coax and the TR switch.

  | Load | 80 m | 40 m | 20 m | 10 m |
  |---|---|---|---|---|
  | 100 Ω (bypass SWR ≈ 2.0) | 1.08, 6 rdg, 0.7 s | 1.18, 6, 0.7 s | 1.21, 6, 0.7 s | 1.42, 43, 4.5 s |
  | 200 Ω (bypass SWR ≈ 4.1) | 1.21, 6, 0.7 s | 1.06, 6, 0.7 s | 1.60 (2.0 in a repeat) | 1.30, 46, 4.7 s |

  15 m with 200 Ω: SWR 5.1 after 22 readings.

  **What the bench showed:**
  - **Elements past resonance.** The first 10 m run jumped four times to CL L201 C220, which
    uses L7, L8, C7 and C8 past resonance, and read |Γ| 0.995. Fixed in `8ff4d8a`: elements are
    used only below 75 % of resonance (`ATU_ELEMENT_LIMIT`), and a setting is never read twice.
  - **Output path.** The load estimates imply 3.5–5.5 ns of path between the ATU output and the
    antenna connector. That rotates the loads, so on 20 m the 200 Ω load lands near 43−j80 Ω,
    where the model's best is |Γ| 0.38 (a coverage hole).
  - **The CL model above 15 MHz.** Offline fits of the probe readings: on 10 m the three LC probes
    agree with one load to 0.011–0.013 rms, but the CL probe is 0.3–0.37 off. So the bridge is
    fine on 10 m, and the CL model is wrong there, as report §3.5 found (CL S11 residual 0.19
    from 15 to 30 MHz).
    - The fourth probe is now LC above 15 MHz.
    - Jumps into CL are still wrong there. On 10 m (200 Ω) the model predicts |Γ| 0.78 for the
      setting the board actually matched at 0.13.
    - Low-R loads need CL, so 15 and 10 m fall back to the slow hill-climb, which can stop in a
      local minimum (15 m: SWR 5.1).
  - **Open (owner's choice: finish the other tests first):** re-measure the RF board in CL with
    single elements on the VNA and refit the CL path. The alternative is a measurement-driven CL
    search above 15 MHz.

- **Stage D done (2026-11-14): per-band, per-antenna memory.**
  - **Storage:** `ED.ATU_State/ATU_Freq_kHz/ATU_SWR[band][antenna]`, plus `ATU_TunePower_W` and
    `ATU_AutoApply`, saved by `Storage.cpp`.
  - **Auto-apply:** `ATUApplyStoredIfChanged()` runs from `HandleTuneState()`. It applies the
    stored setting (or bypass) only when the band or antenna changes, and only in receive; a
    change seen while keyed is applied on the return to receive. Manual changes survive tuning
    steps.
  - **In a tune:** a stored setting within 1 % of the frequency is read first and kept if it is
    within 0.1 of its stored SWR and within 1.5. Each tune stores its result.
  - **Menu:** Tune, Save to memory, Forget band, Auto-apply memory, Tune power. **CAT** `AX5;`
    forgets an entry.
  - **Bench (200 Ω):** stored settings re-applied on 80 ↔ 40 m changes, a manual bypass survived
    a step within the band, and a re-tune on 40 m took 1 reading.

- **Stage E done (2026-11-14): per-band bridge scale; coils measured but not applied.**
  - **First version (`7e78214`):** solved L4–L8 from single-coil readings into a dummy load (§6.6).
    The coils came out 3–16 % high, and the error grew with frequency. The cause is the SWR
    bridge, which reads |Γ| high by a factor that depends on the band, not the coils.
  - **Rework (`8d8b150`, owner's choice: "bridge scale only"):** `ATUCalibrate.cpp` steps
    through 160–6 m.
    - **Reference capacitors:** on each band it reads up to three capacitors alone. Each must be
      below 60 % of its resonance, with model |Γ| between 0.4 and 0.95.
    - **Scale:** the band's scale is the median of measured/model. It is believed only within
      ±15 %. It goes to `ED.ATU_GammaScale[band]`, and every tuner reading on that band is
      divided by it.
    - **Coils:** coil readings are divided by the scale, then solved by bisection.
      They are reported in `AX7` but not stored (`ATU_CAL_STORE_COILS 0`), so the reference
      coil model stays.
    - **Saving:** ED is saved only after the operator's band and frequency are back
      (`ATUTuneRecord.saveConfig`). The first version saved while still on the last calibration
      band, so the radio rebooted there.
    - **CAT:**

      | Command | Effect |
      |---|---|
      | `AX6;` | run the calibration |
      | `AX7;` | coils: stored values, then solved values; negative = rejected |
      | `AX8;` | per-band scales × 1000 |
      | `AX9;` | reset the calibration |
  - **Bench (KI3P prototype, 50 Ω dummy load, 2 W):** 19 readings in 4.7 s. The scales and the
    radio's frequency survived a reboot, and no coil override was stored.

    | Band | References (measured/model) | Stored scale | Coil solved (vs reference) |
    |---|---|---|---|
    | 160 m | C8 1.087 | 1.088 | L7 −12.3 %, L8 −10.6 % |
    | 80 m | C8 1.027, C7 1.012 | 1.020 | — |
    | 40 m | C7 1.047, C6 1.048 | 1.047 | L6 +0.7 % |
    | 30 m | C6 1.059, C7 1.051 | 1.055 | L5 −6.0 % |
    | 20 m | C6 1.052, C5 1.099 | 1.076 | — |
    | 17 m | C5 1.149 | 1.150 | L4 −1.6 % |
    | 15 m | C5 1.111, C4 1.323 | rejected | — |
    | 12 m | C4 1.268 | rejected | — |
    | 10 m | C4 1.241 | rejected | — |

    6 m was not read.

  - **What the bench showed:**
    - **80–30 m:** a pure scale fits. Capacitors on the same band agree within 0.01, and L6 solves
      within 1 % of its VNA value.
    - **20 m and above:** the error behaves more like an added leak vector than a scale. At 20 m
      the two capacitors read 0.043 and 0.050 above the model, rather than in a fixed ratio. On
      15 m the ratio is 1.11 for C5 and 1.32 for C4. Rejecting 15/12/10 m is correct; their
      bridges *are* calibrated.
    - **160 m and 17 m rest on one reference capacitor each.**
      - On 160 m the C8 reading disagrees with the coils: unscaled, L7 reads 0.977 and L8
        1.021 of the model. So the 1.088 scale is probably a C8 model error, and it is what
        makes L7 and L8 solve 11–12 % low.
      - An earlier run of the rework stored 1.005 on 160 m.
      - The 17 m value sits right at the ±15 % limit and looks like the 15 m leak.
  - **Open:** require at least two references that agree before believing a scale. That would
    drop 160 m and 17 m to the unscaled bridge. Alternatively, fit a scale plus an additive leak
    from the references on 20 m and above.

## Proposed algorithm (§6.5, superseded)
memory → bypass check (SWR ≤ 1.3 stays bypassed) → 4 probes (empty LC, one L ≈ 50 Ω, one C ≈
1/50 S, both in CL) → estimate Z_ant by least squares → robust jump (top 300 by model, 6 models
with coils ±2 %) → accept if SWR ≤ 1.5 or within 0.05 of prediction, else re-estimate (up to 4
jumps) → optional hill-climb polish → store. A median of five readings in simulation.

## Firmware plan (original outline, superseded)
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
- (Model approach, now moot) Is a 4-probe estimate robust against the bridge's systematic
  error? Do the layout parasitics hold on a second board?
- Does the search hold on a second board? It knows nothing about the board, so it should; only
  simulated builds have been tried.
- Power handling and contact life at 100 W are untested.
