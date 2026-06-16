---
title: CW Processing (audio filter + Morse decoder)
type: module
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [cw, morse, decoder, goertzel, sidetone, audio-filter, adaptive]
source_refs: []
related: ["[[overview]]", "[[dsp-chain]]", "[[mode-state-machine]]", "[[tune-frequency-control]]", "[[rf-board]]", "[[display-subsystem]]", "[[persistent-config]]", "[[audio-io]]"]
---

# CW Processing (audio filter + Morse decoder)

**Files:** `DSP_CWProcessing.cpp` (~24 KB), `DSP_CWProcessing.h`.

## Where this fits in the CW path
CW (Morse) spans the whole radio; this module is the **receive-side audio** half:

- **Transmit** — the iambic/straight keyer is in [[mode-state-machine]]; it keys the CW VFO via
  `CWon`/`CWoff` on the [[rf-board]] at the tone-offset frequency from [[tune-frequency-control]]
  (`GetCWTXFreq`). Sidetone is generated here (below).
- **Receive** — *this module*: a narrow **CW audio filter** for SNR, plus an **adaptive Morse
  decoder** that turns the received tone into text. Invoked from `ReceiveProcessing` only in the
  `CW_RECEIVE` state (`DSP.cpp:935`, [[dsp-chain]]): `DoCWReceiveProcessing()` then
  `CWAudioFilter()`.

## CW audio filter — `CWAudioFilter()`

A narrow bandpass on the demodulated audio, selected by `ED.CWFilterIndex`
([[persistent-config]]) from five precomputed biquad cascades (`arm_biquad_cascade_df2T_f32`
over `S1_CW_AudioFilter1..5`, the cascades defined in `ReceiveFilterConfig`):

| `CWFilterIndex` | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| width (code label) | 0.8 kHz | 1.0 kHz | 1.3 kHz | 1.8 kHz | 2.0 kHz | Off |

Narrowing the audio passband around the CW tone dramatically improves SNR against adjacent
signals — the receive counterpart to picking a CW filter on an analog rig.

## Tone detection — `DoCWReceiveProcessing()`

Decides mark (tone present) vs. space using a **hybrid** of two detectors and feeds the
envelope to the decoder:
- **Correlation** against an internally generated reference sine (`aveCorrResult`).
- **Goertzel magnitude** at the configured CW tone `CWToneOffsetsHz[ED.CWToneIndex]`
  (`goertzel_mag`).
- Combined: `combinedCoeff = 10 * aveCorrResult * 100 * goertzelMagnitude` (`DSP_CWProcessing.cpp:109`),
  thresholded into the 0/1 audio value passed to `DoCWDecoding`.

### Goertzel algorithm *(general DSP)*
`goertzel_mag(N, f, fs, data)` computes the energy at a **single** frequency far more cheaply
than a full FFT — an IIR recurrence tuned to bin `f`, ideal for "is the CW tone present?"
detection.

## Adaptive Morse decoder — `DoCWDecoding()`

A timing state machine (`MorseStates`) that converts the mark/space envelope into characters,
**adapting to the operator's sending speed** rather than assuming a fixed WPM.

### Standard Morse timing *(general DSP, also in the code comments)*
`dit = 1 unit, dah = 3, inter-element = 1, inter-letter = 3, inter-word = 7`;
`ditLength = 1200 / WPM` via `SetDitLength()`. dah-vs-dit and letter/word boundaries are
distinguished by comparing measured durations against (adaptive) multiples of `ditLength`
(e.g. inter-element gap > `1.95·ditLength` ends a character, > `4.5·ditLength` is a word gap).

### Adaptive timing
Rather than trust a fixed threshold, the decoder builds **histograms** of observed mark
durations (`DoSignalHistogram`) and gap durations (`DoGapHistogram`, `HISTOGRAM_ELEMENTS = 750`
ms-resolution bins), and uses `JackClusteredArrayMax` to find the dominant dit/dah/space
clusters. A running `thresholdGeometricMean` separates dit from dah and tracks speed changes;
`ADAPTIVE_SCALE_FACTOR = 0.8` ages old observations. `IsCWDecodeLocked()` reports whether the
timing statistics have converged. Fastest supported speed: `LOWEST_ATOM_TIME = 20 ms` (≈60 WPM).

### Decoding via a flat Morse tree
Characters are looked up by walking a **binary tree stored as a flat string**
(`bigMorseCodeTree`, `DSP_CWProcessing.cpp:49` — `"-EISH5--4--V…"`, the classic pre-order Morse
layout). Starting at index 0 with a jump of `DECODER_BUFFER_SIZE = 128`, each element halves the
jump and branches (`DSP_CWProcessing.cpp:289-307`):
```
dit:  currentDecoderIndex += 1;            // left child
dah:  currentDecoderIndex += currentDashJump;  // right child
both: currentDashJump >>= 1;               // (applied per element)
```
At the end of a character it emits `bigMorseCodeTree[currentDecoderIndex]` and resets. Decoded
text accumulates in a buffer; `GetMorseCharacterBuffer()` / `IsMorseCharacterBufferUpdated()`
let the [[display-subsystem]] refresh only when new characters arrive. Decoding is gated by
`ED.decoderFlag`.

## Sidetone (transmit)
`InitializeCWProcessing()` also precomputes the **CW sidetone** phase
(`phs = 2π·freqSideTone / 24000`, into `sinBuffer`) so the operator hears their own keying;
volume is `ED.sidetoneVolume`.

## Open questions / discrepancies
- **Filter-width mismatch:** the header doc says "very narrow filter (50–500 Hz typical)" but
  the code labels the five filters **0.8–2.0 kHz**. Reconcile (audio passband vs. RF bandwidth?).
  → [[documentation-todos]]
- The empirical scaling in `combinedCoeff` (`10 × … × 100 ×`) and the correlation-vs-Goertzel
  weighting — document the tuning rationale.
- Confirm the sample rate the decoder/Goertzel operate at (24 kHz RX path vs the 12 kHz the
  sidetone phase implies).
