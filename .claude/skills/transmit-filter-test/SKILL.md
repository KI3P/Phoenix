---
name: transmit-filter-test
description: Verify on real hardware that the transmit DSP filters hold their frequencies when the sample rate changes. Drives the radio's microphone input with an Analog Discovery 2's W1, captures the exciter I/Q outputs together on Ch1/Ch2, and measures the transmit audio passband corners, opposite-sideband suppression, out-of-band rejection and fold-back at both 192 and 176.4 ksps. Use when the user asks to test the transmit filters on hardware, check the transmit audio bandwidth, verify transmit sample-rate independence, or measure sideband suppression.
when_to_use: "test the transmit filters on hardware", "check the transmit bandwidth", "verify transmit sample rate independence", "TX filter HIL test", "measure sideband suppression", "does the transmit audio bandwidth move when the sample rate changes", "run the transmit filter sweep"
argument-hint: "[--rates 192000,176400] [--tests wiring,level,passband,sideband,alias,carrier] [--modulation usb|lsb] [--mic-gain 20] [...]"
allowed-tools: Bash(*venv/bin/python *tx_filter_hil_test.py*) Bash(*venv/bin/python *plot_tx_filter_hil.py*) Bash(*venv/bin/python *test_tx_filter_hil.py*) Bash(ls /dev/ttyACM*) Read
user-invocable: true
---

# Transmit filter rate-independence test, hardware in the loop

Runs `code/tools/tx_filter_hil/tx_filter_hil_test.py` to measure where the
transmit DSP filters actually sit on the real radio, at every sample rate, and
confirm they do not move.

The receive-side equivalent is `/filter-hil-test`. Do not confuse the two: they
need different wiring and measure different filters.

## Hardware setup

The rig must already be wired:

- **W1** drives the radio's microphone input (Teensy audio hat MIC).
- **Scope Ch1 and Ch2** read the exciter's I and Q outputs. Either order is fine —
  the suite detects which is which and compensates in software.
- **AD2 ground** is shared with the radio.
- CAT on `/dev/ttyACM1` (38400), diagnostics on `/dev/ttyACM0` (115200).

No PTT wiring is needed; the radio is keyed over CAT with `TX;`.

If the AD2 or the serial ports are missing the suite exits 2. Report that and
stop — do not retry.

## Preconditions

- **The radio transmits continuously for the whole run**, several minutes per
  sample rate at 100 % duty. Before starting, confirm with the user that the
  exciter I/Q feeds the scope only, or that a dummy load is fitted. Say the
  key-down time back to them from the report.
- **The radio must be in SSB.** `TX;` only keys from SSB or CW receive, and a CW
  key-down transmits a sidetone carrier rather than processed microphone audio.
  The suite forces USB and restores the original modulation.
- **The power setting must not change during the run.** `TXGain` derives the
  exciter drive from it. The suite reads and reports it and does not touch it.
- The user must not touch the front panel during the run — that triggers a
  settings save and would persist the test configuration.

AGC is a receive control and does not matter here, unlike `/filter-hil-test`.

## Running it

```bash
/home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/venv/bin/python \
  /home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/tx_filter_hil/tx_filter_hil_test.py
```

Add `-v` for progress. To measure only part of it, pass `--tests` (from
`wiring,level,passband,sideband,alias,carrier`) or `--rates`. The `alias` test
needs `passband` for its reference level and skips without it.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | All checks passed |
| 1 | At least one filter is not where it should be |
| 2 | Rig or radio problem: no AD2, no serial, or an unrecognised sample rate |
| 3 | Interrupted; the AWG is silenced and PTT dropped, but confirm the radio is receiving |

## What gets checked

| Group | What it measures | What a failure means |
|---|---|---|
| `rig` | Wiring order, sideband, quadrature, mic drive level | The rig is not connected as documented, or the radio is not keying |
| `passband` | Composite transmit response, −3 dB high and low corners, ripple | The high corner is `TXInterpolateBy2Again`, the stage that sets the transmit audio bandwidth; the low corner is the equaliser bank's lowest cell |
| `corner` | Cross-rate comparison of those corners — **the headline** | A generated stage is not being regenerated for the rate |
| `sideband` | Opposite-sideband suppression across the passband | The Hilbert transform or the I/Q correction is off. Checked against a floor, *not* for rate invariance |
| `alias` | Out-of-band rejection, and fold-back when `TXDecimateBy2Again` halves the audio rate | The old flat-to-0.425·Fs decimator table is back |
| `carrier` | DC residue from `ED.DCOffsetI/Q` carrier nulling | Diagnostic only; a large residue raises every other noise floor |

## Reading the result

The primary criterion is **rate invariance**: each corner must land in the same
place at both sample rates, within **2.5 %**.

That tolerance is looser than the receive suite's 1.5 % on purpose. Both generated
transmit stages are 48-tap Kaiser designs, which do not scale exactly when the
normalised cutoff changes: evaluating the tap sets directly gives **+1.2 % of
movement on a correct radio**, and the firmware's own offline test
(`TransmitChain176k.AudioBandwidthHoldsAcrossRates`) allows 2 % for the same
reason. The bug being hunted is **−8.125 %**, so there is better than a factor of
three in hand. **Do not widen `--rate-tol-pct` to make a run pass, and do not
tighten it to 1.5 % — that would fail correct firmware.**

View `<prefix>_summary.png` with `Read` first: three bars that should sit inside
the green tolerance band and nowhere near the dotted −8.125 % line. Then
`<prefix>_passband.png` — both rates' curves should lie on top of each other.

Expect and do not report as faults:

- The high corner moving 1–2 % between rates.
- Sideband suppression differing between rates near the band edges: the Hilbert
  band is a fraction of Fs and is meant to scale.
- Out-of-band rejection reading exactly the noise floor. That is a pass; the
  report says the true rejection is greater than the rig can see.

## Reporting

Give one line per group with a PASS/FAIL and the headline number, then the overall
verdict. Quote the worst `delta_pct` and say whether it matches the −8.125 %
signature — the report flags this explicitly when it does. State the total
key-down time. Mention the Markdown report and PNG paths. If the radio's state was
not fully restored, say so prominently and name the settings that drifted; check
in particular that the radio is receiving and back on its original modulation.

Repeat the three caveats when they bear on the result:

- Every response is a **composite** — nothing in the transmit chain can be
  bypassed over CAT, so the codec front end and the equaliser bank are in series.
  Absolute corners are therefore loose; the rate comparison is not affected.
- A clean fold-back result does not on its own vindicate the decimator: `BandEQ`
  runs before it and attenuates high inputs already.
  `TransmitChain176k.DecimatorStopsBeforeTheFoldPoint` is what proves that stage.
- The transmit equaliser cells are not measured individually because
  `S_Xmt[i].pCoeffs` and `S_Rec[i].pCoeffs` point at the same array — the receive
  suite already sweeps those exact coefficients.

## Guardrails

- **Do not edit the suite in response to a failure.** A failure means the radio's
  filters are not where they should be, or the rig is not connected as
  documented. `test_tx_filter_hil.py` covers the measurement maths and includes a
  check that the comparison fails on a simulated −8.125 % shift — run that if you
  suspect the tool rather than the radio.
- Do not key the radio by any other means while the suite is running.
- If the run is interrupted, the AWG is silenced and PTT dropped automatically,
  but confirm the radio came back to receive before leaving it.
