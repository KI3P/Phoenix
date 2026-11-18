#!/usr/bin/env python3
"""Self-tests for the CESSB model. No hardware needed.

    python3 test_cessb_model.py

The model's conclusions are only worth anything if it is the firmware's chain,
so the load-bearing case is TestModelIsTheFirmwareChain: it requires the model's
-3 dB transmit corner to agree with what the firmware's own unit test measured
(176k_transmit_shift_summary.txt, written by TransmitChain176k.
AudioBandwidthHoldsAcrossRates) to within a few hertz.
"""

from __future__ import annotations

import csv
import os
import re
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cessb  # noqa: E402
import phoenix_tx as tx  # noqa: E402

GTEST_BUILD = os.path.join(tx.REPO, "code", "test", "build")
HAVE_OPENAUDIO = os.path.exists(os.path.join(cessb.DEFAULT_OPENAUDIO, cessb.W7PUA_HEADER))


def audio_path_gain(flt: tx.TxFilters, tone_hz: float) -> float:
    """I-channel gain of TXDecimateBy2Again -> Hilbert -> TXInterpolateBy2Again (/2), as TXAudioPathGain."""
    n = np.arange(4096)
    z = tx.audio24_to_cessb_node(np.cos(2 * np.pi * tone_hz * n / flt.fs24), flt)[1024:]
    k = np.arange(len(z))
    w = 0.5 * (1 - np.cos(2 * np.pi * k / (len(k) - 1)))
    return 2 * abs(np.sum(w * z.real * np.exp(-2j * np.pi * tone_hz * k / flt.fs24))) / w.sum() / 2


def corner_hz(flt: tx.TxFilters, level_db: float = -3.0) -> float:
    """Same search as TXAudioCornerHz in TransmitChain_test.cpp: 300 Hz reference, 20 Hz steps."""
    ref = audio_path_gain(flt, 300.0)
    target = ref * 10 ** (level_db / 20)
    prev_f, prev_g = 300.0, ref
    for f in np.arange(320.0, 6000.0, 20.0):
        g = audio_path_gain(flt, f)
        if g <= target:
            return prev_f + (target - prev_g) * (f - prev_f) / (g - prev_g)
        prev_f, prev_g = f, g
    return -1.0


class TestConstantsMatchFirmware(unittest.TestCase):
    def test_generated_stage_constants(self):
        with open(tx.DSP_FIR_CPP) as fh:
            text = fh.read()
        for name, value in (("TX_DECIMATE3_FC_HZ", tx.TX_DECIMATE3_FC_HZ),
                            ("TX_AUDIO_LPF_FC_HZ", tx.TX_AUDIO_LPF_FC_HZ),
                            ("GENERATED_FIR_ATTENUATION_DB", tx.GENERATED_FIR_ATTENUATION_DB)):
            m = re.search(r"#define\s+" + name + r"\s+([0-9.]+)f?", text)
            self.assertIsNotNone(m, name)
            self.assertAlmostEqual(float(m.group(1)), value, places=3, msg=name)

    def test_fixed_tables_parse(self):
        f = tx.TxFilters.for_rate(192000.0)
        self.assertEqual(len(f.dec1), 48)
        self.assertEqual(len(f.dec2), 48)
        self.assertEqual(len(f.hilb_p45), 100)
        self.assertEqual(len(f.hilb_n45), 100)
        self.assertAlmostEqual(f.dec1.sum(), 1.0, delta=0.01)


class TestFirDesignPort(unittest.TestCase):
    def test_lowpass_minus_6db_at_fc(self):
        from scipy import signal
        h = tx.normalize_dc(tx.calc_fir_lowpass(123, 1500.0, 60.0, 24000.0))
        _, H = signal.freqz(h, worN=[1500.0], fs=24000.0)
        self.assertAlmostEqual(20 * np.log10(abs(H[0])), -6.02, delta=0.3)
        self.assertAlmostEqual(h.sum(), 1.0, places=9)


class TestModelIsTheFirmwareChain(unittest.TestCase):
    def test_corner_matches_firmware_gtest(self):
        c192 = corner_hz(tx.TxFilters.for_rate(192000.0))
        c176 = corner_hz(tx.TxFilters.for_rate(176400.0))
        summary = os.path.join(GTEST_BUILD, "176k_transmit_shift_summary.txt")
        if os.path.exists(summary):
            with open(summary) as fh:
                row = next(r for r in csv.DictReader(fh) if r["stage"] == "TX_audio_bandwidth_minus3dB")
            self.assertAlmostEqual(c192, float(row["f_192k_Hz"]), delta=5.0)
            self.assertAlmostEqual(c176, float(row["f_176k_Hz"]), delta=5.0)
        else:
            # Same bound the firmware test asserts
            self.assertAlmostEqual(c192, 2760.0, delta=150.0)

    def test_hilbert_pair_makes_one_sideband(self):
        f = tx.TxFilters.for_rate(192000.0)
        s = tx.wanted_sign(f)
        n = np.arange(8192)
        z = tx.audio24_to_cessb_node(np.cos(2 * np.pi * 1000.0 * n / f.fs24), f)[2048:]
        k = np.arange(len(z))
        want = abs(np.vdot(np.exp(2j * np.pi * s * 1000.0 * k / f.fs24), z))
        other = abs(np.vdot(np.exp(-2j * np.pi * s * 1000.0 * k / f.fs24), z))
        self.assertGreater(20 * np.log10(want / other), 35.0)


class TestCessbVariants(unittest.TestCase):
    def setUp(self):
        self.f = tx.TxFilters.for_rate(192000.0)
        self.sign = tx.wanted_sign(self.f)
        rng = np.random.default_rng(7)
        from scipy import signal
        b, a = signal.butter(4, [300 / (self.f.fs24 / 2), 2500 / (self.f.fs24 / 2)], "band")
        x = signal.lfilter(b, a, rng.standard_normal(int(2 * self.f.fs24)))
        z = tx.audio24_to_cessb_node(x, self.f)
        self.z = z / np.abs(z[2000:]).max()          # peaks touch the ceiling at 0 dB drive

    def _variants(self):
        out = [("C", lambda z: cessb.cessb_c(z, self.f.fs24, self.sign))]
        if HAVE_OPENAUDIO:
            out.append(("Z", lambda z: cessb.cessb_z(z)))
        return out

    def test_transparent_below_ceiling(self):
        """Below the ceiling the stage is a linear filter: in-band tones pass at unity gain."""
        n = np.arange(int(self.f.fs24))
        for tone in (400.0, 1000.0, 2000.0, 2500.0):
            x = 0.5 * np.cos(2 * np.pi * tone * n / self.f.fs24)
            z = tx.audio24_to_cessb_node(x, self.f)
            k = np.arange(len(z) // 2)
            ref = abs(np.vdot(np.exp(2j * np.pi * self.sign * tone * k / self.f.fs24), z[len(z) // 2:]))
            for name, fn in self._variants():
                y = fn(z)[len(z) // 2:]
                self.assertLess(np.abs(y).max(), 1.0, f"{name} {tone}: test level must not clip")
                got = abs(np.vdot(np.exp(2j * np.pi * self.sign * tone * k / self.f.fs24), y))
                self.assertAlmostEqual(20 * np.log10(got / ref), 0.0, delta=0.5, msg=f"{name} {tone} Hz")

    def test_delay_is_124_samples(self):
        """61 (clipper filter) + 63 (overshoot delay line), as in radioCESSB_Z_transmit_F32."""
        z = 0.3 * self.z
        for name, fn in self._variants():
            y = fn(z)
            c = [abs(np.vdot(z[2000:20000], y[2000 + d:20000 + d])) for d in range(110, 140)]
            self.assertEqual(110 + int(np.argmax(c)), 124, name)

    def test_ceiling_holds_when_overdriven(self):
        z = 2.0 * self.z                              # 6 dB drive
        for name, fn in self._variants():
            peak = np.abs(fn(z)[2000:]).max()
            self.assertLess(peak, 1.05, name)
            self.assertGreater(peak, 0.9, name)

    def test_centring_does_not_change_clipping(self):
        z = 2.0 * self.z
        n = np.arange(len(z))
        lo = np.exp(-2j * np.pi * cessb.CENTRE_HZ * n / self.f.fs24)
        np.testing.assert_allclose(np.abs(cessb.envelope_clip(z * lo)),
                                   np.abs(cessb.envelope_clip(z)), rtol=1e-12, atol=1e-12)

    def test_q15_overload_lets_envelope_reach_root_two(self):
        z = 3.0 * (np.exp(1j * np.pi / 4) * np.ones(10))
        self.assertAlmostEqual(np.abs(cessb.q15_overload(z)).max(), np.sqrt(2), places=9)


if __name__ == "__main__":
    unittest.main(verbosity=2)
