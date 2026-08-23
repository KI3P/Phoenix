#!/usr/bin/env python3
"""
Receive-chain integrity test using an Analog Discovery 2.

Samples the SDR's demodulated audio output on AD2 oscilloscope channel 1
and checks that the recovered tone is at the expected frequency (1 kHz),
expected amplitude (~234 mV RMS), and free of interruptions or
discontinuities. The RF input to the SDR is supplied by an external signal
generator (not the AD2). Writes a PNG plot for human verification and
prints a pass/fail summary plus a machine-parseable JSON line.

Wiring
------
    AD2 Scope Ch1 <- SDR audio output
    External signal generator -> SDR antenna / ADC input

Exit codes
----------
    0   PASS - all checks passed
    1   FAIL - one or more checks failed
    2   ERROR - AD2 not reachable / DWF call failed
"""

import argparse
import ctypes
import json
import os
import sys
import time
from dataclasses import dataclass, asdict
from typing import Optional

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# --- DWF library binding ---------------------------------------------------

def _load_dwf():
    if sys.platform.startswith("win"):
        return ctypes.cdll.dwf
    if sys.platform.startswith("darwin"):
        return ctypes.cdll.LoadLibrary("/Library/Frameworks/dwf.framework/dwf")
    return ctypes.cdll.LoadLibrary("libdwf.so")


# DWF constants (mirrors dwfconstants.py - kept inline so this script has no
# extra import dependency beyond libdwf.so + numpy + matplotlib).
HDWF_NONE = 0
ACQMODE_RECORD = 3
TRIG_SRC_NONE = 0
DWF_STATE_CONFIG = 4
DWF_STATE_PREFILL = 5
DWF_STATE_ARMED = 1
DWF_STATE_DONE = 2


def _dwf_last_error(dwf) -> str:
    buf = ctypes.create_string_buffer(512)
    dwf.FDwfGetLastErrorMsg(buf)
    return buf.value.decode("utf-8", errors="replace")


def _bind_argtypes(dwf):
    c_int = ctypes.c_int
    c_double = ctypes.c_double
    c_byte = ctypes.c_byte

    dwf.FDwfDeviceOpen.argtypes = [c_int, ctypes.POINTER(c_int)]
    dwf.FDwfDeviceCloseAll.argtypes = []
    dwf.FDwfDeviceAutoConfigureSet.argtypes = [c_int, c_int]
    dwf.FDwfGetLastErrorMsg.argtypes = [ctypes.c_char_p]

    dwf.FDwfAnalogInReset.argtypes = [c_int]
    dwf.FDwfAnalogInChannelEnableSet.argtypes = [c_int, c_int, c_int]
    dwf.FDwfAnalogInChannelRangeSet.argtypes = [c_int, c_int, c_double]
    dwf.FDwfAnalogInChannelOffsetSet.argtypes = [c_int, c_int, c_double]
    dwf.FDwfAnalogInAcquisitionModeSet.argtypes = [c_int, c_int]
    dwf.FDwfAnalogInFrequencySet.argtypes = [c_int, c_double]
    dwf.FDwfAnalogInRecordLengthSet.argtypes = [c_int, c_double]
    dwf.FDwfAnalogInConfigure.argtypes = [c_int, c_int, c_int]
    dwf.FDwfAnalogInStatus.argtypes = [c_int, c_int, ctypes.POINTER(c_byte)]
    dwf.FDwfAnalogInStatusRecord.argtypes = [
        c_int,
        ctypes.POINTER(c_int),
        ctypes.POINTER(c_int),
        ctypes.POINTER(c_int),
    ]
    dwf.FDwfAnalogInStatusData.argtypes = [c_int, c_int, ctypes.c_void_p, c_int]


# --- Capture ---------------------------------------------------------------

def record_channel1(
    dwf,
    hdwf,
    sample_rate_hz: float,
    record_len_s: float,
    range_v: float,
    log,
):
    """Capture scope channel 1 in record (streaming) mode and return samples.

    Returns
    -------
    (samples: np.ndarray, lost: int, corrupted: int)
    """
    n_samples = int(round(sample_rate_hz * record_len_s))
    buf = (ctypes.c_double * n_samples)()
    sts = ctypes.c_byte()
    avail = ctypes.c_int()
    lost = ctypes.c_int()
    corrupted = ctypes.c_int()
    total_lost = 0
    total_corrupted = 0

    dwf.FDwfAnalogInReset(hdwf)
    dwf.FDwfAnalogInChannelEnableSet(hdwf, ctypes.c_int(0), ctypes.c_int(1))
    dwf.FDwfAnalogInChannelRangeSet(hdwf, ctypes.c_int(0), ctypes.c_double(range_v))
    dwf.FDwfAnalogInChannelOffsetSet(hdwf, ctypes.c_int(0), ctypes.c_double(0.0))
    dwf.FDwfAnalogInAcquisitionModeSet(hdwf, ctypes.c_int(ACQMODE_RECORD))
    dwf.FDwfAnalogInFrequencySet(hdwf, ctypes.c_double(sample_rate_hz))
    dwf.FDwfAnalogInRecordLengthSet(hdwf, ctypes.c_double(record_len_s))

    # Arm but don't start.
    dwf.FDwfAnalogInConfigure(hdwf, ctypes.c_int(1), ctypes.c_int(0))

    # Wait for analog frontend offset to settle.
    log("Waiting 2.0 s for analog frontend to settle...")
    time.sleep(2.0)

    log(f"Starting acquisition ({n_samples} samples @ {sample_rate_hz:.0f} Hz, {record_len_s*1000:.1f} ms)...")
    dwf.FDwfAnalogInConfigure(hdwf, ctypes.c_int(0), ctypes.c_int(1))

    c_samples = 0
    deadline = time.monotonic() + record_len_s * 5 + 5.0  # generous timeout

    while c_samples < n_samples:
        if time.monotonic() > deadline:
            raise RuntimeError("Timed out waiting for acquisition")

        if dwf.FDwfAnalogInStatus(hdwf, ctypes.c_int(1), ctypes.byref(sts)) != 1:
            raise RuntimeError(f"FDwfAnalogInStatus failed: {_dwf_last_error(dwf)}")

        if c_samples == 0 and sts.value in (DWF_STATE_CONFIG, DWF_STATE_PREFILL, DWF_STATE_ARMED):
            continue

        dwf.FDwfAnalogInStatusRecord(hdwf, ctypes.byref(avail), ctypes.byref(lost), ctypes.byref(corrupted))
        c_samples += lost.value
        total_lost += lost.value
        total_corrupted += corrupted.value

        if avail.value == 0:
            continue

        take = avail.value
        if c_samples + take > n_samples:
            take = n_samples - c_samples

        dwf.FDwfAnalogInStatusData(
            hdwf,
            ctypes.c_int(0),
            ctypes.byref(buf, ctypes.sizeof(ctypes.c_double) * c_samples),
            ctypes.c_int(take),
        )
        c_samples += take

    samples = np.frombuffer(buf, dtype=np.float64).copy()
    return samples, total_lost, total_corrupted


# --- Analysis --------------------------------------------------------------

@dataclass
class Analysis:
    sample_rate_hz: float
    n_samples: int
    duration_s: float
    rms_v: float
    peak_v: float
    dc_offset_v: float
    dominant_freq_hz: float
    snr_db: float
    tone_fraction: float        # fraction of total AC power at the dominant bin (±2 bins)
    envelope_mean_v: float
    envelope_min_v: float
    envelope_max_v: float
    envelope_cv: float          # coefficient of variation: stddev/mean
    max_window_dropout: float   # smallest windowed RMS / median windowed RMS
    max_sample_jump_v: float    # largest |x[n]-x[n-1]| in volts
    expected_jump_v: float      # 2*pi*f*A/Fs - theoretical max for a clean sine


def analyze(samples: np.ndarray, sample_rate_hz: float, expected_freq_hz: float, expected_rms_v: float) -> Analysis:
    n = samples.size
    duration_s = n / sample_rate_hz
    dc = float(np.mean(samples))
    ac = samples - dc
    rms = float(np.sqrt(np.mean(ac * ac)))
    peak = float(np.max(np.abs(ac)))

    # FFT-based dominant-frequency detection.
    # Use a Hann window for low spectral leakage; correct amplitude scaling for the window.
    window = np.hanning(n)
    win_gain = window.mean()  # coherent gain
    spectrum = np.fft.rfft(ac * window) / (n * win_gain) * 2.0
    freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate_hz)
    mag = np.abs(spectrum)

    # Ignore the DC bin and anything below 50 Hz (mains/leakage).
    mask = freqs >= 50.0
    if not np.any(mask):
        dominant_freq = 0.0
        tone_power = 0.0
    else:
        idx_in_mask = int(np.argmax(mag[mask]))
        dominant_idx = int(np.flatnonzero(mask)[0]) + idx_in_mask
        # Parabolic interpolation around the peak for sub-bin frequency precision.
        if 1 <= dominant_idx < mag.size - 1:
            a, b, c = mag[dominant_idx - 1], mag[dominant_idx], mag[dominant_idx + 1]
            denom = (a - 2 * b + c)
            delta = 0.5 * (a - c) / denom if denom != 0 else 0.0
        else:
            delta = 0.0
        bin_hz = sample_rate_hz / n
        dominant_freq = (dominant_idx + delta) * bin_hz

        # Power at the tone (±2 bins) vs total AC power.
        lo = max(dominant_idx - 2, 0)
        hi = min(dominant_idx + 3, mag.size)
        tone_power = float(np.sum(mag[lo:hi] ** 2))

    total_power = float(np.sum(mag ** 2))
    tone_fraction = tone_power / total_power if total_power > 0 else 0.0
    # SNR: tone power vs everything else in the AC band.
    other_power = max(total_power - tone_power, 1e-30)
    snr_db = 10.0 * np.log10(tone_power / other_power) if tone_power > 0 else -120.0

    # Envelope via analytic-signal magnitude (no scipy dependency: build the
    # analytic signal via FFT directly).
    X = np.fft.fft(ac)
    H = np.zeros(n)
    if n % 2 == 0:
        H[0] = 1.0
        H[n // 2] = 1.0
        H[1:n // 2] = 2.0
    else:
        H[0] = 1.0
        H[1:(n + 1) // 2] = 2.0
    analytic = np.fft.ifft(X * H)
    envelope = np.abs(analytic)

    # Trim the first and last ~one cycle to skip Hilbert edge effects.
    trim = max(1, int(round(sample_rate_hz / max(expected_freq_hz, 1.0))))
    env_core = envelope[trim:-trim] if envelope.size > 2 * trim else envelope
    env_mean = float(np.mean(env_core))
    env_std = float(np.std(env_core))
    env_min = float(np.min(env_core))
    env_max = float(np.max(env_core))
    env_cv = env_std / env_mean if env_mean > 0 else float("inf")

    # Windowed RMS to spot dropouts: split into ~10 ms windows and look for
    # the minimum vs the median - a clean tone should be flat.
    win_len = max(1, int(round(0.010 * sample_rate_hz)))
    n_windows = ac.size // win_len
    if n_windows >= 3:
        windows = ac[: n_windows * win_len].reshape(n_windows, win_len)
        win_rms = np.sqrt(np.mean(windows * windows, axis=1))
        median_rms = float(np.median(win_rms))
        min_rms = float(np.min(win_rms))
        max_window_dropout = (min_rms / median_rms) if median_rms > 0 else 0.0
    else:
        max_window_dropout = 1.0

    # Sample-to-sample slew: for an ideal sine A*sin(2*pi*f*t), the maximum
    # per-sample step is 2*pi*f*A/Fs. A glitch (zero pop, missing chunk)
    # produces a jump much larger than that.
    diffs = np.abs(np.diff(ac))
    max_sample_jump = float(np.max(diffs)) if diffs.size > 0 else 0.0
    # Use measured peak (not expected) so the bound holds even when the tone
    # is the wrong amplitude.
    expected_jump = 2.0 * np.pi * max(expected_freq_hz, 1.0) * peak / sample_rate_hz

    return Analysis(
        sample_rate_hz=sample_rate_hz,
        n_samples=n,
        duration_s=duration_s,
        rms_v=rms,
        peak_v=peak,
        dc_offset_v=dc,
        dominant_freq_hz=dominant_freq,
        snr_db=snr_db,
        tone_fraction=tone_fraction,
        envelope_mean_v=env_mean,
        envelope_min_v=env_min,
        envelope_max_v=env_max,
        envelope_cv=env_cv,
        max_window_dropout=max_window_dropout,
        max_sample_jump_v=max_sample_jump,
        expected_jump_v=expected_jump,
    )


# --- Pass/fail -------------------------------------------------------------

@dataclass
class PassFail:
    passed: bool
    checks: dict


def evaluate(a: Analysis, expected_freq_hz: float, freq_tol_hz: float,
             expected_rms_v: float, rms_tol_frac: float,
             min_snr_db: float, max_env_cv: float,
             min_window_ratio: float, jump_margin: float) -> PassFail:
    checks = {}

    freq_err = abs(a.dominant_freq_hz - expected_freq_hz)
    checks["frequency"] = {
        "value_hz": a.dominant_freq_hz,
        "expected_hz": expected_freq_hz,
        "tolerance_hz": freq_tol_hz,
        "error_hz": freq_err,
        "pass": freq_err <= freq_tol_hz,
    }

    rms_err_frac = abs(a.rms_v - expected_rms_v) / expected_rms_v if expected_rms_v else float("inf")
    checks["amplitude_rms"] = {
        "value_v": a.rms_v,
        "expected_v": expected_rms_v,
        "tolerance_frac": rms_tol_frac,
        "error_frac": rms_err_frac,
        "pass": rms_err_frac <= rms_tol_frac,
    }

    checks["snr"] = {
        "value_db": a.snr_db,
        "min_db": min_snr_db,
        "pass": a.snr_db >= min_snr_db,
    }

    checks["envelope_stability"] = {
        "cv": a.envelope_cv,
        "max_cv": max_env_cv,
        "min_v": a.envelope_min_v,
        "max_v": a.envelope_max_v,
        "pass": a.envelope_cv <= max_env_cv,
    }

    checks["window_dropout"] = {
        "min_over_median": a.max_window_dropout,
        "min_required": min_window_ratio,
        "pass": a.max_window_dropout >= min_window_ratio,
    }

    jump_threshold = a.expected_jump_v * jump_margin
    checks["sample_jump"] = {
        "max_jump_v": a.max_sample_jump_v,
        "expected_max_v": a.expected_jump_v,
        "threshold_v": jump_threshold,
        "pass": a.max_sample_jump_v <= jump_threshold,
    }

    passed = all(c["pass"] for c in checks.values())
    return PassFail(passed=passed, checks=checks)


# --- Plotting --------------------------------------------------------------

def write_plot(samples: np.ndarray, sample_rate_hz: float, expected_freq_hz: float,
               analysis: Analysis, verdict: PassFail, png_path: str):
    n = samples.size
    t_ms = np.arange(n) / sample_rate_hz * 1000.0

    fig, (ax_time, ax_freq) = plt.subplots(2, 1, figsize=(11, 7))

    ax_time.plot(t_ms, samples, linewidth=0.6, color="C0", label="Audio output")
    ax_time.set_xlabel("Time (ms)")
    ax_time.set_ylabel("Voltage (V)")
    ax_time.set_title(
        f"SDR audio output  |  RMS={analysis.rms_v*1000:.1f} mV, "
        f"dominant={analysis.dominant_freq_hz:.1f} Hz, SNR={analysis.snr_db:.1f} dB  "
        f"[{ 'PASS' if verdict.passed else 'FAIL' }]"
    )
    ax_time.grid(True, alpha=0.3)
    # Highlight a couple of cycles for sanity-checking the waveform shape.
    if expected_freq_hz > 0:
        cycle_ms = 1000.0 / expected_freq_hz
        ax_time.set_xlim(0, min(5 * cycle_ms, t_ms[-1]))
    ax_time.legend(loc="upper right", fontsize=8)

    # FFT plot (magnitude in dBV).
    ac = samples - np.mean(samples)
    window = np.hanning(n)
    win_gain = window.mean()
    spec = np.fft.rfft(ac * window) / (n * win_gain) * 2.0
    freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate_hz)
    mag_db = 20 * np.log10(np.maximum(np.abs(spec), 1e-9))
    ax_freq.plot(freqs, mag_db, linewidth=0.6, color="C1")
    ax_freq.axvline(expected_freq_hz, color="green", linestyle="--", linewidth=0.8,
                    label=f"expected {expected_freq_hz:.0f} Hz")
    ax_freq.axvline(analysis.dominant_freq_hz, color="red", linestyle=":", linewidth=0.8,
                    label=f"measured {analysis.dominant_freq_hz:.1f} Hz")
    ax_freq.set_xlabel("Frequency (Hz)")
    ax_freq.set_ylabel("Magnitude (dBV)")
    ax_freq.set_xlim(0, min(10 * expected_freq_hz, sample_rate_hz / 2))
    ax_freq.set_ylim(top=10, bottom=-100)
    ax_freq.grid(True, alpha=0.3)
    ax_freq.legend(loc="upper right", fontsize=8)

    fig.tight_layout()
    fig.savefig(png_path, dpi=110)
    plt.close(fig)


# --- Main ------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--sample-rate", type=float, default=200_000.0,
                   help="Scope sample rate (Hz). Default: 200000")
    p.add_argument("--duration", type=float, default=0.200,
                   help="Capture duration in seconds. Must be >= 0.1. Default: 0.200")
    p.add_argument("--scope-range", type=float, default=2.0,
                   help="Scope input range in volts (default: 2.0)")

    p.add_argument("--expected-tone", type=float, default=1000.0,
                   help="Expected demodulated tone frequency (Hz). Default: 1000")
    p.add_argument("--expected-rms", type=float, default=0.234,
                   help="Expected RMS amplitude (V). Default: 0.234")
    p.add_argument("--freq-tol", type=float, default=20.0,
                   help="Frequency tolerance (Hz). Default: 20")
    p.add_argument("--rms-tol", type=float, default=0.25,
                   help="Fractional RMS tolerance. Default: 0.25 (+/-25 percent)")
    p.add_argument("--min-snr", type=float, default=30.0,
                   help="Minimum tone SNR (dB). Default: 30")
    p.add_argument("--max-env-cv", type=float, default=0.10,
                   help="Maximum envelope coefficient of variation. Default: 0.10")
    p.add_argument("--min-window-ratio", type=float, default=0.70,
                   help="Min windowed RMS / median windowed RMS. Default: 0.70")
    p.add_argument("--jump-margin", type=float, default=3.0,
                   help="Max sample-to-sample jump vs theoretical. Default: 3.0")

    p.add_argument("--plot", default=None,
                   help="Path to output PNG (default: receive_chain_<timestamp>.png next to script)")
    p.add_argument("--json", action="store_true", help="Also emit a JSON document on stdout")
    p.add_argument("--quiet", action="store_true", help="Suppress progress output")
    args = p.parse_args()

    if args.duration < 0.100:
        print(f"--duration must be >= 0.1 s (got {args.duration})", file=sys.stderr)
        return 2

    def log(msg):
        if not args.quiet:
            print(msg, file=sys.stderr, flush=True)

    if args.plot is None:
        here = os.path.dirname(os.path.abspath(__file__))
        ts = time.strftime("%Y%m%d_%H%M%S")
        args.plot = os.path.join(here, f"receive_chain_{ts}.png")

    try:
        dwf = _load_dwf()
    except OSError as e:
        print(f"libdwf.so not loadable: {e}. Install the Digilent WaveForms SDK.", file=sys.stderr)
        return 2

    _bind_argtypes(dwf)

    hdwf = ctypes.c_int()
    log("Opening AD2...")
    if dwf.FDwfDeviceOpen(ctypes.c_int(-1), ctypes.byref(hdwf)) != 1 or hdwf.value == HDWF_NONE:
        print(f"Failed to open AD2: {_dwf_last_error(dwf)}", file=sys.stderr)
        return 2

    try:
        # Manual configure so AnalogIn applies only when we ask.
        dwf.FDwfDeviceAutoConfigureSet(hdwf, ctypes.c_int(0))

        samples, lost, corrupted = record_channel1(
            dwf, hdwf,
            sample_rate_hz=args.sample_rate,
            record_len_s=args.duration,
            range_v=args.scope_range,
            log=log,
        )
        if lost:
            log(f"WARNING: {lost} samples lost during capture")
        if corrupted:
            log(f"WARNING: {corrupted} samples flagged corrupted during capture")

        analysis = analyze(samples, args.sample_rate, args.expected_tone, args.expected_rms)
        verdict = evaluate(
            analysis,
            expected_freq_hz=args.expected_tone,
            freq_tol_hz=args.freq_tol,
            expected_rms_v=args.expected_rms,
            rms_tol_frac=args.rms_tol,
            min_snr_db=args.min_snr,
            max_env_cv=args.max_env_cv,
            min_window_ratio=args.min_window_ratio,
            jump_margin=args.jump_margin,
        )

        write_plot(samples, args.sample_rate, args.expected_tone, analysis, verdict, args.plot)

        # Human-readable summary on stdout.
        status = "PASS" if verdict.passed else "FAIL"
        print(f"[{status}] Receive chain test")
        print(f"  Samples:        {analysis.n_samples} ({analysis.duration_s*1000:.1f} ms @ {analysis.sample_rate_hz:.0f} Hz)")
        print(f"  Tone freq:      {analysis.dominant_freq_hz:.2f} Hz  (expected {args.expected_tone:.0f} ± {args.freq_tol:.0f} Hz)")
        print(f"  RMS amplitude:  {analysis.rms_v*1000:.1f} mV       (expected {args.expected_rms*1000:.0f} mV ± {args.rms_tol*100:.0f}%)")
        print(f"  SNR:            {analysis.snr_db:.1f} dB           (min {args.min_snr:.1f} dB)")
        print(f"  Envelope CV:    {analysis.envelope_cv:.4f}         (max {args.max_env_cv:.3f})")
        print(f"  Window dropout: {analysis.max_window_dropout:.3f}   (min {args.min_window_ratio:.3f})")
        print(f"  Max sample jump:{analysis.max_sample_jump_v*1000:.2f} mV (theoretical {analysis.expected_jump_v*1000:.2f} mV)")
        if lost or corrupted:
            print(f"  Capture issues: lost={lost} corrupted={corrupted}")
        print(f"  Plot:           {args.plot}")
        for name, chk in verdict.checks.items():
            mark = "OK " if chk["pass"] else "BAD"
            print(f"   [{mark}] {name}")

        if args.json:
            doc = {
                "passed": verdict.passed,
                "analysis": asdict(analysis),
                "checks": verdict.checks,
                "capture": {
                    "lost_samples": lost,
                    "corrupted_samples": corrupted,
                    "sample_rate_hz": args.sample_rate,
                    "duration_s": args.duration,
                },
                "plot": args.plot,
            }
            print(json.dumps(doc))

        return 0 if verdict.passed else 1

    finally:
        try:
            dwf.FDwfAnalogInReset(hdwf)
        except Exception:
            pass
        dwf.FDwfDeviceCloseAll()


if __name__ == "__main__":
    sys.exit(main())
