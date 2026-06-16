---
title: TX Carrier Null (LO leakage suppression)
type: concept
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [carrier-null, lo-leakage, dc-offset, transmit, calibration]
source_refs: []
related: ["[[theory-overview]]", "[[iq-imbalance-correction]]", "[[ssb-phasing-method]]", "[[hardware-state-machine]]", "[[rf-board]]", "[[persistent-config]]"]
---

# TX Carrier Null (LO leakage suppression)

On a direct-conversion *transmitter*, a residual **carrier** can leak through at the LO
frequency even when the modulating audio is silent. Phoenix nulls it by injecting a small,
calibrated **DC offset** into the baseband I/Q. This is a **distinct** problem from I/Q image
([[iq-imbalance-correction]]): image = the *opposite sideband* (a conjugate term); carrier =
*LO feedthrough* sitting exactly at the suppressed-carrier frequency.

## Why a DC offset nulls the carrier *(general DSP)*
After the quadrature mixer, a **constant (DC) component** on the baseband I/Q up-converts to a
tone at exactly the **LO/carrier frequency**. LO leakage and converter DC offsets put an
unwanted DC there. Adding an equal-and-opposite DC offset to I and Q cancels it — so the
transmitted carrier is suppressed. Two knobs (`DCOffsetI`, `DCOffsetQ`) cover both quadrature
axes of the leakage vector.

## How Phoenix applies it
In the transmit output path, a per-band DC offset is added to the I and Q sample streams just
before they go to the codec (`DSP.cpp:1065-1067`):
```c
arm_offset_q15(sp_L2, ED.DCOffsetI[band], sp_L2, USB_BUFFER_SIZE);   // I + offset
arm_offset_q15(sp_R2, ED.DCOffsetQ[band], sp_R2, USB_BUFFER_SIZE);   // Q + offset
```
`ED.DCOffsetI/Q[NUMBER_OF_BANDS]` are int16 per-band values in [[persistent-config]] (default
0).

## Calibration — `TransmitCarrierCalSm`
The values are found by the transmit-carrier calibration routine
(`HardwareSm_TransmitCarrierCalibration.cpp`, driven by the `TransmitCarrierCalSm` sub-machine
in [[hardware-state-machine]]; state map in [[calibration-state-machines]]). It **sweeps `DCOffsetI` then `DCOffsetQ` to maximize carrier
suppression (dBc)** — the same narrowing coordinate-descent used for I/Q balance
([[iq-imbalance-correction]]): step each offset, measure the residual carrier via the RX path
(`GetTXCarrierVals`), keep the value giving the deepest null (`maxDBC` / `maxDBC_parameter`),
and auto-advance through all bands. Per-band because LO leakage varies with frequency.

## The three TX calibrations, compared
| Calibration | Corrects | Knob(s) | `ED` field |
|---|---|---|---|
| TX I/Q balance | opposite-sideband **image** | amp + phase | `IQXAmp/XPhaseCorrectionFactor` |
| **TX carrier null** | **LO leakage / carrier** | DC offset I, Q | `DCOffsetI/Q` |
| Power | PA output level | attenuation/gain | `PowerCal_*` |

All three are orchestrated by [[hardware-state-machine]] and run against the same TX hardware
([[rf-board]]).

## Open questions
- Units/scale of `DCOffsetI/Q` (raw q15 counts) and the achievable suppression in dBc.
- How the residual carrier is measured during cal (which RX bin / path).
