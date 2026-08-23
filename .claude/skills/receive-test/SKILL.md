---
name: receive-test
description: Run the receive-chain integrity test on the SDR. Samples the SDR's demodulated audio output on an Analog Discovery 2 and verifies it is a clean 1 kHz tone at ~234 mV RMS with no interruptions or discontinuities. An external signal generator must already be driving the SDR's RF input. Use when the user asks to test the receiver, verify the receive chain, check audio output, or run a receive-chain test.
when_to_use: "test receiver", "test receive chain", "check audio output", "verify demodulation", "receive chain test", "RX test"
argument-hint: "[--duration SECONDS] [--expected-tone HZ] [--expected-rms VOLTS] [...other thresholds]"
allowed-tools: Bash(python3 *receive_chain_test.py*) Bash(*venv/bin/python *receive_chain_test.py*) Bash(ls /dev/ttyACM*) Read
user-invocable: true
---

# Receive Chain Integrity Test

Run `receive_chain_test.py` to verify the SDR receive chain is demodulating an externally-supplied RF input into a clean audio tone at the expected frequency and amplitude.

## Hardware setup

- **External signal generator** drives the SDR's RF / ADC input. The script does NOT generate this signal — the user is responsible for the generator settings.
- **Analog Discovery 2** must be connected via USB.
- **AD2 Scope Ch 1** <- SDR **audio output**.

With the generator and SDR tuned correctly, the audio output should be a 1 kHz sine wave at roughly 234 mV RMS.

If the AD2 is not plugged in the script will exit with code 2.

## Running the test

```bash
/home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/venv/bin/python \
  /home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/receive_chain_test.py \
  --duration 0.200 \
  --min-snr 25 --max-env-cv 0.05
```

The capture must be at least 100 ms (`--duration 0.1`); default is 200 ms. Increase to `--duration 1.0` if you want a more confident dropout / discontinuity check.

`--min-snr 25 --max-env-cv 0.05` are tuned to the known-good baseline of this receive chain when driven by the external signal generator (typical ~36 dB SNR, ~0.01 envelope CV). They give some headroom over the baseline so genuine regressions trip the check.

### Exit codes

| Code | Meaning |
|------|---------|
| 0    | PASS — all checks passed |
| 1    | FAIL — one or more checks failed (signal present but not clean) |
| 2    | ERROR — AD2 unreachable, libdwf failure, or invalid arguments |

### What gets checked

| Check | Default threshold | What it catches |
|-------|------------------|-----------------|
| `frequency` | within ±20 Hz of 1 kHz | wrong IF / wrong tuning, miscoded NCO |
| `amplitude_rms` | within ±25% of 234 mV | gain stage broken / saturated, generator level wrong |
| `snr` | tone vs everything else ≥ 25 dB (skill default; script default 30 dB) | noisy / distorted demodulation |
| `envelope_stability` | analytic-signal envelope CV ≤ 0.05 (skill default; script default 0.10) | amplitude wobble, AM artifacts |
| `window_dropout` | min 10 ms-window RMS / median ≥ 0.70 | intermittent dropouts / muted spans |
| `sample_jump` | max Δsample ≤ 3× theoretical for measured peak | step glitches, missing chunks |

The frequency and amplitude tolerances cover the headline "correct frequency / correct amplitude" requirement. The other three are the "no interruptions or discontinuities" checks.

### Output

- Prints a per-check `[OK ]` / `[BAD]` summary plus the headline numbers.
- Writes a PNG plot (time-domain trace + FFT magnitude) to `code/tools/receive_chain_<timestamp>.png` by default; override with `--plot path.png`.
- Add `--json` to also emit a single-line JSON document with the full analysis on stdout, after the human summary.

## Tuning thresholds

The skill's recommended `--min-snr 25 --max-env-cv 0.05` sit a comfortable margin above the known-good baseline (~36 dB SNR, ~0.01 CV with the external generator) so they trip on real regressions, not noise. Loosen only if the user reports that the generator level or wiring has changed:

- `--min-snr 15` — if the generator output has been reduced or there's added cabling loss
- `--max-env-cv 0.10` — if a small amount of amplitude wobble is acceptable
- `--rms-tol 0.30` — to widen the ±25% amplitude tolerance to ±30%

Don't change `--freq-tol` unless the test setup is using a non-standard tuning offset — a wrong dominant frequency is almost always a real bug.

## Interpreting failures

| Failed check(s) | Most likely cause |
|-----------------|-------------------|
| `frequency` only | NCO / tuning offset wrong, or the external generator frequency changed |
| `amplitude_rms` near zero | audio path muted, gain stage dead, wrong scope range, or generator off |
| `amplitude_rms` clipped at the scope range | bump `--scope-range 5.0` and retry |
| `snr` low, `frequency` OK | broadband noise in the audio path — open the PNG and look at the FFT |
| `envelope_stability` or `window_dropout` | intermittent demodulation — check buffer underruns / DMA timing |
| `sample_jump` only | individual glitches, e.g. one chunk lost in the audio buffer |

When the test fails, view the PNG with `Read` — the time-domain trace tells you whether you have a clean tone with noise versus an interrupted / glitched waveform, and the FFT shows whether the energy is concentrated at 1 kHz or spread out.

Do not edit `receive_chain_test.py` in response to failures — the failures indicate a problem with the radio's receive chain, not a bug in the test program.

## Reporting

End with one line per check (PASS/FAIL + headline number) and an overall verdict. Mention the PNG path so the user can inspect the trace. If any check failed, briefly point at the most likely cause from the table above.
