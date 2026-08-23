#!/usr/bin/env python3
"""Recover the analog design specs behind the Phoenix frozen filter tables.

The receive chain used to ship several coefficient tables that had been designed
offline at a single sample rate (24 ksps audio, i.e. 192 ksps ADC / 8).  Running
the radio at any other rate scaled every corner and centre frequency by
``actual_rate / 24000``.  Those tables are now generated at run time from an
analog spec instead, so they land on the same frequencies at any sample rate.

This script is what produced those specs.  It reads the reference tables in
``code/test/reference_filters.cpp`` (verbatim copies of the tables that used to
live in ``DSP_FIR.cpp``) and prints the C literals that belong in
``InitializeReceiveAudioFilterCoeffs()`` and ``InitializeTransmitFilterCoeffs()``.

It is a developer tool, run once.  Its output is pasted into DSP_FIR.cpp - it is
not part of the build.  Re-run it if a reference table ever changes.

Usage:
    python3 code/tools/extract_filter_prototypes.py

Requires numpy and scipy.
"""

import os
import re
import sys

import numpy as np

try:
    import scipy.signal as sig
except ImportError:  # pragma: no cover - developer tool
    sys.exit("scipy is required: pip install scipy")


REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REFERENCE_FILE = os.path.join(REPO, "code", "test", "reference_filters.cpp")

# The audio sample rate the frozen tables were designed at: 192000 / DF, DF = 8.
DESIGN_FS_HZ = 24000.0

# Labelled centre frequencies of the 14 equaliser cells, in Hz.  These are the
# values shown in the UI and printed in the original table comments.
EQ_BAND_FC_HZ = [198.425, 250.0, 314.98, 400.0, 500.0, 630.0, 793.0,
                 1000.0, 1259.0, 1587.0, 2000.0, 2500.0, 3150.0, 4000.0]

# Labelled cutoffs of the five CW audio filters, in Hz.  Only used to annotate
# the output - the number the generator actually needs is the ripple edge.
CW_LABEL_HZ = [840.0, 1080.0, 1320.0, 1800.0, 2000.0]

EQ_SECTIONS = 4      # biquads per equaliser cell
CW_ORDER = 12        # poles in each CW audio lowpass


def read_table(source, name):
    """Return the floats of a `float32_t <name>[N] = { ... };` definition."""
    match = re.search(r"float32_t\s+" + name + r"\s*\[\d+\]\s*=[^{]*\{(.*?)\}\s*;",
                      source, re.S)
    if match is None:
        raise KeyError("no table named %s in %s" % (name, REFERENCE_FILE))
    body = re.sub(r"//.*", "", match.group(1))
    return np.array([float(v) for v in
                     re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", body)])


def arm_to_sos(coeffs):
    """Convert ARM biquad coefficients {b0,b1,b2,-a1,-a2} to scipy sos rows."""
    coeffs = np.asarray(coeffs).reshape(-1, 5)
    sos = np.zeros((coeffs.shape[0], 6))
    sos[:, 0:3] = coeffs[:, 0:3]
    sos[:, 3] = 1.0
    sos[:, 4] = -coeffs[:, 3]
    sos[:, 5] = -coeffs[:, 4]
    return sos


def ripple_edge_hz(coeffs, fs_hz):
    """Find the ripple edge of a Chebyshev type I lowpass.

    The tables are normalised so DC sits at 0 dB and the passband ripples above
    it.  The ripple edge is therefore the highest frequency at which the
    response is still at or above the DC level; past it the filter rolls off
    monotonically.  That frequency is exactly the `Wn` a Chebyshev designer
    takes as its argument.
    """
    freq, resp = sig.sosfreqz(arm_to_sos(coeffs), worN=1000000, fs=fs_hz)
    db = 20.0 * np.log10(np.abs(resp) + 1e-30)
    at_or_above_dc = np.where(db >= db[0])[0]
    return freq[at_or_above_dc[-1]], db[:at_or_above_dc[-1]].max() - db[0]


def analog_bandpass_sections(coeffs, fs_hz, fc_hz):
    """Recover the analog (wn, Q) of each biquad in a bandpass cascade.

    Each frozen equaliser biquad has b1 = 0 and b2 = -b0, so its zeros sit at
    z = +/-1.  That is the bilinear image of the analog bandpass
    ``(wn/Q)s / (s^2 + (wn/Q)s + wn^2)``, which means the design survives an
    inverse bilinear transform exactly.

    wn is returned as a ratio of the *prewarped* centre frequency.  Rebuilding
    from that ratio pins the digital peak on fc at whatever sample rate the
    radio is running, which is the whole point of the exercise.
    """
    prewarped_fc = 2.0 * fs_hz * np.tan(np.pi * fc_hz / fs_hz)
    sections = []
    for b0, _b1, _b2, neg_a1, neg_a2 in np.asarray(coeffs).reshape(-1, 5):
        pole = np.roots([1.0, -neg_a1, -neg_a2])[0]
        analog_pole = (2.0 * fs_hz) * (pole - 1.0) / (pole + 1.0)
        wn = abs(analog_pole)
        sections.append((wn / prewarped_fc, wn / (2.0 * abs(analog_pole.real))))
    return sections


def rebuild_bandpass(sections, fc_hz, fs_hz, gain):
    """Mirror of CalcBandpassCascadeCoeffs, used here to check the round trip."""
    prewarped_fc = 2.0 * fs_hz * np.tan(np.pi * fc_hz / fs_hz)
    k = 2.0 * fs_hz
    out = []
    for wn_ratio, q in sections:
        wn = wn_ratio * prewarped_fc
        denom = k * k + k * wn / q + wn * wn
        b0 = (k * wn / q) / denom
        out.append([-b0 * gain, 0.0, b0 * gain,
                    -2.0 * (wn * wn - k * k) / denom,
                    -(k * k - k * wn / q + wn * wn) / denom])
    return np.array(out).ravel()


def rebuild_chebyshev(order, ripple_db, edge_hz, fs_hz):
    """Mirror of CalcChebyshevILowpassCoeffs, used here to check the round trip."""
    eps = np.sqrt(10.0 ** (ripple_db / 10.0) - 1.0)
    v0 = np.arcsinh(1.0 / eps) / order
    wc = 2.0 * fs_hz * np.tan(np.pi * edge_hz / fs_hz)
    k = 2.0 * fs_hz
    out = []
    for i in range(order // 2):
        theta = np.pi * (2 * i + 1) / (2 * order)
        real = -np.sinh(v0) * np.sin(theta) * wc
        imag = np.cosh(v0) * np.cos(theta) * wc
        wn = np.hypot(real, imag)
        q = wn / (2.0 * abs(real))
        denom = k * k + k * wn / q + wn * wn
        b0 = wn * wn / denom
        out.append([b0, 2.0 * b0, b0,
                    -2.0 * (wn * wn - k * k) / denom,
                    -(k * k - k * wn / q + wn * wn) / denom])
    out = np.array(out)
    dc = abs(sig.sosfreqz(arm_to_sos(out.ravel()), worN=[0], fs=fs_hz)[1][0])
    out[:, 0:3] /= dc ** (1.0 / len(out))
    return out.ravel()


def max_error_db(reference, generated, fs_hz, flo_hz, fhi_hz):
    """Worst-case magnitude difference between two biquad cascades, in dB."""
    freq, ref = sig.sosfreqz(arm_to_sos(reference), worN=50000, fs=fs_hz)
    _, gen = sig.sosfreqz(arm_to_sos(generated), worN=50000, fs=fs_hz)
    band = (freq >= flo_hz) & (freq <= fhi_hz)
    ref_db = 20.0 * np.log10(np.abs(ref) + 1e-30)
    gen_db = 20.0 * np.log10(np.abs(gen) + 1e-30)
    return np.max(np.abs(ref_db[band] - gen_db[band]))


def corner_hz(coeffs_or_taps, fs_hz, level_db, is_fir=False):
    """Frequency at which the response first drops below `level_db`."""
    if is_fir:
        freq, resp = sig.freqz(coeffs_or_taps, worN=400000, fs=fs_hz)
    else:
        freq, resp = sig.sosfreqz(arm_to_sos(coeffs_or_taps), worN=400000, fs=fs_hz)
    db = 20.0 * np.log10(np.abs(resp) + 1e-30)
    db -= db[0]
    below = np.where(db < level_db)[0]
    return freq[below[0]] if below.size else float("nan")


def main():
    with open(REFERENCE_FILE) as handle:
        source = handle.read()

    print("Analog design specs recovered from %s" % os.path.relpath(REFERENCE_FILE, REPO))
    print("Design sample rate: %.0f Hz\n" % DESIGN_FS_HZ)

    # ------------------------------------------------------------------
    # CW audio lowpass filters: Chebyshev type I, 12 poles
    # ------------------------------------------------------------------
    print("=" * 72)
    print("CW audio lowpass filters (Chebyshev type I, %d poles)" % CW_ORDER)
    print("=" * 72)
    edges, ripples = [], []
    for index, label in enumerate(CW_LABEL_HZ, start=1):
        table = read_table(source, "CW_AudioFilterCoeffs%d_ref" % index)
        edge, ripple = ripple_edge_hz(table, DESIGN_FS_HZ)
        edges.append(edge)
        ripples.append(ripple)
        rebuilt = rebuild_chebyshev(CW_ORDER, ripple, edge, DESIGN_FS_HZ)
        error = max_error_db(table, rebuilt, DESIGN_FS_HZ, 0.0, label * 3.0)
        print("  %6.0f Hz label: ripple edge %8.2f Hz, ripple %.4f dB, "
              "round trip %.4f dB, -6 dB at %7.1f Hz"
              % (label, edge, ripple, error, corner_hz(table, DESIGN_FS_HZ, -6.0)))

    print("\n  Passband ripple across all five filters: %.4f .. %.4f dB"
          % (min(ripples), max(ripples)))
    print("\n  /* C literal for DSP_FIR.cpp */")
    print("  static const float32_t CW_AUDIO_RIPPLE_EDGE_HZ[CW_AUDIO_FILTER_COUNT] = {")
    print("      " + ", ".join("%.1ff" % e for e in edges))
    print("  };")

    # ------------------------------------------------------------------
    # Equaliser cells: 4 stagger-tuned analog bandpass sections
    # ------------------------------------------------------------------
    print("\n" + "=" * 72)
    print("Equaliser cells (%d analog bandpass sections each)" % EQ_SECTIONS)
    print("=" * 72)
    protos, gains, worst = [], [], 0.0
    for index, fc in enumerate(EQ_BAND_FC_HZ, start=1):
        table = read_table(source, "EQ_Band%dCoeffs_ref" % index)
        sections = analog_bandpass_sections(table, DESIGN_FS_HZ, fc)
        protos.append(sections)

        # Per-section gain that puts the cascade peak at unity, matching the
        # normalisation of the frozen tables.
        unity = rebuild_bandpass(sections, fc, DESIGN_FS_HZ, 1.0)
        _, resp = sig.sosfreqz(arm_to_sos(unity), worN=200000, fs=DESIGN_FS_HZ)
        gain = (1.0 / np.max(np.abs(resp))) ** (1.0 / EQ_SECTIONS)
        gains.append(gain)

        rebuilt = rebuild_bandpass(sections, fc, DESIGN_FS_HZ, gain)
        error = max_error_db(table, rebuilt, DESIGN_FS_HZ, fc * 0.3, fc * 3.0)
        worst = max(worst, error)
        print("  band %2d  fc %8.2f Hz  gain %.6f  round trip %.2e dB"
              % (index, fc, gain, error))

    print("\n  Worst round trip error over all %d cells: %.2e dB" % (len(protos), worst))
    print("\n  /* C literals for DSP_FIR.cpp */")
    print("  static const float32_t EQ_BAND_FC_HZ[EQUALIZER_CELL_COUNT] = {")
    for row in range(0, len(EQ_BAND_FC_HZ), 5):
        chunk = EQ_BAND_FC_HZ[row:row + 5]
        print("      " + ", ".join("%.3ff" % f for f in chunk) + ",")
    print("  };")
    print("  static const float32_t EQ_BAND_GAIN[EQUALIZER_CELL_COUNT] = {")
    for row in range(0, len(gains), 4):
        chunk = gains[row:row + 4]
        print("      " + ", ".join("%.9ff" % g for g in chunk) + ",")
    print("  };")
    print("  static const BandpassProtoSection EQ_BAND_PROTO[EQUALIZER_CELL_COUNT]"
          "[EQ_PROTO_SECTIONS] = {")
    for index, sections in enumerate(protos, start=1):
        body = ", ".join("{%.9ff, %.9ff}" % (r, q) for r, q in sections)
        print("      {%s},  /* band %d */" % (body, index))
    print("  };")

    # ------------------------------------------------------------------
    # FIR stages: report the corners the Kaiser designs have to reproduce
    # ------------------------------------------------------------------
    print("\n" + "=" * 72)
    print("FIR stages (targets for CalcFIRCoeffs)")
    print("=" * 72)
    fir_specs = [
        ("CW_Filter_Coeffs2_ref", DESIGN_FS_HZ, "CW decode FIR"),
        ("FIR_int3_12ksps_48tap_2k7_ref", DESIGN_FS_HZ, "TX audio bandwidth (interp by 2)"),
        ("coeffs12K_8K_LPF_FIR_ref", DESIGN_FS_HZ, "TX decimate by 2, 24k -> 12k"),
    ]
    for name, fs_hz, description in fir_specs:
        taps = read_table(source, name)
        print("  %s (%d taps, %s)" % (description, len(taps), name))
        print("      DC gain %.6f" % taps.sum())
        for level in (-3.0, -6.0, -60.0):
            hz = corner_hz(taps, fs_hz, level, is_fir=True)
            print("      %5.0f dB at %8.1f Hz  (%.4f * Fs)" % (level, hz, hz / fs_hz))
    print("\n  CalcFIRCoeffs treats its fc argument as the -6 dB point, so pass the")
    print("  -6 dB figure above rather than the -3 dB one when matching a reference.")
    print("\n  Note: coeffs12K_8K_LPF_FIR feeds a decimate-by-2 stage, so its stopband")
    print("  must sit below 0.25 * Fs. The reference table does not - it is flat well")
    print("  past that - which is why the replacement is a genuine lowpass rather than")
    print("  a reproduction of the original response.")


if __name__ == "__main__":
    main()
