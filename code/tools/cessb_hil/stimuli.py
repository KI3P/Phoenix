"""Periodic test signals for the CESSB bench test.

Every stimulus here is exactly periodic, so one period fits the AD2's custom
waveform buffer and the AWG repeats it without a seam. That makes each
measurement exactly repeatable - the same waveform on every run and every radio
- and lets the offline model (code/tools/cessb_model) predict the result for the
very same signal.

* ``two_tone``: 700 + 1900 Hz, equal amplitude. The classic SSB linearity test.
  Its products land on known frequencies, one of them (2f1 - f2) in the
  opposite sideband, which is where an uncentred CESSB leaks.
* ``vowel``: a synthetic voiced vowel - a Rosenberg glottal pulse train through
  three formant resonators. It has the property that matters here: the Hilbert
  pair turns its sharp glottal pulses into envelope peaks well above the average,
  as real voice does. Steady, so its peak-to-average ratio is lower than speech's.
* ``syllable``: the same vowel in a raised-cosine burst, one per 0.25 s loop,
  silence between. The syllable-rate envelope is what gives speech most of its
  crest factor, and with it this stimulus behaves like the W9GR voice clip in the
  offline model: CESSB gains 2.4 / 4.1 / 5.2 dB at 3 / 6 / 9 dB of drive, against
  2.8 / 4.6 / 5.3 dB for real speech. It is the main voice stand-in.

All buffers are normalised to a peak of 1.0, which is what the AWG expects of a
custom waveform: its amplitude setting then gives the peak in volts.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal

#: Formants (Hz, bandwidth Hz) of two contrasting vowels, adult male values.
VOWELS = {
    "ah": dict(f0_hz=125.0, formants=((700.0, 80.0), (1220.0, 90.0), (2600.0, 120.0))),
    "ee": dict(f0_hz=200.0, formants=((270.0, 60.0), (2290.0, 100.0), (3010.0, 120.0))),
}


@dataclass(frozen=True)
class Stimulus:
    """One period of a test waveform."""

    name: str
    samples: np.ndarray        # one period, peak 1.0
    period_hz: float           # repetition rate: the AWG plays the buffer this often
    tones_hz: tuple = ()       # component frequencies, when it is made of tones

    def at_rate(self, fs_hz: float, seconds: float) -> np.ndarray:
        """The stimulus resampled to fs_hz for ``seconds`` (for the offline model)."""
        n = len(self.samples)
        t = np.arange(int(round(fs_hz * seconds))) / fs_hz
        # Periodic, so band-limited interpolation is exact: evaluate the DFT series.
        spec = np.fft.rfft(self.samples) / n
        k = np.arange(len(spec))
        k_max = len(spec) - 1 if n % 2 else len(spec) - 2   # leave out Nyquist
        out = np.full(len(t), spec[0].real)
        phase = 2 * np.pi * self.period_hz * t
        for kk in range(1, k_max + 1):
            if abs(spec[kk]) < 1e-9:
                continue
            out += 2 * np.real(spec[kk] * np.exp(1j * kk * phase))
        return out


def _normalise(x: np.ndarray) -> np.ndarray:
    x = x - np.mean(x)
    return x / np.max(np.abs(x))


def two_tone(n: int, f1_hz: float = 700.0, f2_hz: float = 1900.0) -> Stimulus:
    period_hz = float(np.gcd(int(f1_hz), int(f2_hz)))
    k = np.arange(n)
    x = (np.sin(2 * np.pi * (f1_hz / period_hz) * k / n)
         + np.sin(2 * np.pi * (f2_hz / period_hz) * k / n))
    return Stimulus("two_tone", _normalise(x), period_hz, (f1_hz, f2_hz))


def _rosenberg_pulse(n: int, open_frac: float = 0.4, close_frac: float = 0.16) -> np.ndarray:
    """One period of the Rosenberg glottal flow pulse, sampled n times."""
    t = np.arange(n) / n
    tp, tn = open_frac, close_frac
    g = np.zeros(n)
    rise = t < tp
    fall = (t >= tp) & (t < tp + tn)
    g[rise] = 0.5 * (1 - np.cos(np.pi * t[rise] / tp))
    g[fall] = np.cos(0.5 * np.pi * (t[fall] - tp) / tn)
    return g


def vowel(n: int, name: str = "ah") -> Stimulus:
    """A steady synthetic vowel, one pitch period in n samples.

    The glottal flow is differentiated (lip radiation) and passed through three
    second-order resonators. The resonators are run over enough repeated periods
    to reach the periodic steady state, and the last period is kept, so the
    buffer joins to itself with no transient.
    """
    spec = VOWELS[name]
    f0 = spec["f0_hz"]
    fs = f0 * n
    g = np.diff(np.tile(_rosenberg_pulse(n), 2))[-n:]     # differentiated, periodic
    x = np.tile(g, 40)
    for fc, bw in spec["formants"]:
        r = np.exp(-np.pi * bw / fs)
        a = [1.0, -2 * r * np.cos(2 * np.pi * fc / fs), r * r]
        x = signal.lfilter([1.0 - r], a, x)
    return Stimulus(f"vowel_{name}", _normalise(x[-n:]), f0)


def _formant_filter(x: np.ndarray, fs: float, name: str) -> np.ndarray:
    for fc, bw in VOWELS[name]["formants"]:
        r = np.exp(-np.pi * bw / fs)
        a = [1.0, -2 * r * np.cos(2 * np.pi * fc / fs), r * r]
        x = signal.lfilter([1.0 - r], a, x)
    return x


def syllable(n: int, name: str = "ah", loop_s: float = 0.25, burst_frac: float = 0.6) -> Stimulus:
    """A vowel burst with a syllable-rate envelope, one per loop_s, in n samples.

    The pitch is chosen so a whole number of pitch periods fits the loop (128 Hz
    for a 0.25 s loop: 32 periods), so the buffer joins to itself with no seam.
    At n = 4096 the AWG plays it at 16.4 kS/s; its images above 13 kHz are
    removed by the transmit chain's decimation filters long before the 3.5 kHz
    channel filter.
    """
    fs = n / loop_s
    target = VOWELS[name]["f0_hz"] * loop_s
    periods = min((d for d in range(1, n + 1) if n % d == 0), key=lambda d: abs(d - target))
    per = n // periods
    g = np.diff(np.tile(_rosenberg_pulse(per), 2))[-per:]
    x = _formant_filter(np.tile(g, 40 * periods), fs, name)[-n:]
    t = np.arange(n) / n
    env = np.where(t < burst_frac, np.sin(np.pi * t / burst_frac) ** 2, 0.0)
    return Stimulus(f"syllable_{name}", _normalise(x * env), 1.0 / loop_s)


def all_stimuli(n: int) -> dict[str, Stimulus]:
    """The stimuli the suite runs, in order."""
    out = {"two_tone": two_tone(n)}
    s = vowel(n, "ah")
    out[s.name] = s
    s = syllable(n, "ah")
    out[s.name] = s
    return out
