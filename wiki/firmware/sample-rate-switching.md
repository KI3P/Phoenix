---
title: Runtime Sample-Rate Switching (192 / 176.4 ksps)
type: decision
status: draft
created: 2026-07-29
updated: 2026-07-30
tags: [sample-rate, dsp, i2s, menu, cat, persistent-config, 176k, 192k]
source_refs: []
related: ["[[digital-mode]]", "[[runtime-filter-design]]", "[[multirate-decimation]]", "[[audio-io]]", "[[persistent-config]]", "[[tune-frequency-control]]", "[[cat-control]]", "[[ui-state-machine]]", "[[display-subsystem]]", "[[zoom-fft]]", "[[theory-overview]]"]
---

# Runtime Sample-Rate Switching (192 / 176.4 ksps)

The raw ADC/DAC rate is now **selectable at run time and persisted**, where it used to be a
compile-time constant in all but name. Two rates are offered: **192 ksps** (the historical
rate) and **176.4 ksps**. Landed on `rx-dsp-176k-stage-test`, merged for V1.4.0.

This page covers the *mechanism*. The filters that had to be made rate-independent before it
could work are in [[runtime-filter-design]]; the bench verification is in
[[filter-hil-test]].

## What changed, in one line

`SampleRate` used to be a plain `uint8_t` written once at startup. It is now a **reference into
the persisted config**:

```c
// Globals.cpp:106-109
uint8_t& SampleRate = ED.sampleRate;
```

with `ED.sampleRate` defaulting to `SAMPLE_RATE_192K` (`SDT.h:287`) and declared `extern uint8_t&
SampleRate` (`SDT.h:735`). Every one of the many `SR[SampleRate].rate` call sites keeps working
unmodified while the value moves into [[persistent-config]] and gets serialized to JSON
(`Storage.cpp:69`, restored at `:278` and `:504`).

This supersedes the wiki's earlier claim that `SampleRate` is "set once and **never reassigned**
anywhere in the firmware" — see [[multirate-decimation]], corrected 2026-07-29.

## `ChangeSampleRate()` — the switch itself

`MainBoard_AudioIO.cpp:519`. Order matters throughout:

1. **Bail if unchanged** (`:520`) — the whole operation is expensive and audibly disruptive.
2. **Compensate the tuning, before `SampleRate` is updated** (`:530-532`). The RX VFO is
   programmed to `centerFreq_Hz` and the receive DSP shifts by +Fs/4, so the received frequency
   is `centerFreq_Hz − Fs/4`. Changing Fs moves that by `ΔFs/4` — 3.9 kHz between these two
   rates — so `centerFreq_Hz` is shifted by the same amount to hold the dial frequency
   constant. Applied to **both VFOs** so a later VFO switch is also correct. This must be
   computed while `SR[SampleRate]` still names the *old* rate.
3. **`AudioNoInterrupts()`** (`:534`), then assign `SampleRate`.
4. **`SetI2SFreq()`** (`:537`) reprograms the SAI1 PLL divider chain for the new rate.
5. **`InitializeSignalProcessing()`** (`:541`) rebuilds the entire DSP chain — all three
   `ReceiveFilterConfig` instances plus the transmit one (`DSP.cpp:758-762`), which is what
   regenerates the rate-dependent coefficients ([[runtime-filter-design]]) and re-derives AGC
   and CW parameters.
6. **`UpdateSampleRateDependentOscillators()`** (`:503`) retunes the sidetone and the TX-IQ-cal
   tone. Both are OpenAudio synthesized oscillators whose `frequency()` argument is scaled by
   `AUDIO_SAMPLE_RATE_EXACT / SR[SampleRate].rate`, so without this they shift audibly.
7. **Flush the four input queues** (`:545-548`) so samples captured at the old rate are not
   processed at the new one, then `AudioInterrupts()`.
8. **`UpdateTuneState()`** (`:554`) reprograms the Si5351 for the compensated centre frequency.
   Deliberately *outside* the interrupts-disabled window, because it does I²C transactions.

### The leak that had to be fixed first
`InitializeDecimationFilter` `malloc`s its coefficient and state buffers. At boot-only
initialization nobody noticed it never freed; called repeatedly on rate changes it would leak
every time. It now frees the previous allocation first — a prerequisite for step 5 being
callable more than once.

## Ways to reach it

| Path | Where | Notes |
|---|---|---|
| Front panel | new **"Sample Rate"** primary menu, `MainBoard_DisplayMenus.cpp:580-611` | Two options, "192 ksps" / "176.4 ksps". `primaryMenu[]` grew 8 → **9** entries (`:602`, `MainBoard_Display.h:371`). |
| CAT | `SR0;` = 176.4k, `SR1;` = 192k; `SR;` reads back | `CAT.cpp:801-838`. **Rejected while transmitting** — see [[cat-control]]. |
| Boot | automatic | `InitializeStorage()` runs before `InitializeSignalProcessing()`/`InitializeAudio()` (`Globals.cpp:528-533`), so the restored rate is simply in force by the time the DSP is built. There is no "apply saved rate" step. |

## Display paths that assumed 192 kHz

Three places had the old rate baked in and were rewritten to derive from `SR[SampleRate].rate`:

- **Spectrum frequency ticks.** Previously a fixed pixel table with a non-round frequency step.
  Ticks are now positioned by `FreqToBin()` at *round* frequencies
  (`MainBoard_DisplayHome.cpp:550-571`), so labels stay aligned with the trace at any rate. The
  clear strip was widened so old ticks are wiped on zoom/tune changes.
- **Audio spectrum axis.** Span is now `SR[SampleRate].rate / 32` (`:1301-1303`) rather than a
  hard-coded 0–6000 Hz — 6.0 kHz at 192 ksps, 5.5 kHz at 176.4 ksps. Without this, tone
  frequencies read wrong at the new rate.
- **Settings pane** gained a `"Rate:"` row (`:1654`); Key Type moved down one.

See [[display-subsystem]]. [[zoom-fft]]'s bin widths are likewise rate-dependent and its
"375 Hz base bin" figure is now a 192-ksps-only number.

## Simulator

The OpenAudio mock was made rate-aware: test-tone carrier at Fs/4, sample pacing and SDL output
rate derived from the current rate, and SDL audio re-initialized when the rate changes
(`code/test/OpenAudio_ArduinoLibrary_mock.cpp`, `RadioSimulator_main.cpp`).

## The 176.4 ksps rate

What is verifiable from the code: 176400 = 44100 × 4, so after the `÷8` RX decimation
([[multirate-decimation]]) the audio rate is **22.05 ksps** rather than the 24 ksps of the
192 ksps path — i.e. it lands on the 44.1 kHz family instead of the 48 kHz one. Both rates are
marked `// OK` in the `SR[]` table (`Globals.cpp:79-98`); most of the other 16 entries are
marked `NOT OK` and are not offered by the menu.

⚠️ **The motivation is not recorded anywhere in the tree** — not in the commit message
(`9c8bb16`), not in `code/docs/RX_DSP_Chain_Parameters.md`. Host-side resampling
([[usb-audio]]) is the obvious guess but is a guess; see Open questions.

## Open questions
- **Why 176.4 ksps was added.** Owner question — the tree does not say. If it is USB-audio
  resampling, that connection belongs on [[usb-audio]].
- Whether a rate change during CW keying or with the AGC hung can produce an audible artefact
  beyond the intended brief mute — `ChangeSampleRate` is only interlocked against *transmit*
  over CAT, not over the menu.
- Whether `ED.sampleRate` restored from a config written by an older firmware (no `sampleRate`
  key) reliably falls back to 192 ksps. The `doc["sampleRate"] | ED.sampleRate` idiom
  (`Storage.cpp:278`) says yes, but it has not been tested against a real V1.3 config file.
- Group delay and loop-budget impact at 176.4 ksps versus 192 ksps ([[real-time-constraints]]).
