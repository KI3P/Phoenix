"""Metrology for the transmit filter HIL suite.

The receive suite measures a real audio signal, where a tone at ``f`` and one at
``-f`` are the same thing. The transmit suite measures the complex pair ``I + jQ``
coming out of the exciter, where they are not: a single-sideband transmitter is
*supposed* to put all its energy on one side of DC, and how much leaks onto the
other side is the headline specification of the whole chain.

So everything here works on the two-sided spectrum, and every level carries a
sign. ``+f`` and ``-f`` are called *wanted* and *image*; which one is which
depends on the selected sideband and on which scope input landed on I, both of
which are discovered at run time.

Scalar curve fitting that does not care about the signal being complex - grids,
crossings, percentage deltas - is imported from :mod:`filter_hil.measure` rather
than reimplemented, so both suites share one set of well-exercised helpers.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Optional, Sequence

import numpy as np

from filter_hil.measure import (geom_grid, interpolate_crossing,  # noqa: F401
                                parabolic_peak, pct_delta, stats)

from .ad2 import IqCapture

#: Levels below this are the float noise of the measurement, not signal.
FLOOR_DBV = -140.0


def to_db(x: float) -> float:
    """Amplitude ratio to decibels, floored so a silent bin does not diverge."""
    return 20.0 * math.log10(max(float(x), 1e-15))


# --- spectra ---------------------------------------------------------------

def complex_spectrum(z: np.ndarray, fs_hz: float) -> tuple[np.ndarray, np.ndarray]:
    """Two-sided spectrum of a complex signal, in volts peak.

    Returns ``(freqs_hz, amplitude)`` with the frequencies sorted ascending from
    ``-fs/2``, so negative frequencies are addressable directly rather than
    through a wrapped index.

    Scaled so that ``A * exp(2j*pi*f*t)`` reads ``A`` at ``f``. Note the absence
    of the factor of two that :func:`filter_hil.measure.spectrum_dbv` applies: a
    real cosine splits its amplitude between two conjugate lines and needs them
    put back together, a complex exponential has only the one line.
    """
    n = len(z)
    if n < 16:
        return np.zeros(0), np.zeros(0)
    ac = z - np.mean(z)
    window = np.hanning(n)
    coherent_gain = window.sum() / n
    spec = np.fft.fftshift(np.fft.fft(ac * window)) / (n * coherent_gain)
    freqs = np.fft.fftshift(np.fft.fftfreq(n, d=1.0 / fs_hz))
    return freqs, np.abs(spec)


def correlate_complex(z: np.ndarray, fs_hz: float, f_hz: float) -> float:
    """Amplitude of a known-frequency component of a complex signal.

    Reading the tallest FFT bin costs up to 1.4 dB when the component falls
    between bins. That cancels when two captures are compared at the same
    frequency, but not when a ratio between two *different* frequencies is
    wanted - which is exactly what sideband suppression is. So the wanted and the
    image are both read this way.

    ``f_hz`` is signed.
    """
    n = len(z)
    if n < 16:
        return 0.0
    ac = z - np.mean(z)
    window = np.hanning(n)
    ref = np.exp(-2j * np.pi * f_hz * np.arange(n) / fs_hz)
    return float(np.abs(np.sum(ac * window * ref)) / window.sum())


def locate_line(freqs: np.ndarray, mag: np.ndarray, f0_hz: float,
                bw_hz: float) -> tuple[float, float]:
    """Interpolated frequency and amplitude of the strongest line near f0_hz.

    A window rather than a single bin: the radio's audio frequency differs from
    the requested one by tens of ppm - the Teensy synthesises its I2S clock with a
    fractional divider - and a Hann window spreads a line over several bins.
    """
    if freqs.size == 0:
        return float(f0_hz), 0.0
    sel = np.where(np.abs(freqs - f0_hz) <= bw_hz)[0]
    if sel.size == 0:
        return float(f0_hz), 0.0
    local = int(sel[np.argmax(mag[sel])])
    delta = parabolic_peak(mag, local)
    df = freqs[1] - freqs[0]
    return float(freqs[local] + delta * df), float(mag[local])


def dominant_line(cap: IqCapture, swap: bool, f_min_hz: float = 100.0,
                  f_max_hz: Optional[float] = None) -> tuple[float, float]:
    """Signed frequency and amplitude of the strongest line in a capture.

    Used by the wiring detection, which has to find where the transmitted energy
    landed rather than look where it was expected. ``f_min_hz`` excludes the
    carrier-nulling residue around DC, which is real signal but not the tone.
    """
    freqs, mag = complex_spectrum(cap.analytic(swap), cap.sample_rate_hz)
    if freqs.size == 0:
        return 0.0, 0.0
    hi = f_max_hz if f_max_hz is not None else cap.sample_rate_hz / 2.0
    sel = np.where((np.abs(freqs) >= f_min_hz) & (np.abs(freqs) <= hi))[0]
    if sel.size == 0:
        return 0.0, 0.0
    idx = int(sel[np.argmax(mag[sel])])
    df = freqs[1] - freqs[0]
    return float(freqs[idx] + parabolic_peak(mag, idx) * df), float(mag[idx])


# --- one measured point ----------------------------------------------------

@dataclass
class IqMeasurement:
    """One tone measured out of one synchronous I/Q capture."""

    f_target_hz: float          # audio frequency asked of the AWG
    f_measured_hz: float        # signed frequency the energy actually landed on
    wanted_v: float             # amplitude on the wanted side of DC
    image_v: float              # amplitude on the other side
    level_dbv: float            # wanted, in dBV
    suppression_db: float       # wanted over image, positive is good
    carrier_dbc: float          # DC residue relative to the wanted tone
    thd_db: float               # harmonics of the wanted tone, relative to it
    snr_db: float
    dc_ch1_v: float
    dc_ch2_v: float
    peak_v: float
    clipped: bool
    lost: int
    corrupted: int
    valid: bool
    reason: str = ""


def measure_iq_tone(cap: IqCapture, f_audio_hz: float, *, swap: bool,
                    sideband_sign: int, scope_range_v: float,
                    scope_offset_v: float = 0.0, clip_margin: float = 0.90,
                    search_bw_hz: float = 60.0, n_harmonics: int = 5,
                    min_snr_db: float = 20.0, max_thd_db: float = -30.0,
                    thd_invalidates: bool = False) -> IqMeasurement:
    """Measure the transmitted tone, its image, and whether to trust the result.

    ``sideband_sign`` says which side of DC the wanted energy is on once ``swap``
    has been applied; both come from the wiring detection. The image is read at
    the exact mirror frequency of where the wanted line was *measured*, not at
    the mirror of where it was asked for, so a few tens of ppm of clock error do
    not bleed the skirt of the wanted line into the image reading.

    A point is invalid when the capture dropped samples, when the scope input
    clipped, or when the tone is buried. Invalid points are excluded from fits
    rather than dragging them off.

    ``thd_db`` is reported but by default does not invalidate a point: a little
    distortion at 2f does not corrupt the level measured at f. Auto-levelling
    passes ``thd_invalidates=True``, because there it is the signal that the
    microphone input has been driven too hard.
    """
    fs = cap.sample_rate_hz
    z = cap.analytic(swap)
    freqs, mag = complex_spectrum(z, fs)

    sign = 1 if sideband_sign >= 0 else -1
    f_measured, _ = locate_line(freqs, mag, sign * abs(f_audio_hz), search_bw_hz)

    wanted = correlate_complex(z, fs, f_measured)
    image = correlate_complex(z, fs, -f_measured)

    dc1, dc2 = cap.dc()
    # The exciter's DC bias is a property of the analog output stage; what the
    # firmware controls is the *difference* the carrier-nulling offsets add on
    # top of the scope's own centring. Reported relative to the tone, which is
    # how carrier suppression is specified.
    carrier_v = math.hypot(dc1 - scope_offset_v, dc2 - scope_offset_v)
    peak = cap.peak()

    # Noise: everything outside the wanted line, its image, its harmonics and the
    # region around DC. The DC exclusion has to be generous - the carrier residue
    # is not a single bin once the Hann window has spread it.
    df = float(freqs[1] - freqs[0]) if freqs.size > 1 else 1.0
    guard = max(search_bw_hz, 3 * df)
    mask = np.abs(freqs) > max(30.0, 3 * df)
    mask &= np.abs(freqs - f_measured) > guard
    mask &= np.abs(freqs + f_measured) > guard
    for k in range(2, n_harmonics + 1):
        for fk in (k * f_measured, -k * f_measured):
            if abs(fk) < fs / 2:
                mask &= np.abs(freqs - fk) > guard
    noise_power = float(np.sum(mag[mask] ** 2))
    tone_power = wanted ** 2
    snr = (10.0 * math.log10(tone_power / noise_power)
           if noise_power > 0 and tone_power > 0 else -99.0)

    # Harmonic distortion. Harmonics of a single-sideband tone land on the same
    # side of DC as the tone itself, so only that side is summed.
    harm_power = 0.0
    for k in range(2, n_harmonics + 1):
        fk = k * f_measured
        if abs(fk) < fs / 2:
            _, hk = locate_line(freqs, mag, fk, guard)
            harm_power += hk ** 2
    thd = (10.0 * math.log10(harm_power / tone_power)
           if tone_power > 0 and harm_power > 0 else -99.0)

    suppression = to_db(wanted) - to_db(image)
    carrier_dbc = to_db(carrier_v) - to_db(wanted)

    # The AD2's range is peak to peak and centred on the configured offset, so
    # the window is offset +/- range/2 and the clip test is against half the
    # range, measured from the window centre rather than from zero.
    excursion = cap.excursion(scope_offset_v)
    clipped = excursion >= clip_margin * 0.5 * scope_range_v

    reasons = []
    if cap.lost or cap.corrupted:
        reasons.append(f"capture dropped {cap.lost} lost / {cap.corrupted} "
                       f"corrupted samples")
    if clipped:
        reasons.append(f"excursion {excursion:.3f} V from the {scope_offset_v:+.2f} V "
                       f"centre exceeds {clip_margin:.0%} of the "
                       f"{0.5*scope_range_v:.2f} V half-range")
    if snr < min_snr_db:
        reasons.append(f"SNR {snr:.1f} dB below {min_snr_db:.1f} dB")
    if thd_invalidates and thd > max_thd_db:
        reasons.append(f"THD {thd:.1f} dB above {max_thd_db:.1f} dB")

    return IqMeasurement(
        f_target_hz=float(f_audio_hz),
        f_measured_hz=float(f_measured),
        wanted_v=wanted,
        image_v=image,
        level_dbv=to_db(wanted),
        suppression_db=suppression,
        carrier_dbc=carrier_dbc,
        thd_db=thd,
        snr_db=snr,
        dc_ch1_v=dc1,
        dc_ch2_v=dc2,
        peak_v=peak,
        clipped=clipped,
        lost=cap.lost,
        corrupted=cap.corrupted,
        valid=not reasons,
        reason="; ".join(reasons),
    )


def spur_level_dbc(cap: IqCapture, swap: bool, f_spur_hz: float,
                   reference_v: float, search_bw_hz: float = 60.0) -> float:
    """Level at a spurious frequency, relative to a passband reference.

    Both signs of ``f_spur_hz`` are examined and the larger returned. A fold-back
    product's sideband is not predictable: the decimator folds it before the
    Hilbert transform decides which side of DC things end up on, so an alias can
    emerge on either side. Looking only at the wanted side would under-report it.
    """
    z = cap.analytic(swap)
    freqs, mag = complex_spectrum(z, cap.sample_rate_hz)
    if freqs.size == 0 or reference_v <= 0.0:
        return float("nan")
    _, hi_pos = locate_line(freqs, mag, abs(f_spur_hz), search_bw_hz)
    _, hi_neg = locate_line(freqs, mag, -abs(f_spur_hz), search_bw_hz)
    return to_db(max(hi_pos, hi_neg)) - to_db(reference_v)


# --- curve features -------------------------------------------------------

def passband_reference_db(freqs: Sequence[float], levels_db: Sequence[float],
                          f_lo_hz: float, f_hi_hz: float) -> float:
    """Passband level of a sweep: the median inside a known-flat window.

    A median rather than a mean or a maximum. The transmit passband is the sum of
    14 equaliser cells and is not perfectly smooth, and a single noisy point near
    the top would drag a maximum up and move every corner derived from it.
    """
    f = np.asarray(freqs, dtype=float)
    y = np.asarray(levels_db, dtype=float)
    sel = np.where((f >= f_lo_hz) & (f <= f_hi_hz) & np.isfinite(y))[0]
    if sel.size == 0:
        return float("nan")
    return float(np.median(y[sel]))


def corner_from_sweep(freqs: Sequence[float], levels_db: Sequence[float],
                      target_db: float, *, side: str,
                      from_hz: float) -> float:
    """Frequency at which a sweep crosses target_db, walking away from from_hz.

    ``side='high'`` walks upwards from ``from_hz`` and returns the first crossing;
    ``side='low'`` walks downwards. Interpolates linearly in (Hz, dB) between the
    two points that straddle the crossing.

    Walking outwards from inside the passband rather than scanning the whole
    sweep matters: the response comes back up out of the stopband in places - the
    equaliser's reconstruction is not monotonic far from the passband, and fold-back
    products land wherever they land - so a global search can return a crossing
    on the far side of a stopband ripple.
    """
    f = np.asarray(freqs, dtype=float)
    y = np.asarray(levels_db, dtype=float)
    keep = np.isfinite(f) & np.isfinite(y)
    f, y = f[keep], y[keep]
    if f.size < 2:
        return float("nan")
    order = np.argsort(f)
    f, y = f[order], y[order]

    start = int(np.argmin(np.abs(f - from_hz)))
    if side == "high":
        indices = range(start, f.size - 1)
        step = 1
    elif side == "low":
        indices = range(start, 0, -1)
        step = -1
    else:
        raise ValueError(f"side must be 'high' or 'low', not {side!r}")

    for i in indices:
        j = i + step
        if y[i] >= target_db >= y[j]:
            return interpolate_crossing(f[i], y[i], f[j], y[j], target_db)
    return float("nan")


def sweep_ripple_db(freqs: Sequence[float], levels_db: Sequence[float],
                    f_lo_hz: float, f_hi_hz: float) -> float:
    """Peak-to-peak variation of a sweep inside a window."""
    f = np.asarray(freqs, dtype=float)
    y = np.asarray(levels_db, dtype=float)
    sel = np.where((f >= f_lo_hz) & (f <= f_hi_hz) & np.isfinite(y))[0]
    if sel.size < 2:
        return float("nan")
    return float(np.max(y[sel]) - np.min(y[sel]))


def worst_in_band(freqs: Sequence[float], values_db: Sequence[float],
                  f_lo_hz: float, f_hi_hz: float,
                  best: bool = False) -> tuple[float, float]:
    """The (frequency, value) of the worst - or best - point inside a window.

    Used for sideband suppression, where the number that matters is the worst
    case across the passband rather than an average of it.
    """
    f = np.asarray(freqs, dtype=float)
    y = np.asarray(values_db, dtype=float)
    sel = np.where((f >= f_lo_hz) & (f <= f_hi_hz) & np.isfinite(y))[0]
    if sel.size == 0:
        return float("nan"), float("nan")
    idx = int(sel[np.argmax(y[sel])]) if best else int(sel[np.argmin(y[sel])])
    return float(f[idx]), float(y[idx])
