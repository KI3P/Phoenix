---
title: "Dev: Controlled Envelope SSB (#35)"
type: roadmap
status: draft
created: 2026-11-18
updated: 2026-11-18
tags: [development, proposal, transmit, ssb, cessb, speech-processing, dsp, cat]
source_refs: []
related: ["[[controlled-envelope-ssb]]", "[[development-backlog]]", "[[dsp-chain]]", "[[ssb-phasing-method]]", "[[runtime-filter-design]]", "[[digital-mode]]", "[[automatic-power-swr-calibration]]", "[[tx-filter-hil-test]]", "[[cat-control]]", "[[real-time-constraints]]"]
---

# Dev: Controlled Envelope SSB (#35)

**GitHub issue:** [KI3P/Phoenix#35](https://github.com/KI3P/Phoenix/issues/35), *"Implement
CESSB"*, which asks to "improve SSB transmission by implementing controlled envelope SSB".

**References:**
- The groups.io thread linked from the issue (login-only; not read for this proposal).
- [W7PUA's CESSB page for the Teensy F32 library](https://www.janbob.com/electron/CESSB_Teensy/CESSB_Teensy.html).
- The theory is on [[controlled-envelope-ssb]].
- The evidence for this proposal is the offline model in
  `code/tools/cessb_model/` (README there; numbers in `results/results.md`).

**Status:** Phases 0 and 1 are done on branch `cessb-proposal`, as of 2026-11-18:
- **Phase 0:** the offline model.
- **Phase 1:** the DSP stage, the `TXGain` move, the mic-gain fix, an Audio Options menu
  toggle and gtests. CESSB is **off by default** until the bench and on-air checks of
  Phase 3.

See [Phase 1 as built](#phase-1-as-built) for where the build departs from the design below.

## Summary

- **What to port.** Port the CESSB *clipper and overshoot controller* natively into
  `TransmitProcessing()`, at the existing 24 kHz node. Do not drop in W7PUA's
  `radioCESSB_Z_transmit_F32` class.
- **Which variant.** Use the **centred** variant: shift the channel to straddle DC, clip and
  filter, shift back. It keeps the opposite sideband about 44 dB cleaner than the Z class,
  for about 0.3 dB less talk power.
- **Power control.** Move `TXGain` after CESSB, and set the clip ceiling to the power-cal
  tone's envelope. The SSB power setting then becomes a true **PEP** limit.
- **Controls.** Expose the stage as a speech processor: on/off, plus a drive level. Use
  Kenwood `PR`/`PL` over CAT.

At 6 dB drive the model gives:
- **+4.6 dB** of wanted-sideband power at equal measured PEP;
- **1.3 %** overshoot after the interpolators;
- opposite sideband at **−71 dBc** and out-of-channel energy at **−86 dBc**.

For comparison, overdriving the current chain by the same 6 dB gives +2.5 dB, 41 % overshoot,
−24 dBc and −36 dBc.

## Where Phoenix is today

`TransmitProcessing()` (`DSP.cpp:1200-1236`) for `SSB_TRANSMIT`:

```
mic @Fs ─ ÷4 ─ ÷2 ─ BandEQ ─ TXGain(I) ─ Q:=I ─ ÷2 ─ Hilbert ±45° (12k) ─ ×2 ─┐
                                                                    24 kHz node │
   ┌────────────────────────────────────────────────────────────────────────────┘
   └─ IQ correction ─ SidebandSelection ─ ×2 ─ ×4 ─ PlayIQData (float→q15 + DC offsets)
```

Rates are Fs/8 = 24 kHz at 192 ksps and 22.05 kHz at 176.4 ksps.

Gaps that matter for CESSB:

- **Nothing controls PEP.** The only limiter is accidental: `arm_float_to_q15` in
  `PlayIQData` (`DSP.cpp:1096`) saturates I and Q *separately*. That is a square clip. It lets
  the envelope reach √2 of full scale and splatters badly. In the model it puts out-of-channel
  energy at −36 dBc after 6 dB of overdrive.
- **Power is set by DSP gain.** `TXGain` (`DSP.cpp:1120`) scales I by the per-band calibration
  plus `10·log10(P/P_cal)`. The RF attenuator stays at 0 dB. The calibration point is a steady
  800 Hz tone of amplitude 0.08 into the chain (`MainBoard_AudioIO.cpp:519`), giving
  `SSB_20W_CAL_POWER_POINT_W` = 3 W or `SSB_100W_CAL_POWER_POINT_W` = 15 W (`SDT.h:291`).
  Voice peaks above that amplitude overshoot the requested power without limit.
- **No speech processing exists.** There is no compressor, clipper or ALC, and nothing for
  it in `ED`. The Kenwood `PR` name became free when `PR` was renamed `HR` (7918d48).
- **Mic gain bug, a prerequisite.** `AudioControlSGTL5000::micGain(unsigned int dB)` takes
  an unsigned value. The menu (−10…30, `MainBoard_DisplayMenus.cpp:463`) and CAT `MG`
  (down to −40) both allow negative values. A negative value wraps to a huge number, which
  selects the 40 dB preamp and maximum input gain. CESSB's drive depends on mic level, so
  fix this first.

## Recommended design

### 1. A native stage, not the OpenAudio class

Add a `CESSBProcess(DataBlock*, TransmitFilterConfig*)` in a new `DSP_CESSB.cpp/.h`. Call it
in `TransmitProcessing()` between `TXInterpolateBy2Again` and `ApplyIQCorrection`
(`DSP.cpp:1224-1226`).

Why not `radioCESSB_Z_transmit_F32`:
- **It belongs to a graph Phoenix's transmit chain doesn't use.** It is an `AudioStream_F32`
  graph object, and the transmit chain uses no F32 blocks. It is all hand-written CMSIS on
  `DataBlock`s.
- **It duplicates stages Phoenix already has.** It brings its own ÷4 decimator, 201-tap
  Hilbert and ×2 interpolators, and accepts only 44–50 kHz input. Of its stages, only the
  clipper and overshoot section (`.cpp:155-221`, about 60 lines) does anything Phoenix's
  chain lacks.
- **Its filters don't follow the sample rate.** They are fixed tables designed at 24 ksps.
  At 176.4 ksps the node runs at 22.05 kHz, and the model shows the Z filter pulls the
  transmit −3 dB edge from 2750 to **2625 Hz**. A native stage can generate its filter in Hz
  with `CalcFIRCoeffs` inside `InitializeTransmitFilterCoeffs` (`DSP_FIR.cpp:1369`), the same
  way the [[runtime-filter-design]] stages do.
- **It can't be unit-tested here.** The gtest mocks cannot run F32 graph objects. A
  `DataBlock` stage tests like every other transmit stage.

### 2. The centred variant

The model compared both variants with the firmware's real coefficients (192 ksps, W9GR voice,
each at its best `gainCompensate`):

| 6 dB drive | Z class (gc 1.4) | Centred (gc 1.9) | Today, overdriven 6 dB |
|---|---:|---:|---:|
| Wanted-sideband power gain at equal PEP | 4.88 dB | 4.59 dB | 2.48 dB |
| Overshoot after the ×2, ×4 interpolators | 0.9 % | 1.3 % | 41.4 % |
| Opposite sideband, voice | −27.4 dBc | **−71.0 dBc** | −23.8 dBc |
| Out of channel (3.3–12 kHz, both sides) | −82.9 dBc | −86.2 dBc | −36.0 dBc |
| Two-tone 2f₁−f₂ (500 Hz into the opposite SB) | −11.2 dBc | **−62.5 dBc** | — |
| Two-tone 2f₂−f₁ (3100 Hz) | −26.2 dBc | −28.6 dBc | — |
| −3 dB passband below ceiling, 192k / 176.4k | to 2725 / 2625 Hz | to 2700 / 2725 Hz | to 2700 / 2750 Hz |

**Why the Z class leaves the opposite sideband dirty.** Its real lowpass passes −3 to +3 kHz.
On a zero-IF signal sitting at 0 to +3 kHz, every clip product that lands on the other side of
the carrier gets through ([[controlled-envelope-ssb]]). That puts a −11 dBc two-tone product
500 Hz into the opposite sideband: worse than a typical PA's IMD3, in the next channel down.
The centred variant costs 0.3 dB of talk power and removes it.

**Centred-variant parameters, from the model's sweeps** (`cessb.py` constants):
- **Centre and width:** centre 1500 Hz, lowpass −6 dB at ±1450 Hz, so the channel runs from
  50 to 2950 Hz. This doesn't narrow the existing passband. At ±1500 Hz the 3100 Hz two-tone
  product came through at −18 dBc. At 1450 ± 1400 Hz it fell to −53 dBc, but the response at
  2700 Hz dropped by 0.8 dB.
- **Filter:** 123 taps with a 40 dB Kaiser design. That overshoots less than a 60 dB design
  of the same length (1.3 % against 1.5 %).
- **Overshoot gain:** `gainCompensate` 1.9, Hershberger's Weaver value. That gives 1.3 %
  overshoot; 2.2 gives 0.8 %; 1.4 (W7PUA's Z value) gives 4.1 %.
- **Frequency shift:** at 192 ksps, 1500/24000 = 1/16, so the shift oscillator is a
  16-entry table. At 176.4 ksps use a phase accumulator with `arm_sin_cos_f32`, or a
  renormalised complex rotation.

**Talk power against drive** (centred, gc 1.9, equal measured PEP):

| drive dB | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| gain dB | 0.0 | 0.9 | 1.9 | 2.8 | 3.6 | 4.2 | 4.6 | 5.0 | 5.3 |
| overshoot % | −0.3 | 1.0 | 0.8 | 1.5 | 1.7 | 1.4 | 1.3 | 1.2 | 1.1 |

**Post-CESSB interpolators.** The overshoot at the DAC is within 0.3 points of the overshoot
at the node, so the two ×2/×4 FIR interpolators do not regrow the peaks. The analog exciter
and PA filters are not modelled (see Phase 3).

**Latency:** 124 samples at the node (61-sample filter delay plus the 63-sample overshoot
delay line), so **5.2 ms** at 192k and 5.6 ms at 176.4k.

### 3. Levels: the ceiling is the power setting

1. **Move `TXGain` after CESSB.** It should scale both I and Q, just before
   `ApplyIQCorrection`. Every stage it moves past is linear, so with CESSB bypassed the
   output is unchanged. A gtest should prove that bit for bit, within float rounding.
   `TXgainDSP` stays the same global that `HardwareSm_PowerCalibration.cpp:410` reads.
2. **Normalise into CESSB by `A_ref`.** `A_ref` is the node envelope that the 0.08 cal tone
   produces. Measure it in Phase 1 with the real stages; it depends on the Hilbert and
   interpolator gains and on BandEQ's gain at 800 Hz. Clip at 1.0, then multiply by
   `A_ref · 10^(TXgainDSP/20)`.
3. **Result:** a voice peak can produce no more than the power a cal tone at the ceiling
   produces, which is `powerOutSSB[band]`. **The SSB power setting becomes PEP**, and average
   power rises with drive. The existing per-band SSB calibration stays valid, because cal
   runs with CESSB bypassed (see section 4).
4. **Add drive gain.** A pre-clip gain `ED.cessbDrive_dB` (0…10 dB) sets how hard the voice
   hits the ceiling. The microphone gain still sets the voice level; the processor level sets
   the compression.

### 4. When CESSB runs

- **Bypassed in:**
  - `DIGITAL_TRANSMIT`: FT8 and other data modes need a linear chain. [[digital-mode]]
    already warns against compression.
  - `CALIBRATE_OFFSET_MARK`: the SSB power and SWR calibration tone.
  - `CALIBRATE_TX_IQ_MARK`.
  - `SSB_TRANSMIT` when disabled.
  The gate is the same `modeSM.state_id` test as `DSP.cpp:1205`.
- **Proposed meaning of "processor off":** the stage stays in line at 0 dB drive, as a pure
  PEP limiter, as W7PUA recommends. Normal speech then passes untouched. Only peaks that
  would exceed the power setting are clipped, cleanly, rather than splattering in the q15
  converter.
- **Full bypass for test only:** a non-persisted flag, so the linear-chain HIL suite can run
  ([[tx-filter-hil-test]]).

### 5. Settings, UI and CAT

- **`ED` fields:** `cessbEnabled` (bool) and `cessbDrive_dB` (int, 0–10). Persist them next to
  `currentMicGain` in `Storage.cpp` (`:141`, `:372`, `:623`, `:838`), with defaults
  "enabled" and a starting drive of 4–6 dB, to be tuned on air (Phase 3).
- **Menu:** *(superseded for on/off: the toggle is under Audio Options, see [Phase 1 as built](#phase-1-as-built))* in the Microphone menu, add "Processor" (on/off) and "Proc level" next to
  "Mic gain" (`MainBoard_DisplayMenus.cpp:463-475`).
- **Meter:** a processing meter on the TX state-of-health pane, beside the existing VU bar
  (`MainBoard_DisplayHome.cpp:1012`). Per block, show the pre-clip peak envelope relative to
  the ceiling in dB, like W7PUA's `getLevels()`. The operator sets mic gain so that peaks
  read about 0 dB with the processor off.
- **CAT:** use the Kenwood TS-480 commands (`code/docs/ts_480_pc.pdf`):
  - `PR P1;` turns the speech processor on or off. Map it to `cessbEnabled`.
  - `PL P1P1P1 P2P2P2;` sets the input and output levels, each 000–100. Map the input
    level to `cessbDrive_dB` (0–100 → 0–10 dB). Accept the output level and echo it back,
    but don't act on it, because `PC` already owns output power.
  - Neither name is in the `CAT.cpp` table today.

### 6. Budget

- **CPU:** 4 FIRs × 123 taps × 256 samples, about 126 k MACs, plus about 30 operations per
  sample (`|z|`, sqrt, divide, 3-way max, the shift). That is about **0.2 ms per 10.7 ms
  block**, roughly 2 % of the loop. Confirm with `/timing-measurement`.
- **Memory:** two FIR state pairs of (256+122) floats, two 64-sample complex delay lines and a
  123-tap coefficient table: about 7 KB in `TransmitFilterConfig` (`SDT.h:627`).
- **Stack:** keep the scratch buffers static, not on the stack. [[memory-layout-and-stack]]
  records the reboot loop that the 16 KB VLA in `ZoomFFTExe` caused.

## Tests

**Unit (gtest)**, in `code/test/TransmitChain_test.cpp`, written in the style of
`MicTxChain()` (`:1181`) and `TXToneAmplitude` (`:1010`):
- **Transparency:** in-band tones below the ceiling come through within ±0.5 dB.
- **Ceiling:** the peak envelope stays ≤ 1.02 × ceiling for a two-tone input and for a
  synthetic voice-like signal at 6 dB drive.
- **Centring:** the opposite-sideband two-tone product (−500 Hz) is below −55 dBc at 6 dB
  drive.
- **Bypass:** in `DIGITAL_TRANSMIT` and both calibration states the output is bit-identical
  to the chain without the stage.
- **`TXGain` move:** neutral with CESSB bypassed.
- **Rate independence:** the passband and ceiling hold at 176.4 and 192 ksps.
- **`A_ref`:** a 0.08 cal tone through the real stages has envelope exactly 1.0 at the
  ceiling.
- **Mic gain:** a negative `currentMicGain` no longer reaches `micGain()` as a wrapped
  unsigned value.

**Model (Python):** `code/tools/cessb_model/test_cessb_model.py` pins the model to the
firmware. Its −3 dB corner must match the gtest's to within 5 Hz; it does to about 1 Hz.

**Bench (Phase 3):**
- **CESSB HIL test.** Play `W9GR48.WAV` and a two-tone signal from the AD2's AWG into the
  mic, and capture exciter I/Q as `tx_filter_hil` does. Measure PAPR, PEP against the
  power setting, opposite-sideband level and out-of-channel energy, with the processor
  off and at drives 0/3/6 dB.
- **RF.** Use the CMU200 spectrum for splatter (two-tone IMD at the antenna, with the
  PA's own IMD in the loop) and its average and peak readings for the talk-power gain at
  equal PEP. This is where overshoot regrowth in the analog exciter and PA filters
  shows up. The model cannot see that regrowth.
- **Existing suites.** Run `tx_filter_hil` and `/transmit-test` with CESSB fully bypassed.
  Both assume a linear chain.
- **On air.** Get A/B reports at a fixed PEP.

## Phase 1 as built

**Files:**
- `DSP_CESSB.cpp/.h` (new): the stage.
- `DSP.cpp`: `TransmitProcessing()` and `TXGain()`.
- `DSP_FFT.cpp`: `InitializeTransmitFilters()` calls `InitializeCESSB()`.
- `MainBoard_DisplayMenus.cpp`: the Audio Options item.
- `Storage.cpp`: the setting.
- `MainBoard_AudioIO.cpp`: `MicGainForCodec()`.
- `SDT.h`: the `ED.cessbEnabled` field.
- Tests: `TransmitChainCESSB.*` in `code/test/TransmitChain_test.cpp`.

**Order in `TransmitProcessing()`:**
`… TXInterpolateBy2Again → CESSBProcess (if CESSBActive()) → TXGain (I and Q) → IQ correction …`

Where the build departs from the design above:

- **The menu toggle is under Audio Options, and "disable" means bypass** (the user's
  decision). The item's label states the action, "Enable CESSB" or "Disable CESSB", as the
  ATU menu's toggles do. When disabled, the stage does not run at all; the "limiter at 0 dB
  drive" idea in section 4 is not used. `ED.cessbEnabled` defaults to `false` and persists
  as `cessbEnabled`.
- **There is no separate drive control yet.** Microphone gain sets how hard the voice hits
  the ceiling. A processor-level setting, the meter and CAT `PR`/`PL` remain Phase 2.
- **The channel is on *negative* frequencies at the node.** CMSIS FIR functions take their
  coefficient tables time-reversed. That leaves the symmetric lowpasses unchanged, but it
  mirrors the ±45° Hilbert pair, so before `SidebandSelection()` the audio sits on the LSB
  side for either sideband. That fits the firmware's own remark that the maths selects LSB
  by default. Hence `CESSB_CENTRE_HZ = −1500`. The Phase 0 model had used `lfilter`
  without reversal and so was mirrored; it found the side by experiment, so its numbers
  were unaffected, and it now reverses the taps too. The first firmware build shifted the
  wrong way and filtered the voice away. The test
  `ChannelIsWhereTheStageCentresIt` now pins the side.
- **The filter is made exactly symmetric.** `CalcFIRCoeffs` centres an odd-length lowpass
  on a half sample (taps 61 and 62 are equal for 123 taps). That would misalign the
  overshoot controller's whole-sample delay line by half a sample. The fix is to design
  124 taps and drop the first, which is 44 dB below the peak. The model does the same.
  Its numbers barely moved: −71.0 against −71.3 dBc.
- **The ceiling is `CESSB_CEILING = 0.1295`.** That is the node envelope of the 0.08,
  800 Hz calibration tone with the default equaliser: 0.12955 at 192 ksps and 0.12949 at
  176.4 ksps. `CeilingIsTheCalibrationTone` pins it to within 1 %.
- **The mic gain is clamped.** `MicGainForCodec()` clamps to 0–63 dB before `micGain()`,
  at both call sites.

**gtest results, `TransmitChainCESSB.*`** (12 tests):
- Two-tone at 6 dB drive: node peak 1.91 → 0.953 × ceiling, and 0.955 × ceiling at the DAC
  rate.
- Two-tone intermod: 2f₁−f₂ at −62.7 dBc and 2f₂−f₁ at −29.2 dBc. The model gives −62.5
  and −28.6.
- Unity gain within ±0.5 dB below the ceiling at 400–2500 Hz, at both rates.
- Moving `TXGain` changes the output by less than 1e-5.
- The bypass states are respected.
- History is cleared after a skipped block.
- The menu toggle, persistence, the default (off) and the mic-gain clamp all behave as
  intended.

## Phase 3: bench results (exciter I/Q, 2026-11-18)

**Suite:** `code/tools/cessb_hil/`, run on the KI3P prototype.
- The AD2 drives the mic input with repeatable periodic signals.
- Ch1 and Ch2 capture the exciter's I and Q.
- CAT `PR` switches CESSB off and on at every point.
- Conditions: 176.4 ksps, 5 W, mic gain 5 dB.
- Two full runs gave the same numbers to within 0.01 dB on the ceiling and 0.01 dB on
  the voice-stand-in gain. 43/43 checks pass.

| Result | Bench | Model / firmware |
|---|---|---|
| Ceiling, single tone, held over +9 dB of overdrive | flat to 0.002 dB | flat |
| Ceiling follows the power setting (5 → 2 W) | −3.97 dB | −3.98 dB |
| Unity gain below the ceiling (1 kHz, 50 mV) | +0.10 dB | passband ripple |
| Voice stand-in (syllable "ah"), talk-power gain at 3 / 6 / 9 dB drive | +2.37 / **+4.11** / +5.24 dB | +2.57 / 4.08 / 5.15 |
| Steady vowel "ah", same | +1.94 / 3.22 / 4.02 dB | +2.06 / 3.17 / 3.64 |
| Peak with CESSB on, all stimuli and drives | ≤ 1.031 × ceiling | ≤ 1.014 (model stops at the DAC) |
| Peak with CESSB off, same drives | up to 2.97 × | — |
| Out of channel (3.3–6 kHz) with CESSB on | −47 to −58 dBc | — |
| Two-tone 2f₁−f₂ (opposite SB) at 6 dB | −48 to −50 dBc | −55.8 dBc (gtest at 176.4k) |

What the results show:
- **Peak regrowth after the DAC is small.** The codec and analog exciter add about 1.5–2 %
  of peak over the model's figure. That answers open question 3 at the exciter; the PA and
  its filters remain unmeasured.
- **The talk-power gain is as modelled.** The voice stand-in comes within 0.2 dB of the
  model at 6 and 9 dB of drive.
- **Not resolvable on the bench:** the opposite sideband. The exciter's own image of a tone
  moves between about −40 and −63 dBc from one capture to the next, at every frequency,
  CESSB on or off. That is a property of the radio, logged in [[documentation-todos]].
  CESSB's contribution is −57 to −100 dBc (model) and the stage removes images (gtest
  `DoesNotImageTheChannel`, −114 dBc). The 2f₁−f₂ reading is a fixed bench spur at −500 Hz
  (−50 dBc at 0 dB drive, CESSB on or off) added to the DSP's −55.8 dBc.
- **Found on the way:**
  - The DSP's 2f₁−f₂ is 7 dB worse at 176.4 ksps than at 192 ksps (−55.8 against −62.7
    dBc), in both the firmware and the model. `OppositeSidebandStaysClean` now checks both
    rates.
  - The AD2 is too coarse at a few millivolts (±1 dB between captures at 5 mV). The suite
    ignores levels below 20 mV for the transparency check.

## Phases

| Phase | Work | Done when |
|---|---|---|
| 0 | Offline model, variant choice, parameters (this branch) | ✅ `results/results.md` |
| 1 | Fix mic gain; `DSP_CESSB.cpp`; filter design in `InitializeCESSB`; `TXGain` move; ceiling; bypass gating; Audio Options on/off toggle with storage; gtests | ✅ 2026-11-18, see [Phase 1 as built](#phase-1-as-built) |
| 2 | Processor-level (drive) setting, meter, CAT `PR`/`PL`, wiki [[cat-control]] | `PR` ✅ 2026-11-18 (needed by the bench suite); the level setting, `PL` and the meter remain |
| 3 | Bench and RF validation; tune the default drive and `gainCompensate` | Exciter I/Q ✅ 2026-11-18 (see Phase 3 above); RF (PA splatter, PEP at the antenna) and on-air still to do |
| 4 | User docs; wiki [[dsp-chain]] update; close #35 | — |

## Open questions

1. **On by default?** The recommendation is enabled, at a moderate drive. Even at 0 dB drive
   it replaces q15 splatter with a clean PEP limit. It does change the on-air sound for
   existing users. Should KI3P decide?
2. **`A_ref` and the equaliser.** The SSB power calibration tone passes through `BandEQ`, so
   the calibration includes the EQ gain at 800 Hz *at the time it was run*. Should `A_ref`
   follow the current EQ (compute that cell sum at 800 Hz when filters are initialised), or
   be taken as flat?
3. **Regrowth after the DAC.** The SGTL5000 reconstruction filter, the exciter's analog
   stages and the PA low-pass filters are not linear-phase. Only the bench can say how much
   of the 1.3 % comes back. If it is a lot, the remedy is a lower ceiling (a margin) rather
   than more DSP.
4. **EQ before the clipper.** The proposal keeps `BandEQ` before CESSB, as Hershberger
   recommends: shape first, then control peaks. EQ boost then increases clipping. Is that
   acceptable as documented behaviour?
5. **AM.** Phoenix has no AM transmit (the transmit states are SSB, CW and digital), so
   CESSB is SSB-only.
6. **`gainCompensate` at 2.2.** It gives lower overshoot (0.8 %) for the same power in the
   model. Should it be the default, pending the bench?
