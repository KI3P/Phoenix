---
title: Code Heritage & Multi-Author History
type: decision
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [history, heritage, licensing, contributors]
source_refs: []
related: ["[[overview]]"]
---

# Code Heritage & Multi-Author History

Phoenix is not a from-scratch codebase — it is the latest fork in a long amateur-radio
lineage. Understanding the heritage explains stylistic inconsistencies, duplicated idioms,
and the mix of generated and hand-written code.

## Lineage

> "This code base is originally derived from the **Teensy Convolution SDR**, copyright
> Frank Dziock DD4WH, substantially modified by the contributors listed below."
> — `code/src/PhoenixSketch/Contributors.txt`

Origin: <https://github.com/DD4WH/Teensy-ConvolutionSDR>

The "Convolution SDR" name reflects the original DSP approach: filtering by FFT
convolution. Much of the spectrum/FFT machinery in [[dsp-chain]] traces back to this root.

The intermediate ancestor is the **T41-EP** ("Experimenter's Platform") SDR, documented in
the Al Peter (AC8GY) & Jack Purdum (W8TEE) book — a theory source worth ingesting into the
`theory/` area when available (see [[theory-overview]]; no e-copy currently exists, so the
theory pages are reconstructed from code + DSP fundamentals).

## Known contributors

From `Contributors.txt`:

- Oliver King, KI3P *(current Phoenix maintainer)*
- Jack Purdum, W8TEE *(T41-EP book co-author)*
- Al Peter, AC8GY *(T41-EP book co-author)*
- Greg Raven, KF5N
- John Melton, G0ORX
- Jerry Kaidor, KF6VB
- WMXZ
- Frank Dziock, DD4WH *(original Teensy Convolution SDR author)*
- Michael Wild
- David Martin, VK3KQT
- Joerg Uthoff, DB2OO
- Harry Brash, GM3RVL
- Tom Metty, AJ8X

## Licensing

Phoenix is **GPL v3** (or later). Every source file carries the standard GPLv3 header
referencing `Contributors.txt`.

## Implications for the wiki

- **Style heterogeneity** is expected — code reflects many hands across many years.
- The **Phoenix** rework (KI3P-led) introduced the StateSmith-generated state machines and
  the board-abstraction layer ([[rf-board]], [[filter-boards]]), which are newer than the
  DSP core.
- When a design decision looks idiosyncratic, check whether it is inherited from the
  Convolution SDR / T41-EP era versus a deliberate Phoenix choice. Record such findings as
  `decision` pages.
