---
name: transmit-test
description: Run the PTT signal integrity test on the radio transmitter hardware. Use when the user asks to test the transmitter, run a PTT test, check I/Q outputs, or verify transmit signal quality. Requires an Analog Discovery 2 connected to the radio.
when_to_use: "test transmitter", "run PTT test", "check I/Q", "verify transmit", "signal integrity test", "test radio"
argument-hint: "[iterations]"
arguments: [iterations]
allowed-tools: Bash(python3 transmit_test.py*) Read
user-invocable: true
---

# Transmit PTT Signal Integrity Test

Run `transmit_test.py` to verify the radio transmitter's I/Q outputs are clean sinusoids across repeated PTT cycles. This detects intermittent failures where PTT activation produces discontinuities, zeroed channels, or distorted waveforms.

## Hardware requirements

An Analog Discovery 2 must be connected via USB with the following wiring:
- **DIO-0** controls PTT (LOW = transmit, HIGH = off)
- **W1** drives the microphone input (500 Hz, 100 mV sine)
- **Scope Ch1/Ch2** read the I and Q quadrature outputs

## Running the test

Run from the `transmit` directory:

```
cd /home/oliver/Sync/Ham/T41/Software/Phoenix/code/test/transmit
python3 transmit_test.py -n $iterations
```

If no iteration count is provided, default to 5 for a quick smoke test. For a full test run, use `-n 100`.

### Interpreting results

The program exits with code 0 if all iterations pass, or code 1 if any fail. Each iteration reports:

- **PASS/FAIL** status
- **RMS amplitude** of each channel (expect 50-200 mV; below 5 mV = dead channel)
- **SNR** in dB (expect >40 dB; below 20 dB = distorted)
- **Dominant frequency** (expect 500 Hz)

Failed iterations automatically save raw waveform data to `failures/` as `.npz` files along with a png plot.

### Typical failure modes

| Symptom | Likely cause |
|---------|-------------|
| RMS near zero on one/both channels | I/Q output not starting after PTT |
| Low SNR, correct frequency | Discontinuity or glitch in waveform |

### Common options

| Flag | Default | Description |
|------|---------|-------------|
| `-n`, `--iterations` | 100 | Number of PTT cycles |
| `--settle-time` | 0.5 | Seconds after PTT engage before capture |
| `--acquire-time` | 0.1 | Capture duration in seconds |
| `--interval` | 0.5 | Seconds between iterations |
| `--min-snr` | 20.0 | Minimum SNR (dB) to pass |
| `--save-all` | off | Save waveform data for passing iterations too |

## If the test fails

1. Report the summary line and any failure reasons to the user.
2. If `.npz` files were saved, you can load and inspect them:
   ```python
   import numpy as np
   data = np.load("failures/fail_iter0001_TIMESTAMP.npz")
   ch1, ch2 = data["ch1"], data["ch2"]  # I and Q waveform arrays
   sr = float(data["sample_rate"])       # 100000 Hz
   ```
3. Do NOT attempt to fix `transmit_test.py` based on test failures -- the failures indicate a hardware/firmware problem in the radio, not a bug in the test program.

## If the AD2 is not connected

The program will print `Failed to open device` and exit with code 1. This is expected if no AD2 is plugged in. Do not retry -- inform the user that the hardware is not available.
