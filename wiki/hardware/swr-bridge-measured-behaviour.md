---
title: SWR Bridge — Measured Behaviour (bench tests, Oct 2026)
type: hardware
status: draft
created: 2026-10-24
updated: 2026-10-25
tags: [swr, ad8307, bridge, directivity, calibration, power-sensing, pa, measurement]
source_refs: [sources/ad8307-datasheet, sources/t41-ep-schematics]
related: ["[[filter-board-electronics]]", "[[log-amp-power-detection]]", "[[filter-boards]]", "[[calibration-state-machines]]", "[[cat-control]]"]
---

# SWR Bridge — Measured Behaviour

Two radios were measured on 2026-10-24 to test the 50 Ω/open/short calibration before
writing it into the firmware. **The forward channel calibrates well; the SWR reading cannot
be made accurate by calibration.** Inside the radio, the bridge leaks 10–20 dB more than the
VNA measured on a bare board. Known loads therefore read 30–90 % high, by an amount that
differs between radios and bands.

What was learned, in short:
- **Forward power:** a slope and offset fitted per radio and per band to a wattmeter give
  ±0.25 dB on radio 1 and ±0.5 dB on radio 2, from about 16 mW to full power.
- **SWR:** a good 50 Ω load reads SWR 1.3–2.1 and a 2:1 load reads 2.7–4.7. A magnitude-only
  bridge cannot correct this, so the SWR display is unreliable until the leak is found and
  fixed in hardware.
- **Calibration shortcuts fail.** Slopes from DIGITAL gain steps read 2–7 % low, because the
  transmitter is not dB-linear. Carrying a band's offset to another band with the VNA path
  loss is off by up to 3 dB.
- **Faults found on the way:**
  - radio 2's PA oscillates into a short on 10 m;
  - radio 2 once stopped transmitting the DIGITAL tone until it was power-cycled;
  - radio 1 rebooted from a static discharge when a load was fitted.

## How it was measured
The script `code/tools/swr/swr_bench_measure.py` ran each radio under CAT control. Its
`README.md` and the notebook `swr_calibration_50_open_short.ipynb` (Section 11) give the
method and analysis. The firmware commands it relies on are in [[cat-control]].
- **Frequencies:** 1.9, 7.1, 14.2 and 28.5 MHz. Every row used the 20 W PA and ANT1.
- **Bridge readings:** both detectors were read raw, before the display's running average,
  over 2 s of each key-down (`BR`).
- **Forward power:** a CMU-200 on a 60 dB directional coupler between the radio and a 50 Ω
  dummy load, at 300 kHz bandwidth, averaging 10 readings per row.
- **Standards and loads:** an open, a short, 100 Ω and 200 Ω ceramic resistors, and the
  dummy load. Each load's Γ came from its VNA sweep.
  - The reflected channel was driven by DIGITAL-mode tones from −11 to −26 dBFS, about 1 W
    and below.
  - Forward rows were CW at fixed set-points plus DIGITAL tones from −6 to −30 dBFS.

| Session | Radio | Loads fitted | Notes |
|---|---|---|---|
| Radio 1 | 1 | On the external coupler's output | Rebooted once (static discharge); rows after it were re-run |
| Radio 2, session 1 | 2 | On the external coupler's output | Four 10 m short rows oscillating, excluded |
| Radio 2, session 2 ("radio 3" sheet) | 2 | Directly on the radio's connector | The DIGITAL tone was lost until a power cycle; those rows were re-run. The 10 m short was skipped (oscillation) |

**The external coupler barely matters on 40 m and 20 m.** Radio 2 was measured both ways. On
40 m and 20 m its loads read nearly the same either way, for example 100 Ω at 3.3–3.4 through
the coupler and 3.1–3.2 direct on 40 m. On 10 m the extra line length changes the readings
substantially. Loads for 10 m must therefore go directly on the connector.

## Forward channel (power)
Per-band fit of the detector reading to the CMU-200:

| Band | Radio 1 slope | Radio 2 S1 slope | Radio 2 S2 slope | Fit residual rms (worst), dB |
|---|---|---|---|---|
| 160 m | 24.61 | 23.42 | 22.96 | 0.09 (0.14) / 0.23 (0.39) / 0.21 (0.42) |
| 40 m | 24.63 | 24.62 | 23.95 | 0.10 (0.23) / 0.23 (0.52) / 0.19 (0.41) |
| 20 m | 24.14 | 25.03 | 25.04 | 0.11 (0.21) / 0.08 (0.14) / 0.11 (0.24) |
| 10 m | 24.77 | 25.35 | 25.48 | 0.16 (0.26) / 0.12 (0.23) / 0.15 (0.23) |

Slopes are in mV/dB. "S1" and "S2" are radio 2's two sessions.

- **Log-law curvature limits accuracy.** Radio 2's residuals curve smoothly: about −0.5 dB at
  the bottom of the range, +0.4 dB in the middle, −0.35 dB near the top. This is the AD8307's
  conformance error, within the datasheet's ±1 dB ([[ad8307-datasheet]]). A slope and offset
  cannot remove it. Radio 1's chip is about twice as linear.
- **One slope does not fit every band on radio 2.** Its 160 m slope is 5–7 % below 40 m in
  both sessions, which fits the open-OFS offset loop near 1.5 MHz
  ([[log-amp-power-detection]]). Radio 1 shows no 160 m effect. Its slopes agree within 2 %.
  The "one slope per chip" rule therefore holds on some chips and not others.
- **Per-radio calibration is required.**
  - The two radios' intercepts differ by about 6–7 dB on 40 m, within the AD8307's ±5 dB
    spread.
  - Their key-up floors differ to match: 557–570 / 587–608 mV (forward / reflected) on
    radio 1, against 452–466 / 637–650 mV on radio 2.
- **Calibrations drift between sessions.** On radio 2, session 1's calibration applied to
  session 2's readings is off by +0.27 to +0.53 dB (mean), against 0.11–0.21 dB rms for
  session 2's own calibration. Whether the radio or the reconnected measurement chain moved is
  not known.
- **Gain steps (DIGITAL tone steps of 3 dB) cannot replace the wattmeter for the slope.** The
  CMU-200 shows each 3 dB tone step moving the output by only 2.5–2.97 dB (0.83–0.99 dB/dB),
  even 20 dB below the top level. The gain-step slope therefore reads 0.4–1.7 mV/dB (2–7 %)
  low on every band of both radios.
- **Carrying the forward offset between bands with the VNA path loss fails.** This
  contradicts the robustness expected in [[filter-board-electronics]]. The error against the
  40 m calibration, in dB:

  | Band | Radio 1 | Radio 2 S1 | Radio 2 S2 |
  |---|---|---|---|
  | 160 m | −0.33 | +0.34 | +0.16 |
  | 20 m | −0.43 | −1.98 | −2.13 |
  | 10 m | −2.57 | −3.0 | −3.3 |

  The 60 dB external coupler was assumed flat and has not been swept. Part of the 10 m error
  may therefore be the coupler's, but the 20 m difference between the radios is the boards'.
  Each band needs its own wattmeter point.
- **Detector floor.** The key-up floor is about −62 dBm at the detector by the nominal law,
  roughly 16 dB above the AD8307's own −78 dBm floor, so something is picked up. It equals
  about 25 µW on the line and only affects reflected readings of good loads below about
  0.1 W. The firmware should subtract it in the power domain.

## Reflected channel and SWR
The reflected slope and offset from the open and short fit well: 0.02–0.12 dB rms, with the
reflected slope within about 1 mV/dB of the forward slope. The open and short read alike
within 0.02 dB on 40 m. Even so, known loads read far too high. SWR read with the wattmeter
forward calibration plus the open/short reflected calibration:

| Band | Load (true SWR) | Radio 1 | Radio 2 S1 | Radio 2 S2 (direct) | VNA-predicted range |
|---|---|---|---|---|---|
| 40 m | 50 Ω (1.01) | 1.35† | 1.37–1.39 | 1.33–1.36 | 1.05–1.06 |
| 40 m | 100 Ω (2.03) | 2.84–3.01 | 3.29–3.35 | 3.09–3.24 | 1.91–2.17 |
| 40 m | 200 Ω (4.06) | 5.86–6.09 | 6.54–6.67 | 6.84–6.87 | 3.68–4.53 |
| 20 m | 50 Ω (1.01) | 1.26† | 1.80–1.81 | 1.67 | 1.09–1.11 |
| 20 m | 100 Ω (2.03) | 2.68–2.70 | 3.64–3.71 | 3.75–3.89 | 1.82–2.29 |
| 20 m | 200 Ω (4.06) | 5.37–5.77 | 6.56–7.63 | 7.74–8.19 | 3.43–4.92 |
| 10 m | 50 Ω (1.02) | 1.83† | 2.56–2.64 | 2.07–2.13‡ | 1.28–1.33 |
| 10 m | 100 Ω (2.03) | 3.43–3.84 | 2.18–2.23 | 4.42–4.69‡ | 1.53–2.84 |
| 10 m | 200 Ω (4.08) | 6.66–7.32 | 3.97–4.01 | 9.94–10.11‡ | 2.81–7.00 |

† Radio 1's 50 Ω reading is the dummy load through the external coupler. On radio 2, that
chain read within 0.1 of the bare load on 40 m and 20 m.
‡ No short on 10 m in session 2 (oscillation), so this is an open-only reflected calibration.

**The leak implied by the 50 Ω reading** is the bridge's effective directivity:

| Band | Radio 1 | Radio 2 | VNA, bare board as built |
|---|---|---|---|
| 40 m | about −16.5 dB | about −16.8 dB | −36.5 dB |
| 20 m | about −18.8 dB | about −12 dB | −26.5 dB |
| 10 m | about −10.7 dB | about −9 dB | −17.0 dB |

The VNA figure is J23→reflected detector over ANT4→reflected detector, from the board sweeps
in [[filter-board-electronics]]. A usable bridge needs better than about −25 dB.

**What this rules out:**
- **Not a calibration error.** A wrong reflected offset would scale every load's |Γ| by the
  same factor. Here the factor depends on the load: on 40 m, 1.44 for 100 Ω and 1.18 for
  200 Ω, while the open and short read 1.00.
- **Not the external coupler** (above), except on 10 m.
- **Not a shifted balance impedance.** A bridge that balances at some Z0 ≠ 50 Ω fits the open,
  short, 100 Ω and 200 Ω readings on radio 1. But it mispredicts radio 2's bare 50 Ω load:
  2.0 predicted against 1.33 measured on 40 m, and 1.5 against 2.6 on 10 m. The fitted Z0 also
  wanders between bands and radios.
- **Not correctable in firmware.** The detectors measure magnitude only, so a load's reading
  depends on its unknown phase relative to the leak. The leak also differs between the two
  radios: on 20 m a good load reads 1.26 on radio 1 and 1.67–1.81 on radio 2. On radio 2 at
  10 m, the 100 Ω and 200 Ω loads happened to read correctly in session 1 while the 50 Ω load
  read 2.6. That shows how a correct reading can be a coincidence of phase.

**Cause: unknown.** The VNA measured a bare board driven by the VNA. In the radio the PA
drives it, and the bridge sits with the rest of the hardware. Candidates:
- stray coupling from the PA or its wiring into the reflected detector, which bypasses the
  coupler (the high key-up floor shows the detectors do pick up stray signals);
- how L44 is wired into its surroundings in the radio (leads, stray coupling from the line,
  return path and ground at pad 5), which the model below shows matters more than its windings.

**L44 modelled from its winding (2026-10-25).** L44 is a 1:10 current transformer (hole 1)
and a 10:1 voltage transformer (hole 2) on one BN-43-202 binocular, described in
[[filter-board-electronics]]. `code/tools/swr/l44_coupler_model.py` solves it by nodal
analysis. It treats the core as a three-limb magnetic circuit, so the two holes couple
through the centre web. *(infer: AL, ferrite dispersion and leakage are assumed values.)*
- **The balance rests on the port loads.** The reflected port nulls a forward wave only when
  the forward port is loaded by 50 Ω × n_ct/n_vt, and vice versa. The pads give 50.8 Ω.
  R20/R28 are not fitted; fitted, they would give 26 Ω and about 7 dB directivity.
- **A correctly built L44 leaks about −30 dB on 160 m and −30 to −37 dB on 40–10 m.** The
  160 m leak comes from the core: magnetising current and the coupling between the holes.
  This is close to the board as built, which leaks more on 20 m and 10 m.
- **Flat errors:** one turn wrong, or a port load 10 % off, leaks about −25 dB on every band.
- **Rising errors:** to give radio 2's leak on its own, an imperfection would need 12–16 pF
  from the line to a coupled port, or 280–350 nH in series with the line after the voltage
  tap. Only these two hold roughly across bands, and both far exceed what L44's windings and
  leads supply.
- **The out-of-circuit sweep matches the radios.** L44 swept on its own (`RF_in_to_*.s2p`,
  fixture not recorded) leaks −16.4/−11.3/−7.4 dB on 40/20/10 m. Radio 2 leaks
  −16.0/−10.9/−7.1 dB. Fitted to the board, the same coupler measured about −32 dB on 40 m.
  Used as the calibration model's leak, that sweep fits radio 2's load readings to 2.8 dB rms,
  against 8.7 dB for the board sweep. That is the right size but not quite the right phase.

The discriminating test is to transmit into a good 50 Ω load and look at the reflected
detector's input, with the coupler's reflected output connected and then disconnected. A VNA
sweep of the bridge as installed in a radio would also locate it.

## Transmitter behaviour seen during the tests
- **PA oscillation into a short on 10 m (radio 2).** Keyed into a short directly on the
  connector, the forward detector reads about 2100 mV whatever the tone level, with or
  without the tone. That is well above the 1848 mV of full 10 m output into 50 Ω, which
  suggests a VHF oscillation, where the coupler and detector respond more. Through the
  external coupler's line, the PA oscillated only at tones of −17 dBFS and below. A
  mismatched 10 m antenna could trigger it in normal use.
- **DIGITAL transmit stuck without the tone (radio 2).** The radio keyed and the bridge
  collected readings, but the output did not depend on the tone: about 640 mV forward on
  40 m, against a 466 mV floor. It survived leaving and re-entering DIGITAL mode and
  restarting the PC's audio stream, and cleared only with a power cycle. CW transmit worked
  meanwhile. It first appeared after several band changes made while in DIGITAL mode; the
  trigger is not established.
- **Reboot on static discharge (radio 1).** The Teensy reset (USB disconnect and
  re-enumeration) when a load was fitted by hand. The radio came back with its stored
  settings.
- **20 W PA power ceilings** (CW at 0 dB attenuation, into 50 Ω):

  | Band | 160 m | 40 m | 20 m | 10 m |
  |---|---|---|---|---|
  | Radio 1 | 11.7 W | 15.0 W | 10.4 W | 6.0 W |
  | Radio 2 | 12.6–12.7 W | 16.6–16.9 W | 9.4–9.9 W | 5.3–5.7 W |

  Set-points above these, such as 10 W on 10 m, give the ceiling, and the attenuator sits at
  0 dB.

## Implications
- **Firmware power calibration:** fit a slope and offset per band, per radio, against a
  wattmeter, using steady carriers. Expect ±0.25–0.5 dB. Re-check between sessions, or
  accept about 0.5 dB drift. Do not take the slope from DIGITAL gain steps or the offset from
  the VNA path model.
- **SWR display:** treat as indicative only until the bridge leak is understood. An open/short
  calibration of the reflected channel is self-consistent but does not make SWR accurate.
- **Measurement method** (for future bench work):
  - verify every DIGITAL row actually carries the tone (the script's tone check);
  - fit loads directly on the connector for 10 m;
  - do not key repeatedly into a short on 10 m.

## Open questions
- Where does the in-radio leak come from (above)? Does it differ with the 100 W PA?
- What triggers the stuck-DIGITAL fault, and is it firmware?
- Is radio 2's 10 m oscillation into a short a PA stability problem common to the design?
- What is the 60 dB external coupler's coupling from 1.8 to 30 MHz? It would settle the 10 m
  offset-transfer error.
