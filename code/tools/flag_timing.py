#!/usr/bin/env python3
"""
Measure timing of code execution flow by capturing the Flag(uint8_t) signal
output on Teensy pins 28-31 via an Analog Discovery 2 (AD2) digital input.

Flag value encoding (set inside Globals.cpp::Flag(val)):
    pin 31 -> bit 0 (LSB)
    pin 30 -> bit 1
    pin 29 -> bit 2
    pin 28 -> bit 3 (MSB)
so Flag(val) writes the 4-bit value `val` (0..15) across the four pins.

Default physical wiring (Teensy pin -> AD2 DIO line):
    28 -> DIO1, 29 -> DIO2, 30 -> DIO3, 31 -> DIO4
The DIO mapping is overridable on the command line.

Output is a single JSON document on stdout, intended for ingestion by a
Claude Code Skill. All progress / diagnostic text goes to stderr (suppressed
with --quiet). The exit code is 0 on success, non-zero on any error; in the
error case stdout contains a JSON object with {"error": "..."} so the caller
can parse it uniformly.

Top-level JSON shape:
    {
      "metadata":     { sample_rate_hz, sample_period_s, buffer_size,
                        duration_s, trigger, debounce_us, pin_mapping, ... },
      "transitions":  [ {index, time_s, from_flag, to_flag}, ... ],
      "segments":     [ {flag, start_time_s, end_time_s, duration_s}, ... ],
      "summary":      { flag_segment_stats: { "<flag>": {count, min_s, ...} },
                        transition_counts:  { "<a>-><b>": N, ... } },
      "measurement":  (only if --measure FROM TO given) {
                        from_flag, to_flag, count, intervals_s,
                        min_s, max_s, mean_s, median_s, stddev_s }
    }
"""

import argparse
import ctypes
import json
import sys
import time

import numpy as np

# ----- Pin / bit mapping ---------------------------------------------------

# Teensy pin -> Flag bit position. Fixed by the Flag() implementation.
TEENSY_PIN_TO_BIT = {28: 3, 29: 2, 30: 1, 31: 0}

# Teensy pin -> AD2 DIO line (default; overridable via --dio-map).
DEFAULT_TEENSY_TO_DIO = {28: 1, 29: 2, 30: 3, 31: 4}


# ----- DWF library binding -------------------------------------------------

def _load_dwf():
    if sys.platform.startswith("win"):
        return ctypes.cdll.dwf
    if sys.platform.startswith("darwin"):
        return ctypes.cdll.LoadLibrary("/Library/Frameworks/dwf.framework/dwf")
    return ctypes.cdll.LoadLibrary("libdwf.so")


def _bind_dwf_argtypes(dwf):
    """Declare argtypes for the DWF entrypoints we use."""
    c_int = ctypes.c_int
    c_uint = ctypes.c_uint
    c_double = ctypes.c_double
    c_byte = ctypes.c_byte

    dwf.FDwfDeviceOpen.argtypes = [c_int, ctypes.POINTER(c_int)]
    dwf.FDwfDeviceCloseAll.argtypes = []
    dwf.FDwfGetLastErrorMsg.argtypes = [ctypes.c_char_p]

    dwf.FDwfDigitalInReset.argtypes = [c_int]
    dwf.FDwfDigitalInInternalClockInfo.argtypes = [c_int, ctypes.POINTER(c_double)]
    dwf.FDwfDigitalInDividerSet.argtypes = [c_int, c_uint]
    dwf.FDwfDigitalInBufferSizeSet.argtypes = [c_int, c_int]
    dwf.FDwfDigitalInBufferSizeInfo.argtypes = [c_int, ctypes.POINTER(c_int)]
    dwf.FDwfDigitalInSampleFormatSet.argtypes = [c_int, c_int]
    dwf.FDwfDigitalInAcquisitionModeSet.argtypes = [c_int, c_int]
    dwf.FDwfDigitalInTriggerSourceSet.argtypes = [c_int, c_byte]
    dwf.FDwfDigitalInTriggerPositionSet.argtypes = [c_int, c_uint]
    dwf.FDwfDigitalInTriggerSet.argtypes = [c_int, c_uint, c_uint, c_uint, c_uint]
    dwf.FDwfDigitalInConfigure.argtypes = [c_int, c_int, c_int]
    dwf.FDwfDigitalInStatus.argtypes = [c_int, c_int, ctypes.POINTER(c_byte)]
    dwf.FDwfDigitalInStatusData.argtypes = [c_int, ctypes.c_void_p, c_int]
    dwf.FDwfDigitalInStatusRecord.argtypes = [
        c_int,
        ctypes.POINTER(c_int),
        ctypes.POINTER(c_int),
        ctypes.POINTER(c_int),
    ]


# ----- Acquisition ---------------------------------------------------------

# DWF constants we need
TRIG_SRC_NONE = 0
TRIG_SRC_DETECTOR_DIGITAL_IN = 3
ACQMODE_SINGLE = 0
ACQMODE_RECORD = 3
STATE_DONE = 2


def _dwf_last_error(dwf):
    buf = ctypes.create_string_buffer(512)
    dwf.FDwfGetLastErrorMsg(buf)
    return buf.value.decode("utf-8", errors="replace")


def acquire(sample_rate_hz, buffer_size, trigger, trigger_pattern_mask,
            trigger_pattern_value, trigger_change_mask, trigger_position_frac,
            mode, log):
    """Capture digital samples from the AD2 and return them as a uint16 array.

    `mode` is one of "auto", "single", "record". In "auto" the path is chosen
    based on whether the requested capture fits in the device's on-board
    buffer. Single-shot is preferred for short windows (less overhead);
    record mode streams over USB so it supports arbitrary-length captures.

    Returns (samples, actual_sample_rate_hz, extras), where extras is a dict
    with keys: mode, lost_samples, corrupt_samples, device_buffer_size.
    Each sample's bits correspond to DIO0..DIO15 (bit 0 = DIO0).
    """
    dwf = _load_dwf()
    _bind_dwf_argtypes(dwf)

    hdwf = ctypes.c_int()
    log(f"Opening AD2 device...")
    if dwf.FDwfDeviceOpen(-1, ctypes.byref(hdwf)) != 1 or hdwf.value == 0:
        raise RuntimeError(f"Failed to open AD2: {_dwf_last_error(dwf)}")

    try:
        dwf.FDwfDigitalInReset(hdwf)

        # Internal clock is typically 100 MHz; compute divider for requested rate.
        clk = ctypes.c_double()
        dwf.FDwfDigitalInInternalClockInfo(hdwf, ctypes.byref(clk))
        internal_clock_hz = clk.value
        divider = max(1, int(round(internal_clock_hz / sample_rate_hz)))
        actual_rate = internal_clock_hz / divider
        log(f"Internal clock: {internal_clock_hz/1e6:.3f} MHz, "
            f"divider={divider}, actual sample rate {actual_rate/1e6:.6f} MHz")

        dwf.FDwfDigitalInDividerSet(hdwf, ctypes.c_uint(divider))

        # 16-bit samples cover DIO0..DIO15 in one word.
        dwf.FDwfDigitalInSampleFormatSet(hdwf, 16)

        # Device-reported max buffer size determines single-vs-record choice.
        max_buf = ctypes.c_int()
        dwf.FDwfDigitalInBufferSizeInfo(hdwf, ctypes.byref(max_buf))
        device_max = max_buf.value

        if mode == "auto":
            mode = "single" if buffer_size <= device_max else "record"
            log(f"Auto-selected acquisition mode: {mode} "
                f"(requested {buffer_size} samples, device max {device_max}).")
        elif mode == "single" and buffer_size > device_max:
            log(f"Forced single mode but requested buffer {buffer_size} > "
                f"device max {device_max}; clamping to {device_max}.")
            buffer_size = device_max

        if mode == "single":
            dwf.FDwfDigitalInBufferSizeSet(hdwf, buffer_size)
            dwf.FDwfDigitalInAcquisitionModeSet(hdwf, ACQMODE_SINGLE)
        else:
            # Record mode: set mode FIRST, then query the device max again
            # (some firmwares report different maxima per acquisition mode),
            # then size the on-device ring buffer to the largest available
            # for USB headroom. Total capture length is set via
            # TriggerPositionSet.
            dwf.FDwfDigitalInAcquisitionModeSet(hdwf, ACQMODE_RECORD)
            dwf.FDwfDigitalInBufferSizeInfo(hdwf, ctypes.byref(max_buf))
            device_max = max_buf.value
            dwf.FDwfDigitalInBufferSizeSet(hdwf, device_max)

        # Trigger configuration.
        if trigger == "none":
            dwf.FDwfDigitalInTriggerSourceSet(hdwf, ctypes.c_byte(TRIG_SRC_NONE))
            if mode == "record":
                # In record mode, TriggerPositionSet specifies total samples
                # to acquire (not a buffer offset like in single mode).
                dwf.FDwfDigitalInTriggerPositionSet(
                    hdwf, ctypes.c_uint(buffer_size))
        else:
            dwf.FDwfDigitalInTriggerSourceSet(
                hdwf, ctypes.c_byte(TRIG_SRC_DETECTOR_DIGITAL_IN))
            if mode == "single":
                # Pre-trigger position measured in samples from the start of
                # the buffer; default is mid-buffer so we see context either
                # side of the trigger event.
                pre_trig_samples = int(buffer_size * trigger_position_frac)
                dwf.FDwfDigitalInTriggerPositionSet(
                    hdwf, ctypes.c_uint(buffer_size - pre_trig_samples))
            else:
                # Record mode: TriggerPositionSet is total samples after trigger.
                dwf.FDwfDigitalInTriggerPositionSet(
                    hdwf, ctypes.c_uint(buffer_size))

            fs_low = trigger_pattern_mask & ~trigger_pattern_value
            fs_high = trigger_pattern_mask & trigger_pattern_value
            fs_rise = trigger_change_mask
            fs_fall = trigger_change_mask
            dwf.FDwfDigitalInTriggerSet(
                hdwf,
                ctypes.c_uint(fs_low),
                ctypes.c_uint(fs_high),
                ctypes.c_uint(fs_rise),
                ctypes.c_uint(fs_fall),
            )
            log(f"Trigger configured: mode={trigger}, "
                f"low=0x{fs_low:04x}, high=0x{fs_high:04x}, "
                f"rise=0x{fs_rise:04x}, fall=0x{fs_fall:04x}")

        # Give analog/digital front end a moment to settle.
        time.sleep(0.5)

        log(f"Starting {mode}-mode acquisition for {buffer_size} samples "
            f"({buffer_size/actual_rate*1e3:.3f} ms)...")
        dwf.FDwfDigitalInConfigure(hdwf, 0, 1)

        extras = {
            "mode": mode,
            "lost_samples": 0,
            "corrupt_samples": 0,
            "device_buffer_size": device_max,
        }

        if mode == "single":
            sts = ctypes.c_byte()
            deadline = time.time() + 30.0
            while True:
                dwf.FDwfDigitalInStatus(hdwf, 1, ctypes.byref(sts))
                if sts.value == STATE_DONE:
                    break
                if time.time() > deadline:
                    raise RuntimeError(
                        "Timed out waiting for acquisition to complete "
                        "(trigger may never have fired)")
                time.sleep(0.005)

            log("Acquisition done; reading samples.")
            raw = (ctypes.c_uint16 * buffer_size)()
            dwf.FDwfDigitalInStatusData(hdwf, raw, buffer_size * 2)
            samples = np.frombuffer(raw, dtype=np.uint16).copy()
            return samples, actual_rate, extras

        # ---- Record (streaming) mode ----------------------------------------
        # Preallocate the destination numpy buffer and read DWF chunks
        # directly into it via pointer arithmetic. This avoids per-iteration
        # ctypes allocation and an extra memcpy, both of which cost enough
        # to overflow the AD2's small on-device ring buffer at >=500 kHz.
        samples = np.empty(buffer_size, dtype=np.uint16)
        samples_base = samples.ctypes.data
        idx = 0
        c_available = ctypes.c_int()
        c_lost = ctypes.c_int()
        c_corrupt = ctypes.c_int()
        sts = ctypes.c_byte()
        # Allow up to 3x expected duration plus 10 s slack for trigger wait.
        expected_s = buffer_size / actual_rate
        deadline = time.time() + max(30.0, expected_s * 3 + 10.0)
        last_log = time.time()

        while idx < buffer_size:
            dwf.FDwfDigitalInStatus(hdwf, 1, ctypes.byref(sts))
            dwf.FDwfDigitalInStatusRecord(
                hdwf, ctypes.byref(c_available),
                ctypes.byref(c_lost), ctypes.byref(c_corrupt))
            extras["lost_samples"] += c_lost.value
            extras["corrupt_samples"] += c_corrupt.value

            n = c_available.value
            if n > 0:
                n = min(n, buffer_size - idx)
                dst = ctypes.c_void_p(samples_base + idx * 2)
                dwf.FDwfDigitalInStatusData(hdwf, dst, n * 2)
                idx += n
                # When draining backlog, loop again immediately without
                # sleeping so we don't fall behind.
                if n >= 256:
                    continue

            if sts.value == STATE_DONE and c_available.value == 0:
                break

            now = time.time()
            if now - last_log > 1.0:
                log(f"Record progress: {idx}/{buffer_size} samples "
                    f"({idx/buffer_size*100:.1f}%), "
                    f"lost={extras['lost_samples']}, "
                    f"corrupt={extras['corrupt_samples']}")
                last_log = now
            if now > deadline:
                raise RuntimeError(
                    f"Timed out in record mode after {idx}/{buffer_size} "
                    f"samples (trigger may never have fired, or USB throughput "
                    f"is insufficient at {actual_rate/1e6:.3f} MHz)")
            # Short sleep only when ring is mostly empty; long enough to let
            # the device accumulate a useful chunk, short enough to stay
            # well below the ring-buffer overflow time.
            time.sleep(0.0002)

        log(f"Record done; captured {idx} samples, "
            f"lost={extras['lost_samples']}, corrupt={extras['corrupt_samples']}.")
        return samples[:idx], actual_rate, extras
    finally:
        dwf.FDwfDeviceCloseAll()


# ----- Analysis ------------------------------------------------------------

def decode_flag_values(samples, teensy_to_dio):
    """Map 16-bit DIO samples down to the 4-bit Flag value at each sample."""
    flag = np.zeros(samples.shape, dtype=np.uint8)
    for teensy_pin, bit in TEENSY_PIN_TO_BIT.items():
        dio = teensy_to_dio[teensy_pin]
        bit_vals = ((samples >> dio) & 1).astype(np.uint8)
        flag |= bit_vals << bit
    return flag


def find_segments(flag_values):
    """Return contiguous-run segments as (start_idx, end_idx_exclusive, flag).

    A segment covers samples [start, end). end == start + length.
    """
    if flag_values.size == 0:
        return []
    change_idx = np.flatnonzero(np.diff(flag_values.astype(np.int16)) != 0) + 1
    starts = np.concatenate(([0], change_idx))
    ends = np.concatenate((change_idx, [flag_values.size]))
    return [(int(s), int(e), int(flag_values[s])) for s, e in zip(starts, ends)]


def debounce_segments(segments, min_samples):
    """Drop transient segments shorter than min_samples by merging them into
    their predecessor. This collapses the brief intermediate states produced
    by Flag()'s four sequential digitalWrite() calls.
    """
    if min_samples <= 1 or not segments:
        return segments
    out = []
    for seg in segments:
        start, end, flag = seg
        length = end - start
        if length < min_samples and out:
            # Extend the previous segment over this transient.
            ps, pe, pf = out[-1]
            out[-1] = (ps, end, pf)
        else:
            out.append(seg)
    # Merge any now-adjacent runs that share the same flag value.
    merged = []
    for seg in out:
        if merged and merged[-1][2] == seg[2]:
            ps, _, pf = merged[-1]
            merged[-1] = (ps, seg[1], pf)
        else:
            merged.append(seg)
    return merged


def build_transitions(segments, sample_period_s):
    transitions = []
    for prev, curr in zip(segments, segments[1:]):
        idx = curr[0]
        transitions.append({
            "index": idx,
            "time_s": idx * sample_period_s,
            "from_flag": prev[2],
            "to_flag": curr[2],
        })
    return transitions


def build_segment_dicts(segments, sample_period_s):
    out = []
    for start, end, flag in segments:
        out.append({
            "flag": flag,
            "start_index": start,
            "end_index": end,
            "start_time_s": start * sample_period_s,
            "end_time_s": end * sample_period_s,
            "duration_s": (end - start) * sample_period_s,
        })
    return out


def stats(values):
    if not values:
        return {"count": 0}
    arr = np.asarray(values, dtype=np.float64)
    n = arr.size
    return {
        "count": int(n),
        "min_s": float(arr.min()),
        "max_s": float(arr.max()),
        "mean_s": float(arr.mean()),
        "median_s": float(np.median(arr)),
        "p95_s": float(np.percentile(arr, 95)),
        "p99_s": float(np.percentile(arr, 99)),
        "stddev_s": float(arr.std(ddof=0)) if n > 1 else 0.0,
        "total_s": float(arr.sum()),
    }


def summarize(segments, transitions, sample_period_s):
    flag_durations = {}
    for start, end, flag in segments:
        flag_durations.setdefault(flag, []).append((end - start) * sample_period_s)

    transition_counts = {}
    for t in transitions:
        key = f"{t['from_flag']}->{t['to_flag']}"
        transition_counts[key] = transition_counts.get(key, 0) + 1

    return {
        "flag_segment_stats": {
            str(flag): stats(durs) for flag, durs in sorted(flag_durations.items())
        },
        "transition_counts": dict(sorted(transition_counts.items())),
    }


def compute_measurement(segments, from_flag, to_flag, sample_period_s):
    """Time from the *start* of each `from_flag` segment to the *start* of the
    next `to_flag` segment. This is the natural way to read 'how long did the
    code between Flag(FROM) and Flag(TO) take?'
    """
    intervals = []
    i = 0
    while i < len(segments):
        if segments[i][2] != from_flag:
            i += 1
            continue
        from_start = segments[i][0]
        # Look for the next segment whose flag is to_flag.
        j = i + 1
        while j < len(segments) and segments[j][2] != to_flag:
            j += 1
        if j < len(segments):
            to_start = segments[j][0]
            intervals.append((to_start - from_start) * sample_period_s)
            i = j
        else:
            break
    return {
        "from_flag": from_flag,
        "to_flag": to_flag,
        "intervals_s": [float(x) for x in intervals],
        **stats(intervals),
    }


# ----- CLI -----------------------------------------------------------------

def parse_dio_map(arg):
    """Parse "28:1,29:2,30:3,31:4" -> {28: 1, ...}."""
    out = {}
    for item in arg.split(","):
        item = item.strip()
        if not item:
            continue
        pin_str, dio_str = item.split(":")
        out[int(pin_str)] = int(dio_str)
    for pin in TEENSY_PIN_TO_BIT:
        if pin not in out:
            raise argparse.ArgumentTypeError(
                f"--dio-map must include Teensy pin {pin}")
    return out


def build_parser():
    p = argparse.ArgumentParser(
        description="Capture Flag() pin signals on AD2 and report timing.")
    p.add_argument("--sample-rate", type=float, default=10e6,
                   help="Target sample rate in Hz (default 10 MHz). "
                        "Actual rate is the nearest integer divider of the "
                        "AD2 internal clock (typically 100 MHz).")
    p.add_argument("--duration", type=float, default=None,
                   help="Record duration in seconds. If set, overrides "
                        "--buffer-size: buffer_size = sample_rate * duration.")
    p.add_argument("--buffer-size", type=int, default=8192,
                   help="Number of samples to capture (default 8192). "
                        "Clamped to device maximum.")
    p.add_argument("--debounce-us", type=float, default=2.0,
                   help="Discard transient flag segments shorter than this "
                        "duration in microseconds. Useful because Flag() "
                        "writes its four pins sequentially, producing brief "
                        "intermediate values during the transition (~1us "
                        "total on Teensy 4.1). Default 2us covers the "
                        "transient at sample rates from 500kHz to 10MHz. "
                        "Set to 0 to disable.")
    p.add_argument("--mode", default="auto",
                   choices=["auto", "single", "record"],
                   help="Acquisition mode. 'auto' (default) picks single-shot "
                        "if the request fits in the AD2's on-board buffer, "
                        "otherwise switches to record (USB streaming) mode "
                        "for arbitrary-length captures. 'single' is fastest "
                        "for short windows but capped at the device buffer "
                        "(~8K samples). 'record' streams over USB and "
                        "supports 1+ second captures, but watch for "
                        "lost_samples in the output.")
    p.add_argument("--max-lost-frac", type=float, default=0.001,
                   help="In record mode, fail (exit non-zero) if the "
                        "fraction of lost samples exceeds this threshold "
                        "(default 0.001 = 0.1%%). Lost samples mean USB "
                        "couldn't keep up; the resulting timing is suspect.")
    p.add_argument("--trigger", default="none",
                   choices=["none", "change", "flag"],
                   help="Trigger mode. 'none' captures immediately. "
                        "'change' triggers on any transition of the four "
                        "flag pins. 'flag' triggers when the four pins "
                        "match --trigger-flag.")
    p.add_argument("--trigger-flag", type=int, default=None,
                   help="With --trigger=flag, the 4-bit flag value (0-15) "
                        "to trigger on.")
    p.add_argument("--trigger-position", type=float, default=0.1,
                   help="Fraction of the buffer to capture BEFORE the "
                        "trigger event (0.0..1.0, default 0.1).")
    p.add_argument("--measure", nargs=2, metavar=("FROM", "TO"), type=int,
                   default=None,
                   help="Report intervals from Flag(FROM) start to the next "
                        "Flag(TO) start. Adds a 'measurement' key to the JSON.")
    p.add_argument("--dio-map", type=parse_dio_map,
                   default=DEFAULT_TEENSY_TO_DIO,
                   help="Teensy-pin:AD2-DIO mapping, comma-separated. "
                        "Default: 28:1,29:2,30:3,31:4")
    p.add_argument("--include-raw", action="store_true",
                   help="Include the raw decoded flag-value array in the "
                        "JSON output (large; off by default).")
    p.add_argument("--quiet", action="store_true",
                   help="Suppress progress messages on stderr.")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)

    def log(msg):
        if not args.quiet:
            print(msg, file=sys.stderr, flush=True)

    try:
        # Resolve buffer size.
        if args.duration is not None:
            buffer_size = max(2, int(round(args.sample_rate * args.duration)))
        else:
            buffer_size = args.buffer_size

        # Build trigger masks from the DIO map.
        flag_dio_mask = 0
        for pin, dio in args.dio_map.items():
            flag_dio_mask |= (1 << dio)

        if args.trigger == "none":
            trigger = "none"
            pattern_mask = 0
            pattern_value = 0
            change_mask = 0
        elif args.trigger == "change":
            trigger = "change"
            pattern_mask = 0
            pattern_value = 0
            change_mask = flag_dio_mask
        elif args.trigger == "flag":
            if args.trigger_flag is None or not 0 <= args.trigger_flag <= 15:
                raise ValueError(
                    "--trigger=flag requires --trigger-flag in 0..15")
            trigger = f"flag={args.trigger_flag}"
            pattern_mask = flag_dio_mask
            pattern_value = 0
            for pin, bit in TEENSY_PIN_TO_BIT.items():
                if (args.trigger_flag >> bit) & 1:
                    pattern_value |= (1 << args.dio_map[pin])
            change_mask = 0  # level trigger
        else:
            raise ValueError(f"Unknown trigger mode: {args.trigger}")

        samples, actual_rate, extras = acquire(
            sample_rate_hz=args.sample_rate,
            buffer_size=buffer_size,
            trigger=trigger,
            trigger_pattern_mask=pattern_mask,
            trigger_pattern_value=pattern_value,
            trigger_change_mask=change_mask,
            trigger_position_frac=args.trigger_position,
            mode=args.mode,
            log=log,
        )
        sample_period_s = 1.0 / actual_rate

        lost_frac = (extras["lost_samples"] / samples.size
                     if samples.size else 0.0)
        if extras["lost_samples"] or extras["corrupt_samples"]:
            log(f"WARNING: lost={extras['lost_samples']} "
                f"corrupt={extras['corrupt_samples']} "
                f"({lost_frac*100:.3f}% lost). "
                f"Timing may be inaccurate.")

        flag_values = decode_flag_values(samples, args.dio_map)
        raw_segments = find_segments(flag_values)
        debounce_samples = int(round(args.debounce_us * 1e-6 * actual_rate))
        segments = debounce_segments(raw_segments, debounce_samples)
        log(f"Decoded {len(raw_segments)} raw segments, "
            f"{len(segments)} after debounce ({debounce_samples} samples).")

        transitions = build_transitions(segments, sample_period_s)
        summary = summarize(segments, transitions, sample_period_s)

        output = {
            "metadata": {
                "sample_rate_hz": actual_rate,
                "sample_period_s": sample_period_s,
                "buffer_size": int(samples.size),
                "duration_s": samples.size * sample_period_s,
                "trigger": trigger,
                "trigger_position_frac": args.trigger_position
                    if trigger != "none" else None,
                "debounce_us": args.debounce_us,
                "debounce_samples": debounce_samples,
                "acquisition_mode": extras["mode"],
                "lost_samples": extras["lost_samples"],
                "corrupt_samples": extras["corrupt_samples"],
                "lost_fraction": lost_frac,
                "device_buffer_size": extras["device_buffer_size"],
                "pin_mapping": {
                    str(pin): {"bit": TEENSY_PIN_TO_BIT[pin],
                                "dio": args.dio_map[pin]}
                    for pin in sorted(TEENSY_PIN_TO_BIT)
                },
            },
            "transitions": transitions,
            "segments": build_segment_dicts(segments, sample_period_s),
            "summary": summary,
        }

        if args.measure is not None:
            from_flag, to_flag = args.measure
            output["measurement"] = compute_measurement(
                segments, from_flag, to_flag, sample_period_s)

        if args.include_raw:
            output["raw_flag_values"] = flag_values.tolist()

        json.dump(output, sys.stdout, separators=(",", ":"))
        sys.stdout.write("\n")

        if lost_frac > args.max_lost_frac:
            log(f"ERROR: lost sample fraction {lost_frac*100:.3f}% exceeds "
                f"--max-lost-frac {args.max_lost_frac*100:.3f}%.")
            return 2
        return 0

    except Exception as exc:
        err = {"error": str(exc), "error_type": type(exc).__name__}
        json.dump(err, sys.stdout)
        sys.stdout.write("\n")
        log(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
