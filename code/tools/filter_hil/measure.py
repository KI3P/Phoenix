"""Tone metrology and curve-feature extraction for the filter HIL suite.

The suite measures filter responses by injecting a tone and reading the level of
that tone out of the demodulated audio. Everything here is about doing that
accurately enough to compare two sample rates: the shift being hunted is 8.125 %
in frequency, and the tolerance is 1.5 %, so amplitude estimates need to be good
to a small fraction of a decibel and frequency estimates to a fraction of a bin.

The FFT approach is lifted from ``code/tools/receive_chain_test.py`` - Hann
window with coherent-gain correction, plus parabolic interpolation for sub-bin
peak location. Peak picking in the time domain, which some of the older tools
use, is not good enough here: a sine sampled at 6 points per cycle can read over
a decibel low depending on where the samples land.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable, Sequence

import numpy as np

from .ad2 import Capture

# Levels below this are the float noise of the measurement, not signal.
FLOOR_DBV = -140.0


def _to_db(x: float) -> float:
    return 20.0 * math.log10(max(float(x), 1e-15))


def spectrum_dbv(samples: np.ndarray, fs_hz: float) -> tuple[np.ndarray, np.ndarray]:
    """Return (freqs_hz, amplitude) of a real signal.

    Amplitude is in volts peak, corrected for the Hann window's coherent gain,
    so a 1 V amplitude sine reads 1.0 at its own frequency.
    """
    n = len(samples)
    if n < 16:
        return np.zeros(0), np.zeros(0)
    ac = samples - np.mean(samples)
    window = np.hanning(n)
    coherent_gain = window.sum() / n
    spec = np.fft.rfft(ac * window) / (n * coherent_gain) * 2.0
    freqs = np.fft.rfftfreq(n, d=1.0 / fs_hz)
    return freqs, np.abs(spec)


def parabolic_peak(mag: np.ndarray, idx: int) -> float:
    """Sub-bin offset of a peak, by fitting a parabola to its three points."""
    if idx <= 0 or idx >= len(mag) - 1:
        return 0.0
    a, b, c = mag[idx - 1], mag[idx], mag[idx + 1]
    denom = a - 2.0 * b + c
    if denom == 0.0:
        return 0.0
    return float(0.5 * (a - c) / denom)


def correlate_amplitude(samples: np.ndarray, fs_hz: float, f_hz: float) -> float:
    """Amplitude of a known-frequency component, by windowed correlation.

    Reading the tallest FFT bin costs up to 1.4 dB when the tone falls between
    bins - Hann's scalloping loss. That cancels when two captures are compared at
    the same frequency, which is how this suite mostly works, but not when an
    absolute level is wanted. Correlating against a complex exponential at the
    measured frequency has no such error.
    """
    n = len(samples)
    if n < 16:
        return 0.0
    ac = samples - np.mean(samples)
    window = np.hanning(n)
    phase = 2.0 * np.pi * f_hz * np.arange(n) / fs_hz
    ref = np.exp(-1j * phase)
    return float(2.0 * np.abs(np.sum(ac * window * ref)) / window.sum())


def band_amplitude(freqs: np.ndarray, mag: np.ndarray, f0_hz: float,
                   bw_hz: float) -> tuple[float, float]:
    """Peak amplitude within +/- bw_hz of f0_hz, and its interpolated frequency.

    A window rather than a single bin, because the radio's audio frequency can
    differ from the requested one by a few Hz (the Fs/4 mapping is measured, not
    assumed) and because a Hann window spreads a tone over several bins.
    """
    if freqs.size == 0:
        return 0.0, f0_hz
    sel = np.where(np.abs(freqs - f0_hz) <= bw_hz)[0]
    if sel.size == 0:
        return 0.0, f0_hz
    local = int(sel[np.argmax(mag[sel])])
    delta = parabolic_peak(mag, local)
    df = freqs[1] - freqs[0]
    return float(mag[local]), float((local + delta) * df)


@dataclass
class ToneMeasurement:
    """One tone level read out of one capture."""

    f_target_hz: float
    f_measured_hz: float
    amplitude_v: float
    level_dbv: float
    rms_v: float
    peak_v: float
    dc_v: float
    snr_db: float
    thd_db: float
    clipped: bool
    lost: int
    corrupted: int
    valid: bool
    reason: str = ""


def measure_tone(cap: Capture, f_expect_hz: float, *, scope_range_v: float,
                 clip_margin: float = 0.90, search_bw_hz: float = 60.0,
                 n_harmonics: int = 5, min_snr_db: float = 20.0,
                 max_thd_db: float = -30.0,
                 thd_invalidates: bool = False) -> ToneMeasurement:
    """Measure one tone, and decide whether the measurement can be trusted.

    A point is marked invalid when the capture dropped samples, when the input
    clipped, or when the tone is buried. Invalid points are excluded from curve
    fits rather than dragging them off.

    ``thd_db`` is always reported but by default does **not** invalidate a
    point. Measured through a receiver, the harmonic bins contain the receiver's
    own noise as much as any real distortion, so at the 15-20 dB SNR this rig
    achieves the figure tracks the noise floor rather than the linearity - and
    a little distortion at 2f does not corrupt the level measured at f anyway.
    Auto-levelling passes ``thd_invalidates=True`` because there it genuinely is
    the signal that the drive has gone too far.
    """
    fs = cap.sample_rate_hz
    samples = cap.samples
    freqs, mag = spectrum_dbv(samples, fs)

    # Locate the tone from the spectrum, then read its amplitude by correlating
    # at that frequency - the FFT bin alone is up to 1.4 dB low off-bin.
    _, f_measured = band_amplitude(freqs, mag, f_expect_hz, search_bw_hz)
    amplitude = correlate_amplitude(samples, fs, f_measured)
    dc = float(np.mean(samples))
    ac = samples - dc
    rms = float(np.sqrt(np.mean(ac ** 2)))
    peak = float(np.max(np.abs(samples))) if samples.size else 0.0

    # Noise = everything above 20 Hz that is not the tone or its harmonics.
    df = freqs[1] - freqs[0] if freqs.size > 1 else 1.0
    mask = freqs > 20.0
    for k in range(1, n_harmonics + 1):
        fk = f_measured * k
        if fk < fs / 2:
            mask &= np.abs(freqs - fk) > max(search_bw_hz, 3 * df)
    noise_power = float(np.sum(mag[mask] ** 2))
    tone_power = amplitude ** 2
    snr = 10.0 * math.log10(tone_power / noise_power) if noise_power > 0 and tone_power > 0 else -99.0

    # Harmonic distortion, relative to the fundamental.
    harm_power = 0.0
    for k in range(2, n_harmonics + 1):
        fk = f_measured * k
        if fk < fs / 2:
            hk, _ = band_amplitude(freqs, mag, fk, max(search_bw_hz, 3 * df))
            harm_power += hk ** 2
    thd = 10.0 * math.log10(harm_power / tone_power) if tone_power > 0 and harm_power > 0 else -99.0

    clipped = peak >= clip_margin * scope_range_v

    reasons = []
    if cap.lost or cap.corrupted:
        reasons.append(f"capture dropped {cap.lost} lost / {cap.corrupted} corrupted samples")
    if clipped:
        reasons.append(f"peak {peak:.3f} V exceeds {clip_margin:.0%} of the {scope_range_v} V range")
    if snr < min_snr_db:
        reasons.append(f"SNR {snr:.1f} dB below {min_snr_db:.1f} dB")
    if thd_invalidates and thd > max_thd_db:
        reasons.append(f"THD {thd:.1f} dB above {max_thd_db:.1f} dB")

    return ToneMeasurement(
        f_target_hz=f_expect_hz,
        f_measured_hz=f_measured,
        amplitude_v=amplitude,
        level_dbv=_to_db(amplitude),
        rms_v=rms,
        peak_v=peak,
        dc_v=dc,
        snr_db=snr,
        thd_db=thd,
        clipped=clipped,
        lost=cap.lost,
        corrupted=cap.corrupted,
        valid=not reasons,
        reason="; ".join(reasons),
    )


def dominant_tone(cap: Capture, f_min_hz: float = 50.0,
                  f_max_hz: float | None = None) -> tuple[float, float]:
    """Frequency and amplitude of the strongest component in a band.

    Used by the Fs/4 mapping calibration, which has to find where the audio
    actually landed rather than look where it was expected.
    """
    freqs, mag = spectrum_dbv(cap.samples, cap.sample_rate_hz)
    if freqs.size == 0:
        return 0.0, 0.0
    hi = f_max_hz if f_max_hz is not None else cap.sample_rate_hz / 2
    sel = np.where((freqs >= f_min_hz) & (freqs <= hi))[0]
    if sel.size == 0:
        return 0.0, 0.0
    idx = int(sel[np.argmax(mag[sel])])
    df = freqs[1] - freqs[0]
    return float((idx + parabolic_peak(mag, idx)) * df), float(mag[idx])


# --- grids and curve features ---------------------------------------------

def geom_grid(f_lo_hz: float, f_hi_hz: float, n: int) -> np.ndarray:
    """Geometrically spaced frequencies - constant fractional resolution."""
    return np.geomspace(f_lo_hz, f_hi_hz, n)


def peak_from_three(freqs: Sequence[float], levels_db: Sequence[float]) -> float:
    """Interpolated peak of a resonance, fitted in (log f, dB).

    A bandpass response is close to parabolic in log-frequency near its peak, so
    fitting the three highest points there locates the centre far better than
    taking the largest sample - which would quantise to the probe grid.
    """
    f = np.asarray(freqs, dtype=float)
    y = np.asarray(levels_db, dtype=float)
    if f.size < 3:
        return float(f[int(np.argmax(y))]) if f.size else float("nan")

    i = int(np.argmax(y))
    i = max(1, min(i, f.size - 2))
    x = np.log(f[i - 1:i + 2])
    yy = y[i - 1:i + 2]
    denom = yy[0] - 2.0 * yy[1] + yy[2]
    if denom == 0.0:
        return float(f[i])
    delta = 0.5 * (yy[0] - yy[2]) / denom
    # Guard against a fit that runs away when the three points are nearly flat.
    if not np.isfinite(delta) or abs(delta) > 1.0:
        return float(f[i])
    step = x[1] - x[0]
    return float(np.exp(x[1] + delta * step))


def interpolate_crossing(f_lo: float, db_lo: float, f_hi: float, db_hi: float,
                         target_db: float) -> float:
    """Linear interpolation for where a response crosses a level."""
    if db_hi == db_lo:
        return f_lo
    return f_lo + (target_db - db_lo) * (f_hi - f_lo) / (db_hi - db_lo)


def find_crossing(probe: Callable[[float], float], target_db: float,
                  f_lo_hz: float, f_hi_hz: float, tol_hz: float = 5.0,
                  max_iter: int = 12) -> tuple[float, list[tuple[float, float]]]:
    """Bisect a monotonically falling response for where it crosses target_db.

    Returns the crossing frequency and every (frequency, level) pair probed, so
    the report can show how the number was arrived at.

    The caller must supply a bracket where the response is above the target at
    f_lo and below it at f_hi; returns NaN if that does not hold, rather than
    converging on nonsense.
    """
    trace: list[tuple[float, float]] = []

    def sample(f: float) -> float:
        db = probe(f)
        trace.append((f, db))
        return db

    lo, hi = float(f_lo_hz), float(f_hi_hz)
    db_lo = sample(lo)
    db_hi = sample(hi)

    # A probe can come back NaN because the tone dropped below the noise, which
    # is what the far side of a filter skirt looks like. Bisect towards it as if
    # it were deep in the stopband, but never interpolate through it - the
    # crossing is worked out at the end from the finite probes only.
    def below(db: float) -> bool:
        return (not np.isfinite(db)) or db <= target_db

    if np.isfinite(db_lo) and db_lo < target_db:
        return float("nan"), trace
    if np.isfinite(db_hi) and db_hi > target_db:
        return float("nan"), trace

    for _ in range(max_iter):
        if hi - lo <= tol_hz:
            break
        mid = 0.5 * (lo + hi)
        if below(sample(mid)):
            hi = mid
        else:
            lo = mid

    finite = sorted((f, db) for f, db in trace if np.isfinite(db))
    for (f0, d0), (f1, d1) in zip(finite, finite[1:]):
        if d0 >= target_db >= d1:
            return interpolate_crossing(f0, d0, f1, d1, target_db), trace
    return float("nan"), trace


def bandwidth_3db(freqs: Sequence[float], levels_db: Sequence[float]) -> tuple[float, float, float]:
    """(-3 dB low edge, high edge, Q) of a sampled bandpass response.

    Interpolates between grid points on each side of the peak. Returns NaNs when
    the sweep does not actually straddle the -3 dB level on both sides.
    """
    f = np.asarray(freqs, dtype=float)
    y = np.asarray(levels_db, dtype=float)
    if f.size < 3:
        return float("nan"), float("nan"), float("nan")

    order = np.argsort(f)
    f, y = f[order], y[order]
    i = int(np.argmax(y))
    target = y[i] - 3.0

    lo = float("nan")
    for j in range(i, 0, -1):
        if y[j - 1] <= target <= y[j]:
            lo = interpolate_crossing(f[j - 1], y[j - 1], f[j], y[j], target)
            break

    hi = float("nan")
    for j in range(i, f.size - 1):
        if y[j + 1] <= target <= y[j]:
            hi = interpolate_crossing(f[j], y[j], f[j + 1], y[j + 1], target)
            break

    if not (np.isfinite(lo) and np.isfinite(hi)) or hi <= lo:
        return lo, hi, float("nan")
    centre = peak_from_three(f, y)
    return lo, hi, float(centre / (hi - lo))


def stats(values: Sequence[float], unit: str = "hz") -> dict:
    """Summary statistics, keyed the way flag_timing.py keys them."""
    arr = np.asarray([v for v in values if np.isfinite(v)], dtype=float)
    if arr.size == 0:
        return {"count": 0}
    return {
        "count": int(arr.size),
        f"min_{unit}": float(np.min(arr)),
        f"max_{unit}": float(np.max(arr)),
        f"mean_{unit}": float(np.mean(arr)),
        f"median_{unit}": float(np.median(arr)),
        f"p95_{unit}": float(np.percentile(arr, 95)),
        f"stddev_{unit}": float(np.std(arr)),
    }


def pct_delta(reference: float, other: float) -> float:
    """Percentage change from reference to other."""
    if not np.isfinite(reference) or reference == 0.0 or not np.isfinite(other):
        return float("nan")
    return 100.0 * (other - reference) / reference
