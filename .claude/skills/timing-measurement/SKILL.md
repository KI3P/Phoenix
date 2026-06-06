---
name: timing-measurement
description: Measure execution time of functions or code regions in the PhoenixSketch firmware by instrumenting the source with Flag() calls and capturing the resulting pin transitions on an Analog Discovery 2. Use when the user wants to profile, time, benchmark, or characterize how long a piece of radio code takes to run.
when_to_use: "how long does X take", "measure execution time", "profile function", "time the radio code", "benchmark code on radio", "characterize timing", "how fast does X run"
argument-hint: "<what to measure, e.g. 'PerformSignalProcessing' or 'each step of loop()'>"
allowed-tools: Bash(arduino-cli*) Bash(ls /dev/ttyACM*) Bash(*flag_timing.py*) Bash(git diff*) Bash(sleep*) Read Edit
user-invocable: true
---

# Timing Measurement

Measure how long a piece of code in the PhoenixSketch firmware takes to execute by instrumenting it with `Flag()` calls, capturing the resulting pin transitions on the AD2, and analyzing them with `code/tools/flag_timing.py`.

## How it works

`Flag(uint8_t val)` (defined in `Globals.cpp`) writes a 4-bit value to Teensy pins 28-31. The AD2 records those transitions; `flag_timing.py` decodes them back into flag values and reports the time between any pair. Each `Flag()` call takes roughly **1 µs** (four sequential `digitalWrite()`s); subtract from intervals when sub-µs precision matters.

## Hardware requirements

- Radio connected via USB at `/dev/ttyACM0`
- AD2 connected via USB with Teensy pins 28→DIO1, 29→DIO2, 30→DIO3, 31→DIO4
- `libdwf.so` available (Digilent WaveForms SDK)

Check both before starting. If the radio is missing, stop and tell the user. If the AD2 driver is missing, the capture step will fail with a clear error.

## Flag value allocation

Existing usage in `Loop.cpp::loop()`: **0, 1, 2, 4, 8**. Pick values from the unused set when instrumenting: **3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15**. Trigger on a value that doesn't collide with anything along the path so the AD2 fires at the right point.

Before instrumenting, grep for other `Flag(` calls in the area of interest to avoid colliding with code added in other parts of the firmware:

```bash
grep -rn "Flag(" /home/oliver/Sync/Ham/T41/Software/Phoenix/code/src/PhoenixSketch/
```

## Workflow

### 1. Understand what to measure

Parse the user's request to identify:

- **The code region(s)**: a function call site, a block inside a function, multiple stages in a sequence
- **The trigger condition**: does the code run every loop iteration (easy), only on user input (need to prompt user to press a key during capture), or only in a specific mode (CW/SSB/transmit — confirm with the user)
- **The expected magnitude**: sub-µs, µs, ms — drives the sample-rate choice in step 5

If the request is ambiguous (e.g. "time the DSP"), ask the user which function or block before editing source.

### 2. Plan the instrumentation

- **Single-region timing**: pick one unused flag value `A` for "start" and a second `B` for "end". Insert `Flag(A);` immediately before the region and `Flag(B);` immediately after.
- **Multi-stage timing**: pick a distinct unused value for each stage boundary so all transitions are visible.
- Mark every inserted line with the comment `// TEMP timing instrumentation` so it's trivial to find and revert.

State the plan back to the user — which files, which lines, which flag values — before editing if the change touches more than one file or more than ~4 insertion points.

### 3. Insert instrumentation

Use Edit to add the `Flag()` calls. Keep the rest of the file untouched.

### 4. Compile and flash

From `code/`:

```bash
cd /home/oliver/Sync/Ham/T41/Software/Phoenix/code && \
arduino-cli compile \
  --fqbn "teensy:avr:teensy41:usb=serial2,speed=600,opt=o1lto,keys=en-us" \
  --output-dir ../ArduinoOutput \
  src/PhoenixSketch/PhoenixSketch.ino

arduino-cli upload \
  --fqbn "teensy:avr:teensy41:usb=serial2,speed=600,opt=o1lto,keys=en-us" \
  --port /dev/ttyACM0 \
  --input-dir /home/oliver/Sync/Ham/T41/Software/Phoenix/ArduinoOutput \
  /home/oliver/Sync/Ham/T41/Software/Phoenix/code/src/PhoenixSketch/PhoenixSketch.ino
```

If compilation fails, report the error and stop — don't attempt upload, and revert the instrumentation before exiting.

### 5. Capture and measure

Wait ~3 s for the radio to reboot, then run `flag_timing.py`. The script has two acquisition paths and picks automatically (`--mode auto`):

- **Single-shot** (used when the requested capture fits in the AD2 on-board buffer, ~4-8K samples): low overhead, sub-µs trigger precision. Works well up to 10 MHz.
- **Record / streaming** (used for longer captures): streams samples over USB. The AD2's on-device ring is only ~4 KiB, so the script's Python polling loop has to drain it faster than `buffer_size / sample_rate`. Empirically, **250 kHz is the practical ceiling for clean 1-second captures**; 500 kHz typically loses ~1% of samples; 1 MHz loses 5–25%.

Pick the sample rate by what you're measuring. Resolution is one sample period; buffer cost scales linearly with `sample_rate × duration` (each sample is 2 bytes).

| Use case | --sample-rate | --duration | Mode | Memory | Resolution |
|---|---|---|---|---|---|
| Single tight event < 100 µs | `10e6` (default) | omit | single | small | 100 ns |
| Single event 0.1 – 1 ms | `5e6` | omit | single | small | 200 ns |
| Multi-iteration profile, 1 s, ms-scale work | `250e3` | `1` | record | ~0.5 MB | 4 µs |
| Multi-iteration profile, 1 s, sub-ms events | `500e3` | `1` | record | ~1 MB | 2 µs (expect ~1% lost) |

Trigger on the first flag value in the sequence so the capture starts at a known point. For single-region timing use `--measure FROM TO` to get statistics over multiple loop iterations:

```bash
sleep 3 && \
/home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/venv/bin/python \
  /home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/flag_timing.py \
  --sample-rate 10e6 \
  --trigger flag --trigger-flag <START_FLAG> \
  --trigger-position 0.05 \
  --measure <START_FLAG> <END_FLAG> \
  --quiet
```

For a 1-second multi-iteration profile (many loop() cycles captured in a single run):

```bash
sleep 3 && \
/home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/venv/bin/python \
  /home/oliver/Sync/Ham/T41/Software/Phoenix/code/tools/flag_timing.py \
  --sample-rate 250e3 --duration 1 \
  --debounce-us 8 \
  --trigger flag --trigger-flag <START_FLAG> \
  --quiet > /tmp/profile.json
# Then: jq '.summary.flag_segment_stats' /tmp/profile.json
```

At 250 kHz the resolution is 4 µs, which is plenty for ms-scale draw work
but means very short transitions (Flag(4) event-processing windows, the
loop-idle Flag(0) gap) are absorbed into the trailing segment by debounce.
Bump `--sample-rate 500e3 --debounce-us 4` if you need to resolve sub-50-µs
events, accepting ~1% lost samples.

For multi-stage timing, omit `--measure` and read `summary.flag_segment_stats` from the JSON: each flag value's entry reports `count, min_s, max_s, mean_s, median_s, p95_s, p99_s, stddev_s, total_s` across all occurrences in the capture. The full per-event list is in `segments` (each with `flag, start_time_s, end_time_s, duration_s`).

If the capture times out with "Timed out waiting for acquisition" the instrumented code path isn't executing — check the radio mode, verify the user has done whatever action triggers the code (key press, PTT, etc.), and try again. Increase trigger position frac if you want more pre-trigger context.

If `metadata.lost_samples > 0`, USB couldn't sustain the requested rate. Drop the sample rate (e.g. 5 MHz → 1 MHz) and re-capture; the timing in that run is suspect. The script also exits non-zero (code 2) automatically when lost samples exceed `--max-lost-frac` (default 0.1%).

### 6. Analyze and report

From the JSON output, report:

- **Mean / median / p95 / p99 / max** of the measured interval(s) — for multi-iteration captures, p95 and p99 are the headline numbers for spotting tail regressions
- **Sample count** (how many iterations the capture saw)
- **Caveats**: subtract ~1 µs per intervening `Flag()` call if precision below 1 µs matters; flag any outliers
- **Anomalies**: unexpected flag values in the trace, very different mean vs. p99 (suggests intermittent slow paths), non-zero `lost_samples` (timing unreliable — re-capture at lower sample rate)

Present as a short table or bullet list. Include the raw JSON only if the user asks.

### 7. Revert instrumentation

This step is mandatory — do not leave temporary `Flag()` calls in the source tree.

1. Use Edit to remove every line tagged `// TEMP timing instrumentation` and the inserted `Flag()` call on the same line.
2. Confirm with `git diff code/src/PhoenixSketch/` that the only remaining diff is the pre-existing state (the file is back to where it started).
3. Re-compile and re-upload so the running firmware matches the source tree.

If you skip the re-flash, the radio will keep emitting the instrumentation pins on every loop iteration, which is confusing for the next person who uses the radio.

## Pitfalls

- **Two acquisition modes**: single-shot uses the AD2's ~8K-sample on-board buffer (low overhead, sub-µs trigger precision). Record mode streams over USB for arbitrary-length captures (1+ second). `--mode auto` (the default) picks one based on `sample_rate × duration`; force with `--mode {single,record}`.
- **Lost samples in record mode**: the AD2's on-device ring buffer is only ~4 KiB, and the Python streaming loop in `flag_timing.py` has to drain it faster than the device fills it. In practice this means ~250 kHz for clean 1-second captures; 500 kHz typically loses ~1%; 1 MHz loses 5–25%. `metadata.lost_samples` and `lost_fraction` report this. The script exits non-zero (code 2) when `lost_fraction` exceeds `--max-lost-frac` (default 0.1%). Drop the sample rate and re-capture, or raise `--max-lost-frac` if you're willing to accept some gaps in the timeline.
- **Debounce eats short segments**: default `--debounce-us 2.0` absorbs glitches from `Flag()`'s ~1 µs sequential writes and stays effective across the full 500 kHz – 10 MHz sample-rate range. It also absorbs intentional flag values shorter than 2 µs. Set `--debounce-us 0` if you need to see every transition.
- **Multi-iteration captures**: a 1-second record at ~10 ms per `loop()` iteration captures ~100 iterations. `summary.flag_segment_stats` reports per-flag p50/p95/p99 across all of them — use this to characterize variability and detect tail regressions when comparing runs.
- **Loop-coupled measurements**: if the instrumented code is inside `loop()`, other loop work (interrupts, display refresh) can perturb timing. Compare mean to p99 to spot this.
- **Mode-gated code**: SSB/CW/transmit code only runs when the radio is in that mode. Ask the user to set the mode before capture if needed. For UI-state-gated draw functions (e.g. `DrawHome` only runs in the HOME UI state), confirm the radio is in the right state for the whole capture.

## Reporting

End with a one- or two-line summary of what was measured and the headline number (e.g. `PerformSignalProcessing: 2.84 ms mean (min 2.81, max 2.91, n=4 iterations)`), then a sentence confirming the instrumentation has been reverted and the radio reflashed.
