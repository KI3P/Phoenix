---
title: "Dev: Transmit Spectrum Display (#10)"
type: roadmap
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [development, feature, transmit, spectrum, display, dual-vfo]
source_refs: []
related: ["[[development-backlog]]", "[[dsp-chain]]", "[[zoom-fft]]", "[[tune-frequency-control]]", "[[hardware-state-machine]]", "[[display-subsystem]]"]
---

# Dev: Transmit Spectrum Display (#10)

**GitHub issue:** [KI3P/Phoenix#10](https://github.com/KI3P/Phoenix/issues/10) — *"Feature
request: can we see the transmit spectrum?"*

> Goal (from the issue): run the receive chain in parallel with the transmit chain and plot the
> transmit spectrum while transmitting. Some TX power leaks into the RX chain through the T/R
> switch, so a parallel RX-PSD can show what's being transmitted.

This is a **scoping note** — what exists and what remains — not a solution design.

## Current state — substantially implemented for SSB + dual-VFO

While documenting the DSP and tuning code we found most of this already exists:
- **Parallel RX-PSD during TX:** `PerformSignalProcessing` calls `TransmitReceiveProcessing()`
  in the `SSB_TRANSMIT` state when `HasDualVFOs()` (`DSP.cpp:53-55`). That routine reads the
  IQ input, applies RF gain + I/Q correction, and runs `ZoomFFTExe(...)` to compute the
  display PSD (`DSP.cpp:768-792`) — i.e. a truncated RX chain, PSD only, no audio demod.
- **Dedicated VFO placement:** `HandleTuneState(TuneSSBTX)` retunes the RX VFO to **freq/3** on
  dual-VFO radios specifically "to watch the 3rd harmonic when displaying the transmit
  spectrum" (`HardwareSm.cpp:662-666`, documented in [[tune-frequency-control]]). The
  fundamental leaking through the T/R switch is too strong, so it monitors the 3rd harmonic.
- A separate filter/zoom config (`RXTXfilters`, `RXTXZoom`) exists for this path.

So for the **common case (SSB, dual-VFO)** the TX spectrum is computed during transmit. See
[[dsp-chain]] and [[zoom-fft]].

## What remains to be done (to scope)
- **Verify the display actually renders it** and how — does the spectrum pane
  ([[display-subsystem]]) show the TX PSD distinctly (label, colour, frequency axis) during TX?
- **3rd-harmonic presentation:** since the monitored signal is at freq/3, clarify how the axis
  / readout is presented to the user so it isn't misleading.
- **CW transmit:** the dispatch only covers `SSB_TRANSMIT` (and cal states) — CW TX has no
  TX-spectrum path. Is that wanted?
- **Single-VFO radios:** the whole path is gated by `HasDualVFOs()`; single-VFO hardware can't
  self-monitor this way. Is any fallback desired?
- **Performance:** running a parallel PSD during TX adds load — confirm it fits the
  [[real-time-constraints]] budget.

## Relevant code & wiki
`DSP.cpp` (`TransmitReceiveProcessing`), `HardwareSm.cpp` (`HandleTuneState` TuneSSBTX),
`MainBoard_DisplayHome.cpp` (spectrum pane). Wiki: [[dsp-chain]], [[zoom-fft]],
[[tune-frequency-control]], [[hardware-state-machine]], [[display-subsystem]].

## Open questions
- Is this issue effectively *done* for the main case and just needs confirmation, or is the
  rendering incomplete? (Needs a check on hardware / the display pane.)
