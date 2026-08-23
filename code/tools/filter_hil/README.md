# Filter rate-independence test, hardware in the loop

Verifies on the real radio that the receive DSP filters land on their labelled
frequencies at every supported sample rate. An Analog Discovery 2 injects a
quadrature tone into the I/Q receive inputs and reads the demodulated audio back
off the speaker output; the suite sweeps, measures, and compares the two rates.

## Wiring

```
AD2 W1  ------> radio I receive input   (order does not matter, see below)
AD2 W2  ------> radio Q receive input
AD2 Ch1 <------ speaker output
AD2 GND ------- radio ground            (shared return is required)

/dev/ttyACM1 ... CAT control, 38400 baud
/dev/ttyACM0 ... diagnostic port, 115200 baud
```

**W1 and W2 can be either way round.** The suite works out which is which by
trying both quadrature senses and seeing which one the SSB filter passes, then
compensates in software. Nothing needs rewiring.

**Check how the speaker output is referenced before connecting Ch1.** If the
audio amplifier is bridged, neither terminal sits at ground and tying the scope's
`1-` to chassis shorts half the output stage. Connect Ch1 differentially across
the speaker terminals unless the output is known to be single-ended.

## Before running

- **AGC must be off.** It compresses the amplitude differences the suite
  measures, so every filter skirt would read flat. There is no CAT command for
  it; set it from the radio's menu. The suite refuses to run otherwise.
- **Firmware must have the `SR`, `CF`, `EQ` and `FL` CAT commands.** They were
  added alongside this suite. `printf 'SR;' > /dev/ttyACM1` should answer rather
  than returning `?;`.
- **Do not touch the front panel while it runs.** Menu interaction triggers a
  settings save, which would make the test configuration permanent.

## Running

```bash
cd code/tools
./venv/bin/python filter_hil/filter_hil_test.py
```

The defaults are the values that were found to work on this bench: AF volume 30,
5 V scope range, up to 1 V of drive, and a 12 dB SNR floor. Takes roughly ten
minutes for both rates. Results land in
`filter_hil/results/filter_hil_<timestamp>.{json,md}` plus PNGs.

Useful variations:

```bash
# Just the CW filters, one rate, verbose
./venv/bin/python filter_hil/filter_hil_test.py --tests iq,level,map,ref,cw \
    --rates 192000 -v

# Faster, coarser pass
./venv/bin/python filter_hil/filter_hil_test.py --grid-points 21 --eq-points 7

# Include the AM DC blocker (needs the MD4 -> AM CAT fix)
./venv/bin/python filter_hil/filter_hil_test.py --tests iq,level,map,ref,cw,eq,ssb,am

# Check arguments and the report path without touching hardware
./venv/bin/python filter_hil/filter_hil_test.py --dry-run

# Redraw figures from a previous run, no hardware
./venv/bin/python filter_hil/plot_filter_hil.py filter_hil/results/filter_hil_*.json
```

Exit codes: `0` pass, `1` fail, `2` error (no hardware, or preflight refused),
`3` aborted.

## How it works

The receive chain shifts **twice** before decimating: by Fs/4 (`FreqShiftFs4`)
and then by the fine tune plus any CW sidetone (`FreqShiftF`). So the injection
that demodulates to zero audio sits at

```
centre = |Fs/4 + fineTune|
```

and a wanted audio frequency is `centre +/- f`. Worked example from this bench,
at 176.4 ksps with the radio tuned to a fine tune of -12250 Hz:

| Fs/4 | fine tune | centre | inject for 1 kHz |
|---|---|---|---|
| 44100 | -12250 | 31850 | 32850 |
| 48000 (at 192 ksps) | -12250 | 35750 | 36750 |

**The fine tune is the part that is easy to forget and impossible to work
around.** It is whatever the operator last tuned to and is routinely several
kilohertz, so leaving it out puts every injection far outside the passband: the
radio stays silent, and no amount of extra drive helps. If the tone on the
radio's spectrum display is not sitting under the blue fine-tune bar, this is
why.

Recomputing Fs/4 per rate is the crux of the whole exercise, and the suite
measures the relationship rather than assuming it (test `map`). That test also
reports how far the real demodulation centre sits from nominal - about
-180 ppm on this radio, which is the Teensy's fractional I2S clock, not an
error.

Every response is the **difference between two captures at the same injected
frequency** — filter engaged and bypassed. Everything the two have in common
cancels exactly: the AWG's amplitude flatness, the codec front end, the
decimation rolloff, the SSB mask, the volume setting, the DAC and the speaker
amplifier. That is what makes an absolute-level measurement on a speaker output
good enough to characterise a filter.

| Filter | Measured as |
|---|---|
| CW audio filters | `level(CF=k) - level(CF=5)` |
| Equaliser cells | one cell at 100, the rest at 0, against the flat reference |
| SSB convolution filter | `level(FW=x) - level(FW=widest)` |
| AM DC blocker | envelope level at `f_mod`, against 300 Hz |

## Reading the result

**The primary criterion is rate invariance, not absolute accuracy.** A corner
measured at 192 ksps must land at the same frequency at 176.4 ksps. The bug this
suite exists to catch moves it by **-8.125 %** (`176400/192000`); the tolerance is
1.5 %, so there is a factor of five between correct and broken. Absolute accuracy
against the labelled frequency is checked too, but more loosely, because a smooth
tilt anywhere in the analog path can bias it without saying anything about the
firmware.

Look at `_cw_overlay.png` first. Both rates should lie on top of each other and
well clear of the dotted line marking where the old frozen tables would have put
them.

**The SSB filter is the control.** It has always derived its coefficients from
the true sample rate, so it was never affected by the bug. If it shows a shift,
the rig or the analysis is wrong, not the firmware.

Some equaliser cells are marked *edge limited*, meaning the SSB filter rather
than the cell is shaping what was measured. Cells 0 and 1 (198 and 250 Hz) sit at
or below the 200 Hz low cut, and cell 13 (4000 Hz) sits in the decimation skirt,
whose corner is a fraction of the sample rate and is *meant* to move with it. The
suite opens the SSB filter up before sweeping the cells so the top ones are
measurable at all, and marks a cell edge limited by comparing it against the
passband actually in use. Their absolute tolerance is relaxed; their rate
invariance is not, since the same skirt applies at both rates.

## When it fails

| Symptom | Likely cause |
|---|---|
| Preflight aborts on AGC | AGC is on. Turn it off in the radio's menu. |
| `iq_order` reports negative SNR for all four combinations, and the speaker output measures a couple of hundred mV RMS with the AWG silent | AGC is on and running the gain up on a quiet band, burying the injected tone 25-30 dB under the hiss. Confirm with `PD;` — if the tone is visible in the radio's own spectrum but not in the audio, the DSP is receiving it and the problem is downstream gain. |
| `iq_order` finds no audio either way | No shared ground, radio muted, W1/W2 not connected - or the fine tune was not accounted for (see "How it works") |
| Everything reads as noise and the tone on the radio's display is nowhere near the blue fine-tune bar | The injection is not allowing for `fineTuneFreq_Hz` |
| `iq_order` rejects the wrong side by only a few dB | The tone is not reaching both I and Q. Check both W1 and W2; the suite reports each channel's contribution as a phase margin. |
| `mapping` offset is wrong | The firmware did not reconfigure the Fs/4 shift for the new rate. Everything downstream is meaningless; that rate is skipped. |
| Everything reads -8.125 % | The frozen coefficient tables are back |
| SSB control also shifts | The rig or the analysis is wrong, not the firmware |
| Reference sweep is flat | AGC or a limiter is active despite the preflight check |
| Audio clips or the noise floor is hundreds of mV | AF volume too high. It rises very steeply: 54 clipped a 5 V range on this radio, 30 gives ~30 mV of noise |
| A corner comes back as "not measured" | The crossing is below the rig's dynamic range. Measured through a receiver the tone vanishes into the noise about 5 dB under the passband, which is why the SSB edge is called at -3 dB rather than -6 dB |
| Points marked invalid | Check `lost`/`corrupted` in the JSON: the scope rate may be too high, or the drive too low or clipping |

Do not edit the test in response to a failure — a failure means the radio's
filters are not where they should be, or the rig is not connected as documented.

## Files

| File | Contents |
|---|---|
| `ad2.py` | ctypes wrapper for libdwf: quadrature AWG, record-mode capture |
| `radio.py` | CAT and diagnostic serial, `ED;` parsing, state save/restore |
| `measure.py` | tone metrology, corner and peak finding |
| `bandtable.py` | design constants mirrored from the firmware |
| `tests.py` | the individual tests and the cross-rate comparison |
| `report.py` | JSON, stdout summary, Markdown report |
| `filter_hil_test.py` | CLI entry point and sequencing |
| `plot_filter_hil.py` | JSON in, PNGs out; runs without hardware |
| `test_filter_hil.py` | self-tests for the measurement maths, no hardware |

Run the self-tests after changing anything in `measure.py` or `tests.py`:

```bash
./venv/bin/python filter_hil/test_filter_hil.py
```

They include a check that the comparison **fails** when handed a simulated
-8.125 % shift, which is what stops the suite quietly passing everything.

`bandtable.py` mirrors constants from `DSP_FIR.cpp` and `Globals.cpp` by hand.
Nothing detects a firmware value edited without updating it, so re-check that
file when the filter design constants change.
