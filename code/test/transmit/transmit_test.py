#!/usr/bin/env python3
"""
Transmit PTT Signal Integrity Test

Repeatedly engages PTT on a radio transmitter via an Analog Discovery 2,
captures I/Q output waveforms, and analyzes them for discontinuities and
signal quality issues.

Usage:
    python transmit_test.py              # 100 iterations with defaults
    python transmit_test.py -n 5         # quick 5-iteration test
    python transmit_test.py --help       # show all options
"""

import sys
import os
import time
import signal as signal_module
import argparse
from ctypes import *
from pathlib import Path
from datetime import datetime

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Add Waveforms SDK samples directory so we can import dwfconstants
sys.path.insert(0, "/usr/share/digilent/waveforms/samples/py")
from dwfconstants import *


# ---------------------------------------------------------------------------
# SDK helpers
# ---------------------------------------------------------------------------

def load_dwf():
    """Load the Waveforms SDK shared library."""
    if sys.platform.startswith("win"):
        return cdll.dwf
    elif sys.platform.startswith("darwin"):
        return cdll.LoadLibrary("/Library/Frameworks/dwf.framework/dwf")
    else:
        return cdll.LoadLibrary("libdwf.so")


def dwf_error_msg(dwf_lib):
    """Return the last DWF error message as a string."""
    buf = create_string_buffer(512)
    dwf_lib.FDwfGetLastErrorMsg(buf)
    return buf.value.decode()


# ---------------------------------------------------------------------------
# Hardware configuration
# ---------------------------------------------------------------------------

def configure_device(dwf_lib, args):
    """Open the AD2 and configure all I/O. Returns device handle."""
    hdwf = c_int()

    version = create_string_buffer(16)
    dwf_lib.FDwfGetVersion(version)
    print(f"DWF Version: {version.value.decode()}")

    print("Opening device...")
    dwf_lib.FDwfDeviceOpen(c_int(-1), byref(hdwf))
    if hdwf.value == hdwfNone.value:
        print(f"Failed to open device: {dwf_error_msg(dwf_lib)}")
        sys.exit(1)

    # Manual configure mode
    dwf_lib.FDwfDeviceAutoConfigureSet(hdwf, c_int(0))

    # --- Analog output: W1 = 500 Hz sine, 50 mV amplitude ---
    ch_out = c_int(0)
    dwf_lib.FDwfAnalogOutNodeEnableSet(hdwf, ch_out, AnalogOutNodeCarrier, c_int(1))
    dwf_lib.FDwfAnalogOutNodeFunctionSet(hdwf, ch_out, AnalogOutNodeCarrier, funcSine)
    dwf_lib.FDwfAnalogOutNodeFrequencySet(hdwf, ch_out, AnalogOutNodeCarrier, c_double(500.0))
    dwf_lib.FDwfAnalogOutNodeAmplitudeSet(hdwf, ch_out, AnalogOutNodeCarrier, c_double(0.05))
    dwf_lib.FDwfAnalogOutNodeOffsetSet(hdwf, ch_out, AnalogOutNodeCarrier, c_double(0.0))
    dwf_lib.FDwfAnalogOutConfigure(hdwf, ch_out, c_int(1))  # start
    print("W1: 500 Hz sine, 50 mV amplitude — running")

    # --- Scope: Ch1 and Ch2 ---
    sample_rate = args.sample_rate
    n_samples = int(sample_rate * args.acquire_time)

    for ch in [0, 1]:
        dwf_lib.FDwfAnalogInChannelEnableSet(hdwf, c_int(ch), c_int(1))
        dwf_lib.FDwfAnalogInChannelRangeSet(hdwf, c_int(ch), c_double(5.0))    # AD2 minimum HW range
        dwf_lib.FDwfAnalogInChannelOffsetSet(hdwf, c_int(ch), c_double(1.6))   # center window at 1.6V

    dwf_lib.FDwfAnalogInAcquisitionModeSet(hdwf, acqmodeRecord)
    dwf_lib.FDwfAnalogInFrequencySet(hdwf, c_double(sample_rate))
    dwf_lib.FDwfAnalogInRecordLengthSet(hdwf, c_double(args.acquire_time))
    dwf_lib.FDwfAnalogInConfigure(hdwf, c_int(1), c_int(0))  # apply config, don't start
    print(f"Scope: {sample_rate/1000:.0f} kHz, {args.acquire_time*1000:.0f} ms "
          f"({n_samples} samples), range 5 V, offset +1.6 V")

    # --- Digital IO: DIO-0 as output, default HIGH (PTT off) ---
    dwf_lib.FDwfDigitalIOOutputEnableSet(hdwf, c_int(0x0001))
    dwf_lib.FDwfDigitalIOOutputSet(hdwf, c_int(0x0001))  # HIGH = PTT off
    dwf_lib.FDwfDigitalIOConfigure(hdwf)
    print("DIO-0: output, HIGH (PTT disengaged)")

    # --- DigitalIn: capture DIO 1-4 (Teensy pins 28-31, Flag() bus) ---
    # AD2 internal clock is 100 MHz; divider chosen to match scope sample rate.
    if args.capture_flag:
        clk_hz = c_double()
        dwf_lib.FDwfDigitalInInternalClockInfo(hdwf, byref(clk_hz))
        divider = int(round(clk_hz.value / sample_rate))
        dwf_lib.FDwfDigitalInDividerSet(hdwf, c_uint(divider))
        # 8-bit sample format covers DIO 0..7 (we use DIO 1..4)
        dwf_lib.FDwfDigitalInSampleFormatSet(hdwf, c_int(8))
        dwf_lib.FDwfDigitalInAcquisitionModeSet(hdwf, acqmodeRecord)
        dwf_lib.FDwfDigitalInInputOrderSet(hdwf, c_int(1))
        # In record mode the run is unbounded; we stop after n_samples are read.
        print(f"DigitalIn: {clk_hz.value/divider/1000:.1f} kHz, "
              f"capturing DIO 1-4 (Flag bus) in record mode")

    # Wait for offset stabilization
    print("Waiting 2 s for offset stabilization...")
    time.sleep(2)

    return hdwf, sample_rate, n_samples


# ---------------------------------------------------------------------------
# PTT control
# ---------------------------------------------------------------------------

def ptt_engage(dwf_lib, hdwf):
    """Set DIO-0 LOW to engage PTT (radio transmits)."""
    dwf_lib.FDwfDigitalIOOutputSet(hdwf, c_int(0x0000))
    dwf_lib.FDwfDigitalIOConfigure(hdwf)


def ptt_disengage(dwf_lib, hdwf):
    """Set DIO-0 HIGH to disengage PTT."""
    dwf_lib.FDwfDigitalIOOutputSet(hdwf, c_int(0x0001))
    dwf_lib.FDwfDigitalIOConfigure(hdwf)


# ---------------------------------------------------------------------------
# Data acquisition
# ---------------------------------------------------------------------------

def acquire_data(dwf_lib, hdwf, n_samples, capture_flag=False):
    """Acquire record-mode data from both scope channels.

    If capture_flag is True, also acquire 8-bit DIO snapshots in parallel
    (DIO 1-4 carry the firmware Flag() bus; DIO-0 is the PTT output).

    Returns (ch1_array, ch2_array, dio_array_or_None, lost, corrupted).
    """
    buf1 = (c_double * n_samples)()
    buf2 = (c_double * n_samples)()
    dio_buf = (c_uint8 * n_samples)() if capture_flag else None

    # Start digital first so it's armed when scope acquisition begins
    if capture_flag:
        dwf_lib.FDwfDigitalInConfigure(hdwf, c_int(0), c_int(1))
    dwf_lib.FDwfAnalogInConfigure(hdwf, c_int(0), c_int(1))

    sts = c_byte()
    c_available = c_int()
    c_lost = c_int()
    c_corrupted = c_int()
    total_samples = 0
    total_lost = 0
    total_corrupted = 0

    while total_samples < n_samples:
        dwf_lib.FDwfAnalogInStatus(hdwf, c_int(1), byref(sts))

        if total_samples == 0 and sts.value in (
            DwfStateConfig.value, DwfStatePrefill.value, DwfStateArmed.value
        ):
            continue

        dwf_lib.FDwfAnalogInStatusRecord(
            hdwf, byref(c_available), byref(c_lost), byref(c_corrupted)
        )

        total_lost += c_lost.value
        total_corrupted += c_corrupted.value
        total_samples += c_lost.value  # count lost as consumed

        if c_available.value == 0:
            continue

        avail = c_available.value
        if total_samples + avail > n_samples:
            avail = n_samples - total_samples

        dwf_lib.FDwfAnalogInStatusData(
            hdwf, c_int(0),
            byref(buf1, sizeof(c_double) * total_samples), c_int(avail)
        )
        dwf_lib.FDwfAnalogInStatusData(
            hdwf, c_int(1),
            byref(buf2, sizeof(c_double) * total_samples), c_int(avail)
        )
        total_samples += avail

    ch1 = np.frombuffer(buf1, dtype=np.float64).copy()
    ch2 = np.frombuffer(buf2, dtype=np.float64).copy()

    dio = None
    if capture_flag:
        # DigitalIn record mode: pull samples as they arrive until we have n_samples.
        d_avail = c_int(); d_lost = c_int(); d_corrupted = c_int()
        d_sts = c_byte()
        d_total = 0
        # Bounded loop — at the same sample rate as scope, this should fill in
        # acquire_time seconds, with very small slack for SDK overhead.
        deadline = time.monotonic() + 5.0
        while d_total < n_samples and time.monotonic() < deadline:
            dwf_lib.FDwfDigitalInStatus(hdwf, c_int(1), byref(d_sts))
            dwf_lib.FDwfDigitalInStatusRecord(
                hdwf, byref(d_avail), byref(d_lost), byref(d_corrupted)
            )
            avail = d_avail.value
            if avail == 0:
                if d_sts.value == DwfStateDone.value:
                    break
                continue
            if d_total + avail > n_samples:
                avail = n_samples - d_total
            dwf_lib.FDwfDigitalInStatusData(
                hdwf,
                byref(dio_buf, d_total),  # uint8 buf, byte offset == sample offset
                c_int(avail),
            )
            d_total += avail
        # Stop digital acquisition cleanly
        dwf_lib.FDwfDigitalInConfigure(hdwf, c_int(0), c_int(0))
        dio = np.frombuffer(dio_buf, dtype=np.uint8).copy()

    return ch1, ch2, dio, total_lost, total_corrupted


# ---------------------------------------------------------------------------
# Signal analysis
# ---------------------------------------------------------------------------

def check_amplitude(signal, min_rms):
    """Check that the AC-coupled RMS amplitude exceeds min_rms.

    Returns (passed, rms_value).
    """
    ac = signal - np.mean(signal)
    rms = np.sqrt(np.mean(ac ** 2))
    return rms >= min_rms, rms


def check_discontinuities(signal):
    """Check for sudden jumps/discontinuities in the waveform.

    For a pure sinusoid, max(|derivative|) / rms(derivative) = sqrt(2).
    A ratio much larger than that indicates a discontinuity.

    Returns (passed, peak_to_rms_ratio).
    """
    deriv = np.diff(signal)
    rms_deriv = np.sqrt(np.mean(deriv ** 2))
    if rms_deriv == 0:
        return False, float("inf")
    ratio = np.max(np.abs(deriv)) / rms_deriv
    # sqrt(2) ~ 1.414 for pure sine; allow up to 5.0 for noise margin
    return ratio < 5.0, ratio


def check_spectral_purity(signal, sample_rate, expected_freq, min_snr_db):
    """Check that the signal is a clean sinusoid via FFT.

    Returns (passed, snr_db, dominant_freq).
    """
    ac = signal - np.mean(signal)
    n = len(ac)
    windowed = ac * np.hanning(n)

    spectrum = np.abs(np.fft.rfft(windowed))
    freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate)

    # Find dominant frequency
    peak_bin = np.argmax(spectrum)
    dominant_freq = freqs[peak_bin]

    # Frequency check
    freq_ok = abs(dominant_freq - expected_freq) < 50.0

    # SNR: power in fundamental +/- 3 bins vs. everything else
    fund_lo = max(0, peak_bin - 3)
    fund_hi = min(len(spectrum), peak_bin + 4)  # exclusive
    power = spectrum ** 2
    fund_power = np.sum(power[fund_lo:fund_hi])
    total_power = np.sum(power[1:])  # exclude DC bin
    noise_power = total_power - fund_power

    if noise_power <= 0:
        snr_db = 100.0  # effectively infinite
    else:
        snr_db = 10.0 * np.log10(fund_power / noise_power)

    passed = freq_ok and snr_db >= min_snr_db
    return passed, snr_db, dominant_freq


def analyze_channel(signal, sample_rate, name, min_rms, min_snr_db,
                    expected_freq=500.0):
    """Run all signal quality checks on one channel.

    Returns a dict with pass/fail, metrics, and failure reasons.
    """
    reasons = []

    amp_ok, rms = check_amplitude(signal, min_rms)
    if not amp_ok:
        reasons.append(f"{name}: low amplitude (RMS={rms:.4f} V)")

    disc_ok, ratio = check_discontinuities(signal)
    if not disc_ok:
        reasons.append(f"{name}: discontinuity (peak/rms deriv={ratio:.1f})")

    spec_ok, snr_db, dom_freq = check_spectral_purity(
        signal, sample_rate, expected_freq, min_snr_db
    )
    if not spec_ok:
        if abs(dom_freq - expected_freq) >= 50.0:
            reasons.append(f"{name}: unexpected freq ({dom_freq:.1f} Hz)")
        if snr_db < min_snr_db:
            reasons.append(f"{name}: low SNR ({snr_db:.1f} dB)")

    return {
        "name": name,
        "passed": len(reasons) == 0,
        "rms": rms,
        "snr_db": snr_db,
        "dominant_freq": dom_freq,
        "deriv_ratio": ratio,
        "failure_reasons": reasons,
    }


# ---------------------------------------------------------------------------
# Data saving
# ---------------------------------------------------------------------------

def save_waveform(outdir, iteration, ch1, ch2, sample_rate, dio=None):
    """Save raw waveform data as .npz and a plot as .png."""
    fail_dir = Path(outdir) / "failures"
    fail_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    stem = f"fail_iter{iteration:04d}_{ts}"

    npz_path = fail_dir / f"{stem}.npz"
    if dio is not None:
        np.savez(npz_path, ch1=ch1, ch2=ch2, sample_rate=sample_rate, dio=dio)
    else:
        np.savez(npz_path, ch1=ch1, ch2=ch2, sample_rate=sample_rate)

    png_path = fail_dir / f"{stem}.png"
    t_ms = np.arange(len(ch1)) / sample_rate * 1000
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    ax1.plot(t_ms, ch1, linewidth=0.5)
    ax1.set_ylabel("Voltage (V)")
    ax1.set_title(f"Iteration {iteration} - I channel")
    ax1.grid(True, alpha=0.3)
    ax2.plot(t_ms, ch2, linewidth=0.5)
    ax2.set_ylabel("Voltage (V)")
    ax2.set_xlabel("Time (ms)")
    ax2.set_title(f"Iteration {iteration} - Q channel")
    ax2.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(png_path, dpi=150)
    plt.close(fig)

    return npz_path, png_path


# ---------------------------------------------------------------------------
# Main test loop
# ---------------------------------------------------------------------------

def run_test(args):
    dwf_lib = load_dwf()
    hdwf, sample_rate, n_samples = configure_device(dwf_lib, args)

    # Safety: ensure PTT is disengaged on exit
    def cleanup(*_):
        print("\nDisengaging PTT and closing device...")
        ptt_disengage(dwf_lib, hdwf)
        dwf_lib.FDwfAnalogOutReset(hdwf, c_int(0))
        dwf_lib.FDwfDeviceCloseAll()

    def sigint_handler(sig, frame):
        cleanup()
        sys.exit(1)

    signal_module.signal(signal_module.SIGINT, sigint_handler)

    results = []

    try:
        for i in range(args.iterations):
            iter_num = i + 1
            print(f"\n--- Iteration {iter_num}/{args.iterations} ---")

            ptt_engage(dwf_lib, hdwf)
            time.sleep(args.settle_time)

            ch1, ch2, dio, lost, corrupted = acquire_data(
                dwf_lib, hdwf, n_samples, capture_flag=args.capture_flag
            )

            ptt_disengage(dwf_lib, hdwf)

            if lost > 0:
                print(f"  WARNING: {lost} samples lost")
            if corrupted > 0:
                print(f"  WARNING: {corrupted} samples corrupted")

            res_i = analyze_channel(
                ch1, sample_rate, "I", args.min_rms, args.min_snr
            )
            res_q = analyze_channel(
                ch2, sample_rate, "Q", args.min_rms, args.min_snr
            )

            passed = res_i["passed"] and res_q["passed"]
            status = "PASS" if passed else "FAIL"

            print(f"  [{status}] I: RMS={res_i['rms']:.4f}V "
                  f"SNR={res_i['snr_db']:.1f}dB "
                  f"freq={res_i['dominant_freq']:.1f}Hz  "
                  f"Q: RMS={res_q['rms']:.4f}V "
                  f"SNR={res_q['snr_db']:.1f}dB "
                  f"freq={res_q['dominant_freq']:.1f}Hz")

            if not passed:
                for r in res_i["failure_reasons"] + res_q["failure_reasons"]:
                    print(f"    -> {r}")
                npz_path, png_path = save_waveform(
                    args.output_dir, iter_num, ch1, ch2, sample_rate, dio
                )
                print(f"    Saved: {npz_path}")
                print(f"    Plot:  {png_path}")
            elif args.save_all:
                save_waveform(args.output_dir, iter_num, ch1, ch2, sample_rate, dio)

            results.append({
                "iteration": iter_num,
                "passed": passed,
                "i": res_i,
                "q": res_q,
                "lost": lost,
                "corrupted": corrupted,
            })

            if i < args.iterations - 1:
                time.sleep(args.interval)

        # --- Summary ---
        print("\n" + "=" * 64)
        total = len(results)
        n_passed = sum(1 for r in results if r["passed"])
        n_failed = total - n_passed
        print(f"SUMMARY: {n_passed}/{total} passed, {n_failed}/{total} failed")
        print("=" * 64)

        if n_failed > 0:
            print("\nFailed iterations:")
            for r in results:
                if not r["passed"]:
                    reasons = (r["i"]["failure_reasons"]
                               + r["q"]["failure_reasons"])
                    print(f"  #{r['iteration']}: {', '.join(reasons)}")

        i_snrs = [r["i"]["snr_db"] for r in results]
        q_snrs = [r["q"]["snr_db"] for r in results]
        print(f"\nI channel SNR: min={min(i_snrs):.1f}  "
              f"max={max(i_snrs):.1f}  mean={np.mean(i_snrs):.1f} dB")
        print(f"Q channel SNR: min={min(q_snrs):.1f}  "
              f"max={max(q_snrs):.1f}  mean={np.mean(q_snrs):.1f} dB")

        sys.exit(0 if n_failed == 0 else 1)

    finally:
        cleanup()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Transmit PTT signal integrity test using Analog Discovery 2"
    )
    parser.add_argument(
        "-n", "--iterations", type=int, default=100,
        help="number of PTT test cycles (default: 100)")
    parser.add_argument(
        "--settle-time", type=float, default=0.5,
        help="seconds to wait after PTT engage before capture (default: 0.5)")
    parser.add_argument(
        "--acquire-time", type=float, default=0.1,
        help="scope acquisition duration in seconds (default: 0.1)")
    parser.add_argument(
        "--interval", type=float, default=0.5,
        help="seconds to wait between iterations (default: 0.5)")
    parser.add_argument(
        "--sample-rate", type=float, default=100000,
        help="scope sample rate in Hz (default: 100000)")
    parser.add_argument(
        "--min-snr", type=float, default=20.0,
        help="minimum SNR in dB to pass spectral purity check (default: 20.0)")
    parser.add_argument(
        "--min-rms", type=float, default=0.005,
        help="minimum RMS amplitude in V to pass (default: 0.005)")
    parser.add_argument(
        "--output-dir", type=str, default=".",
        help="directory for saving failure data (default: current dir)")
    parser.add_argument(
        "--save-all", action="store_true",
        help="save waveform data for all iterations, not just failures")
    parser.add_argument(
        "--capture-flag", action="store_true",
        help="also capture AD2 DIO 1-4 (Teensy pins 28-31, the Flag() bus) "
             "in parallel with the scope; saved into the .npz as 'dio' uint8")

    args = parser.parse_args()
    run_test(args)


if __name__ == "__main__":
    main()
