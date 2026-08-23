#!/usr/bin/env python3
"""Self-tests for the filter HIL suite. No hardware required.

These check the parts that would silently give wrong answers on the bench: the
tone metrology, the curve-feature extraction, the ED dump parser, and - most
importantly - that the cross-rate comparison actually fails when handed the
frequency shift it exists to catch.

    python3 -m pytest test_filter_hil.py        # or just run it directly
"""

from __future__ import annotations

import math
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from filter_hil import bandtable as bt
from filter_hil import report as report_mod
from filter_hil import tests as T
from filter_hil.ad2 import Capture
from filter_hil.measure import (bandwidth_3db, find_crossing, geom_grid,
                                measure_tone, dominant_tone, parabolic_peak,
                                pct_delta, peak_from_three, spectrum_dbv)
from filter_hil.radio import EdSnapshot

FS = 96000.0
N = int(FS * 0.25)


def _tone(f_hz: float, amplitude: float = 0.5, noise: float = 0.0,
          fs: float = FS, n: int = N) -> Capture:
    t = np.arange(n) / fs
    x = amplitude * np.cos(2 * np.pi * f_hz * t)
    if noise:
        rng = np.random.default_rng(1234)
        x = x + rng.normal(0.0, noise, n)
    return Capture(samples=x, sample_rate_hz=fs, lost=0, corrupted=0, t_utc="")


def check(cond: bool, label: str) -> None:
    if not cond:
        raise AssertionError(label)
    print(f"  ok  {label}")


# --- metrology -------------------------------------------------------------

def test_measure_tone_recovers_amplitude_and_frequency():
    print("measure_tone")
    for f in (137.0, 1000.0, 2750.5, 4000.0):
        m = measure_tone(_tone(f, 0.4), f, scope_range_v=2.0)
        check(abs(m.amplitude_v - 0.4) < 0.004,
              f"amplitude at {f} Hz: {m.amplitude_v:.4f} (want 0.4)")
        check(abs(m.f_measured_hz - f) < 1.0,
              f"frequency at {f} Hz: {m.f_measured_hz:.2f}")


def test_measure_tone_is_accurate_in_db():
    """The whole suite compares levels in dB, so small errors matter."""
    print("measure_tone dB accuracy")
    ref = measure_tone(_tone(1000.0, 0.5), 1000.0, scope_range_v=2.0)
    for atten_db in (-1.0, -3.0, -6.0, -20.0, -40.0):
        amp = 0.5 * 10 ** (atten_db / 20.0)
        m = measure_tone(_tone(1000.0, amp), 1000.0, scope_range_v=2.0)
        got = m.level_dbv - ref.level_dbv
        check(abs(got - atten_db) < 0.05,
              f"{atten_db:+.0f} dB reads {got:+.3f} dB")


def test_measure_tone_flags_bad_captures():
    print("measure_tone validity")
    cap = _tone(1000.0, 0.5)
    cap.lost = 12
    check(not measure_tone(cap, 1000.0, scope_range_v=2.0).valid,
          "a capture with lost samples is marked invalid")

    clipped = measure_tone(_tone(1000.0, 1.95), 1000.0, scope_range_v=2.0)
    check(not clipped.valid and clipped.clipped, "clipping is detected")

    buried = measure_tone(_tone(1000.0, 0.001, noise=0.5), 1000.0,
                          scope_range_v=2.0, min_snr_db=20.0)
    check(not buried.valid, "a tone below the noise is marked invalid")


def test_dominant_tone_finds_an_unknown_frequency():
    print("dominant_tone")
    f, _ = dominant_tone(_tone(1234.5, 0.3))
    check(abs(f - 1234.5) < 1.0, f"found {f:.2f} Hz (want 1234.5)")


def test_parabolic_peak_beats_bin_quantisation():
    print("parabolic_peak")
    # A tone deliberately placed between bins.
    f = 1000.0 + 0.5 * (FS / N)
    freqs, mag = spectrum_dbv(_tone(f, 0.5).samples, FS)
    idx = int(np.argmax(mag))
    coarse = freqs[idx]
    fine = (idx + parabolic_peak(mag, idx)) * (freqs[1] - freqs[0])
    check(abs(fine - f) < abs(coarse - f),
          f"interpolated {fine:.3f} beats bin centre {coarse:.3f} (true {f:.3f})")


# --- curve features --------------------------------------------------------

def _lowpass_db(f_hz: float, corner_hz: float, order: int = 12) -> float:
    """Magnitude of an ideal Butterworth-ish lowpass, in dB."""
    return -10.0 * math.log10(1.0 + (f_hz / corner_hz) ** (2 * order))


def test_find_crossing_locates_a_known_corner():
    print("find_crossing")
    for corner in (840.0, 1320.0, 2000.0):
        got, trace = find_crossing(lambda f, c=corner: _lowpass_db(f, c),
                                   -3.0, corner * 0.6, corner * 1.45, tol_hz=1.0)
        check(abs(got - corner) < 5.0,
              f"-3 dB of a {corner:.0f} Hz lowpass found at {got:.1f} Hz")
        check(len(trace) >= 3, "the bisection trace was recorded")


def test_find_crossing_rejects_a_bad_bracket():
    print("find_crossing bracketing")
    got, _ = find_crossing(lambda f: 0.0, -3.0, 100.0, 200.0)
    check(math.isnan(got), "a response that never crosses returns NaN")


def test_peak_and_bandwidth_of_a_resonance():
    print("peak_from_three / bandwidth_3db")
    fc, q = 1000.0, 4.0
    f = geom_grid(fc * 0.6, fc * 1.6, 25)

    def resp(x):
        return -10.0 * np.log10(1.0 + (q * (x / fc - fc / x)) ** 2)

    y = resp(f)
    peak = peak_from_three(f, y)
    check(abs(peak - fc) / fc < 0.01, f"peak found at {peak:.1f} Hz (want {fc:.0f})")

    lo, hi, q_meas = bandwidth_3db(f, y)
    check(abs(q_meas - q) / q < 0.10, f"Q measured {q_meas:.2f} (want {q:.1f})")


# --- ED parsing ------------------------------------------------------------

ED_FIXTURE = """agc:               0
fineTuneFreq_Hz[0]: -12250
fineTuneFreq_Hz[1]: 0
audioVolume:       55
rfGainAllBands_dB: 0
nrOptionSelect:    0
ANR_notchOn:       0
spectrum_zoom:     1
sampleRate:        13
CWFilterIndex:     5
CWToneIndex:       3
activeVFO:         0
modulation[0]:     0
modulation[1]:     0
currentBand[0]:    5
currentBand[1]:    5
equalizerRec: 100,100,100,100,100,100,100,100,100,100,100,100,100,100
equalizerXmt: 100,100,100,100,100,100,100,100,100,100,100,100,100,100"""


def test_ed_snapshot_parses_the_dump():
    print("EdSnapshot.parse")
    ed = EdSnapshot.parse(ED_FIXTURE.splitlines())
    check(ed.agc == 0 and ed.agc_off, "AGC parsed as off")
    check(ed.sample_rate_hz == 192000, f"sample rate {ed.sample_rate_hz}")
    check(ed.cw_filter_index == 5, "CW filter index")
    check(ed.cw_tone_index == 3, "CW tone index")
    check(len(ed.equalizer_rec) == 14, "all 14 equaliser cells")
    check(ed.modulation_name == "USB", f"modulation {ed.modulation_name}")
    check(ed.active_fine_tune_hz == -12250.0,
          f"fine tune {ed.active_fine_tune_hz:+.0f} Hz")


def test_ed_snapshot_survives_interleaved_debug_output():
    print("EdSnapshot.parse with noise")
    noisy = ["Debug: something happened", "USB_RX: 1234 5678"] + \
            ED_FIXTURE.splitlines() + ["Debug: more chatter"]
    ed = EdSnapshot.parse(noisy)
    check(ed.sample_rate_hz == 192000, "parsed despite interleaved Debug lines")


def test_ed_snapshot_reports_what_is_missing():
    print("EdSnapshot.parse failure")
    try:
        EdSnapshot.parse(["audioVolume: 55"])
    except Exception as exc:
        check("agc" in str(exc), f"names the missing field: {exc}")
    else:
        raise AssertionError("parsing an incomplete dump should raise")


# --- the comparison, which is the point of the whole suite -----------------

def _synthetic_rates(shift: float) -> dict:
    """Build a results structure with 176.4k features scaled by `shift`."""
    out = {}
    for rate in (192000, 176400):
        s = 1.0 if rate == 192000 else shift
        out[str(rate)] = {
            "rate_hz": rate,
            "cw_filters": {"filters": [
                {"index": k, "nominal_hz": f, "corner_3db_hz": f * 1.012 * s,
                 "points": [], "bisection_trace": []}
                for k, f in enumerate(bt.CW_FILTER_NOMINAL_HZ)]},
            "equalizer_cells": {"cells": [
                {"index": i, "nominal_hz": f, "peak_hz": f * s, "q": bt.eq_q(i),
                 "q_expected": bt.eq_q(i),
                 "edge_limited": i in bt.EQ_EDGE_LIMITED_CELLS, "points": []}
                for i, f in enumerate(bt.EQ_CENTRE_HZ)]},
            "ssb_filter": {"settings": [
                {"fw_hz": w, "hi_edge_6db_hz": float(w), "bisection_trace": []}
                for w in (1800, 2400, 2800, 3000)]},
        }
    return out


def test_comparison_passes_a_healthy_radio():
    print("compare_rates on a good build")
    comp, checks = T.compare_rates(_synthetic_rates(1.0), 1.5, 4.0)
    rate_checks = [c for c in checks if c.id.endswith("rate_invariance")]
    check(len(rate_checks) == 5 + 14 + 4, f"{len(rate_checks)} rate checks generated")
    check(all(c.passed for c in rate_checks), "all rate-invariance checks pass")
    check(all(r["verdict"] == "PASS" for r in comp["cw_filters"]),
          "every CW filter passes")


def test_comparison_catches_the_legacy_shift():
    """The suite must fail on the bug it exists to detect."""
    print("compare_rates on the old frozen-table behaviour")
    comp, checks = T.compare_rates(_synthetic_rates(bt.LEGACY_RATE_RATIO), 1.5, 4.0)

    cw = [c for c in checks if c.id.startswith("cw.") and c.id.endswith("rate_invariance")]
    eq = [c for c in checks if c.id.startswith("eq.") and c.id.endswith("rate_invariance")]
    check(not any(c.passed for c in cw), "every CW filter fails")
    check(not any(c.passed for c in eq), "every EQ cell fails")

    check(all(r["legacy_consistent"] for r in comp["cw_filters"]),
          "the shift is recognised as the known frozen-table bug")
    for row in comp["cw_filters"]:
        check(abs(row["delta_pct"] - bt.LEGACY_DELTA_PCT) < 0.01,
              f"CW {row['nominal_hz']:.0f} Hz delta {row['delta_pct']:.3f}%")
    check(any("frozen coefficient tables" in c.message for c in cw),
          "the failure message explains what it matched")


def test_comparison_tolerance_has_margin_over_the_bug():
    """A 1.5% tolerance against an 8.125% bug is a 5x margin."""
    print("tolerance margin")
    margin = abs(bt.LEGACY_DELTA_PCT) / 1.5
    check(margin > 4.0, f"margin between tolerance and bug is {margin:.1f}x")

    # A shift a quarter the size of the bug must still fail.
    comp, checks = T.compare_rates(_synthetic_rates(1.0 - 0.02), 1.5, 4.0)
    cw = [c for c in checks if c.id.startswith("cw.") and c.id.endswith("rate_invariance")]
    check(not any(c.passed for c in cw), "a 2% shift still fails")


def test_comparison_handles_missing_measurements():
    print("compare_rates with gaps")
    data = _synthetic_rates(1.0)
    data["176400"]["cw_filters"]["filters"][2]["corner_3db_hz"] = None
    comp, checks = T.compare_rates(data, 1.5, 4.0)
    skipped = [c for c in checks if c.skipped]
    check(len(skipped) >= 1, "an unmeasured filter is skipped, not failed")
    check(comp["cw_filters"][2]["verdict"] == "SKIP", "verdict is SKIP")


# --- report and plotting ---------------------------------------------------

def _doc(shift: float) -> dict:
    per_rate = _synthetic_rates(shift)
    comp, checks = T.compare_rates(per_rate, 1.5, 4.0)
    return report_mod.build_document(
        provenance={"git_commit": "abc1234", "git_dirty": False,
                    "build_info": "test", "dwf_version": "3.24.4",
                    "versions": {}, "command_line": "test", "host": "test"},
        config={"rate_tol_pct": 1.5, "nominal_tol_pct": 4.0},
        rig={"wiring": "W1 -> I (codec left), W2 -> Q (codec right)",
             "image_rejection_db": 34.2, "drive_amplitude_v": 0.03,
             "scope_rate_hz": FS, "capture_s": 0.25, "scope_range_v": 2.0,
             "trials": {"1": {"level_dbv": -20.0}, "-1": {"level_dbv": -54.0}}},
        baseline={"agc": 0, "sample_rate_hz": 192000, "audio_volume": 55,
                  "cw_filter_index": 5, "active_vfo": 0, "modulation": [0, 0],
                  "current_band": [5, 5], "id": "ID020;"},
        final={}, state_restored=True, residual_diff={}, per_rate=per_rate,
        comparison=comp, checks=checks, duration_s=531.0, warnings=[])


def test_report_renders_both_outcomes():
    print("report rendering")
    for shift, want in ((1.0, "PASS"), (bt.LEGACY_RATE_RATIO, "FAIL")):
        doc = _doc(shift)
        check(doc["summary"]["overall"] == want,
              f"shift {shift:.5f} gives overall {doc['summary']['overall']}")
        with tempfile.TemporaryDirectory() as tmp:
            j = report_mod.write_json(doc, os.path.join(tmp, "r.json"))
            m = report_mod.write_markdown(doc, os.path.join(tmp, "r.md"))
            check(os.path.getsize(j) > 500, "JSON written")
            text = open(m).read()
            check(want in text, f"Markdown states {want}")
            check("frozen" in text.lower(), "Markdown explains the bug being hunted")


def test_plots_render_from_a_document():
    print("plot rendering")
    from filter_hil.plot_filter_hil import render_all
    doc = _doc(bt.LEGACY_RATE_RATIO)
    with tempfile.TemporaryDirectory() as tmp:
        paths = render_all(doc, tmp, "t")
        check(len(paths) >= 4, f"{len(paths)} figures rendered")
        for p in paths:
            check(os.path.getsize(p) > 3000, f"{os.path.basename(p)} is non-trivial")


# --- design constants ------------------------------------------------------

def test_bandtable_matches_firmware_shapes():
    print("bandtable consistency")
    check(len(bt.CW_FILTER_NOMINAL_HZ) == len(bt.CW_FILTER_RIPPLE_EDGE_HZ),
          "one ripple edge per CW filter")
    check(len(bt.EQ_CENTRE_HZ) == bt.EQ_CELL_COUNT, "14 equaliser centres")
    check(len(bt.EQ_BANDWIDTH_HZ) == bt.EQ_CELL_COUNT, "14 equaliser bandwidths")
    check(all(e < n for e, n in zip(bt.CW_FILTER_RIPPLE_EDGE_HZ,
                                    bt.CW_FILTER_NOMINAL_HZ)),
          "every ripple edge sits below its labelled cutoff")
    check(abs(bt.LEGACY_DELTA_PCT + 8.125) < 1e-9,
          f"legacy shift is {bt.LEGACY_DELTA_PCT:.4f}%")
    check(bt.fs_over_4_hz(192000) == 48000 and bt.fs_over_4_hz(176400) == 44100,
          "Fs/4 for both rates")


def test_injection_plan_includes_fine_tune():
    """The demodulation centre is |Fs/4 + fineTune|, not Fs/4.

    Measured on the bench: with Fs/4 = 44100 and fineTune = -12250, injecting
    32350/32850/33350 Hz produced 500/1000/1500 Hz of audio.
    """
    print("InjectionPlan fine tune")
    p = T.InjectionPlan(rate_hz=176400, fine_tune_hz=-12250.0, sideband_sign=+1)
    check(abs(p.centre_hz - 31850.0) < 1e-6, f"centre {p.centre_hz:.0f} Hz")
    for f_audio, want in ((500.0, 32350.0), (1000.0, 32850.0), (1500.0, 33350.0)):
        got = p.inject_hz(f_audio)
        check(abs(got - want) < 1e-6, f"{f_audio:.0f} Hz audio <- {got:.0f} Hz")

    zero = T.InjectionPlan(rate_hz=176400, fine_tune_hz=0.0)
    check(abs(zero.centre_hz - 44100.0) < 1e-6,
          "with no fine tune the centre is Fs/4")


def test_injection_plan_recomputes_per_rate():
    """The crux: the same audio frequency needs a different injection per rate."""
    print("InjectionPlan")
    a = T.InjectionPlan(rate_hz=192000)
    b = T.InjectionPlan(rate_hz=176400)
    check(abs(a.inject_hz(1000.0) - 49000.0) < 1e-6,
          f"192k: 1 kHz audio <- {a.inject_hz(1000.0):.1f} Hz")
    check(abs(b.inject_hz(1000.0) - 45100.0) < 1e-6,
          f"176.4k: 1 kHz audio <- {b.inject_hz(1000.0):.1f} Hz")

    cw = T.InjectionPlan(rate_hz=192000, sidetone_hz=750.0)
    check(abs(cw.inject_hz(1000.0) - 49750.0) < 1e-6,
          "the CW sidetone offset shifts the injection")


def test_injection_plan_handles_lower_sideband():
    """On an LSB band the passband is negative, so audio comes from Fs/4 - f."""
    print("InjectionPlan sideband")
    usb = T.InjectionPlan(rate_hz=192000, sideband_sign=+1)
    lsb = T.InjectionPlan(rate_hz=192000, sideband_sign=-1)
    check(abs(usb.inject_hz(1000.0) - 49000.0) < 1e-6,
          f"USB: {usb.inject_hz(1000.0):.0f} Hz")
    check(abs(lsb.inject_hz(1000.0) - 47000.0) < 1e-6,
          f"LSB: {lsb.inject_hz(1000.0):.0f} Hz")

    lsb176 = T.InjectionPlan(rate_hz=176400, sideband_sign=-1)
    check(abs(lsb176.inject_hz(1000.0) - 43100.0) < 1e-6,
          f"LSB at 176.4k: {lsb176.inject_hz(1000.0):.0f} Hz")

    cw_lsb = T.InjectionPlan(rate_hz=192000, sideband_sign=-1, sidetone_hz=750.0)
    check(abs(cw_lsb.inject_hz(1000.0) - 46250.0) < 1e-6,
          "the sidetone also flips with the sideband")


def main() -> int:
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = []
    for fn in fns:
        try:
            fn()
        except AssertionError as exc:
            print(f"  FAIL {fn.__name__}: {exc}")
            failed.append(fn.__name__)
        except Exception as exc:
            print(f"  ERROR {fn.__name__}: {type(exc).__name__}: {exc}")
            failed.append(fn.__name__)
    print(f"\n{len(fns) - len(failed)}/{len(fns)} passed")
    for name in failed:
        print(f"  failed: {name}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
