---
title: SSB by the Phasing (Hilbert) Method
type: concept
status: draft
created: 2026-06-08
updated: 2026-06-08
tags: [ssb, phasing, hilbert, usb, lsb, demodulation, modulation]
source_refs: []
related: ["[[theory-overview]]", "[[iq-quadrature-sampling]]", "[[fast-convolution-filtering]]", "[[dsp-chain]]", "[[mode-state-machine]]"]
---

# SSB by the Phasing (Hilbert) Method

Phoenix generates and demodulates single-sideband by the **phasing method** — *not* the
filter method and *not* the Weaver/third-method. The defining evidence is the ±45° Hilbert
FIR pair on transmit (`DSP_FFT.cpp:851`) and the asymmetric complex filter mask on receive.

## The principle *(general DSP)*

A real audio tone `cos(ω_a t)` contains **both** +ω_a and −ω_a. To transmit only one
sideband you must suppress one of them. The phasing method builds the **analytic signal**
— a complex signal whose spectrum is one-sided — by pairing the audio with its **Hilbert
transform** (a 90° phase shift at all frequencies):

```
analytic(t) = audio(t) + j·Hilbert{audio(t)}
```

Mixing this complex signal up against the quadrature LO places the audio on exactly one side
of the carrier. Choosing **+** or **−** for the j term (equivalently, swapping I/Q or
negating one) flips between **USB and LSB**. No steep analog/IF crystal filter is needed —
the sideband rejection comes from phase cancellation, which is why image/imbalance accuracy
matters so much ([[iq-imbalance-correction]]).

## Transmit path in Phoenix

`TransmitProcessing()` (`DSP.h:83`) → the relevant stage `HilbertTransform()`
(`DSP_FFT.cpp:851`):

```c
// Operates at 12 kHz, 128-sample blocks
arm_fir_f32(&FIR_Hilbert_L, data->I, data->I, 128);  // +45° FIR  (coeffs_45)
arm_fir_f32(&FIR_Hilbert_R, data->Q, data->Q, 128);  // −45° FIR  (coeffs_neg_45)
```

Key implementation details:
- Rather than one 90° shifter plus a delay, Phoenix uses a **+45°/−45° pair** of 100-tap
  FIRs (`FIR_Hilbert_coeffs_45` / `_neg_45`, `SDT.h:531-532`). Their *difference* is 90°,
  and being symmetric about 0° keeps both branches' amplitude/group-delay matched — the
  standard practical way to build a wideband Hilbert pair.
- Audio is decimated 192 kHz → **12 kHz** first (`TransmitFilterConfig` ÷4·÷2·÷2,
  `SDT.h:537-553`); the Hilbert works over ~5 kHz BW at 12 kHz (`DSP_FFT.cpp:852`).
- Sideband choice is a one-line sign flip: `SidebandSelection()` negates I for USB, LSB is
  the default (`DSP_FFT.cpp:864-869`).
- The quadrature pair is then interpolated back to 192 kHz and sent to the codec → Si5351
  quadrature mixer on the [[rf-board]].

## Receive path — phasing done in the frequency domain

On RX, Phoenix doesn't run a separate time-domain Hilbert. Instead, sideband selection is
folded into the **fast-convolution filter mask** ([[fast-convolution-filtering]]). Because
the baseband is complex ([[iq-quadrature-sampling]]), one sideband is the positive-frequency
half and the other is the negative-frequency half of the FFT. The complex `FIR_filter_mask`
(`InitFilterMask`, `DSP_FFT.cpp`) passes only the wanted half.

By the time `Demodulate()` (`DSP.cpp:629`) runs, the unwanted sideband is already gone, so
for USB/LSB it simply copies the real part to both output channels:
```c
case LSB: case USB:
    arm_copy_f32(data->I, data->Q, data->N);  // real part is the audio
    break;
```
(AM/SAM take different branches — magnitude estimation / synchronous detection.) The LSB vs.
USB distinction shows up earlier, in which FFT bins the mask keeps and which half the PSD is
read from (`DSP_FFT.cpp:748-754`).

## Why this matters
- **Sideband rejection = phase accuracy.** Any I/Q amplitude/phase error reintroduces the
  opposite sideband as an image → [[iq-imbalance-correction]], and the RX/TX I/Q calibration
  routines in [[hardware-state-machine]].
- Operating mode (USB/LSB/CW/AM/SAM) is owned by [[mode-state-machine]] via
  `ED.modulation[ED.activeVFO]`.

## Contrast with alternatives *(general DSP)*
- **Filter method**: generate DSB, cut one sideband with a steep filter. Simple but needs a
  high-Q filter; Phoenix avoids it.
- **Weaver / third method**: mix audio to a low IF, low-pass, mix again. No wideband Hilbert
  needed, but two extra mixers. Phoenix is **not** Weaver — confirm if any module hints
  otherwise before claiming it.
