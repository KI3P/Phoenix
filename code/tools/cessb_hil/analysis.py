"""Measurements on captured exciter I/Q for the CESSB bench test.

Everything takes the complex signal ``z = I + jQ`` as captured by the AD2 (DC
already removed by :meth:`IqCapture.analytic`) and the side of DC the wanted
sideband occupies (``sign``, +1 or -1, found by the wiring detection).

The capture is band-limited to +/-6 kHz with a linear-phase FIR before the
envelope is taken. That removes the scope's broadband noise and the exciter
DAC's images (which the 100 kS/s scope aliases to around 8 kHz) without
touching the SSB channel: a linear-phase filter that is flat across the
channel does not change the channel's envelope, so it cannot hide or invent
overshoot.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
from scipy import signal

LPF_HZ = 6000.0
LPF_TAPS = 101


def db(x: float) -> float:
    return 20.0 * math.log10(max(float(x), 1e-15))


def db_power(x: float) -> float:
    return 10.0 * math.log10(max(float(x), 1e-30))


def band_limit(z: np.ndarray, fs_hz: float) -> np.ndarray:
    """Linear-phase lowpass at LPF_HZ, delay removed, edges trimmed."""
    h = signal.firwin(LPF_TAPS, LPF_HZ, fs=fs_hz)
    y = signal.lfilter(h, 1.0, z)
    d = (LPF_TAPS - 1) // 2
    return y[2 * d:]          # drop the filter's start-up, delay already aligned out


def envelope(z: np.ndarray, fs_hz: float) -> np.ndarray:
    return np.abs(band_limit(z, fs_hz))


def tone_level(z: np.ndarray, fs_hz: float, f_hz: float) -> float:
    """Amplitude of the complex exponential at signed f_hz, Hann-windowed correlation."""
    n = len(z)
    w = np.hanning(n)
    ref = np.exp(-2j * np.pi * f_hz * np.arange(n) / fs_hz)
    return float(np.abs(np.sum((z - np.mean(z)) * w * ref)) / w.sum())


def welch(z: np.ndarray, fs_hz: float, res_hz: float = 25.0):
    nper = int(2 ** round(math.log2(fs_hz / res_hz)))
    nper = min(nper, len(z))
    f, p = signal.welch(z, fs=fs_hz, nperseg=nper, return_onesided=False,
                        window="hann", detrend="constant")
    order = np.argsort(f)
    return f[order], p[order]


def band_power(f: np.ndarray, p: np.ndarray, lo: float, hi: float) -> float:
    a, b = min(lo, hi), max(lo, hi)
    sel = (f >= a) & (f <= b)
    return float(p[sel].sum())


@dataclass
class EnvelopeStats:
    peak: float                # largest envelope sample, volts
    mean_power: float          # mean |z|^2, volts^2
    wanted_fraction: float     # share of the power in the wanted 200-3000 Hz band
    opposite_dbc: float        # opposite-sideband 200-3000 Hz, dB re wanted
    out_of_channel_dbc: float  # 3.3-6 kHz either side, dB re wanted

    @property
    def papr_db(self) -> float:
        return db_power(self.peak ** 2 / self.mean_power)

    @property
    def wanted_papr_db(self) -> float:
        """Peak power over the average power in the wanted sideband only.

        The talk-power figure: power in the opposite sideband or out of channel
        is transmitted but heard by nobody tuned to this signal.
        """
        return db_power(self.peak ** 2 / (self.mean_power * self.wanted_fraction))


def envelope_stats(z: np.ndarray, fs_hz: float, sign: int) -> EnvelopeStats:
    zb = band_limit(z, fs_hz)
    env = np.abs(zb)
    f, p = welch(zb, fs_hz)
    total = float(p.sum())
    wanted = band_power(f, p, sign * 200.0, sign * 3000.0)
    opposite = band_power(f, p, -sign * 200.0, -sign * 3000.0)
    ooc = band_power(f, p, 3300.0, 6000.0) + band_power(f, p, -6000.0, -3300.0)
    return EnvelopeStats(
        peak=float(env.max()),
        mean_power=float(np.mean(env ** 2)),
        wanted_fraction=wanted / total if total > 0 else 0.0,
        opposite_dbc=db_power(opposite / wanted) if wanted > 0 else 0.0,
        out_of_channel_dbc=db_power(ooc / wanted) if wanted > 0 else 0.0,
    )


def two_tone_products(z: np.ndarray, fs_hz: float, sign: int,
                      f1_hz: float = 700.0, f2_hz: float = 1900.0) -> dict:
    """Levels of the two tones and their odd-order products, dB re one tone.

    ``sign`` puts the tones on the wanted side; a product whose signed frequency
    has the other sign is in the opposite sideband.
    """
    zb = band_limit(z, fs_hz)
    t1 = tone_level(zb, fs_hz, sign * f1_hz)
    t2 = tone_level(zb, fs_hz, sign * f2_hz)
    ref = 0.5 * (t1 + t2)
    products = {
        "imd3_low": 2 * f1_hz - f2_hz,     # -500 Hz: opposite sideband
        "imd3_high": 2 * f2_hz - f1_hz,    # 3100 Hz: just past the channel edge
        "imd5_low": 3 * f1_hz - 2 * f2_hz,  # -1700 Hz: opposite sideband
        "imd5_high": 3 * f2_hz - 2 * f1_hz,  # 4300 Hz
        # The mirror of imd3_low, on the wanted side. CESSB does not put anything
        # here, but distortion of the *audio* (microphone input, codec) does: a
        # real 2f1-f2 product at 500 Hz goes through the Hilbert pair onto the
        # wanted side. The exciter's I/Q imbalance then images it onto imd3_low,
        # so a strong one here explains an imd3_low worse than the DSP produces.
        "imd3_low_mirror": -(2 * f1_hz - f2_hz),
    }
    out = {"tone_v": ref, "tone_balance_db": db(t1 / t2) if t2 > 0 else 0.0}
    for name, f in products.items():
        out[name + "_dbc"] = db(tone_level(zb, fs_hz, sign * f) / ref) if ref > 0 else 0.0
        out[name + "_hz"] = sign * f
    return out


def median_envelope(z: np.ndarray, fs_hz: float) -> float:
    """Envelope of a steady single tone: the median is immune to noise spikes."""
    return float(np.median(envelope(z, fs_hz)))


def find_ceiling(levels_on: list[float], levels_off: list[float],
                 compress_db: float = 3.0) -> dict:
    """Locate the CESSB ceiling from a single-tone level sweep.

    ``levels_on`` / ``levels_off`` are the steady envelopes, CESSB on and off, at
    the same rising list of drive amplitudes. Below the ceiling the two agree;
    once the input is well past it, CESSB holds the output flat. The ceiling is
    the mean on-envelope over the points where the off-envelope is at least
    ``compress_db`` above it - points that are unambiguously limited.
    """
    on = np.asarray(levels_on, dtype=float)
    off = np.asarray(levels_off, dtype=float)
    ratio_db = 20 * np.log10(np.maximum(off, 1e-15) / np.maximum(on, 1e-15))
    limited = np.where(ratio_db >= compress_db)[0]
    if limited.size < 2:
        return {"found": False, "ceiling_v": None, "limited_points": int(limited.size)}
    ceiling = float(np.mean(on[limited]))
    spread = float(20 * np.log10(on[limited].max() / on[limited].min()))
    linear = np.where(ratio_db < 0.5)[0]
    return {"found": True, "ceiling_v": ceiling, "limited_points": int(limited.size),
            "limited_spread_db": spread, "first_limited_index": int(limited[0]),
            "linear_points": int(linear.size)}
