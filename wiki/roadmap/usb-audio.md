---
title: "Dev: Bi-directional USB Audio (#13)"
type: roadmap
status: superseded
created: 2026-06-09
updated: 2026-07-30
tags: [development, feature, usb, audio, digital-modes, openaudio, sample-rate, implemented]
source_refs: []
related: ["[[digital-mode]]", "[[development-backlog]]", "[[dsp-chain]]", "[[openaudio-library]]", "[[cat-control]]", "[[sample-rate-switching]]"]
---

# Dev: Bi-directional USB Audio (#13)

**GitHub issue:** [KI3P/Phoenix#13](https://github.com/KI3P/Phoenix/issues/13) — *"Feature
request: bi-directional USB audio"*

> Goal (from the issue): add the ability to **send and receive an audio stream over USB** — so a
> PC can pipe RX audio in and TX audio out (e.g. for digital modes / WSJT-X), complementing the
> existing CAT control ([[cat-control]]).
> Ref: <https://groups.io/g/SoftwareControlledHamRadio/topic/116487848#msg35710>

Scoping note — not a solution design.

## Status: implemented as [[digital-mode]] (2026-07-30)

**This scoping page is superseded.** The feature landed as a third operating mode alongside
SSB and CW, and both directions have since been verified on the bench. See **[[digital-mode]]**
for the design, the code map, the measurements and what remains open.

Answers to the questions this page raised:

- **USB device composition** — `usb=serialmidiaudio`. Teensyduino has no stock "Dual Serial +
  Audio" type, so CAT moves from `SerialUSB1` to the primary `Serial` (via the new `CATSerial`
  alias) and `Debug()` compiles out. The composite-descriptor difficulty this page anticipated
  was real, and was sidestepped rather than solved.
- **Sample rate / format** — the stock **44.1 kHz** endpoint, unmodified. No Teensy core file
  is shadowed.
- **Resampling** — *none*, and none has to be rebuilt on a rate change. Digital mode forces
  176.4 ksps, where the rates line up exactly.

### Correction to the "possible shortcut" below

The instinct was right but the tap point was wrong. This page reasoned from the **Fs/8** RX
*audio* rate (22.05 kHz at 176.4 ksps, an exact 1:2 to 44.1 kHz). The implementation instead
taps one stage earlier, at **Fs/4** in `InterpolateReceiveData()` — and Fs/4 at 176.4 ksps
**is 44,100 Hz exactly**. So there is no ratio at all, not even 1:2. One DSP block is exactly
512 samples there, and 86.1328 blocks/s × 512 = 44,100 samples/s exactly.

A second coincidence at the same rate does the rest: the AudioStream graph clock (Fs/128 =
1378.125 Hz) is exactly 4× the USB audio block rate (344.53 Hz), which is what lets the stock
`AudioInputUSB`/`AudioOutputUSB` objects be reused behind a simple 4:1 pacer.

Whether 176.4 ksps was *originally* added for this reason remains unrecorded — but it is now
load-bearing for digital mode either way.

### Correction to an in-tree debug doc

`code/docs/USB_Audio_TX_Diagnostic_Plan.md:152-158` claims a 0.27 %/s "structural deficit"
between the DSP and USB sample budgets at 192 ksps. **That is an arithmetic error**: it uses
94 blocks/s where the true rate is 192000/2048 = 93.75, and 93.75 × 512 = 48,000 exactly. The
budget balanced all along, so that deficit explains none of the symptoms recorded on the
abandoned `usb_audio` branch. Do not chase it.

## Historical scoping notes (pre-implementation)

Kept for context on how the problem was framed before it was solved.

### Current state (as of 2026-06-09) — in active debugging, not yet a feature

- The audio codec interface is `MainBoard_AudioIO` ([[dsp-chain]]); a grep finds **no USB-audio
  endpoints** (`AudioInputUSB`/`AudioOutputUSB`) wired in there yet.
- There is substantial **investigation already underway** — several debug docs sit in the
  working tree (`code/docs/USB_Audio_Debug_Guide.md`, `USB_Audio_RX_Diagnostics.md`,
  `USB_Audio_TX_Debug_Report.md`, `USB_Audio_TX_Diagnostic_Plan.md`, `usb_audio_debug_18Jan26.md`,
  `Transmit_Audio_Purity_Investigation.md`). **Start here** for context before designing.
- Teensy USB audio shares the USB device with serial; CAT already uses `SerialUSB1` (Dual
  Serial). USB Audio + Dual Serial composite descriptor interactions are likely part of the
  difficulty being debugged.

## What remains to be done (to scope)
- Consolidate the findings from the in-tree debug docs into a concrete plan (those docs are the
  primary source — read them first).
- Decide the USB device composition (audio + serial coexistence) and sample-rate/format.
- Route USB audio into/out of the DSP chain (RX audio → USB in; USB out → TX modulator),
  including resampling to/from the internal rates. ⚠️ **Those rates are no longer fixed**
  ([[sample-rate-switching]]): [[dsp-chain]] runs RX audio at **24 kHz @192 ksps / 22.05 kHz
  @176.4 ksps**, TX audio at **12 / 11.025 kHz**. Any resampler has to be rebuilt on a rate
  change, the same way the filters are ([[runtime-filter-design]]).
- **Possible shortcut:** at 176.4 ksps the RX audio rate is 22.05 kHz — an exact 1:2 of 44.1 kHz,
  where 24 kHz needs a 147:160 ratio to reach 44.1. Whether that is *why* 176.4 ksps was added is
  unrecorded and an open question for the owner ([[sample-rate-switching]]); if so, this page is
  where the connection belongs.
- Relationship to [[openaudio-library]] (#14): a library change may change the USB-audio path,
  so sequence the two.

## Relevant code & docs
`MainBoard_AudioIO.cpp/.h`; the `code/docs/USB_Audio_*` reports; [[dsp-chain]],
[[openaudio-library]].
