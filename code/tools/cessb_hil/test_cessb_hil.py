#!/usr/bin/env python3
"""Self-tests for the CESSB bench test's stimuli and measurements. No hardware.

    ../venv/bin/python test_cessb_hil.py

Synthetic captures with known answers, so a measurement bug that would bias every
bench result the same way - and so pass unnoticed - fails here instead.
"""

from __future__ import annotations

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from cessb_hil import analysis as A  # noqa: E402
from cessb_hil import stimuli as S  # noqa: E402

FS = 100_000.0


def tones(freqs, amps, seconds=0.5):
    t = np.arange(int(FS * seconds)) / FS
    return sum(a * np.exp(2j * np.pi * f * t) for f, a in zip(freqs, amps))


class TestStimuli(unittest.TestCase):
    def test_every_stimulus_is_one_seamless_period_at_full_scale(self):
        for name, st in S.all_stimuli(4096).items():
            self.assertEqual(len(st.samples), 4096, name)
            self.assertAlmostEqual(np.max(np.abs(st.samples)), 1.0, places=12, msg=name)
            self.assertAlmostEqual(np.mean(st.samples), 0.0, places=6, msg=name)
            # Seamless: the jump across the loop point is no bigger than a typical step
            steps = np.abs(np.diff(np.r_[st.samples, st.samples[0]]))
            self.assertLess(steps[-1], 6 * np.median(steps) + 1e-3, name)

    def test_two_tone_holds_its_tones(self):
        st = S.two_tone(4096)
        x = st.at_rate(48000.0, 1.0)
        spec = np.abs(np.fft.rfft(x)) / len(x) * 2
        f = np.fft.rfftfreq(len(x), 1 / 48000.0)
        for tone in (700.0, 1900.0):
            self.assertAlmostEqual(spec[np.argmin(abs(f - tone))], 0.5, delta=0.02)

    def test_at_rate_reproduces_the_buffer(self):
        st = S.vowel(4096, "ah")
        fs = st.period_hz * len(st.samples)
        y = st.at_rate(fs, 1.0 / st.period_hz)
        np.testing.assert_allclose(y, st.samples, atol=1e-6)

    def test_syllable_is_mostly_vowel_then_silence(self):
        st = S.syllable(4096)
        n = len(st.samples)
        # The gap holds only the tiny offset left by removing the mean (< -80 dB)
        self.assertLess(np.max(np.abs(st.samples[int(0.62 * n):])), 1e-4)
        self.assertGreater(np.max(np.abs(st.samples[: int(0.6 * n)])), 0.5)


class TestMeasurements(unittest.TestCase):
    def test_two_tone_products_on_the_right_side(self):
        for sign in (1, -1):
            z = tones([sign * 700, sign * 1900, -sign * 500, sign * 3100],
                      [0.1, 0.1, 0.001, 0.01])
            p = A.two_tone_products(z, FS, sign)
            self.assertAlmostEqual(p["tone_v"], 0.1, delta=0.002)
            self.assertAlmostEqual(p["imd3_low_dbc"], -40.0, delta=0.3)
            self.assertAlmostEqual(p["imd3_high_dbc"], -20.0, delta=0.3)
            self.assertEqual(np.sign(p["imd3_low_hz"]), -sign)

    def test_envelope_stats_of_a_two_tone(self):
        z = tones([-700, -1900], [0.1, 0.1])
        st = A.envelope_stats(z, FS, -1)
        self.assertAlmostEqual(st.peak, 0.2, delta=0.002)
        self.assertAlmostEqual(st.papr_db, 3.01, delta=0.1)
        self.assertAlmostEqual(st.wanted_fraction, 1.0, delta=0.01)
        self.assertLess(st.opposite_dbc, -60.0)

    def test_opposite_sideband_and_out_of_channel(self):
        z = tones([1000, -1000, 4500], [0.1, 0.01, 0.001])
        st = A.envelope_stats(z, FS, 1)
        self.assertAlmostEqual(st.opposite_dbc, -20.0, delta=0.3)
        self.assertAlmostEqual(st.out_of_channel_dbc, -40.0, delta=0.5)

    def test_band_limit_removes_dac_images_without_touching_the_channel(self):
        z = tones([1500, 8500], [0.1, 0.1])     # 8.5 kHz: where the scope aliases DAC images
        env = A.envelope(z, FS)
        self.assertAlmostEqual(np.median(env), 0.1, delta=0.002)
        self.assertLess(np.max(env) - np.min(env), 0.005)

    def test_find_ceiling_on_an_ideal_limiter(self):
        amps = 10 ** (np.arange(-30, 1, 1.5) / 20)          # input, volts
        off = 2.0 * amps
        on = np.minimum(off, 0.5)
        r = A.find_ceiling(list(on), list(off))
        self.assertTrue(r["found"])
        self.assertAlmostEqual(r["ceiling_v"], 0.5, places=9)
        self.assertAlmostEqual(r["limited_spread_db"], 0.0, places=9)

    def test_find_ceiling_reports_a_radio_that_never_limits(self):
        amps = 10 ** (np.arange(-30, 0, 1.5) / 20)
        r = A.find_ceiling(list(amps), list(amps))
        self.assertFalse(r["found"])


class TestPrediction(unittest.TestCase):
    def test_model_prediction_for_the_voice_stand_in(self):
        from cessb_hil.predict import predict
        out = predict(S.syllable(4096), 192000, [0, 6])
        self.assertAlmostEqual(out["0"]["talk_power_gain_db"], 0.0, delta=0.5)
        self.assertGreater(out["6"]["talk_power_gain_db"], 3.0)
        self.assertLess(out["6"]["peak"], 1.03)
        self.assertLess(out["6"]["opposite_dbc"], -55.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
