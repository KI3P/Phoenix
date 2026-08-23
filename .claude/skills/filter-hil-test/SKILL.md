---
name: filter-hil-test
description: Verify on real hardware that the receive DSP filters hold their frequencies when the sample rate changes. Drives the radio's I/Q receive inputs with an Analog Discovery 2's W1/W2 in quadrature, reads the demodulated audio on scope Ch1, and sweeps the CW audio filters, the 14 equaliser cells and the SSB filter at both 192 and 176.4 ksps. Use when the user asks to test the filters on hardware, verify sample-rate independence, or check that a filter change works on the real radio.
when_to_use: "test the filters on hardware", "verify sample rate independence", "HIL filter test", "check the filters on the radio", "run the filter sweep", "does the CW filter move when the sample rate changes"
argument-hint: "[--rates 192000,176400] [--tests iq,level,map,ref,cw,eq,ssb,am] [--rate-tol-pct 1.5] [...]"
allowed-tools: Bash(*venv/bin/python *filter_hil_test.py*) Bash(*venv/bin/python *plot_filter_hil.py*) Bash(*venv/bin/python *test_filter_hil.py*) Bash(ls /dev/ttyACM*) Read
user-invocable: true
---

# Filter rate-independence test, hardware in the loop

Runs `code/tools/filter_hil/filter_hil_test.py` to measure where the receive DSP
filters actually sit on the real radio, at every sample rate, and confirm they do
not move.

## Hardware setup

The rig must already be wired:

- **W1 and W2** drive the radio's I and Q receive inputs. Either order is fine —
  the suite detects which is which and compensates in software.
- **Scope Ch1** reads the speaker output.
- **AD2 ground** is shared with the radio.
- CAT on `/dev/ttyACM1` (38400), diagnostics on `/dev/ttyACM0` (115200).

If the AD2 or the serial ports are missing the suite exits 2. Report that and
stop — do not retry.

## Preconditions

- **AGC must be off** on the radio. It compresses the amplitude differences the
  suite measures, so the filter skirts would read flat. There is no CAT command
  for it; the user must set it from the menu. The suite exits 2 if AGC is on.
- The firmware must have the `SR`, `CF`, `EQ` and `FL` CAT commands.
- The user must not touch the front panel during the run — that triggers a
  settings save and would persist the test configuration.

## Running it

```bash
/home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/venv/bin/python \
  /home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/filter_hil/filter_hil_test.py
```

Takes about nine minutes for both rates. Add `-v` for progress. To measure only
part of it, pass `--tests` (from `iq,level,map,ref,cw,eq,ssb,am`) or
`--rates`. `am` is off by default.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | All checks passed |
| 1 | At least one filter is not where it should be |
| 2 | Rig or radio problem: no AD2, no serial, or preflight refused (usually AGC on) |
| 3 | Interrupted; the radio may still be in the test configuration |

## What gets checked

| Group | What it measures | What a failure means |
|---|---|---|
| `rig` | I/Q wiring sense, image rejection, drive level, AGC state | The rig is not connected as documented |
| `mapping` | That an input tone at `Fs/4 + f` produces audio at `f`, at each rate | The firmware did not reconfigure the frequency shift; everything else at that rate is meaningless and is skipped |
| `cw` | The five CW audio filters' -3 dB corners, ripple and stopband | The Chebyshev design is not being regenerated for the rate |
| `eq` | All 14 equaliser cell centres and Q | The cell prototypes are not being re-transformed for the rate |
| `ssb` | The SSB filter edge versus the commanded bandwidth — **the control** | The rig or the analysis is wrong, *not* the firmware |
| `am` | The AM DC blocker corner (only when requested) | The blocker pole is not derived from the rate |

## Reading the result

The primary criterion is **rate invariance**: each frequency must land in the
same place at both sample rates, within 1.5 %. The bug this exists to catch
produces exactly **-8.125 %**, so there is a 5x margin between correct and broken.
When a result matches that figure the report says so explicitly.

View `<prefix>_cw_overlay.png` with `Read` first — both rates should lie on top
of each other and well clear of the dotted legacy-shift line. Then
`<prefix>_summary.png` for the whole picture at a glance.

The SSB filter is the control: it always derived its coefficients from the true
sample rate, so a shift there indicts the measurement rather than the radio.

Equaliser cells 0, 1 and 13 are marked *edge limited* and judged loosely on
absolute accuracy — the first two sit at the SSB low cut and the last in the
decimation skirt, which is meant to scale with the rate. Their rate-invariance
check is still applied at full strength.

## Reporting

Give one line per group with a PASS/FAIL and the headline number, then the
overall verdict. Quote the worst `delta_pct` and say whether it matches the
-8.125 % signature. Mention the Markdown report and PNG paths. If the radio's
state was not fully restored, say so prominently and name the settings that
drifted.

## Guardrails

- **Do not edit the suite in response to a failure.** A failure means the
  radio's filters are not where they should be, or the rig is not connected as
  documented. `test_filter_hil.py` covers the measurement maths and includes a
  check that the comparison fails on a simulated -8.125 % shift — run that if you
  suspect the tool rather than the radio.
- Do not widen `--rate-tol-pct` to make a run pass.
- If the run is interrupted, the AWG is silenced automatically but the radio may
  be left mid-configuration; re-running the suite restores it.
