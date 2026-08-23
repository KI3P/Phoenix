---
title: DSP Chain (Audio, FFT, FIR, Noise, CW)
type: module
status: draft
created: 2026-06-08
updated: 2026-07-30
tags: [dsp, fft, fir, agc, noise-reduction, cw, openaudio, sample-rate]
source_refs: []
related: ["[[digital-mode]]", "[[overview]]", "[[real-time-constraints]]", "[[mode-state-machine]]", "[[display-subsystem]]", "[[code-heritage]]", "[[audio-io]]", "[[runtime-filter-design]]", "[[sample-rate-switching]]", "[[tx-filter-hil-test]]"]
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

## Rebuilding the chain

`InitializeSignalProcessing()` (`DSP.cpp:758-762`) builds all four filter configurations — the
three `ReceiveFilterConfig` instances (`RXfilters` at the user's zoom, `RXTXfilters`,
`TXIQfilters`) plus `TXfilters`. It runs at boot **and on every sample-rate change**
([[sample-rate-switching]]), which is what regenerates the Hz-specified coefficients
([[runtime-filter-design]]).

That the three receive instances share code but not state matters: two bugs found in 2026-07
were exactly a failure of that separation — the AM DC blocker's state was a **file-scope static
shared across all three**, and `ApplyEQBandFilter` scrubbed the *receive* instance's state even
when called on the transmit path.

⚠️ **Transmit audio changed in V1.4.0 even at 192 ksps.** The decimate-by-2 feeding the Hilbert
stage had a stopband that did not exist (flat to 0.425·Fs where ÷2 needs 0.25·Fs), folding
6–9.5 kHz back into the transmitted audio. See [[runtime-filter-design]], and
[[tx-filter-hil-test]] for the bench measurement of the fold-back.

Two ordering details of `TransmitProcessing()` (`DSP.cpp:1124-1155`) that matter to anyone
measuring the transmit chain:

- **`BandEQ` runs before `TXDecimateBy2Again`** (`:1137` vs `:1140`), so the equaliser bank — whose
  top cell is 4 kHz — attenuates high-frequency microphone input *before* the stage whose stopband
  prevents fold-back. That makes an end-to-end fold-back test weaker evidence about the decimator
  than it looks ([[tx-filter-hil-test]]).
- **`Q := I` right after `TXGain`** (`arm_copy_f32(data.I, data.Q, 256)`, `:1139`), so from there
  the chain is mono and only the microphone's **left** channel ever reaches the exciter. The
  Hilbert pair rebuilds the quadrature relationship downstream ([[ssb-phasing-method]]).
- **`TXGain` scales the exciter output by the requested power** (`:1101-1119`), via
  `CalculateSSBTXGain` on `ED.powerOutSSB[band]`. Any measurement of exciter amplitude is
  therefore only comparable at a fixed power setting.

## Related theory
- [[iq-quadrature-sampling]] — complex baseband, the foundation
- [[ssb-phasing-method]] — Hilbert ±45° TX pair + frequency-domain RX sideband selection
- [[fast-convolution-filtering]] — the FFT overlap-save channel filter (Convolution-SDR core)
- [[multirate-decimation]] — ÷8 RX / ÷16 TX rate changes
- [[runtime-filter-design]] — which stages are specified in Hz (regenerated per rate) vs as a
  fraction of Fs (left alone), and why prewarping is what makes that work
- [[filter-hil-test]] — bench verification of the above on the real radio
- [[iq-imbalance-correction]], [[agc-design]], [[noise-reduction]], [[zoom-fft]]
- [[synchronous-am-detection]] — SAM (PLL carrier recovery) vs envelope AM
- [[audio-equalizer]] — 14-band parallel filterbank (RX + TX)

## To flesh out
- [ ] Block diagram of the actual RX and TX chains with sample rates per stage.
- [ ] Which NR algorithm(s) are implemented (spectral subtraction? LMS?).
- [ ] FFT size, windowing, and the [[spectrum-refresh-floor]] timing relationship.
- [ ] AGC attack/decay design.
