# Transmit filter rate-independence test, hardware in the loop

Verifies on the real radio that the transmit DSP filters land on their designed
frequencies at every supported sample rate. An Analog Discovery 2 drives the
microphone input with a tone and captures the exciter's I and Q outputs
*together*; the suite sweeps, measures, and compares the two rates.

Companion to `../filter_hil/`, which does the same job for the receive chain.
The two share the CAT plumbing and the scalar curve fitting; the measurement
itself is different in kind, and so is what it can and cannot prove.

## Wiring

```
AD2 W1  ------> radio microphone input (Teensy audio hat MIC)
AD2 Ch1 <------ exciter I output       (order does not matter, see below)
AD2 Ch2 <------ exciter Q output
AD2 GND ------- radio ground           (shared return is required)

/dev/ttyACM1 ... CAT control, 38400 baud
/dev/ttyACM0 ... diagnostic port, 115200 baud
```

**Ch1 and Ch2 can be either way round.** The suite works out which is which and
compensates in software. Nothing needs rewiring.

**No PTT wiring is needed.** The radio is keyed over CAT with `TX;` and unkeyed
with `RX;`.

**The exciter outputs sit on a DC bias near +1.6 V.** That is why the scope
offset defaults to 1.6 V; the AD2's range is peak to peak about that offset, so
the usable window is 1.6 ± 2.5 V on the default 5 V range.

## Before running

- **The radio transmits continuously for the whole sweep**, several minutes per
  sample rate, at 100 % duty. Run it with the exciter I/Q feeding the scope only,
  or with a dummy load fitted. The suite reports total key-down time.
- **Leave the power setting alone.** `TXGain` derives the exciter drive from the
  requested output power, so changing it mid-run moves every level measured. The
  suite reads it, reports it, and does not touch it.
- **The radio must be in SSB.** `TX;` only keys from `SSB_RECEIVE` or
  `CW_RECEIVE`, and a CW key-down transmits a carrier from the sidetone
  oscillator rather than processed microphone audio, so it exercises none of
  these filters. The suite forces USB (or LSB with `--modulation lsb`) and
  restores the original modulation afterwards.
- **Do not touch the front panel while it runs.** Menu interaction triggers a
  settings save, which would make the test configuration permanent.

AGC is irrelevant here — it is a receive-path control — so unlike the receive
suite there is nothing to switch off first.

## Running

```bash
cd code/tools
./venv/bin/python tx_filter_hil/tx_filter_hil_test.py
```

Results land in `tx_filter_hil/results/tx_filter_hil_<timestamp>.{json,md}` plus
PNGs.

Useful variations:

```bash
# Passband only, one rate, verbose
./venv/bin/python tx_filter_hil/tx_filter_hil_test.py \
    --tests wiring,level,passband --rates 192000 -v

# Faster, coarser pass
./venv/bin/python tx_filter_hil/tx_filter_hil_test.py \
    --passband-points 25 --alias-points 9

# Measure on the lower sideband instead
./venv/bin/python tx_filter_hil/tx_filter_hil_test.py --modulation lsb

# Raise the radio's mic gain for the run (restored afterwards)
./venv/bin/python tx_filter_hil/tx_filter_hil_test.py --mic-gain 20

# Check arguments and the report path without touching hardware
./venv/bin/python tx_filter_hil/tx_filter_hil_test.py --dry-run

# Redraw figures from a previous run, no hardware
./venv/bin/python tx_filter_hil/plot_tx_filter_hil.py \
    tx_filter_hil/results/tx_filter_hil_*.json
```

Exit codes: `0` pass, `1` fail, `2` error (no hardware, or preflight refused),
`3` aborted.

## How it works

### The transmit chain

From `TransmitProcessing` in `DSP.cpp`:

| Stage | Rate in → out | Corner specified as |
|---|---|---|
| `TXDecimateBy4` | Fs → Fs/4 | fraction of Fs |
| `TXDecimateBy2` | Fs/4 → Fs/8 | fraction of Fs |
| `BandEQ(TX)` | Fs/8 | **hertz** — 14 cells, 198 Hz to 4 kHz |
| `TXGain` | — | power-dependent scalar |
| `TXDecimateBy2Again` | Fs/8 → Fs/16 | **hertz** — `TX_DECIMATE3_FC_HZ` 3500 |
| `HilbertTransform` | Fs/16 | fixed 100-tap table |
| `TXInterpolateBy2Again` | Fs/16 → Fs/8 | **hertz** — `TX_AUDIO_LPF_FC_HZ` 3039.6 |
| `TXInterpolateBy2` | Fs/8 → Fs/4 | fraction of Fs |
| `TXInterpolateBy4` | Fs/4 → Fs | fraction of Fs |

The three specified in hertz have to be regenerated on every rate change. The
rest are anti-alias and anti-image filters whose corners are fractions of Fs, and
scaling with the rate is exactly what those should do.

### Why both scope channels at once

The exciter output is a complex baseband signal: a 1 kHz microphone tone comes
out as a complex exponential at ±1 kHz, one real sine on each of I and Q, 90°
apart. Reconstructing `I + jQ` from a synchronous capture gives the **two-sided**
spectrum, and with it:

- the **magnitude response** at `+f`, which is the transmit audio response, and
- the level at `-f`, which is the **opposite-sideband suppression** — the
  headline specification of an SSB transmitter.

Both come out of the same capture, at no extra cost. Two separate single-channel
captures would have no defined phase relationship between them, and suppression
could not be measured at all.

### Resolving the wiring, and why it takes two captures

Two unknowns: which scope input is on I, and which side of DC the wanted energy
sits on. Unlike the receive rig they cannot be separated by trying combinations
and seeing which one produces a signal — every combination produces a perfectly
good tone. All that changes is which side of DC it lands on, and one capture
cannot tell "Ch1 on I, upper sideband" from "Ch1 on Q, lower sideband".

What separates them is the radio's own sideband switch. `SidebandSelection`
inverts I for USB and leaves LSB alone, so commanding USB conjugates the
transmitted signal and moves the tone across DC. Swapping the scope probes
conjugates it too — but nobody rewires the bench between two captures, so the
*change* between LSB and USB isolates the radio's contribution from the wiring's.

### Scope sample rate

100 kHz by default, and deliberately **not** a divisor of the radio's 192 kHz
output rate. The exciter DAC leaves residual images near its own sample rate; at
96 ksps an image at `192000 - f` aliases to exactly `-f`, straight onto the mirror
frequency the suppression reading uses, and would cap the measurable suppression.
At 100 ksps the same image lands at `8000 + f`, clear of everything measured.

## Reading the result

**The primary criterion is rate invariance.** A corner measured at 192 ksps must
land in the same place at 176.4 ksps.

The tolerance is **2.5 %**, looser than the receive suite's 1.5 %, and the reason
matters. Both generated transmit stages are 48-tap Kaiser-Bessel windowed sincs,
and a 48-tap design does not scale exactly when its normalised cutoff changes —
the taps are quantised to the same 48 positions at both rates, so the realised
corner lands slightly differently. Evaluating the two tap sets directly gives a
cascade −3 dB corner of 2726 Hz at 24 ksps and 2759 Hz at 22.05 ksps: **+1.2 % on
a correct radio.** `TransmitChain176k.AudioBandwidthHoldsAcrossRates` allows 2 %
for the same reason. The bug this suite exists to catch is still **−8.125 %**
(`176400/192000`), so there is better than a factor of three between correct and
broken. A tolerance tighter than the design's own reproducibility would cry wolf.

Look at `_summary.png` first: three bars, all of which should sit inside the green
band and nowhere near the dotted −8.125 % line. Then `_passband.png`, where the
two rates' curves should lie on top of each other.

### What is measured, and what each result is worth

| Group | Measures | Weight |
|---|---|---|
| `rig` | Wiring order, sideband, quadrature, drive level | Gate: nothing else means anything if this fails |
| `passband` | Composite transmit response; −3 dB high and low corners, ripple | **The result.** High corner is `TXInterpolateBy2Again`; low corner is the equaliser bank's lowest cell |
| `corner` | The cross-rate comparison of those corners | **The headline** |
| `sideband` | Opposite-sideband suppression across the passband | Floor, not rate invariance — see below |
| `alias` | Out-of-band rejection, and fold-back when the audio rate halves | Catches a regression of the old flat decimator table |
| `carrier` | DC residue left by `ED.DCOffsetI/Q` carrier nulling | Diagnostic; a large residue raises every other noise floor |

## What this measurement cannot do

Three honest limits, all of which the generated report repeats.

**Nothing in the transmit chain can be bypassed over CAT.** There is no command
for the transmit equaliser (`ED.equalizerXmt`), and the two generated FIR stages
are unconditional. So every response here is a **composite** of the whole path,
microphone input to exciter output, including the codec's front end. The receive
suite's trick — subtract two captures with a filter engaged and bypassed, so the
analog path cancels exactly — is not available. This costs absolute accuracy,
which is why the absolute tolerances are loose. It costs the rate comparison
nothing: no part of the analog path changes when the sample rate does, so it
divides out of a comparison between rates just as cleanly.

**Sideband suppression is checked against a floor, not for rate invariance.** The
Hilbert table is not regenerated per rate, and that is correct — a Hilbert
transformer's usable band is a fraction of its sample rate, so one fixed table is
the same design at any rate and its band edges are *meant* to move with Fs.
Requiring the suppression curve to hold still in hertz would fail correct
firmware. What is measured is the Hilbert accuracy, the per-band I/Q amplitude and
phase correction, and any gain difference between the two exciter outputs, lumped
together — from the exciter's terminals they are not separable.

**A clean fold-back result does not on its own vindicate the decimator.**
`BandEQ` runs *before* `TXDecimateBy2Again` and its highest cell is at 4 kHz, so
it already attenuates an 8 kHz input substantially. A clean result proves nothing
folds into the transmitted audio, which is what matters operationally, but
`TransmitChain176k.DecimatorStopsBeforeTheFoldPoint` is what proves the
decimator's own stopband — it evaluates the tap set at the fold point directly.

**The transmit equaliser cells are not measured individually**, and deliberately
so. `DSP_FFT.cpp` sets `S_Xmt[i].pCoeffs` and `S_Rec[i].pCoeffs` to the *same*
array, so the transmit cells carry byte-for-byte the coefficients the receive
suite already sweeps. Adding a CAT command to solo them would re-verify one table
through a second biquad instance. They are still exercised here, as part of the
composite passband — the low corner is theirs.

## When it fails

| Symptom | Likely cause |
|---|---|
| `rig.sideband_flip` fails: the tone did not change sides between LSB and USB | Only one of Ch1/Ch2 is connected, the radio did not change modulation, or it is not actually transmitting |
| `rig.suppression` reads near 0 dB | The captured pair is real, not complex: one channel missing, or both probes on the same output. A real signal's spectrum is symmetric about DC |
| No tone at all | No shared ground, W1 not on the mic input, the radio did not key (check it is in SSB, not AM/SAM/CW), or the mic gain is at the bottom of its range |
| `rig.drive_level` never reaches the SNR target | Raise `--max-amplitude`, or the radio's mic gain with `--mic-gain 20` |
| Everything reads distorted at any drive | The microphone preamplifier is clipping. Lower `--mic-gain`; the input expects millivolts |
| Levels drift through the run | Something changed the power setting, and with it `TXGain`. Do not touch the front panel |
| Points marked invalid, `clipped` true | Scope range too small or the offset wrong for the exciter's DC bias. Check `--scope-offset` against the reported DC |
| Everything reads −8.125 % | The frozen coefficient tables are back |
| High corner moves 1–2 % | Expected. See the tolerance discussion above |
| Out-of-band rejection reads exactly the noise floor | That is a pass, and the report says so: the true rejection is greater than the rig can see |
| Sideband suppression differs between rates at the band edges | Expected: the Hilbert band scales with Fs by design |

Do not edit the test in response to a failure — a failure means the radio's
filters are not where they should be, or the rig is not connected as documented.
In particular do not widen `--rate-tol-pct` to make a run pass.

## Files

| File | Contents |
|---|---|
| `ad2.py` | Two-channel synchronous record capture, single-channel AWG. Shares the ctypes binding with `filter_hil.ad2` |
| `bandtable.py` | Transmit design constants mirrored from `DSP_FIR.cpp` and `DSP.cpp` |
| `measure.py` | Complex-spectrum metrology: wanted and image levels, suppression, carrier, spurs, corner extraction |
| `tests.py` | The individual tests and the cross-rate comparison |
| `report.py` | Stdout summary and Markdown; document assembly shared with `filter_hil.report` |
| `tx_filter_hil_test.py` | CLI entry point, PTT-safe sequencing, state restore |
| `plot_tx_filter_hil.py` | JSON in, PNGs out; runs without hardware |
| `test_tx_filter_hil.py` | Self-tests for the measurement maths, no hardware |

Run the self-tests after changing anything in `measure.py` or `tests.py`:

```bash
./venv/bin/python tx_filter_hil/test_tx_filter_hil.py
```

They include a check that the comparison **fails** when handed a simulated
−8.125 % shift, and that it names it as that specific regression rather than as
generic drift. That is what stops the suite quietly passing everything.

`bandtable.py` mirrors constants from `DSP_FIR.cpp` and `DSP.cpp` by hand.
Nothing detects a firmware value edited without updating it, so re-check that
file when the transmit filter design constants change. `TX_AUDIO_CORNER_3DB_HZ`
and `TX_STOPBAND_*` in particular were derived by evaluating the generated tap
sets, and their derivation is recorded in the comments beside them.
