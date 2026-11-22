#!/usr/bin/env python3
"""Self-tests for acp_analysis.py (no hardware)."""

from __future__ import annotations

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from cessb_hil.acp_analysis import (RxFilter, analyse_point, channel_mw, db10,  # noqa: E402
                                    phoenix_rx_taps)


def gaussian_trace(fr, tones, rbw):
    """What an RMS-detector trace shows for discrete tones (dBm) with a Gaussian RBW."""
    sigma = rbw / (2 * np.sqrt(2 * np.log(2)))
    p = np.full_like(fr, 1e-15)
    for f0, dbm in tones:
        p += 10 ** (dbm / 10) * np.exp(-0.5 * ((fr - f0) / sigma) ** 2)
    return 10 * np.log10(p)


class FilterTests(unittest.TestCase):
    def test_edges_and_one_sidedness(self):
        f = RxFilter()
        h = db10(f.power(np.array([300.0, 1500.0, 2700.0, -1500.0, 3300.0])))
        self.assertAlmostEqual(h[1], 0.0, places=2)
        self.assertAlmostEqual(h[0], -6.0, delta=0.3)     # windowed sinc: -6 dB at the cut
        self.assertAlmostEqual(h[2], -6.0, delta=0.3)
        self.assertLess(h[3], -100)                       # opposite sideband rejected
        self.assertLess(h[4], -100)                       # next channel's passband

    def test_taps_match_firmware_length_and_symmetry(self):
        t = phoenix_rx_taps(300, 2700)
        self.assertEqual(len(t), 257)
        # complex bandpass: real part symmetric, imaginary antisymmetric
        np.testing.assert_allclose(t.real, t.real[::-1], atol=1e-12)
        np.testing.assert_allclose(t.imag, -t.imag[::-1], atol=1e-12)


class ChannelTests(unittest.TestCase):
    fr = np.linspace(-10000, 10000, 560)
    rbw = 100.0

    def test_tone_power_recovered(self):
        p = gaussian_trace(self.fr, [(1000.0, -20.0)], self.rbw)
        got = db10(channel_mw(self.fr, 10 ** (p / 10), RxFilter(), 0.0, self.rbw))
        self.assertAlmostEqual(float(got), -20.0, delta=0.15)

    @staticmethod
    def row(tones, n=4, rbw=100.0, scale=None):
        """A speech point in cessb_acp_test.py's format; ``scale(i)`` multiplies sweep i."""
        segs = {}
        spans = {"lower": (-3050, 50, -3000, 0), "wanted": (-50, 3050, 0, 2950),
                 "upper_edge": (2950, 3550, 2950, 3500), "upper": (3450, 6050, 3500, 6000)}
        for k, (a, b, lo, hi) in spans.items():
            fr = np.linspace(a, b, 560)
            tiles = []
            for i in range(n):
                p = gaussian_trace(fr, tones, rbw)
                if scale is not None:
                    p = p + 10 * np.log10(scale(i, fr))
                tiles.append(p)
            segs[k] = {"f": fr, "tiles": tiles, "rbw": str(rbw), "own": [lo, hi],
                       "pass": [0] * (n // 2) + [1] * (n - n // 2)}
        return {"label": "x", "kind": "speech", "pep_dbm": -17.0, "avg_dbm": -20.0,
                "pep_w": 1, "avg_w": 1, "papr_db": 3, "segments": segs}

    def test_channels_from_segments(self):
        tones = [(1000.0, -20.0), (4500.0, -60.0), (3300.0, -66.0), (-1500.0, -50.0)]
        r = analyse_point(self.row(tones), RxFilter())
        # 3300 Hz sits at the upper channel's 300 Hz edge: -6 dB, i.e. -72 dBm
        want = 10 * np.log10(10 ** (-60 / 10) + 10 ** (-72 / 10)) + 20.0
        self.assertAlmostEqual(r["upper_acpr_db"], want, delta=0.25)
        self.assertAlmostEqual(r["lower_acpr_db"], -30.0, delta=0.2)
        self.assertNotIn("upper_alt_acpr_db", r)          # not covered by the segments
        self.assertEqual(len(r["upper_acpr_halves"]), 2)
        tot = 10 * np.log10(1 + 10 ** -3 + 10 ** -4.6)    # -20, -50 and -66 dBm in -3050..3500
        self.assertAlmostEqual(r["total_vs_npower_avg_db"], tot, delta=0.1)

    def test_linear_not_log_average(self):
        # the 4.5 kHz tone is present in one sweep of four: linear mean is 6 dB below it
        def scale(i, fr):
            return np.where(np.abs(fr - 4500) < 400, 1.0 if i == 0 else 1e-6, 1.0)
        r = analyse_point(self.row([(1000.0, -20.0), (4500.0, -40.0)], scale=scale), RxFilter())
        self.assertAlmostEqual(r["upper_acpr_db"], -20.0 - 6.02, delta=0.2)
        h = r["upper_acpr_halves"]
        self.assertAlmostEqual(h[0], -20.0 - 3.01, delta=0.2)   # first half holds the burst
        self.assertLess(h[1], -60)


if __name__ == "__main__":
    unittest.main()
