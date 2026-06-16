---
title: "Dev: Change the OpenAudio Library (#14)"
type: roadmap
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [development, proposal, openaudio, audio-quality, dsp, architecture]
source_refs: []
related: ["[[development-backlog]]", "[[dsp-chain]]", "[[usb-audio]]", "[[code-heritage]]", "[[real-time-constraints]]"]
---

# Dev: Change the OpenAudio Library (#14)

**GitHub issue:** [KI3P/Phoenix#14](https://github.com/KI3P/Phoenix/issues/14) — *"Proposal:
Change Open Audio library"*

> Goal (from the issue): adopt a technique to **improve audio quality** by changing the OpenAudio
> library usage.
> Ref: <https://groups.io/g/SoftwareControlledHamRadio/message/35852>

Scoping note — a proposal to evaluate, not a solution design.

## Current state — OpenAudio underpins the whole audio chain

- The real-time audio I/O and block scheduling rest on the **OpenAudio_ArduinoLibrary**
  (`MainBoard_AudioIO`, [[dsp-chain]]) — it provides the 48/96/192 kHz block pipeline the DSP
  chain consumes, and the update ISR whose NVIC priority (208) the front-panel servicing is
  deliberately kept below ([[front-panel]], [[real-time-constraints]]).
- This is foundational, multi-author heritage code ([[code-heritage]]); a change here touches
  buffering, sample rates, and timing assumptions across the DSP chain.

## What remains to be done (to scope)
- Read the referenced groups.io technique and capture **what specifically** it changes (a
  different library? a different OpenAudio configuration? a new block size / sample format?).
- Assess blast radius: which DSP stages, buffer sizes (`READ_BUFFER_SIZE`), and the 10 ms loop
  budget ([[real-time-constraints]]) are affected.
- Relationship to USB audio ([[usb-audio]] #13): a library change may be a prerequisite or
  enabler — sequence the two together.
- Define how "audio quality" improvement will be **measured** (the in-tree
  `Transmit_Audio_Purity_Investigation.md` may be relevant).

## Risk
Highest-risk / highest-blast-radius of the four open issues — it sits under everything in
[[dsp-chain]]. Worth a careful evaluation page before any implementation.

## Relevant code & wiki
`MainBoard_AudioIO.cpp/.h`, the OpenAudio library; [[dsp-chain]], [[usb-audio]],
[[real-time-constraints]], [[code-heritage]].
