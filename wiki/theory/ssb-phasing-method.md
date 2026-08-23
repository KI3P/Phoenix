---
title: SSB by the Phasing (Hilbert) Method
type: concept
status: draft
created: 2026-06-08
updated: 2026-07-29
tags: [ssb, phasing, hilbert, usb, lsb, demodulation, modulation, sample-rate, hil]
source_refs: []
related: ["[[theory-overview]]", "[[iq-quadrature-sampling]]", "[[fast-convolution-filtering]]", "[[dsp-chain]]", "[[mode-state-machine]]", "[[tx-filter-hil-test]]", "[[runtime-filter-design]]", "[[iq-imbalance-correction]]"]
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

⚠️ **The Hilbert table is deliberately *not* regenerated per sample rate**, and that is correct
rather than an oversight. A Hilbert transformer's usable band is a **fraction of its sample
rate**, so one fixed 100-tap table is the same design at any rate — its band edges are *meant* to
scale with Fs, exactly like an anti-alias filter's ([[runtime-filter-design]]). At 176.4 ksps the
Hilbert runs at 11.025 kHz instead of 12 kHz and its ~5 kHz band shrinks in proportion, which
costs nothing because the transmit audio bandwidth is only 2.76 kHz. This is why
[[tx-filter-hil-test]] checks sideband suppression against a **floor** rather than for rate
invariance in hertz: requiring the suppression curve to hold still would fail correct firmware.

## Measuring sideband suppression on the bench

The transmit output is complex baseband, so suppression is directly observable: capture I and Q
**synchronously**, form `I + jQ`, and read the ratio of the line at `+f` to the one at `−f`. Both
come out of a single capture. Two separate single-channel captures would have no defined phase
relationship between them and the ratio would be meaningless — the sideband information lives
entirely in the phase *between* the channels.

The `SidebandSelection()` sign flip also has a use the firmware never intended: because
commanding USB conjugates the transmitted signal, it moves the tone to the other side of DC.
Swapping the two scope probes does the same thing, so a single capture cannot tell the two apart —
but the **change** between LSB and USB can, since nobody rewires the bench mid-measurement. That
is how [[tx-filter-hil-test]] resolves which probe is on I without being told.

Note this measurement cannot separate the Hilbert pair's phase accuracy from
[[iq-imbalance-correction]]'s per-band amplitude/phase correction, or from a plain gain difference
between the two exciter outputs. From the exciter's terminals all three look identical.

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
