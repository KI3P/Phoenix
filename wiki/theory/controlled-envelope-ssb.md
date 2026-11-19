---
title: Controlled Envelope SSB (CESSB)
type: concept
status: draft
created: 2026-11-18
updated: 2026-11-19
tags: [ssb, transmit, cessb, clipper, speech-processing, papr, dsp]
source_refs: []
related: ["[[cessb]]", "[[ssb-phasing-method]]", "[[iq-quadrature-sampling]]", "[[dsp-chain]]", "[[multirate-decimation]]"]
---

# Controlled Envelope SSB (CESSB)

CESSB is Dave Hershberger's (W9GR) method for raising the **average** power of an SSB voice
signal without raising its **peak envelope power (PEP)**. A transmitter whose PA or exciter
limits PEP then sounds about 3 to 5 dB louder, with no hard clipping in the RF chain and no
added splatter. The whole process is baseband DSP on the complex I/Q signal before the
modulator. The firmware plan is [[cessb]].

**Sources:**
- Hershberger, *Controlled Envelope Single Sideband*, QEX Nov/Dec 2014
- Hershberger, *External Processing for CESSB*, QEX Jan/Feb 2016
- Hershberger, QST Feb 2016
- W7PUA's notes and code for the Teensy F32 library: <https://www.janbob.com/electron/CESSB_Teensy/CESSB_Teensy.html>,
  `radioCESSB_Z_transmit_F32.{h,cpp}` in the locally installed OpenAudio library

## Why voice SSB has a high peak-to-average ratio *(general DSP)*

Voice has a high crest factor to begin with. Turning it into SSB with a Hilbert filter adds
more: phase-shifting the audio's harmonics makes envelope peaks that the audio waveform did not
have. Hershberger measured this at **61 %** above the peak of the input audio. On the W9GR voice
clip, the Phoenix chain's I/Q has a PAPR of **8.7 dB** ([[cessb]], model results).

## The three stages *(general DSP)*

Let `z = I + jQ` at baseband, scaled so the allowed envelope is 1.0.

1. **Envelope clipper.** Where `|z| > 1`, set `z ← z/|z|`. This is a *radial* clip: the
   phase is unchanged and only the magnitude is limited. Clipping I and Q separately would
   allow `|z|` up to √2 and distort the phase as well.
2. **Lowpass.** Clipping spreads the spectrum, so a linear-phase FIR removes everything
   outside the SSB channel. Filtering brings the overshoot back, though smaller: about
   **1 dB** of peaks.
3. **Overshoot controller.** Clip the filtered signal again, but keep only the *difference*,
   `d = z − z/max(|z|,1)`. The window takes the largest `|z|` over a few neighbouring
   samples. Filter `d` with the same lowpass, then subtract `gainCompensate · d` from the
   signal, delayed to line up with the filtered `d`. Filtering is linear, so subtracting a
   filtered correction is the same as filtering a corrected signal. Because the correction
   is band-limited, the output stays in the channel. `gainCompensate > 1` over-corrects to
   make up for the smearing that filtering does to the correction.

Result: overshoot falls from **61 % to about 1.3 %** (Hershberger). W7PUA reports **3 to
4 dB** more average power at a drive ratio of about 2:1 into the clipper. The output must not
pass through IIR or other non-linear-phase filtering afterwards: that changes the relative
phase of the spectral components and brings the peaks back.

## Why the clipper's filter must cover both edges of the channel

The envelope `|z|` does not depend on the carrier frequency: `|z·e^{jωn}| = |z|`. The
clipping can therefore happen with the signal at any frequency offset. **The filtering cannot.**

- **Hershberger and W7PUA's Weaver class.** Weaver modulation centres the SSB channel on DC,
  so it occupies about ±1.35 kHz. A real lowpass on I and Q then removes clip products on
  *both* sides of the channel.
- **Zero-IF transmitters.** Phoenix, and W7PUA's Z class, put the channel at 0 to +3 kHz
  (or −3 to 0 kHz). A real lowpass on I and Q has a symmetric response, so it passes −3 to
  +3 kHz. Clip products that fall in the **opposite sideband** are not filtered. W7PUA
  measured opposite-sideband energy about 20 dB below the signal at 4 dB enhancement. The
  Phoenix model puts it at **−27 dBc** for voice and **−11 dBc** for the 2f₁−f₂ two-tone
  product.
- **Weaver output on zero-IF hardware.** It leaves a 1350 Hz tone wherever the mixers leak
  LO. That is why W7PUA wrote the Z class.

**The centred form** gets both benefits. Shift the zero-IF signal down by the channel centre
(about 1.5 kHz), clip and filter with a ±1.45 kHz lowpass, then shift back up. The clipping
decisions are identical. The filters now act as complex bandpasses that cut both edges of the
channel. The output is still zero-IF, so there is no new tone. The cost is two complex
multiplies per sample. In the model the opposite sideband drops from −27 dBc to **−71 dBc**;
in exchange, about 0.3 dB of talk power is lost ([[cessb]]).

## Numbers people quote

| Quantity | Value | Source |
|---|---|---|
| Overshoot, plain SSB → CESSB | 61 % → 1.3 % | Hershberger 2014 |
| `gainCompensate` | 1.9 (Weaver); 1.4 (W7PUA Z class) | W7PUA |
| Drive ratio into the clipper | 1.5 → about 3 dB, 2.0 → about 4 dB, 2.5 → 4–5 dB; 3 is excessive | W7PUA |
| CPU, whole Z class | 211 µs per 128-sample block at 48 ksps on a Teensy 4.0 | W7PUA header |
| Phoenix model, centred, 6 dB drive | +4.6 dB wanted-sideband power at equal PEP; 1.3 % overshoot; opposite sideband −71 dBc | [[cessb]] |
| Phoenix on the radio (KI3P prototype), voice stand-in, 6 dB drive | talk-power gain +4.2 dB at the exciter, +4.5 dB at the antenna (model +4.1 on the same signal); PEP at RF held at the 5 W setting, no overshoot through the PA | [[cessb]] Phase 3 |
| CPU, Phoenix stage | 678 µs per 2048-sample block (11.6 ms at 176.4 ksps), 5.8 % of real time; measured | [[cessb]] Budget |
