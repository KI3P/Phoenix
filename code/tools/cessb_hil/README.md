# CESSB bench test, hardware in the loop

Quantifies Controlled Envelope SSB on the real radio. The AD2 drives the
microphone input with repeatable signals, and the exciter's I and Q outputs are
captured together. CESSB is switched off and on over CAT (`PR0;` / `PR1;`) at
every point, so each result is an A/B comparison on the same signal. The
offline model's prediction for the identical signal is reported alongside.

Design and background: `wiki/roadmap/cessb.md`. Model: `../cessb_model/`.

## Wiring and setup

The same rig as `../tx_filter_hil/`:

```
AD2 W1  ------> radio microphone input
AD2 Ch1 <------ exciter I output   (order auto-detected)
AD2 Ch2 <------ exciter Q output
AD2 GND ------- radio ground
CAT     ------- /dev/ttyACM0       (single-port USB build; --cat-port otherwise)
```

- **Firmware:** needs CAT `PR`, so the CESSB branch from commit "CESSB: CAT PR" onwards.
  The processor-level and meter checks also need `PL` and `RM` (Phase 2); without them
  those checks are skipped with a warning.
  For the KI3P prototype, flash with `KI3P_prototype.patch` applied.
- **Radio state:** the radio must be in SSB (USB or LSB) and receiving.
- **Dummy load:** the radio is keyed for about 1.5 minutes at the set SSB power, so fit a
  dummy load. The suite reads the power setting and restores it; it halves it briefly
  for the power-tracking check.
- **Restores:** modulation, CESSB, power and mic gain (if `--mic-gain` is given) are
  restored at the end, on error and on Ctrl-C.

## Running

```bash
cd code/tools
./venv/bin/python cessb_hil/cessb_hil_test.py                 # everything, ~3 min
./venv/bin/python cessb_hil/cessb_hil_test.py --stimuli two_tone --drives 6
./venv/bin/python cessb_hil/test_cessb_hil.py                 # self-tests, no hardware
```

Results go to `results/cessb_hil_<timestamp>.{json,md}` plus two plots.
Exit codes: `0` pass, `1` fail, `2` error, `3` aborted.

## What it measures

1. **Ceiling.** A 1 kHz tone is stepped up in 1.5 dB steps. With CESSB off the output
   follows the input; with it on, the output follows until the ceiling and then holds.
   The held level is the ceiling, and every other level is quoted against it.
   Checks:
   - the held output is flat;
   - below the ceiling, on and off agree (only at AWG levels ≥ 20 mV; see below).
2. **Power tracking.** Halving the SSB power must move the ceiling by
   `10·log10(P½/P)`, because `TXGain` maps the ceiling onto the requested power (PEP).
2b. **Processor level and meter** (needs Phase 2 firmware: CAT `PL` and `RM`). A tone 6 dB
   below the ceiling, at levels 0, 3, 6 and 10 dB, must come out 6 dB below, 3 dB below,
   then at the ceiling. The compression meter read over `RM` must show the dB over the
   ceiling at 2 dB per dot. The rest of the run uses level 0 dB, and the original `PL` is
   restored at the end.
3. **Stimuli,** each at 0, 3, 6 and 9 dB above the ceiling, CESSB off and on. All are
   periodic, so one period fits the AWG's 4096-sample buffer and every run gets the same
   signal:

   | Stimulus | What it is |
   |---|---|
   | `two_tone` | 700 + 1900 Hz. Odd-order products land on known frequencies; 2f1−f2 is in the opposite sideband |
   | `vowel_ah` | A steady synthetic "ah": a Rosenberg glottal pulse train at 125 Hz through three formant resonators |
   | `syllable_ah` | The same vowel in a raised-cosine burst, one per 0.25 s. The voice stand-in: in the model it gains 2.4 / 4.1 / 5.2 dB at 3 / 6 / 9 dB, against 2.8 / 4.6 / 5.3 dB for real speech (W9GR clip) |

   For each, the suite reports:
   - the peak envelope relative to the ceiling;
   - talk-power gain at equal peak (wanted-sideband power only);
   - opposite-sideband and out-of-channel energy;
   - the odd-order products, for the two-tone.

## Reading the numbers: what the bench can and cannot resolve

- **The AD2 is inaccurate at a few millivolts.** At 5 mV its output moves ±1 dB from one
  capture to the next, with CESSB on or off; at 50 mV captures agree to 0.06 dB. The
  transparency check ignores sweep points below `--min-accurate-amplitude` (20 mV).
- **The opposite sideband is the exciter's, not CESSB's.** On the KI3P prototype the
  image of a tone moves between about −40 and −63 dBc from one capture to the next, at
  every frequency, with CESSB on or off. That is the radio's own image floor. CESSB's
  contribution is −57 to −100 dBc in the model, and the gtest
  `TransmitChainCESSB.DoesNotImageTheChannel` shows the stage removes images (−114 dBc).
  So the opposite-sideband check is only a gross-failure guard (−25 dBc): it would catch
  a wrongly centred stage, not a few dB.
- **2f1−f2 is above the DSP figure.** It reads about −48 to −50 dBc with CESSB on, against
  −55.8 dBc from the firmware at 176.4 ksps. At 0 dB drive, where CESSB barely acts, it
  reads −50 dBc on and off alike, and with CESSB off it falls 3 dB per 3 dB of drive. That
  is a fixed spur at −500 Hz on the bench, not a CESSB product.
- **"Peak with CESSB on" includes regrowth.** It is measured after the codec and the analog
  exciter, so it includes whatever peak regrowth they add. The model, which stops at the
  DAC, gives 1.0–1.4 %.

## At RF: `cessb_rf_test.py`

The same AD2 stimuli, but measured at the transmitter's output with the CMU200,
after the RF board, the PA and the low-pass filters.

```
AD2 W1     ------> radio microphone input
TX output  ------> dummy load, 60 dB tap ------> CMU200 RF1
CAT        ------- /dev/ttyACM0
```

- **Radio:** on the band to test (20 m by default; checked, never changed), in SSB.
  Main-board I/Q connected to the RF board. Dummy load.
- **CMU200:** driven through the bench's own driver
  (`~/Sync/Ham/015_CMU200_Control/mcp_server/cmu200.py`; `CMU200_DRIVER_DIR` to move it).
  Release the MCP server's hold on the GPIB adapter first (`release_instrument`). The
  tools venv needs `pyusb`.
- **Peak and average:** the CMU200's NPOWer meter with its 1 MHz filter gives the average
  and the largest instantaneous power: PEP. Checked on the bench: a steady tone reads
  max − avg = 0.23 dB, a linear two-tone 3.04–3.16 dB (theory 3.01).
- **Never overdriven with CESSB off.** CESSB off is measured only below the ceiling: the
  transparency points (≥ 3 dB below) and the linear reference (`--ref-db`, −1 dB).
- **Keyed per section**, unkeyed for `--rest` seconds between sections.

```bash
./venv/bin/python cessb_hil/cessb_rf_test.py                    # ~6 min, ~3 min key-down
./venv/bin/python cessb_hil/cessb_rf_test.py --stimuli two_tone --drives 6
./venv/bin/python cessb_hil/test_cessb_rf.py                    # self-tests, no hardware
```

It measures:

1. **The ceiling:** the held PEP must equal the power setting through the tap (±1 dB,
   which covers the tap's tolerance), be flat, and agree with CESSB off below the ceiling.
2. **Power tracking:** a 40 % setting moves the held PEP by 10·log10(0.4).
3. **Stimuli:**
   - PEP against the ceiling;
   - talk-power gain at equal PEP, computed as PAPR of the reference minus PAPR with CESSB on;
   - average watts;
   - narrow spectra (±7 kHz, 100 Hz RBW): opposite sideband, out of channel, carrier, and
     the two-tone's odd-order products;
   - a wide max-hold sweep (±25 kHz) for the syllable.

**Spectral limits are set by the transmitter, not CESSB.** At RF the PA's own
intermodulation sets the floor. On the KI3P prototype at 5 W, the linear two-tone with
CESSB off already has 2f1−f2 at −30 dBc (−36 dB re PEP), and its out-of-channel power is
−48 dB re PEP. The RF chain also compresses about 1 dB over the top 7 dB below full power.
CESSB's own out-of-channel output is −97 dBc in the model.

- **Two-tone (steady):** with CESSB on, the opposite sideband, 2f1−f2 and out-of-channel
  power may exceed the linear two-tone reference by at most `--max-rise-db` (3 dB).
- **Voice (`syllable_ah`):** with CESSB on, out-of-channel power in dB re PEP must stay
  below the linear two-tone's from the same run. This asks whether processed voice
  splatters less than the transmitter's standard IMD test signal. The opposite sideband
  is still compared with the voice's own linear reference.
- **Why voice isn't compared with its own linear reference for out-of-channel power:**
  CESSB raises average power about 4–6 dB at equal PEP, so the PA's intermodulation
  rises with it; at 9 dB drive that is about 4–8 dB more out-of-channel power re PEP than
  linear voice. Also, a ratio to the wanted-band power scattered by several dB between runs
  for the bursty syllable (4-sweep averages).

## Files

| File | Purpose |
|---|---|
| `cessb_hil_test.py` | The runner (exciter I/Q) |
| `cessb_rf_test.py` | The RF runner (CMU200 at the antenna connector) |
| `rf.py` | CMU200 NPOWer/spectrum access and spectrum measurements |
| `rf_report.py` | Markdown and plots for the RF runner |
| `test_cessb_rf.py` | Self-tests for `rf.py` |
| `stimuli.py` | The periodic test signals |
| `analysis.py` | Envelope, PAPR, spectral and product measurements on captured I/Q |
| `predict.py` | The model's prediction for the same stimulus, via `../cessb_model` |
| `ad2_custom.py` | `TxAd2` plus custom-waveform playback on W1 |
| `report.py` | Markdown and plots |
| `test_cessb_hil.py` | Self-tests on synthetic captures |
