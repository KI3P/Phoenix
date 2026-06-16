---
title: DSP Chain (Audio, FFT, FIR, Noise, CW)
type: module
status: draft
created: 2026-06-08
updated: 2026-06-09
tags: [dsp, fft, fir, agc, noise-reduction, cw, openaudio]
source_refs: []
related: ["[[overview]]", "[[real-time-constraints]]", "[[mode-state-machine]]", "[[display-subsystem]]", "[[code-heritage]]", "[[audio-io]]"]
---

# DSP Chain (Audio, FFT, FIR, Noise, CW)

The signal-processing core, built on the **OpenAudio_ArduinoLibrary** for real-time
48/96/192 kHz audio. This is the part of the codebase with the deepest lineage back to the
Teensy Convolution SDR ([[code-heritage]]).

## Files
- `DSP.cpp` (~40 KB) / `DSP.h` — top-level audio routing, AGC, filter coordination
- `DSP_FFT.cpp` (~45 KB) / `DSP_FFT.h` — spectrum analysis (feeds the display waterfall);
  `DSP_FFT_stub.cpp` for host-side test builds
- `DSP_FIR.cpp` (~51 KB) — FIR filter coefficient management (bandpass/decimation)
- `DSP_Noise.cpp` (~31 KB) / `DSP_Noise.h` — noise reduction
- `DSP_CWProcessing.cpp` (~24 KB) / `DSP_CWProcessing.h` — CW audio filtering / Morse decode → [[cw-processing]]
- `MainBoard_AudioIO.cpp` / `.h` — audio codec interface (I²S in/out)

## Role
- **RX** (`ReceiveProcessing`, exact order `DSP.cpp:784-952`): read I/Q → RF gain →
  [[iq-imbalance-correction|I/Q correction]] → fine-tune freq shift → **decimate ÷8** →
  volume-scale → **convolution channel filter** ([[fast-convolution-filtering]]) → **AGC**
  ([[agc-design]]) → **demodulate** ([[ssb-phasing-method]]) → band EQ → **noise reduction**
  ([[noise-reduction]]) → optional auto-notch → CW processing (if CW) → **interpolate ×8** →
  volume → play. Note AGC precedes demod (acts on the complex envelope); NR follows it.
  The [[zoom-fft]] taps the raw I/Q separately for the band display.
- **TX**: mic/key → decimate → **Hilbert ±45° pair** (SSB I/Q gen) / CW tone → sideband
  select → interpolate → codec → quadrature mixer. See [[ssb-phasing-method]].
- Mode routing is directed by [[mode-state-machine]].

Filter configuration structs (`ReceiveFilterConfig`, `TransmitFilterConfig`,
`DecimationFilter`, `AGCConfig`) live in `SDT.h` → see [[persistent-config]].

## Related theory
- [[iq-quadrature-sampling]] — complex baseband, the foundation
- [[ssb-phasing-method]] — Hilbert ±45° TX pair + frequency-domain RX sideband selection
- [[fast-convolution-filtering]] — the FFT overlap-save channel filter (Convolution-SDR core)
- [[multirate-decimation]] — ÷8 RX / ÷16 TX rate changes
- [[iq-imbalance-correction]], [[agc-design]], [[noise-reduction]], [[zoom-fft]]
- [[synchronous-am-detection]] — SAM (PLL carrier recovery) vs envelope AM
- [[audio-equalizer]] — 14-band parallel filterbank (RX + TX)

## To flesh out
- [ ] Block diagram of the actual RX and TX chains with sample rates per stage.
- [ ] Which NR algorithm(s) are implemented (spectral subtraction? LMS?).
- [ ] FFT size, windowing, and the [[spectrum-refresh-floor]] timing relationship.
- [ ] AGC attack/decay design.
