---
title: AGC Design (wdsp look-ahead AGC)
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-14
tags: [agc, gain-control, attack, decay, hang, look-ahead, wdsp]
source_refs: []
related: ["[[theory-overview]]", "[[dsp-chain]]", "[[noise-reduction]]", "[[persistent-config]]"]
---

# AGC Design (wdsp look-ahead AGC)

Automatic Gain Control keeps audio output roughly constant as received signal strength
varies over tens of dB. Phoenix uses the **wdsp** AGC (Warren Pratt NR0V's DSP library,
ported via DD4WH — [[code-heritage]]); the struct comment says so outright (`SDT.h:589`) and
the variable names match wdsp exactly. It's a **look-ahead** AGC with a hang state machine,
not a simple envelope-times-gain multiplier.

Implementation: `AGCConfig` (`SDT.h:588`), `InitializeAGC()` (`DSP.cpp:238`), `AGC()`
(`DSP.cpp:313`). It runs at the decimated 24 kHz rate (`DSP.cpp:750`), positioned
**after the channel filter but before demodulation**:
`ConvolutionFilter → AGC → Demodulate` (`DSP.cpp:911-919`). So it acts on the **complex,
single-sideband-filtered baseband**, and its level detector uses the true analytic-signal
magnitude `sqrt(I²+Q²)` — the instantaneous envelope — which is exactly the right quantity to
ride gain on. [[noise-reduction]] runs *later*, on the demodulated audio.

## The pieces of a good AGC *(general DSP)*

1. **Envelope follower.** Track the signal magnitude with a one-pole leaky integrator. Each
   time constant `τ` maps to a per-sample coefficient `α = 1 − exp(−1/(fs·τ))` — exactly the
   form used throughout `InitializeAGC` (`DSP.cpp:275-299`).
2. **Fast attack, slow decay.** Gain must drop *instantly* when a strong signal arrives (or
   it clips), but recover *slowly* (or the background "pumps"). Hence `tau_attack = 1 ms` vs.
   `tau_decay = 50 ms … 2 s` depending on mode (`SDT.h:590-591`).
3. **Look-ahead.** If you compute gain from the signal you're *outputting*, attack is always
   one step late → an overshoot transient escapes. wdsp delays the audio in a **ring buffer**
   so the gain control can "see" a peak *before* it reaches the output and clamp it with zero
   overshoot. This is the defining feature of this AGC.
4. **Hang.** After a signal stops (between syllables, between CW elements), hold the gain for
   a "hang time" instead of immediately ramping up — otherwise the noise floor roars back
   between words. Engaged only above `hang_thresh`.

## How Phoenix implements it

- **Delay-line / look-ahead.** A complex `ring` buffer with `out_index` lagging `in_index` by
  `attack_buffsize = ceil(fs · n_tau · tau_attack)` samples (`DSP.cpp:273-274, 331-340`). The
  output is the *delayed* sample; the gain is derived from the **peak over the look-ahead
  window** `ring_max` (`DSP.cpp:350-363`). With `n_tau = 4`, `tau_attack = 1 ms`, fs = 24 kHz,
  the look-ahead is `ceil(24000·4·0.001) = 96 samples` (~4 ms = 4 attack time-constants).
  `n_tau` also corrects the target for the finite-buffer settling: `out_target =
  out_targ·(1 − e^(−n_tau))·0.9999` (4 τ ⇒ within 1.8 % of `out_targ`).
- **`n_tau` vs `MAX_N_TAU`.** `MAX_N_TAU` is **not a live symbol** — it appears only as a
  commented note `// #define MAX_N_TAU (8)` (with `MAX_SAMPLE_RATE 24000`, `MAX_TAU_ATTACK
  0.01`, `SDT.h:615-617`). Those three are the worst-case constants the static ring is
  hand-sized for: `ring_buffsize = 24000·8·0.01 + 1 = 1921` samples (`SDT.h:618`). The ring is
  thus over-provisioned ~20× vs the operating 96-sample look-ahead, so `n_tau`, `tau_attack`, or
  fs can be raised up to those maxima without reallocating. The *operating* value is `n_tau = 4`.
- **Envelope magnitude.** `pmode = 1` → true magnitude `sqrt(I² + Q²)` of the complex sample
  (`DSP.cpp:343-345`); `pmode = 0` would use the cheaper max(|I|,|Q|).
- **Two back-averages.** `fast_backaverage` and `hang_backaverage` (leaky integrators,
  `DSP.cpp:347-348`) feed the decay/hang decisions.
- **State machine** (`DSP.cpp:368+`): state 0 attack, plus fast-decay, hang, and normal-decay
  branches. A sudden drop larger than `pop_ratio × fast_backaverage` triggers a **fast-decay**
  state (transient/"pop" handling, `DSP.cpp:374-376`); otherwise it either **hangs**
  (`hang_backaverage > hang_level`) or does **normal decay**.
- **Max gain** comes from the per-band threshold: `max_gain = 10^(AGC_thresh/20)`
  (`DSP.cpp:272`) — `AGC_thresh` lives per band in the `band` struct ([[persistent-config]]).

## The gain law / soft-knee transfer curve

Per output sample the gain multiplier is a single expression (`DSP.cpp:454`):
```c
mult = (out_target - slope_constant * fmin(0.0, log10f_fast(inv_max_input * volts))) / volts;
```
where `volts` is the look-ahead peak envelope (`ring_max`-tracked), clamped to a floor of
`min_volts` (`DSP.cpp:447-448`). The init constants (`DSP.cpp:282-291`, defaults `SDT.h:594-596`:
`out_targ = max_input = 1.0`, `var_gain = 1.5`):
- `out_target = out_targ·(1 − e^(−n_tau))·0.9999 ≈ 0.9816` — the target output amplitude.
- `min_volts = out_target / (var_gain·max_gain)` — input floor below which AGC stops acting.
- `slope_constant = out_target·(1 − 1/var_gain) / log10(out_target / (max_input·var_gain·max_gain))`.

Because the *output* amplitude is `volts·mult`, the multiply by `volts` cancels and the curve
becomes clean in three regions (input envelope → output amplitude):

| Region | Input `volts` | Output `volts·mult` | Behaviour |
|---|---|---|---|
| **Limiting** | ≥ `max_input` (1.0) | `out_target` (flat) | `fmin` clamps the log term to 0 → output pinned at `out_target` regardless of input. Full compression. |
| **Soft knee** | `min_volts … max_input` | `out_target − slope_constant·log10(volts/max_input)` | **Linear-in-log(input)**: output falls by `slope_constant` per decade of input. |
| **Linear / max-gain** | < `min_volts` | `volts·max_gain` | `volts` clamped to `min_volts` → constant gain = `max_gain`; AGC inactive (`agc_action = 0`). |

The knee is continuous: at `volts = min_volts` the soft-knee formula gives output
`= out_target/var_gain` and gain exactly `= max_gain`; at `volts = max_input` it gives
`out_target`. So **`var_gain` is the wdsp "slope" control**: the total output rise across the
whole active range is `out_target·(1 − 1/var_gain)`. With `var_gain = 1.5` that is
`0.9816·0.333 ≈ 0.327`, i.e. output climbs from `out_target/1.5 ≈ 0.654` at the max-gain point
to `≈ 0.982` at the limiting threshold — about **+3.5 dB** of deliberate output variation across
the AGC's range (a gentle knee, not a hard limiter). `var_gain = 1` would zero `slope_constant`
→ a hard knee (flat output right down to `min_volts`). `max_gain = 10^(AGC_thresh/20)` sets the
floor gain and hence the knee position via `min_volts`.

## User-facing modes (`InitializeAGC`, `DSP.cpp:243-270`)

| `ED.agc` | hangtime | tau_decay | hang_thresh |
|---|---|---|---|
| `AGCOff` | — | — (fixed gain ×20, `DSP.cpp:323`) | — |
| `AGCLong` | 2.0 s | 2.0 s | — |
| `AGCSlow` | 1.0 s | 0.5 s | — |
| `AGCMed` | 0.0 s | 0.25 s | — |
| `AGCFast` | 0.0 s | 0.05 s | 1.0 |

`AGCOff` bypasses the whole machine and applies a constant `fixed_gain = 20.0`.

## Ordering in the RX chain
`ConvolutionFilter → AGC → Demodulate → BandEQ → NoiseReduction` (`DSP.cpp:911-927`). AGC
acts on the **complex filtered signal before demodulation**, so it sees the clean
analytic-signal envelope rather than post-demod audio that NR/EQ have already reshaped. (My
earlier draft said NR ran before AGC — that was wrong; corrected 2026-06-08.)

## Open questions
- Whether `AGC_thresh` is exposed live on the front panel / menus and its dB range.

## Resolved
- **Soft-knee transfer curve.** Documented above: three regions (flat limiting ≥ `max_input`;
  linear-in-log soft knee `min_volts…max_input` falling `slope_constant`/decade; constant
  `max_gain` below `min_volts`). `var_gain` is the slope knob — total output rise across the
  range = `out_target·(1 − 1/var_gain)` ≈ +3.5 dB at `var_gain = 1.5`. *(resolved 2026-06-14)*
- **`n_tau` vs `MAX_N_TAU`.** `MAX_N_TAU = 8` is a commented-out worst-case constant (not used
  at runtime) baked into the static `ring_buffsize = 24000·8·0.01+1 = 1921` allocation; the
  operating `n_tau = 4` gives a 96-sample (~4 ms) look-ahead at 24 kHz. *(resolved 2026-06-14)*
