#!/usr/bin/env python3
"""Self-tests for the transmit HIL measurement maths. No hardware needed.

    python3 test_tx_filter_hil.py

These exist because the suite's job is to fail when the radio is wrong, and a
measurement bug that biases everything the same way would let it pass silently
instead. The load-bearing case is :class:`TestComparisonCatchesTheBug`, which
feeds the comparison a simulated -8.125 % shift and requires a FAIL: if that ever
passes, the suite has stopped being able to detect the thing it was written for.

Synthetic captures are built from known signals, so every measured quantity has
an answer that can be asserted rather than eyeballed.
"""

from __future__ import annotations

import math
import time
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from filter_hil.radio import Radio  # noqa: E402
from tx_filter_hil import bandtable as bt  # noqa: E402
from tx_filter_hil.ad2 import IqCapture  # noqa: E402
from tx_filter_hil.measure import (complex_spectrum, corner_from_sweep,  # noqa: E402
                                   correlate_complex, dominant_line,
                                   measure_iq_tone, passband_reference_db,
                                   spur_level_dbc, sweep_ripple_db, to_db,
                                   worst_in_band)
from tx_filter_hil.tests import PASSBAND_HI_HZ, PASSBAND_LO_HZ, compare_rates  # noqa: E402

FS = 100_000.0
N = 25_000          # 0.25 s, matching the default capture length


def make_capture(f_hz: float, amplitude_v: float = 0.5, *,
                 image_db: float = -60.0, dc_v: float = 1.6,
                 noise_v: float = 0.0, harmonic_db: float = -90.0,
                 swap_wires: bool = False, extra=None) -> IqCapture:
    """Synthesise a capture of a single-sideband exciter output.

    ``f_hz`` is signed: the side of DC the wanted energy sits on. ``image_db``
    puts a conjugate line at the mirror frequency, which is what finite sideband
    suppression looks like. ``extra`` is a list of ``(freq_hz, amplitude_v)``
    pairs for spurious products.
    """
    t = np.arange(N) / FS
    z = amplitude_v * np.exp(2j * np.pi * f_hz * t)
    z += amplitude_v * 10.0 ** (image_db / 20.0) * np.exp(-2j * np.pi * f_hz * t)
    z += amplitude_v * 10.0 ** (harmonic_db / 20.0) * np.exp(2j * np.pi * 2 * f_hz * t)
    for f_extra, a_extra in (extra or []):
        z += a_extra * np.exp(2j * np.pi * f_extra * t)

    i = np.real(z) + dc_v
    q = np.imag(z) + dc_v
    if noise_v:
        rng = np.random.default_rng(12345)
        i = i + rng.normal(0.0, noise_v, N)
        q = q + rng.normal(0.0, noise_v, N)

    ch1, ch2 = (q, i) if swap_wires else (i, q)
    return IqCapture(ch1=ch1, ch2=ch2, sample_rate_hz=FS, lost=0, corrupted=0,
                     t_utc="1970-01-01T00:00:00+00:00")


class TestBandtable(unittest.TestCase):
    """The mirrored firmware constants have to be self-consistent."""

    def test_legacy_shift_is_the_rate_ratio(self):
        self.assertAlmostEqual(bt.LEGACY_DELTA_PCT, -8.125, places=3)

    def test_audio_and_hilbert_rates(self):
        # DSP.cpp: dec4 then dec2 reaches the audio rate; dec2again halves again.
        self.assertAlmostEqual(bt.audio_rate_hz(192000), 24000.0)
        self.assertAlmostEqual(bt.hilbert_rate_hz(192000), 12000.0)
        self.assertAlmostEqual(bt.audio_rate_hz(176400), 22050.0)
        self.assertAlmostEqual(bt.hilbert_rate_hz(176400), 11025.0)

    def test_fold_point_scales_with_the_rate(self):
        self.assertAlmostEqual(bt.nyquist_after_fold_hz(192000), 6000.0)
        self.assertAlmostEqual(bt.nyquist_after_fold_hz(176400), 5512.5)

    def test_alias_maps_above_the_fold_point_and_leaves_below_alone(self):
        # At 192 ksps the audio rate is 24 k and dec2again folds about 12 k, so
        # 8 kHz reappears at 4 kHz and 3 kHz is untouched.
        self.assertAlmostEqual(bt.alias_of_hz(8000.0, 192000), 4000.0)
        self.assertAlmostEqual(bt.alias_of_hz(3000.0, 192000), 3000.0)
        self.assertAlmostEqual(bt.alias_of_hz(8000.0, 176400), 3025.0)

    def test_equaliser_table_matches_the_firmware_shape(self):
        self.assertEqual(len(bt.EQ_CENTRE_HZ), bt.EQ_CELL_COUNT)
        self.assertEqual(bt.EQ_CENTRE_HZ[0], 198.425)
        self.assertEqual(bt.EQ_CENTRE_HZ[-1], 4000.0)

    def test_nominal_corner_is_inside_the_offline_assertion(self):
        # TransmitChain176k.AudioBandwidthHoldsAcrossRates asserts 2760 +/- 150.
        self.assertLess(abs(bt.TX_AUDIO_CORNER_3DB_HZ - 2760.0), 150.0)

    def test_hilbert_band_scales_with_the_rate(self):
        lo_192, hi_192 = bt.hilbert_band_hz(192000)
        lo_176, hi_176 = bt.hilbert_band_hz(176400)
        self.assertLess(lo_192, hi_192)
        # The band is a fraction of Fs, so it shrinks with the rate by design.
        self.assertAlmostEqual(hi_176 / hi_192, bt.LEGACY_RATE_RATIO, places=4)


class FakeSerial:
    """Just enough pyserial to let Radio.send run without a radio."""

    def __init__(self, reply: bytes = b""):
        self.written = []
        self._reply = bytearray(reply)
        self.is_open = True

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.written.append(data)

    def flush(self):
        pass

    @property
    def in_waiting(self):
        return len(self._reply)

    def read(self, n=1):
        out, self._reply = bytes(self._reply[:n]), self._reply[n:]
        return out

    def close(self):
        self.is_open = False


class TestCatCommandForms(unittest.TestCase):
    """The transmit controls have to match CAT.cpp's declared command lengths.

    ``command_parser`` matches on exact length, so a command one character out is
    silently rejected with ``?;`` rather than misbehaving visibly.
    """

    def _radio(self, reply: bytes = b"") -> Radio:
        r = Radio(timeout_s=0.02, log=lambda *_: None)
        r.cat = FakeSerial(reply)
        return r

    def test_ptt_sends_the_bare_command_and_does_not_wait(self):
        # TX; and RX; are three characters and are *writes*: CAT.cpp suppresses
        # their empty responses, so waiting for a reply costs the whole read
        # timeout on every key and unkey. That inference is length-based
        # everywhere else, which gets these two backwards without a special case.
        r = self._radio()
        started = time.monotonic()
        r.ptt_press(settle_s=0.0)
        r.ptt_release(settle_s=0.0)
        self.assertEqual(r.cat.written, [b"TX;", b"RX;"])
        self.assertLess(time.monotonic() - started, 0.01,
                        "PTT must not block on a reply that never comes")

    def test_read_forms_still_wait_for_their_reply(self):
        r = self._radio(b"MG071;")
        self.assertEqual(r.get_mic_gain_pct(), 71)
        self.assertEqual(r.cat.written, [b"MG;"])

    def test_mic_gain_command_length_matches_cat(self):
        r = self._radio()
        r.set_mic_gain_pct(71)
        self.assertEqual(r.cat.written, [b"MG071;"])   # MG set_len is 3+3
        self.assertEqual(len(r.cat.written[0]), 6)

    def test_mic_gain_is_clamped_to_the_command_range(self):
        r = self._radio()
        r.set_mic_gain_pct(150)
        r.set_mic_gain_pct(-20)
        self.assertEqual(r.cat.written, [b"MG100;", b"MG000;"])

    def test_mic_gain_db_mapping_round_trips_as_the_firmware_does(self):
        # MG_write truncates to an integer number of dB, so the round trip is
        # lossy and the restore has to expect the truncated value back.
        self.assertEqual(Radio.mic_gain_pct_from_db(10), 71)
        self.assertEqual(Radio.mic_gain_db_from_pct(71), 9)
        for db in (-40, -20, 0, 10, 30):
            back = Radio.mic_gain_db_from_pct(Radio.mic_gain_pct_from_db(db))
            self.assertLessEqual(abs(back - db), 1)

    def test_power_command_length_matches_cat(self):
        r = self._radio(b"PC005;")
        r.set_power_w(5)
        self.assertEqual(r.cat.written, [b"PC005;"])   # PC set_len is 3+3


class TestComplexSpectrum(unittest.TestCase):
    """A complex exponential must read its own amplitude, on its own side."""

    def test_amplitude_is_not_halved(self):
        # The real-signal convention doubles the FFT to recombine two conjugate
        # lines. A complex exponential has one line and must not be doubled.
        t = np.arange(N) / FS
        z = 0.4 * np.exp(2j * np.pi * 1000.0 * t)
        freqs, mag = complex_spectrum(z, FS)
        idx = int(np.argmax(mag))
        self.assertAlmostEqual(freqs[idx], 1000.0, delta=FS / N)
        self.assertAlmostEqual(mag[idx], 0.4, delta=0.01)

    def test_negative_frequencies_are_addressable(self):
        t = np.arange(N) / FS
        z = 0.4 * np.exp(-2j * np.pi * 1500.0 * t)
        freqs, mag = complex_spectrum(z, FS)
        idx = int(np.argmax(mag))
        self.assertLess(freqs[idx], 0.0)
        self.assertAlmostEqual(freqs[idx], -1500.0, delta=FS / N)

    def test_frequencies_are_sorted(self):
        freqs, _ = complex_spectrum(np.ones(1024, dtype=complex), FS)
        self.assertTrue(np.all(np.diff(freqs) > 0))

    def test_correlation_beats_bin_picking_off_bin(self):
        # Deliberately between bins, where a Hann-windowed bin reads low.
        f = 1000.0 + 0.5 * FS / N
        t = np.arange(N) / FS
        z = 0.4 * np.exp(2j * np.pi * f * t)
        _, mag = complex_spectrum(z, FS)
        self.assertLess(np.max(mag), 0.4)                       # scalloping loss
        self.assertAlmostEqual(correlate_complex(z, FS, f), 0.4, delta=0.002)


class TestMeasureIqTone(unittest.TestCase):
    """One synthetic capture, every reported quantity checked."""

    def test_reads_level_and_suppression(self):
        cap = make_capture(-1000.0, amplitude_v=0.5, image_db=-42.0)
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        self.assertTrue(m.valid, m.reason)
        self.assertAlmostEqual(m.wanted_v, 0.5, delta=0.01)
        self.assertAlmostEqual(m.level_dbv, to_db(0.5), delta=0.2)
        self.assertAlmostEqual(m.suppression_db, 42.0, delta=1.0)
        self.assertAlmostEqual(m.f_measured_hz, -1000.0, delta=5.0)

    def test_swapping_the_probes_mirrors_the_spectrum(self):
        # The same signal wired the other way round must give the same answer
        # once the swap is declared. This is the whole basis for not caring which
        # probe went on which output.
        wired = make_capture(-1000.0, amplitude_v=0.5, image_db=-42.0)
        swapped = make_capture(-1000.0, amplitude_v=0.5, image_db=-42.0,
                               swap_wires=True)
        a = measure_iq_tone(wired, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        b = measure_iq_tone(swapped, 1000.0, swap=True, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        self.assertAlmostEqual(a.level_dbv, b.level_dbv, delta=0.05)
        self.assertAlmostEqual(a.suppression_db, b.suppression_db, delta=0.5)

    def test_undeclared_swap_reads_the_image_as_the_tone(self):
        # And it must matter: reading with the wrong swap picks up the -42 dB
        # image instead of the tone. This is what the wiring detection prevents.
        swapped = make_capture(-1000.0, amplitude_v=0.5, image_db=-42.0,
                               swap_wires=True)
        wrong = measure_iq_tone(swapped, 1000.0, swap=False, sideband_sign=-1,
                                scope_range_v=5.0, scope_offset_v=1.6)
        self.assertLess(wrong.suppression_db, -30.0)

    def test_a_real_signal_reads_no_suppression(self):
        # Both probes on the same output, or one probe missing: the captured pair
        # is real, its spectrum is symmetric, and suppression collapses to 0 dB.
        # This is the rig fault detect_wiring's suppression check exists to catch.
        t = np.arange(N) / FS
        real = 0.5 * np.cos(2 * np.pi * 1000.0 * t) + 1.6
        cap = IqCapture(ch1=real, ch2=real.copy(), sample_rate_hz=FS, lost=0,
                        corrupted=0, t_utc="1970-01-01T00:00:00+00:00")
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        self.assertLess(abs(m.suppression_db), 1.0)

    def test_carrier_residue_is_measured_against_the_scope_offset(self):
        # 100 mV of DC above the 1.6 V centring, on both channels, against a
        # 0.5 V tone: hypot(0.1, 0.1) / 0.5.
        cap = make_capture(-1000.0, amplitude_v=0.5, dc_v=1.7)
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        expected = to_db(math.hypot(0.1, 0.1)) - to_db(0.5)
        self.assertAlmostEqual(m.carrier_dbc, expected, delta=0.3)

    def test_dc_does_not_leak_into_the_tone_measurement(self):
        # A big DC bias must not change the measured level at all: analytic()
        # removes each channel's mean before anything else happens.
        quiet = make_capture(-1000.0, amplitude_v=0.5, dc_v=0.0)
        biased = make_capture(-1000.0, amplitude_v=0.5, dc_v=2.0)
        a = measure_iq_tone(quiet, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=0.0)
        b = measure_iq_tone(biased, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=2.0)
        self.assertAlmostEqual(a.level_dbv, b.level_dbv, delta=0.01)

    def test_clipping_is_judged_against_the_half_range(self):
        # The AD2's range is peak to peak about the offset, so a 2.4 V amplitude
        # on a 5 V range (+/-2.5 V) is 96% of the window and must be rejected -
        # even though 2.4 is well under 5.
        cap = make_capture(-1000.0, amplitude_v=2.4, dc_v=1.6)
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        self.assertTrue(m.clipped)
        self.assertFalse(m.valid)
        self.assertIn("half-range", m.reason)

    def test_comfortable_amplitude_is_not_called_clipped(self):
        cap = make_capture(-1000.0, amplitude_v=0.5, dc_v=1.6)
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        self.assertFalse(m.clipped)

    def test_dropped_samples_invalidate_a_point(self):
        cap = make_capture(-1000.0)
        cap.lost = 40
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        self.assertFalse(m.valid)
        self.assertIn("lost", m.reason)

    def test_buried_tone_invalidates_a_point(self):
        cap = make_capture(-1000.0, amplitude_v=0.002, noise_v=0.05)
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6,
                            min_snr_db=25.0)
        self.assertFalse(m.valid)
        self.assertIn("SNR", m.reason)

    def test_harmonics_are_reported_as_thd(self):
        cap = make_capture(-1000.0, amplitude_v=0.5, harmonic_db=-35.0)
        m = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                            scope_range_v=5.0, scope_offset_v=1.6)
        self.assertAlmostEqual(m.thd_db, -35.0, delta=2.0)
        # Reported but not fatal by default; fatal when auto-levelling.
        self.assertTrue(m.valid)
        strict = measure_iq_tone(cap, 1000.0, swap=False, sideband_sign=-1,
                                 scope_range_v=5.0, scope_offset_v=1.6,
                                 max_thd_db=-40.0, thd_invalidates=True)
        self.assertFalse(strict.valid)
        self.assertIn("THD", strict.reason)


class TestDominantLine(unittest.TestCase):
    """The wiring detection depends entirely on getting this sign right."""

    def test_finds_the_side_the_energy_is_on(self):
        f, amp = dominant_line(make_capture(+1000.0, amplitude_v=0.5),
                               swap=False, f_min_hz=500.0, f_max_hz=1500.0)
        self.assertGreater(f, 0.0)
        self.assertAlmostEqual(amp, 0.5, delta=0.02)

        f, _ = dominant_line(make_capture(-1000.0, amplitude_v=0.5),
                             swap=False, f_min_hz=500.0, f_max_hz=1500.0)
        self.assertLess(f, 0.0)

    def test_swap_flips_the_reported_side(self):
        cap = make_capture(+1000.0, amplitude_v=0.5)
        a, _ = dominant_line(cap, swap=False, f_min_hz=500.0, f_max_hz=1500.0)
        b, _ = dominant_line(cap, swap=True, f_min_hz=500.0, f_max_hz=1500.0)
        self.assertGreater(a, 0.0)
        self.assertLess(b, 0.0)

    def test_carrier_residue_does_not_win(self):
        # A large DC term must not be mistaken for the tone; f_min_hz excludes it.
        cap = make_capture(-1000.0, amplitude_v=0.05, dc_v=2.0)
        f, _ = dominant_line(cap, swap=False, f_min_hz=500.0, f_max_hz=1500.0)
        self.assertAlmostEqual(f, -1000.0, delta=10.0)


class TestSpurLevel(unittest.TestCase):
    """Fold-back products can emerge on either side of DC."""

    def test_finds_a_spur_on_the_unwanted_side(self):
        # A 4 kHz product on the *positive* side while the tone is negative: the
        # decimator folds before the Hilbert transform picks a side, so this is a
        # realistic case and looking only at the wanted side would miss it.
        cap = make_capture(-8000.0, amplitude_v=0.01,
                           extra=[(+4000.0, 0.5 * 10 ** (-45.0 / 20.0))])
        dbc = spur_level_dbc(cap, swap=False, f_spur_hz=4000.0, reference_v=0.5)
        self.assertAlmostEqual(dbc, -45.0, delta=1.5)

    def test_finds_a_spur_on_the_wanted_side_too(self):
        cap = make_capture(-8000.0, amplitude_v=0.01,
                           extra=[(-4000.0, 0.5 * 10 ** (-45.0 / 20.0))])
        dbc = spur_level_dbc(cap, swap=False, f_spur_hz=4000.0, reference_v=0.5)
        self.assertAlmostEqual(dbc, -45.0, delta=1.5)

    def test_reports_far_down_when_there_is_no_spur(self):
        cap = make_capture(-8000.0, amplitude_v=0.01)
        dbc = spur_level_dbc(cap, swap=False, f_spur_hz=4000.0, reference_v=0.5)
        self.assertLess(dbc, -60.0)


class TestCurveFeatures(unittest.TestCase):
    """Corner extraction on a synthetic response with a known answer."""

    @staticmethod
    def _response(corner_hz: float, freqs):
        """First-order-ish lowpass with a 200 Hz first-order highpass below."""
        f = np.asarray(freqs, dtype=float)
        lp = -10.0 * np.log10(1.0 + (f / corner_hz) ** 8)     # steep, like a FIR
        hp = -10.0 * np.log10(1.0 + (200.0 / f) ** 4)
        return lp + hp

    def test_finds_the_high_corner(self):
        f = np.geomspace(100.0, 4400.0, 41)
        y = self._response(2760.0, f)
        ref = passband_reference_db(f, y, PASSBAND_LO_HZ, PASSBAND_HI_HZ)
        corner = corner_from_sweep(f, y, ref - 3.0, side="high", from_hz=1000.0)
        self.assertAlmostEqual(corner, 2760.0, delta=90.0)

    def test_finds_the_low_corner(self):
        f = np.geomspace(100.0, 4400.0, 61)
        y = self._response(2760.0, f)
        ref = passband_reference_db(f, y, PASSBAND_LO_HZ, PASSBAND_HI_HZ)
        corner = corner_from_sweep(f, y, ref - 3.0, side="low", from_hz=1000.0)
        self.assertAlmostEqual(corner, 200.0, delta=25.0)

    def test_walks_outwards_past_a_stopband_ripple(self):
        # The equaliser's reconstruction is not monotonic far out, so a response
        # that comes back up above the target must not be reported as the corner.
        f = np.array([500.0, 1000.0, 2000.0, 2800.0, 3200.0, 3600.0, 4000.0])
        y = np.array([0.0, 0.0, -1.0, -3.0, -20.0, -2.5, -30.0])
        corner = corner_from_sweep(f, y, -3.0, side="high", from_hz=1000.0)
        self.assertAlmostEqual(corner, 2800.0, delta=1.0)

    def test_returns_nan_when_the_sweep_never_crosses(self):
        f = np.geomspace(100.0, 1000.0, 20)
        y = np.zeros_like(f)
        self.assertTrue(math.isnan(
            corner_from_sweep(f, y, -3.0, side="high", from_hz=500.0)))

    def test_reference_ignores_one_wild_point(self):
        # A median, so a single bad reading cannot move every derived corner.
        f = np.array([500.0, 800.0, 1000.0, 1400.0, 1800.0])
        y = np.array([-20.0, -20.0, -20.0, +5.0, -20.0])
        self.assertAlmostEqual(
            passband_reference_db(f, y, PASSBAND_LO_HZ, PASSBAND_HI_HZ),
            -20.0, delta=0.01)

    def test_ripple_and_worst_in_band(self):
        f = np.array([500.0, 1000.0, 1500.0, 1800.0])
        y = np.array([-1.0, 0.0, -0.5, -1.5])
        self.assertAlmostEqual(sweep_ripple_db(f, y, 500.0, 1800.0), 1.5, delta=0.01)

        supp = np.array([40.0, 35.0, 38.0, 42.0])
        f_worst, worst = worst_in_band(f, supp, 500.0, 1800.0)
        self.assertAlmostEqual(worst, 35.0)
        self.assertAlmostEqual(f_worst, 1000.0)
        f_best, best = worst_in_band(f, supp, 500.0, 1800.0, best=True)
        self.assertAlmostEqual(best, 42.0)
        self.assertAlmostEqual(f_best, 1800.0)


def _per_rate(corner_192: float, corner_176: float,
              lo_192: float = 200.0, lo_176: float = 200.0) -> dict:
    """Minimal per-rate structure carrying just the compared corners."""
    return {
        "192000": {"rate_hz": 192000,
                   "passband": {"corner_hi_3db_hz": corner_192,
                                "corner_hi_6db_hz": corner_192 * 1.1,
                                "corner_lo_3db_hz": lo_192}},
        "176400": {"rate_hz": 176400,
                   "passband": {"corner_hi_3db_hz": corner_176,
                                "corner_hi_6db_hz": corner_176 * 1.1,
                                "corner_lo_3db_hz": lo_176}},
    }


class TestComparisonCatchesTheBug(unittest.TestCase):
    """The load-bearing tests. If these pass wrongly, the suite is useless."""

    def test_identical_corners_pass(self):
        _, checks = compare_rates(_per_rate(2760.0, 2760.0), 1.5, 8.0)
        invariance = [c for c in checks if c.id.endswith(".rate_invariance")]
        self.assertTrue(invariance)
        self.assertTrue(all(c.passed for c in invariance),
                        [c.message for c in invariance if not c.passed])

    def test_a_frozen_table_shift_fails(self):
        # This is the regression the whole suite exists to catch: every corner
        # scaled by 176400/192000.
        shifted = _per_rate(2760.0, 2760.0 * bt.LEGACY_RATE_RATIO,
                            lo_176=200.0 * bt.LEGACY_RATE_RATIO)
        comparison, checks = compare_rates(shifted, 1.5, 8.0)
        invariance = [c for c in checks if c.id.endswith(".rate_invariance")]
        self.assertTrue(all(not c.passed for c in invariance),
                        "a -8.125% shift must fail every rate-invariance check")
        # And it must be recognised as that specific regression, not just as
        # some generic drift, because that is what makes the report actionable.
        hi = next(r for r in comparison["corners"] if r["id"] == "hi_3db")
        self.assertTrue(hi["legacy_consistent"])
        self.assertEqual(hi["verdict"], "FAIL")
        self.assertAlmostEqual(hi["delta_pct"], bt.LEGACY_DELTA_PCT, delta=0.01)

    def test_a_shift_just_inside_tolerance_passes(self):
        _, checks = compare_rates(_per_rate(2760.0, 2760.0 * 1.01), 1.5, 8.0)
        hi = next(c for c in checks if c.id == "corner.hi_3db.rate_invariance")
        self.assertTrue(hi.passed)

    def test_a_shift_just_outside_tolerance_fails(self):
        _, checks = compare_rates(_per_rate(2760.0, 2760.0 * 1.03), 1.5, 8.0)
        hi = next(c for c in checks if c.id == "corner.hi_3db.rate_invariance")
        self.assertFalse(hi.passed)
        # A 3% drift is not the frozen-table signature and must not be labelled it.
        self.assertNotIn("frozen coefficient tables", hi.message)

    def test_an_unmeasurable_corner_skips_rather_than_passes(self):
        data = _per_rate(2760.0, 2760.0)
        data["176400"]["passband"]["corner_hi_3db_hz"] = None
        comparison, checks = compare_rates(data, 1.5, 8.0)
        hi = next(c for c in checks if c.id == "corner.hi_3db.rate_invariance")
        self.assertTrue(hi.skipped)
        self.assertFalse(hi.passed)
        row = next(r for r in comparison["corners"] if r["id"] == "hi_3db")
        self.assertEqual(row["verdict"], "SKIP")

    def test_a_single_rate_produces_no_comparison(self):
        one = {"192000": _per_rate(2760.0, 2760.0)["192000"]}
        comparison, checks = compare_rates(one, 1.5, 8.0)
        self.assertEqual(comparison["corners"], [])
        self.assertEqual(checks, [])

    def test_absolute_accuracy_is_checked_on_the_high_corner(self):
        # Both rates agree, but at the wrong frequency: rate invariance passes and
        # the nominal check has to be the one that objects.
        _, checks = compare_rates(_per_rate(2200.0, 2200.0), 1.5, 8.0)
        nominal = next(c for c in checks if c.id == "corner.hi_3db.nominal")
        self.assertFalse(nominal.passed)
        invariance = next(c for c in checks
                          if c.id == "corner.hi_3db.rate_invariance")
        self.assertTrue(invariance.passed)

    def test_checks_serialise_without_nan(self):
        # NaN is not valid JSON and numpy bools are not serialisable; the report
        # writer relies on Check.as_dict having already dealt with both.
        _, checks = compare_rates(_per_rate(2760.0, float("nan")), 1.5, 8.0)
        for c in checks:
            d = c.as_dict()
            self.assertIsInstance(d["passed"], bool)
            self.assertTrue(d["value"] is None or math.isfinite(d["value"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
