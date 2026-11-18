# CESSB model

An offline model of the Phoenix SSB transmit chain with Controlled Envelope SSB
(W9GR) inserted at the 24 kHz node. It exists to answer, before any firmware
is written, the questions the proposal in `wiki/roadmap/cessb.md` depends on:
how much talk power CESSB buys in *this* chain, which variant to port, what
`gainCompensate` should be, and whether the stages after it regrow the peaks.

No hardware is needed.

```bash
python3 test_cessb_model.py      # self-tests (~1 s)
python3 run_model.py             # sweeps, ~90 s; writes results/
python3 run_model.py --wav other.wav --rates 192k
```

`run_model.py` reads W7PUA's `W9GR48.WAV` voice clip and `clipperOut` filter
table from the OpenAudio library (`~/Arduino/libraries/OpenAudio_ArduinoLibrary`).
Neither is copied into this repository. The tests that need the library skip
themselves if it is not installed.

## Files

| File | What it is |
|---|---|
| `phoenix_tx.py` | `TransmitProcessing()` from the firmware's own coefficients: fixed tables parsed out of `DSP_FIR.cpp`, generated stages from a line-for-line port of `CalcFIRCoeffs()`. |
| `cessb.py` | The variants: **Z**, a port of W7PUA's `radioCESSB_Z_transmit_F32` clipper/overshoot section; **C**, the same processing with the SSB channel shifted to straddle DC first; and **q15_overload**, what the firmware does today when overdriven (`arm_float_to_q15` saturating I and Q separately). |
| `run_model.py` | Drive and `gainCompensate` sweeps on voice, a two-tone test, passband below the ceiling, delay and CPU estimate, at 192 and 176.4 ksps. |
| `test_cessb_model.py` | Self-tests. The important one checks that the model's −3 dB transmit corner matches the firmware's own unit test to within 5 Hz. |
| `results/` | The last run: `results.md` (tables), `results.json`, and the plots. |

## How far to trust it

- **The DSP is the firmware's.** The −3 dB corner agrees with the result of
  `TransmitChain176k.AudioBandwidthHoldsAcrossRates` to about 1 Hz at both rates.
- **BandEQ is taken as flat.** It sits before the clipper and only reshapes the
  voice spectrum, so it changes the voice and nothing about the variants' behaviour.
- **The model stops at the DAC.** Peak regrowth in the codec, the analog exciter
  and the PA filters is not modelled. That is what Phase 3 of the proposal
  measures on the bench.
