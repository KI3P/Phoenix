#!/usr/bin/env python3
"""Self-tests for the CESSB RF test's spectrum measurements. No hardware.

    ../venv/bin/python test_cessb_rf.py

Synthetic analyzer traces with known answers: a Gaussian RBW response per
component on a noise floor, as the CMU200 draws them.
"""

from __future__ import annotations

import math
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from cessb_hil import rf as R  # noqa: E402

FC = 14.175e6
SPAN = 7000.0
RBW = 100.0


def trace(components, floor_dbm=-105.0, n=560):
    """dBm per point for {offset_hz: dBm}, with a Gaussian RBW."""
    f = np.linspace(FC - SPAN, FC + SPAN, n)
    sigma = RBW / 2.355
    p = np.full(n, R.mw(floor_dbm))
    for off, lvl in components.items():
        p = p + R.mw(lvl) * np.exp(-0.5 * ((f - FC - off) / sigma) ** 2)
    return f, 10 * np.log10(p)


class SpectrumTests(unittest.TestCase):
    def test_sign_puts_the_wanted_side_positive(self):
        f, p = trace({-1000.0: -30.0})
        fr = R.rel(f, FC, -1)                    # LSB: -1 kHz is wanted
        self.assertAlmostEqual(R.tone_dbm(fr, p, 1000.0), -30.0, delta=0.3)

    def test_two_tone_products(self):
        comps = {700.0: -29.0, 1900.0: -29.0, -500.0: -79.0, 3100.0: -59.0,
                 -1700.0: -89.0, 4300.0: -69.0}
        f, p = trace(comps)
        r = R.two_tone_rf(R.rel(f, FC, 1), p)
        self.assertAlmostEqual(r["imd3_low_dbc"], -50.0, delta=0.4)
        self.assertAlmostEqual(r["imd3_high_dbc"], -30.0, delta=0.4)
        self.assertAlmostEqual(r["imd5_high_dbc"], -40.0, delta=0.4)
        self.assertAlmostEqual(r["imd3_high_dbpep"], -36.0, delta=0.4)
        self.assertAlmostEqual(r["pep_dbm_from_tones"], -22.98, delta=0.4)

    def test_band_ratios(self):
        # Wanted: 10 equal tones in 200-3000 Hz; opposite: one tone 40 dB below their sum
        wanted = {200.0 + 280.0 * k: -40.0 for k in range(10)}
        comps = dict(wanted)
        comps[-1500.0] = -40.0 + 10 * math.log10(10) - 40.0
        comps[5000.0] = -40.0 + 10.0 - 50.0
        f, p = trace(comps, floor_dbm=-140.0)
        s = R.spectrum_stats(R.rel(f, FC, 1), p)
        self.assertAlmostEqual(s.opposite_dbc, -40.0, delta=0.3)
        self.assertAlmostEqual(s.out_of_channel_dbc, -50.0, delta=0.3)

    def test_integrated_power_of_a_tone(self):
        # A CW component read through the RBW integrates back to its own power
        f, p = trace({5000.0: -60.0}, floor_dbm=-160.0)
        got = R.db10(R.integrated_mw(R.rel(f, FC, 1), p, 4000.0, 6000.0, RBW))
        self.assertAlmostEqual(got, -60.0, delta=0.1)

    def test_npower_papr(self):
        self.assertAlmostEqual(R.NPower(-26.0, -22.99, -60.0, FC).papr_db, 3.01, places=2)


if __name__ == "__main__":
    unittest.main(verbosity=1)
