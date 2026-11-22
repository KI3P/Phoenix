"""CMU200 access and spectrum measurements for the CESSB RF test.

Two kinds of measurement on the transmitter's output (through an attenuating tap):

* **Peak and average power** from the CMU200's narrow-band power meter (NPOWer)
  with its widest, 1 MHz Gaussian filter, which passes the whole SSB channel.
  ``avg`` is the mean power over a statistics cycle, ``max`` the largest
  instantaneous power seen in it: PEP. Verified on the bench (2026-11-19): a
  steady tone reads max - avg = 0.23 dB (residual carrier and image beat), a
  linear two-tone 3.04-3.16 dB (theory 3.01 dB).
* **Spectra** from the CMU200's spectrum analyzer: tone and product levels, and
  band powers (wanted sideband, opposite sideband, out of channel) as ratios.

The CMU200 driver is the one behind the bench's MCP server
(``~/Sync/Ham/015_CMU200_Control/mcp_server/cmu200.py``). It has no NPOWer
method, so :meth:`Cmu.npower` drives that subsystem through the driver's
serialised command helpers. Only one program can hold the GPIB adapter: release
the MCP server's hold (its ``release_instrument`` tool) before running.
"""

from __future__ import annotations

import math
import os
import sys
from dataclasses import dataclass

import numpy as np

DEFAULT_DRIVER_DIR = os.environ.get(
    "CMU200_DRIVER_DIR", os.path.expanduser("~/Sync/Ham/015_CMU200_Control/mcp_server"))


def db10(x: float) -> float:
    return 10.0 * math.log10(max(float(x), 1e-30))


def mw(dbm):
    return 10.0 ** (np.asarray(dbm, dtype=float) / 10.0)


# ---------------------------------------------------------------------------
# Instrument
# ---------------------------------------------------------------------------

@dataclass
class NPower:
    avg_dbm: float      # mean power over the statistics cycle
    max_dbm: float      # largest instantaneous power: PEP
    min_dbm: float
    freq_hz: float      # the CMU's frequency counter (strongest component)

    @property
    def papr_db(self) -> float:
        return self.max_dbm - self.avg_dbm


class Cmu:
    """The CMU200 on one RF input, for NPOWer and spectrum measurements."""

    def __init__(self, rf_input: str = "RF1", max_level_dbm: float = 0.0,
                 driver_dir: str = DEFAULT_DRIVER_DIR):
        if driver_dir not in sys.path:
            sys.path.insert(0, driver_dir)
        from cmu200 import CMU200   # noqa: E402  (the bench's own driver)
        self.dev = CMU200()
        self.rf_input = rf_input
        self.max_level_dbm = max_level_dbm

    def __enter__(self) -> "Cmu":
        return self

    def __exit__(self, *exc) -> None:
        self.release()

    def release(self) -> None:
        self.dev.release()

    def identity(self) -> str:
        return self.dev.status().get("idn", "")

    def npower(self, freq_hz: float, stats: int = 200, bw_hz: float = 1e6) -> NPower:
        d = self.dev

        def _measure():
            d.power_config = None             # the driver's cache: we change the level mode
            d._select_input(self.rf_input)
            d._write_checked(f"SENSe:LEVel:MAXimum {self.max_level_dbm}")
            d._write_checked("SENSe:LEVel:MODE MANual")
            d._write_checked(f"SENSe:RFANalyzer:FREQuency {freq_hz}")
            d._write_checked(f"SENSe:NPOWer:BWIDth:RESolution {bw_hz}")
            d._write_checked(f"CONFigure:NPOWer:CONTrol {int(stats)},SINGleshot,NONE,NONE")
            old = d.inst.timeout
            d.inst.timeout = 120000
            try:
                reply = d._query("READ:NPOWer?")
            finally:
                d.inst.timeout = old
            v = [float(x) for x in reply.split(",")]
            if any(math.isnan(x) for x in v[3:6]):
                raise RuntimeError(f"NPOWer gave no valid result: {reply!r}")
            return NPower(avg_dbm=v[3], max_dbm=v[5], min_dbm=v[4], freq_hz=v[6])

        return d._run(_measure)

    def spectrum(self, start_hz: float, stop_hz: float, rbw_hz: float,
                 detector: str = "RMS", ref_dbm: float = -10.0, sweeps: int = 1,
                 trace: str = "current", timeout_s: float = 300.0):
        f, p, settings = self.dev.spectrum_sweep(
            start_hz, stop_hz, rf_input=self.rf_input, rbw_hz=rbw_hz, detector=detector,
            reference_level_dbm=ref_dbm, sweeps=sweeps, trace=trace, timeout_s=timeout_s)
        p = np.array([-200.0 if x is None else x for x in p])
        return np.asarray(f, dtype=float), p, settings


# ---------------------------------------------------------------------------
# Spectrum measurements. Frequencies relative to the carrier, signed so that
# the wanted sideband is positive (sign = +1 for USB, -1 for LSB).
# ---------------------------------------------------------------------------

def rel(f_hz: np.ndarray, carrier_hz: float, sign: int) -> np.ndarray:
    return sign * (np.asarray(f_hz) - carrier_hz)


def tone_dbm(fr: np.ndarray, p: np.ndarray, f0: float, tol_hz: float = 60.0) -> float:
    """Level of a discrete component near f0: the largest trace point within tol."""
    sel = np.abs(fr - f0) <= tol_hz
    return float(p[sel].max()) if sel.any() else float("nan")


def band_mw(fr: np.ndarray, p: np.ndarray, lo: float, hi: float) -> float:
    """Sum of trace power over lo..hi. Only ratios between bands of one trace mean anything."""
    a, b = min(lo, hi), max(lo, hi)
    sel = (fr >= a) & (fr <= b)
    return float(mw(p[sel]).sum())


def integrated_mw(fr: np.ndarray, p: np.ndarray, lo: float, hi: float, rbw_hz: float) -> float:
    """Power in lo..hi, mW: trace points summed, times point spacing over the RBW's noise
    bandwidth (1.065 x RBW for the CMU200's Gaussian filters)."""
    df = float(np.median(np.diff(fr))) if len(fr) > 1 else rbw_hz
    return band_mw(fr, p, lo, hi) * abs(df) / (1.065 * rbw_hz)


@dataclass
class SpectrumStats:
    opposite_dbc: float        # opposite sideband 200-3000 Hz, re the wanted 200-3000 Hz
    out_of_channel_dbc: float  # 3.3 kHz to the span edge, both sides, re wanted
    carrier_dbc: float         # residual carrier, re the wanted band power
    worst_ooc_dbc: float       # largest single point beyond 3.3 kHz, re the wanted band power
    ooc_mw: float | None = None  # out-of-channel power, integrated (needs rbw_hz)


def spectrum_stats(fr: np.ndarray, p: np.ndarray, edge_hz: float | None = None,
                   rbw_hz: float | None = None) -> SpectrumStats:
    edge = float(np.max(np.abs(fr))) if edge_hz is None else edge_hz
    wanted = band_mw(fr, p, 200.0, 3000.0)
    opposite = band_mw(fr, p, -3000.0, -200.0)
    ooc = band_mw(fr, p, 3300.0, edge) + band_mw(fr, p, -edge, -3300.0)
    far = np.abs(fr) >= 3300.0
    worst = float(p[far].max()) if far.any() else -200.0
    return SpectrumStats(
        opposite_dbc=db10(opposite / wanted),
        out_of_channel_dbc=db10(ooc / wanted),
        carrier_dbc=tone_dbm(fr, p, 0.0) - db10(wanted),
        worst_ooc_dbc=worst - db10(wanted),
        ooc_mw=None if rbw_hz is None else
        integrated_mw(fr, p, 3300.0, edge, rbw_hz) + integrated_mw(fr, p, -edge, -3300.0, rbw_hz),
    )


def two_tone_rf(fr: np.ndarray, p: np.ndarray, f1: float = 700.0, f2: float = 1900.0,
                tol_hz: float = 60.0) -> dict:
    """Tone levels and odd-order products, dBc re one tone and dB re PEP.

    PEP of two equal tones is 6.02 dB above either, so ``_dbpep = _dbc - 6.02``
    for equal tones; computed from the actual two levels otherwise.
    """
    t1, t2 = tone_dbm(fr, p, f1, tol_hz), tone_dbm(fr, p, f2, tol_hz)
    ref = 0.5 * (t1 + t2)
    pep = 20 * math.log10(10 ** (t1 / 20) + 10 ** (t2 / 20))
    products = {
        "imd3_low": 2 * f1 - f2, "imd3_high": 2 * f2 - f1,
        "imd5_low": 3 * f1 - 2 * f2, "imd5_high": 3 * f2 - 2 * f1,
        "imd7_low": 4 * f1 - 3 * f2, "imd7_high": 4 * f2 - 3 * f1,
    }
    out = {"tone1_dbm": t1, "tone2_dbm": t2, "pep_dbm_from_tones": pep}
    for name, f in products.items():
        lv = tone_dbm(fr, p, f, tol_hz)
        out[name + "_hz"] = f
        out[name + "_dbc"] = lv - ref
        out[name + "_dbpep"] = lv - pep
    return out
