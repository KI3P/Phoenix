---
title: Documentation TODOs & Open Questions
type: roadmap
status: draft
created: 2026-06-08
updated: 2026-06-16
tags: [todo, open-questions, discrepancies, wiki-maintenance]
source_refs: []
related: ["[[index]]", "[[iq-imbalance-correction]]", "[[multirate-decimation]]", "[[noise-reduction]]", "[[fast-convolution-filtering]]", "[[dsp-chain]]"]
---

# Documentation TODOs & Open Questions

Running list of (a) wiki pages still needing work and (b) **code/comment discrepancies and
possible bugs** surfaced while writing the wiki. Items here are leads for future code reading,
hardware testing, or a question to the human. Resolve an item → fix the page, cite the
finding, and strike it from this list.

This is a planning surface, not a defect tracker — but anything that looks like a **real bug**
is tagged **⚠ possible bug** so it stands out.

## Code/comment discrepancies & possible bugs

### Hardware (from T41-EP schematics, ingested 2026-06-14)
- ~~⚠ AD8307 (log) vs linear power math.~~ — **RESOLVED, not a bug (2026-06-14).** Net trace +
  schematic-owner confirmation: AD8307 OUT (U14 fwd / U19 rev) → AD7991 CH0/CH1; REF3440 →
  Vin3/Vref. The **default digital path already does the correct AD8307 log conversion**
  (mV/25 − 84 → dBm → W, `LPFBoard.cpp:640-643`). The linear `(V·10)²/50` belongs to the
  `USE_ANALOG_SWR` branch (linear diode detectors on Teensy pins 26/27, off by default). The
  flag originated from a **wiki mis-attribution**, now corrected. → [[filter-boards]]
- ~~Phantom second SGTL5000.~~ — **RESOLVED (2026-06-14, schematic owner).** There is **one**
  SGTL5000 (Teensy hat, `sgtl5000_teensy`): mic in **+** TX I/Q out. The `pcm5102_mainBoard`
  object represents the **PCM5102 speaker DAC** (U5), and **PCM1808** (U6) is RX I/Q in. The
  object being typed `AudioControlSGTL5000` is a **harmless mistype** — PCM5102/PCM1808 are
  control-less I²S parts, so its `.enable()/.inputSelect()` calls are no-ops; the I²S data path
  carries the audio. *Code clarity fix (retype/rename + drop the "two SGTL5000s" comment), not a
  functional bug.* → [[hardware-platform]], [[t41-ep-schematics]]
- ~~**Net-tracing leads.**~~ — **ALL RESOLVED 2026-06-14/15** (owner-assisted): ATtiny85 purpose;
  PE4312↔MCP23017 mapping; LPF band-code→decoder→relay + external filter board; U21 upper bits
  (unused/NC); I²S/MCLK routing (MCLK=23/LRCLK=20/BCLK=21 shared; DAUDIO_OUT=32→PCM5102;
  ADC_IN←PCM1808). Per-band LPF L/C→cutoff intentionally **out of scope** (owner). The remaining
  non-schematic hardware items (PA bias/rails; per-band SWR cal vs. measured power = bench) are
  not net traces. → [[hardware-platform]], [[rf-board]], [[filter-boards]]

### DSP — I/Q calibration
- ~~**Tone offset vs. FFT bins.**~~ — **RESOLVED (2026-06-14).** The CW tone is set to
  `band_center + SR[SampleRate].rate/4` (`HardwareSm.cpp:613`) = 48 kHz at the default 192 kHz
  raw rate, so the "48 kHz above/below the LO" comment is correct. The "±6 kHz at 24 kHz"
  premise was wrong: the PSD read here is the **192 kHz raw, 1×-zoom 512-bin display FFT**
  (cal forces `spectrum_zoom = 0`, `HardwareSm.cpp:498`) → 375 Hz/bin, bin 256 = LO, so
  ±Fs/4 = bins 128/384. Wanted = bin 384 (tone is Fs/4 *above* LO), image = bin 128.
  → [[iq-imbalance-correction]]
- ~~**Separation metric units.**~~ — **RESOLVED (2026-06-14).** `psdnew` holds
  `log10f_fast(magnitude)` (`DSP_FFT.cpp:144,186`), so `(upper − lower)` is a log₁₀ ratio and
  the `×10` expresses sideband separation in **dB**. → [[iq-imbalance-correction]]
- **Correction-model completeness.** The RX/TX correction is single-axis (scale I only +
  phase shear). Does this leave a residual image vs. a full 2×2 correction matrix, and is it
  measurable at the achieved IRR? → [[iq-imbalance-correction]]

### DSP — noise reduction
- ~~**⚠ possible bug:** `DSP.cpp:687` `arm_scale_f32(data->Q, 2, data->I, …)`~~ — **RESOLVED,
  not a bug** (investigated 2026-06-08). `Xanr` writes its result to `data->Q` and leaves
  `data->I` holding the pre-NR signal; downstream `InterpolateReceiveData` reads `data->I`, so
  the line correctly copies the denoised `Q` into `I` with a 2× makeup gain (cf. ×30 for Kim).
  The notch block at `DSP.cpp:930-933` uses the same `Xanr`→`copy(Q,I)` pattern. *Clarity nit
  only:* the "Xanr outputs to Q while the chain treats I as primary" convention plus the magic
  2× are fragile — worth a code comment, not a fix.
- ~~**Auto-notch invocation.**~~ — **RESOLVED:** the auto/manual notch is `Xanr(&data, 1)` at
  `DSP.cpp:930`, gated by `ED.ANR_notchOn == 1`, right after `NoiseReduction()`, followed by
  `arm_copy_f32(data.Q, data.I, …)`. Distinct from the NR-mode `Xanr(&data, 0)` inside
  `NoiseReduction`. → [[noise-reduction]]
- ~~**NR sample-rate comments.**~~ — **RESOLVED (2026-06-14).** Stale DD4WH comment, not a real
  rate assumption. `Kim1_NR` runs at the **24 kHz** decimated RX rate (raw 192 kHz ÷ DF1·DF2 =
  4·2 = 8, `SDT.h:409-410`) → 24000/256 = **93.75 Hz/bin**. The `46.9Hz [12000Hz/256] @96kHz`
  comment (`DSP_Noise.cpp:141,155`) is leftover Convolution-SDR text; the code divides by the
  **live** `data->sampleRate_Hz / NR_FFT_L` so it is correct regardless. → [[noise-reduction]]

### DSP — multirate / filtering
- ~~**TX vs RX filter design.**~~ — **RESOLVED (2026-06-14).** Primarily **heritage**, not a
  deliberate determinism/latency choice. The fixed 48-tap tables are legacy DD4WH coefficients
  (arrays named/commented "RXfilters", `DSP_FIR.cpp:174-175`) reused wholesale by the TX path;
  RX *decimation* was refactored into the harris-formula runtime design. The runtime path's
  rate-independence is dormant — `SampleRate` is hardcoded 192 kHz (`Globals.cpp:106`, never
  reassigned), so RX recomputes identical coeffs each boot. (RX is a hybrid: decimation
  runtime-sized; interpolation fixed 48/32 taps.) → [[multirate-decimation]]
- **Round-trip group delay.** Measure the RX decimate→convolve→interpolate latency against the
  ~10 ms loop budget. → [[multirate-decimation]], [[real-time-constraints]]
- **Spectrum refresh floor.** Reconcile the `.ino`'s 200 ms spectrum figure with the measured
  ~80 ms floor and the role of `NCHUNKS`. → [[real-time-constraints]], [[zoom-fft]]
- ~~**Zoom default mismatch.**~~ — **RESOLVED (2026-06-14).** Not a conflict: `Config.h:78
  ZOOM 3` is a **front-panel button-ID** macro (`Config.h:74-96`, dispatched as `case ZOOM:`
  in `Loop.cpp:457,818`, which does `ED.spectrum_zoom++`), *not* a zoom level. Power-on zoom is
  governed solely by `ED.spectrum_zoom` (default `1` = 2×, `SDT.h:274`; or a restored JSON
  value). → [[zoom-fft]]

### Units / naming
- ~~**VFO frequency unit mislabeled.**~~ — **FULLY RESOLVED (2026-06-16).** First the `RFBoard.h`
  / `Tune.h` docstrings were corrected to centi-Hz (Hz × 100) on 2026-06-14; then the owner
  completed the cross-file rename of every `_dHz` *symbol* (`GetTXRXFreq_dHz`, `frequency_dHz`,
  etc.) to `_cHz`, eliminating the historical "decihertz" misnomer entirely. Wiki updated to
  match. *Reconciled (2026-06-16):* the rename has now merged onto `encoder-i2c-speed` (0 `_dHz`
  / 46 `_cHz` in `code/src/PhoenixSketch/`), so source and wiki agree — the earlier branch caveat
  is closed.
  → [[rf-board]], [[tune-frequency-control]]

### Stale source comments
> **All items below FIXED IN SOURCE (2026-06-14)** — comment-only edits now **committed and
> merged** onto `encoder-i2c-speed` (commit `c95f932` "Unit and documentation fixes", 2026-06-16).
> The wiki was already correct.
- ~~**CAT protocol: TS-480 vs TS-2000.**~~ — Fixed `CAT.h:25` "TS-2000" → "TS-480" (now
  consistent with `CAT.cpp` and `code/docs/ts_480_pc.pdf`; ID020 = TS-480). → [[cat-control]]
- ~~**"EEPROM" is actually LittleFS+SD JSON.**~~ — Fixed the `.ino` header ("Load settings
  from flash/SD…", "Storage.cpp/h: settings persistence … not true EEPROM", and the `ED`
  blurb now notes the name is historical) and `Storage.cpp:675`. Type name `EEPROMData` left
  as-is (historical). → [[persistent-config]]
- ~~**"TuneSm" does not exist.**~~ — Fixed `PhoenixSketch.ino` (TuneSm → HardwareSm in the
  philosophy bullet, ASCII diagram, state-machine list, and module list) and `CLAUDE.md`
  ("Frequency Control State" section + module/task lines). Both now state Tune.cpp is a
  pure-function library and the tune state lives in HardwareSm.
  → [[tune-frequency-control]], [[hardware-state-machine]]
- ~~**HardwareSm is hand-written, not generated.**~~ — Made explicit in `PhoenixSketch.ino`
  (section intro + HardwareSm entry) and `HardwareSm.cpp` header ("hand-written, NOT generated
  by StateSmith"). → [[hardware-state-machine]], [[state-machine-architecture]]
- ~~**`.ino` ModeSm state list.**~~ — Fixed: now "SSB_RECEIVE, SSB_TRANSMIT, CW_RECEIVE, six
  CW_TRANSMIT_* keyer sub-states, and the CALIBRATE_* states (… there is no TUNE state)".
  → [[mode-state-machine]]

### DSP — AGC
- ~~**Soft-knee transfer curve.**~~ — **RESOLVED (2026-06-14).** Gain law `DSP.cpp:454`
  produces three regions: flat limiting at `out_target` for `volts ≥ max_input`; a
  linear-in-log soft knee (`out_target − slope_constant·log10(volts/max_input)`) for
  `min_volts ≤ volts < max_input`; and constant `max_gain` below `min_volts`. `var_gain` is the
  slope control — total output rise across the active range = `out_target·(1 − 1/var_gain)`
  ≈ +3.5 dB at the default `var_gain = 1.5`. → [[agc-design]]
- ~~**n_tau vs MAX_N_TAU.**~~ — **RESOLVED (2026-06-14).** `MAX_N_TAU = 8` is a commented-out
  worst-case constant (`SDT.h:615-617`), not used at runtime — it (with `MAX_SAMPLE_RATE` and
  `MAX_TAU_ATTACK`) sizes the static `ring_buffsize = 24000·8·0.01+1 = 1921`. Operating
  `n_tau = 4` → 96-sample (~4 ms) look-ahead at 24 kHz; ring is over-provisioned ~20×.
  → [[agc-design]]

## Pages still to write or promote

> **Status (2026-06-08):** every firmware **module** page and all **9 theory spine** pages are
> drafts. What remains is *deeper-dive* content — new pages for subsystems that were only
> referenced, the per-page "Remaining:" gaps below, and hardware-measurement follow-ups.

### Not yet written (new pages needed)
- ~~**CW processing**~~ — **done** ([[cw-processing]]: audio filter, hybrid correlation+Goertzel
  tone detection, adaptive Morse decoder via flat tree + timing histograms, sidetone). Remaining:
  reconcile the header "50–500 Hz" vs code "0.8–2.0 kHz" filter-width labels; decoder sample rate.
- ~~**Equalizer**~~ — **done** ([[audio-equalizer]]: parallel 14-band biquad filterbank, RX/TX
  gains, alternating-sign reconstruction). Remaining: band centre-freq/Q table; gain range.
- ~~**Synchronous AM (SAM) detection**~~ — **done** ([[synchronous-am-detection]]: 2nd-order PLL
  carrier recovery, fade leveler, carrier-offset readout). Remaining: exact zeta/tau constants.
- ~~**TX carrier-null calibration**~~ — **done** ([[tx-carrier-null]]: DC-offset LO-leakage
  suppression + the dBc sweep). Remaining: DCOffset units / achievable dBc.
- ~~**Built-in test (BIT)**~~ — **done** ([[built-in-test]]: startup I²C presence flags →
  status screen). Remaining: is it re-runnable / extensible to functional checks?
- ~~**Calibration sub-state-machines**~~ — **done** ([[calibration-state-machines]]: 3 identical
  minimize-sweep SMs + PowerCalSm curve-fit, with state/event maps). Remaining: per-SM `Vars`;
  whether the 3 identical SMs share one diagram.

**All previously-listed "not yet written" pages are now drafts.**

### Per-page "Remaining" gaps (deepen existing drafts)
- [[hardware-state-machine]]: full state maps of the 4 cal sub-SMs (above);
  `RFCalTransmitIQSingleVFO` external-analyzer flow.
- [[rf-board]]: local Etherkit-Si5351 library modifications vs. upstream.
- [[filter-boards]]: full LPF band↔BCD table; directional-coupler / per-band power calibration.
- [[front-panel]]: per-button id→physical-label map; measured latency win from the branch.
- [[display-subsystem]]: waterfall colour-map / `psdnew`→pixel mapping; touch input;
  `primaryMenu[8]` contents.
- [[cat-control]]: verify the `IF` status-string field layout vs. `code/docs/ts_480_pc.pdf`.
- [[persistent-config]]: JSON schema completeness; versioning/migration behaviour.
- Theory: deepen via hardware measurement (group delays, AGC transfer curve, IRR achieved).

### Theory stubs → drafts
- ~~[[agc-design]]~~ — **done** (wdsp look-ahead AGC, hang state machine).
- ~~[[zoom-fft]]~~ — **done** (raw-192k IIR-decimate zoom; corrected the earlier "operates on
  24 kHz baseband" error — it does not).
- All nine theory spine pages are now drafts. Next: deepen via measurement/hardware, or start
  CW-processing and equalizer pages (see below).

### Firmware module stubs (have "To flesh out" checklists)
- ~~[[mode-state-machine]]~~ — **done** (full state hierarchy, event map, keyer timing,
  paddle memory, calibration branch).
- ~~[[tune-frequency-control]]~~ — **done** (was the mis-named "tune-state-machine" stub;
  Tune.cpp is a function library, not an SM — page retitled, all links updated).
- ~~[[hardware-state-machine]]~~ — **done** (RFHardwareState/TuneState mappings, T/R
  switching, calibration orchestration). Remaining: full state maps of the 4 generated cal
  sub-SMs.
- ~~[[ui-state-machine]]~~ — **done** (13 states, full transition map, DFE resolved). All
  firmware state-machine pages are now drafts.
- ~~[[rf-board]]~~ — **done** (Si5351 quadrature VFO incl. the <3.2 MHz timed-delay path,
  attenuators, T/R switching, cal loopback). Remaining: local Etherkit-lib modifications.
- ~~[[filter-boards]]~~ — **done** (BPF/LPF/back-end relays via shared hardwareRegister, AD7991
  SWR/power math). Remaining: full LPF band↔BCD table; coupler calibration.
- ~~[[front-panel]]~~ — **done** (2×MCP23017, Rotary_V12 encoders, two-tier ISR+1ms-timer
  servicing = the encoder-i2c-speed branch design). Remaining: per-button id→label map.
- ~~[[display-subsystem]]~~ — **done** (Pane dirty-flag model, 13 home panes enumerated,
  DrawDisplay router, VariableParameter editor + menu model). Remaining: waterfall colour-map /
  pixel mapping, touch input, primaryMenu[8] contents. **All firmware module stubs now drafts.**
- ~~[[cat-control]]~~ — **done** (25-command dispatch table, transport, event/ED apply paths).
  Remaining: verify `IF` status-string layout vs. `code/docs/ts_480_pc.pdf`.
- ~~[[persistent-config]]~~ — **done** (`ED` field groups, JSON-on-LittleFS+SD persistence,
  ParamSave scratch). Remaining: JSON schema completeness + versioning/migration behavior.

_(The "not yet written" pages are consolidated at the top of this section.)_

## How to use this list
When the human asks "what's left to document?" or "what looked wrong in the code?", start
here. When a code discrepancy is confirmed as a bug, note it here AND consider whether it
belongs in the actual issue tracker / a fix branch — the wiki records the *finding*, not the
fix.
