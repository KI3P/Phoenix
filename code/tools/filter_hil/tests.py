"""The individual hardware tests.

Every filter response here is measured as the *difference* between two captures
taken at the same injected frequency, one with the filter under test engaged and
one with it bypassed or flat. Everything the two captures have in common - the
AWG's amplitude flatness, the codec front end, the decimation rolloff, the SSB
mask, the volume setting, the DAC and the speaker amplifier - cancels exactly,
leaving only the filter. Absolute levels are never compared across rates.

The pass criterion that matters is **rate invariance**: a corner or centre
measured at 192 ksps must land at the same frequency when measured at
176.4 ksps. The bug this suite exists to catch moves them by 8.125 %, and the
tolerance is 1.5 %, so there is a factor of five between "correct" and "broken".
Absolute agreement with the labelled frequency is checked too, but more loosely,
because a smooth tilt in the analog path can bias it without saying anything
about the firmware.
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable, Optional, Sequence

import numpy as np

from . import bandtable as bt
from .ad2 import Ad2
from .measure import (ToneMeasurement, bandwidth_3db, find_crossing, geom_grid,
                      measure_tone, dominant_tone, peak_from_three, pct_delta)
from .radio import EdSnapshot, Radio, RadioStateGuard


@dataclass
class Check:
    """One pass/fail assertion, flat enough to drop straight into a report."""

    id: str
    group: str
    description: str
    value: Optional[float]
    limit: Optional[float]
    units: str
    passed: bool
    skipped: bool = False
    message: str = ""

    def as_dict(self) -> dict:
        return {
            "id": self.id, "group": self.group, "description": self.description,
            "value": _clean(self.value), "limit": _clean(self.limit), "units": self.units,
            "passed": self.passed, "skipped": self.skipped, "message": self.message,
        }


@dataclass
class TestResult:
    name: str
    checks: list = field(default_factory=list)
    data: dict = field(default_factory=dict)
    skipped: bool = False
    reason: str = ""


def _clean(x):
    """JSON has no NaN; represent unmeasurable values as null."""
    if x is None:
        return None
    if isinstance(x, (int, bool)):
        return x
    return float(x) if np.isfinite(x) else None


@dataclass
class InjectionPlan:
    """How to turn a wanted audio frequency into an AWG frequency.

    ``ReceiveProcessing`` shifts twice before decimating: by Fs/4
    (``FreqShiftFs4``, DSP.cpp) and then by the fine tune plus any CW sidetone
    (``FreqShiftF``). So the tone that demodulates to zero audio sits at
    ``|Fs/4 + fineTune|``, and everything else is measured either side of it.

    Fs/4 is 48 kHz at 192 ksps but 44.1 kHz at 176.4 ksps; recomputing it per
    rate is the whole point of the exercise. The fine tune is whatever the
    operator last tuned to - routinely several kHz - and leaving it out puts
    every injection outside the passband, so the radio stays silent and no
    amount of extra drive helps.

    ``sideband_sign`` says which side of that centre the passband lies on, and
    ``phase_sign`` says which AWG channel drives I. Both are discovered by
    experiment rather than derived from the radio's reported mode: getting
    either wrong produces silence rather than a subtly wrong answer, so trying
    all four combinations is both cheap and safer than modelling the chain.
    """

    rate_hz: int
    fine_tune_hz: float = 0.0
    sidetone_hz: float = 0.0
    mapping_correction_hz: float = 0.0
    phase_sign: int = 1
    sideband_sign: int = 1
    amplitude_v: float = 0.03

    @property
    def fs_over_4_hz(self) -> float:
        return bt.fs_over_4_hz(self.rate_hz)

    @property
    def centre_hz(self) -> float:
        """Injection frequency that demodulates to DC.

        ReceiveProcessing shifts by Fs/4 (`FreqShiftFs4`) and then again by the
        fine tune (`FreqShiftF`), so the tone that lands at zero audio sits at
        |Fs/4 + fineTune| - **not** at Fs/4. The fine tune is whatever the
        operator last tuned to and is routinely several kHz, so ignoring it puts
        every injection far outside the passband and nothing is heard at all.
        """
        return abs(self.fs_over_4_hz + self.fine_tune_hz)

    def inject_hz(self, f_audio_hz: float) -> float:
        offset = self.sideband_sign * (f_audio_hz + self.sidetone_hz)
        return self.centre_hz + offset + self.mapping_correction_hz


class Injector:
    """Drives one audio frequency and reads its level back."""

    def __init__(self, ad2: Ad2, plan: InjectionPlan, settle_s: float,
                 min_snr_db: float, max_thd_db: float) -> None:
        self.ad2 = ad2
        self.plan = plan
        self.settle_s = settle_s
        self.min_snr_db = min_snr_db
        self.max_thd_db = max_thd_db

    def point(self, f_audio_hz: float) -> ToneMeasurement:
        f_in = self.plan.inject_hz(f_audio_hz)
        self.ad2.set_quadrature(f_in, self.plan.amplitude_v, self.plan.phase_sign)
        cap = self.ad2.capture_after(self.settle_s)
        return measure_tone(cap, f_audio_hz,
                            scope_range_v=self.ad2.cfg.scope_range_v,
                            min_snr_db=self.min_snr_db,
                            max_thd_db=self.max_thd_db)

    def sweep(self, freqs_hz: Iterable[float]) -> list[ToneMeasurement]:
        return [self.point(float(f)) for f in freqs_hz]


def _point_dict(f_audio: float, plan: InjectionPlan, m: ToneMeasurement,
                ref_dbv: Optional[float] = None) -> dict:
    d = {
        "f_audio_hz": round(f_audio, 3),
        "f_inject_hz": round(plan.inject_hz(f_audio), 3),
        "f_measured_hz": _clean(m.f_measured_hz),
        "level_dbv": _clean(m.level_dbv),
        "snr_db": _clean(m.snr_db),
        "thd_db": _clean(m.thd_db),
        "clipped": m.clipped,
        "valid": m.valid,
    }
    if ref_dbv is not None:
        d["ref_dbv"] = _clean(ref_dbv)
        d["resp_db"] = _clean(m.level_dbv - ref_dbv)
    if m.reason:
        d["reason"] = m.reason
    return d


# ===========================================================================
#  T0 preflight
# ===========================================================================

def run_preflight(radio: Radio, ad2: Ad2, allow_agc_on: bool,
                  log: Callable[[str], None]) -> tuple[TestResult, EdSnapshot]:
    """Confirm the rig answers and the radio is in a measurable state."""
    checks: list[Check] = []

    ident = radio.expect("ID;")
    checks.append(Check("rig.cat", "rig", "Radio answers on the CAT port",
                        None, None, "", True, message=f"ID reply {ident}"))

    ed = radio.dump_ed()
    log(f"Radio: rate {ed.sample_rate_hz} sps, band {ed.current_band[ed.active_vfo]}, "
        f"mode {ed.modulation_name}, AGC {'off' if ed.agc_off else 'ON'}, "
        f"volume {ed.audio_volume}")

    # AGC would compress the very amplitude differences the suite measures,
    # flattening every filter skirt into a straight line. There is no CAT
    # command for it, so this can only be checked, not fixed.
    agc_ok = ed.agc_off
    checks.append(Check(
        "rig.agc_off", "rig", "AGC is off so amplitude sweeps are meaningful",
        float(ed.agc), 0.0, "mode", agc_ok or allow_agc_on, skipped=False,
        message=("AGC is off" if agc_ok else
                 f"AGC mode is {ed.agc}; it will flatten every filter skirt. "
                 f"Turn it off in the radio's menu." +
                 (" Continuing because --allow-agc-on was given; RESULTS ARE UNRELIABLE."
                  if allow_agc_on else ""))))

    checks.append(Check(
        "rig.sample_rate_known", "rig", "Radio reports a CAT-selectable sample rate",
        float(ed.sample_rate_hz), None, "hz", ed.sample_rate_hz in bt.SAMPLE_RATES_HZ,
        message=f"{ed.sample_rate_hz} sps"))

    return TestResult("preflight", checks, {"ed": ed.summary(), "id": ident}), ed


# ===========================================================================
#  T1 I/Q wiring order
# ===========================================================================

def detect_iq_order(ad2: Ad2, plan: InjectionPlan, settle_s: float,
                    min_rejection_db: float, forced_sign: Optional[int],
                    amplitude_v: float,
                    log: Callable[[str], None]) -> tuple[TestResult, int, int]:
    """Find the quadrature sense and the sideband, by trying all four.

    Two independent unknowns:

    * **Which AWG channel drives I.** Swapping I and Q conjugates the baseband
      signal, so a tone that lands at +1 kHz with one wiring lands at -1 kHz with
      the other.
    * **Which side of Fs/4 the passband sits on.** An upper-sideband band takes
      audio from ``Fs/4 + f``, a lower-sideband band from ``Fs/4 - f``.

    Either one wrong puts the tone outside the SSB convolution filter, which
    rejects it hard - discriminating against the unwanted sideband is that
    filter's entire job. So the right combination stands out unmistakably, and
    the wrong ones fail loudly rather than quietly reading a bit low.

    Nothing needs rewiring: the suite compensates in software.
    """
    probe_hz = 1000.0
    # Detection runs at the approved ceiling rather than the auto-level floor:
    # picking the right combination is a relative comparison, and at 10 mV the
    # tone is under the receiver's own noise so all four look alike. The level is
    # brought back down by autolevel immediately afterwards.
    plan.amplitude_v = amplitude_v
    trials: dict[str, dict] = {}
    best_key, best_amp, best = None, -1.0, None
    second_amp = -1.0

    phase_candidates = (forced_sign,) if forced_sign is not None else (+1, -1)

    for phase in phase_candidates:
        for side in (+1, -1):
            plan.phase_sign, plan.sideband_sign = phase, side
            f_in = plan.inject_hz(probe_hz)
            ad2.set_quadrature(f_in, plan.amplitude_v, phase)
            cap = ad2.capture_after(settle_s)
            m = measure_tone(cap, probe_hz, scope_range_v=ad2.cfg.scope_range_v,
                             min_snr_db=0.0, max_thd_db=99.0)
            key = f"phase{phase:+d}_side{side:+d}"
            trials[key] = {"phase_sign": phase, "sideband_sign": side,
                           **_point_dict(probe_hz, plan, m)}
            log(f"  W2 {phase*90:+d} deg, {'USB' if side > 0 else 'LSB'} side: "
                f"{m.level_dbv:7.1f} dBV at {m.f_measured_hz:7.1f} Hz, "
                f"SNR {m.snr_db:5.1f} dB  (inject {f_in:.0f} Hz)")
            if m.amplitude_v > best_amp:
                second_amp, best_amp = best_amp, m.amplitude_v
                best_key, best = key, m
            elif m.amplitude_v > second_amp:
                second_amp = m.amplitude_v

    phase_sign = trials[best_key]["phase_sign"]
    sideband_sign = trials[best_key]["sideband_sign"]
    plan.phase_sign, plan.sideband_sign = phase_sign, sideband_sign

    # The number that has to be unambiguous is the *sideband*: which side of the
    # demodulation centre to inject. Compare like with like - same phase sense,
    # opposite side - because that is the choice being made.
    #
    # The phase sense is a much weaker effect and is deliberately not gated on.
    # Imperfect quadrature (an amplitude imbalance between W1 and W2 at the
    # radio's inputs is normal) leaves an image at the mirror frequency, but the
    # SSB convolution filter rejects that image anyway, so both phase senses
    # produce a usable tone and only the amplitude differs. Requiring them to
    # differ would fail a perfectly good rig.
    other_side = trials.get(f"phase{phase_sign:+d}_side{-sideband_sign:+d}")
    other_amp = other_side["level_dbv"] if other_side else None
    rejection = (best.level_dbv - other_amp) if other_amp is not None else 99.0

    phase_margin = 99.0
    other_phase = trials.get(f"phase{-phase_sign:+d}_side{sideband_sign:+d}")
    if other_phase and other_phase["level_dbv"] is not None:
        phase_margin = best.level_dbv - other_phase["level_dbv"]

    wiring = ("W1 -> I (codec left), W2 -> Q (codec right)" if phase_sign > 0
              else "W1 -> Q (codec right), W2 -> I (codec left)")
    sideband = ("above the demodulation centre" if sideband_sign > 0
                else "below the demodulation centre")

    passed = rejection >= min_rejection_db and best.snr_db > 6.0
    if best.snr_db <= 6.0:
        msg = ("no audio from any combination - check that the AD2 ground is "
               "shared with the radio, that W1 and W2 are connected to the I/Q "
               "inputs, and that the radio is unmuted")
    elif rejection < min_rejection_db:
        msg = (f"only {rejection:.1f} dB between injecting above and below the "
               f"demodulation centre; the SSB filter should reject the wrong "
               f"side much harder than that")
    else:
        msg = (f"inject {sideband}; {wiring} (phase margin {phase_margin:+.1f} dB); "
               f"wrong side rejected by {rejection:.1f} dB")

    check = Check("rig.sideband", "rig",
                  "The wrong side of the demodulation centre is clearly rejected",
                  rejection, min_rejection_db, "db", passed, message=msg)

    data = {
        "phase_sign": phase_sign,
        "sideband_sign": sideband_sign,
        "detect_amplitude_v": amplitude_v,
        "wiring": wiring,
        "sideband": sideband,
        "image_rejection_db": _clean(rejection),
        "phase_margin_db": _clean(phase_margin),
        "forced": forced_sign is not None,
        "trials": trials,
    }
    return TestResult("iq_order", [check], data), phase_sign, sideband_sign


# ===========================================================================
#  T2 drive level
# ===========================================================================

def autolevel(ad2: Ad2, plan: InjectionPlan, settle_s: float, start_v: float,
              max_v: float, step_db: float, min_snr_db: float, max_thd_db: float,
              log: Callable[[str], None]) -> tuple[TestResult, float]:
    """Find the smallest drive that gives a clean measurement.

    Steps up only, and never past ``max_v``: the tolerance of these inputs is
    not documented, so the suite starts quiet and stops as soon as the signal is
    good enough rather than reaching for the largest usable level.
    """
    probe_hz = 1000.0
    amplitude = start_v
    trials = []
    chosen = None

    while amplitude <= max_v * 1.0001:
        plan.amplitude_v = amplitude
        ad2.set_quadrature(plan.inject_hz(probe_hz), amplitude, plan.phase_sign)
        cap = ad2.capture_after(settle_s)
        m = measure_tone(cap, probe_hz, scope_range_v=ad2.cfg.scope_range_v,
                         min_snr_db=min_snr_db, max_thd_db=max_thd_db,
                         thd_invalidates=True)
        trials.append({"amplitude_v": round(amplitude, 6), **_point_dict(probe_hz, plan, m)})
        log(f"  {amplitude*1000:6.1f} mV -> {m.level_dbv:6.1f} dBV, "
            f"SNR {m.snr_db:5.1f} dB, THD {m.thd_db:6.1f} dB, peak {m.peak_v:.3f} V")

        if m.snr_db >= min_snr_db and m.thd_db <= max_thd_db and not m.clipped:
            chosen = amplitude
            break
        amplitude *= 10.0 ** (step_db / 20.0)

    if chosen is None:
        chosen = min(amplitude, max_v)
        plan.amplitude_v = chosen
        msg = (f"never reached {min_snr_db:.0f} dB SNR below {max_v*1000:.0f} mV; "
               f"raise --max-amplitude or the radio's volume")
        passed = False
    else:
        plan.amplitude_v = chosen
        msg = f"{chosen*1000:.1f} mV"
        passed = True

    check = Check("rig.drive_level", "rig",
                  "A drive level giving adequate SNR without clipping",
                  chosen * 1000.0, max_v * 1000.0, "mv", passed, message=msg)
    return TestResult("autolevel", [check], {"amplitude_v": chosen, "trials": trials}), chosen


# ===========================================================================
#  T3 Fs/4 mapping
# ===========================================================================

#: How far the measured demodulation centre may sit from its nominal position.
#: Generous on purpose: the Teensy synthesises its I2S clock with a fractional
#: divider, so the true sample rate is a few tens of ppm off nominal and the
#: centre moves with it. The residual is folded into `mapping_correction_hz`
#: rather than treated as an error. What this check exists to catch is the gross
#: case - a chain that did not reconfigure its frequency shift for the new rate,
#: which would land kilohertz away, not tens of hertz.
MAPPING_OFFSET_TOL_HZ = 150.0


def calibrate_mapping(ad2: Ad2, plan: InjectionPlan, settle_s: float,
                      log: Callable[[str], None]) -> TestResult:
    """Confirm - and trim - the input-to-audio frequency relationship.

    Fits ``f_audio = slope * f_inject + offset`` over three probes. The offset
    must come out at -(Fs/4) plus any sidetone shift; if the firmware failed to
    reconfigure the frequency shift for the new rate, this is where it shows,
    and nothing measured afterwards would mean anything.
    """
    probes = (400.0, 800.0, 1400.0)
    f_in, f_out = [], []
    for f_audio in probes:
        fi = plan.inject_hz(f_audio)
        ad2.set_quadrature(fi, plan.amplitude_v, plan.phase_sign)
        cap = ad2.capture_after(settle_s)
        f_meas, _ = dominant_tone(cap, f_min_hz=100.0)
        f_in.append(fi)
        f_out.append(f_meas)
        log(f"  inject {fi:9.1f} Hz -> audio {f_meas:7.1f} Hz (wanted {f_audio:.1f})")

    slope, offset = np.polyfit(np.asarray(f_in), np.asarray(f_out), 1)
    residuals = [abs(o - (slope * i + offset)) for i, o in zip(f_in, f_out)]

    # On a lower-sideband band the audio frequency falls as the injected one
    # rises, so the fitted slope is -1 and the offset has the opposite sign.
    s = plan.sideband_sign
    expected_slope = float(s)
    expected_offset = -s * (plan.centre_hz + plan.mapping_correction_hz) \
                      - plan.sidetone_hz
    offset_err = offset - expected_offset

    checks = [
        Check("map.slope", "mapping",
              "Audio frequency tracks the injected frequency one for one",
              float(slope), expected_slope, "ratio",
              abs(slope - expected_slope) <= 2e-3,
              message=f"slope {slope:+.5f} (expected {expected_slope:+.0f})"),
        Check("map.offset", "mapping",
              f"Demodulation centre is |Fs/4 + fineTune| = {plan.centre_hz:.0f} Hz "
              f"(Fs/4 {plan.fs_over_4_hz:.0f}, fine tune {plan.fine_tune_hz:+.0f})",
              float(offset), float(expected_offset), "hz",
              abs(offset_err) <= MAPPING_OFFSET_TOL_HZ,
              message=(f"measured offset {offset:.1f} Hz, expected "
                       f"{expected_offset:.1f} Hz ({offset_err:+.1f} Hz, "
                       f"{1e6*offset_err/plan.fs_over_4_hz:+.0f} ppm of Fs/4)")),
        Check("map.residual", "mapping", "Per-probe fit residual is small",
              float(max(residuals)), 5.0, "hz", max(residuals) <= 5.0,
              message=f"worst residual {max(residuals):.2f} Hz"),
    ]

    # Fold the error back in so later injections land where they are asked to.
    # inject_hz adds the correction directly, so it carries the sideband sign.
    correction = plan.mapping_correction_hz - s * offset_err
    data = {
        "slope": _clean(slope),
        "offset_hz": _clean(offset),
        "expected_offset_hz": _clean(expected_offset),
        "centre_hz": _clean(plan.centre_hz),
        "fine_tune_hz": _clean(plan.fine_tune_hz),
        "residual_hz": _clean(max(residuals)),
        "correction_hz": _clean(correction),
        "offset_error_hz": _clean(offset_err),
        "offset_error_ppm": _clean(1e6 * offset_err / plan.fs_over_4_hz),
        "sidetone_hz": plan.sidetone_hz,
        "probes": [{"f_audio_hz": a, "f_inject_hz": i, "f_measured_hz": o}
                   for a, i, o in zip(probes, f_in, f_out)],
    }
    if all(c.passed for c in checks):
        plan.mapping_correction_hz = correction

    return TestResult("mapping", checks, data)


# ===========================================================================
#  T4 reference sweep
# ===========================================================================

def reference_sweep(inj: Injector, grid_hz: np.ndarray,
                    log: Callable[[str], None]) -> TestResult:
    """The common-path denominator every filter response divides by."""
    points = []
    levels = {}
    for f in grid_hz:
        m = inj.point(float(f))
        points.append(_point_dict(float(f), inj.plan, m))
        levels[float(f)] = m.level_dbv

    valid = [p for p in points if p["valid"]]
    in_band = [p for p in points if 300.0 <= p["f_audio_hz"] <= 2500.0]
    in_band_valid = [p for p in in_band if p["valid"]]
    frac = len(in_band_valid) / len(in_band) if in_band else 0.0

    db = [p["level_dbv"] for p in in_band if p["level_dbv"] is not None]
    dyn = (max(db) - min(db)) if db else 0.0

    # A passband that is flat to within a decibel across two octaves is not a
    # passband, it is a compressor. Most likely AGC is on after all.
    # Compare two in-band frequencies; a point outside the SSB passband would
    # report the filter skirt as an analog tilt.
    tilt = 0.0
    in_band_levels = {p["f_audio_hz"]: p["level_dbv"] for p in in_band_valid
                      if p["level_dbv"] is not None}
    if len(in_band_levels) >= 2:
        lo = min(in_band_levels, key=lambda k: abs(k - 500.0))
        hi = max(in_band_levels)
        tilt = in_band_levels[hi] - in_band_levels[lo]

    checks = [
        Check("ref.coverage", "reference",
              "Most of the passband gives a usable measurement",
              100.0 * frac, 95.0, "percent", frac >= 0.95,
              message=f"{len(in_band_valid)}/{len(in_band)} points valid in 300-2500 Hz"),
    ]

    log(f"  {len(valid)}/{len(points)} points valid, in-band dynamic range "
        f"{dyn:.1f} dB, 1k->4k tilt {tilt:+.1f} dB")

    return TestResult("reference_sweep", checks, {
        "points": points,
        "analog_tilt_db": _clean(tilt),
        "in_band_dynamic_range_db": _clean(dyn),
    })


def _ref_lookup(reference: TestResult) -> dict:
    return {p["f_audio_hz"]: p["level_dbv"] for p in reference.data["points"]
            if p["level_dbv"] is not None}


# ===========================================================================
#  T5 CW audio filters
# ===========================================================================

def test_cw_filters(radio: Radio, inj: Injector, indices: Sequence[int],
                    tol_hz: float, passband_hi_hz: float,
                    log: Callable[[str], None]) -> TestResult:
    """Measure each CW filter's -3 dB corner, ripple and stopband.

    Every probe is a pair: the same injected frequency captured with the filter
    bypassed (``CF5``) and engaged. ``CWAudioFilter`` is a plain series stage, so
    the difference is exactly the Chebyshev response and nothing else.
    """
    filters = []
    checks: list[Check] = []

    for k in indices:
        nominal = bt.CW_FILTER_NOMINAL_HZ[k]
        log(f"  CW filter {k} ({nominal:.0f} Hz)")

        cache: dict[float, float] = {}

        def response_db(f_hz: float, _k=k, _cache=cache) -> float:
            """Filter magnitude at f_hz, as engaged-minus-bypassed."""
            key = round(f_hz, 2)
            if key in _cache:
                return _cache[key]
            radio.set_cw_filter(bt.CW_FILTER_OFF)
            off = inj.point(f_hz)
            radio.set_cw_filter(_k)
            on = inj.point(f_hz)
            val = on.level_dbv - off.level_dbv if (on.valid and off.valid) else float("nan")
            _cache[key] = val
            return val

        # Coarse shape first, so the report has a curve and not just a number.
        coarse_f = geom_grid(nominal * 0.45, nominal * 1.6, 13)
        coarse_db = [response_db(float(f)) for f in coarse_f]

        corner, trace = find_crossing(response_db, -3.0,
                                      nominal * 0.6, nominal * 1.45, tol_hz)

        passband = [d for f, d in zip(coarse_f, coarse_db)
                    if 0.2 * nominal <= f <= 0.7 * nominal and np.isfinite(d)]
        ripple = (max(passband) - min(passband)) if len(passband) >= 2 else float("nan")
        # Probe the stopband where the CW filter is the only thing attenuating.
        # 1.5x the cutoff would sit on the SSB filter's own skirt for the wider
        # CW settings, and would measure that instead.
        stop_hz = min(nominal * 1.5, 0.85 * passband_hi_hz)
        stop = response_db(stop_hz) if stop_hz > nominal * 1.15 else float("nan")

        filters.append({
            "index": k,
            "nominal_hz": nominal,
            "corner_3db_hz": _clean(corner),
            "passband_ripple_db": _clean(ripple),
            "stopband_atten_db": _clean(stop),
            "stopband_probe_hz": _clean(stop_hz),
            "points": [{"f_audio_hz": round(float(f), 2), "resp_db": _clean(d)}
                       for f, d in zip(coarse_f, coarse_db)],
            "bisection_trace": [[round(f, 2), _clean(d)] for f, d in trace],
        })
        log(f"    -3 dB at {corner:.1f} Hz, ripple {ripple:.2f} dB, "
            f"stopband {stop:.1f} dB")

        checks.append(Check(
            f"cw.{k}.shape_ripple", "cw",
            f"{nominal:.0f} Hz CW filter passband is flat",
            _clean(ripple), 1.0, "db",
            bool(np.isfinite(ripple) and ripple <= 1.0),
            message=f"{ripple:.2f} dB peak to peak" if np.isfinite(ripple) else "not measured"))
        checks.append(Check(
            f"cw.{k}.stopband", "cw",
            f"{nominal:.0f} Hz CW filter attenuates above its cutoff",
            _clean(stop), -25.0, "db",
            bool(np.isfinite(stop) and stop <= -25.0),
            skipped=not np.isfinite(stop),
            message=(f"{stop:.1f} dB at {stop_hz:.0f} Hz" if np.isfinite(stop)
                     else f"no room between the {nominal:.0f} Hz cutoff and the "
                          f"{passband_hi_hz:.0f} Hz SSB passband to probe a stopband")))

    radio.set_cw_filter(bt.CW_FILTER_OFF)
    return TestResult("cw_filters", checks, {"filters": filters})


# ===========================================================================
#  T6 equaliser cells
# ===========================================================================

def test_equalizer_cells(radio: Radio, inj: Injector, reference: TestResult,
                         cells: Sequence[int], n_points: int,
                         passband_hi_hz: float,
                         log: Callable[[str], None]) -> TestResult:
    """Locate each equaliser cell's peak and bandwidth.

    A cell is isolated by setting it to 100 and the other thirteen to 0. The
    cells are summed in parallel, so this leaves exactly one of them - no
    approximation.

    The SSB filter is opened up first. At its normal 3 kHz the top two cells
    (3150 and 4000 Hz) sit on or beyond the skirt and their shape is set by the
    SSB filter rather than by the cell, so they cannot be measured at all.
    """
    results = []
    checks: list[Check] = []

    wanted_hi = max(bt.EQ_CENTRE_HZ[b] * 1.45 for b in cells) if cells else passband_hi_hz
    widened_hi = passband_hi_hz
    if wanted_hi > passband_hi_hz:
        widened_hi = float(min(wanted_hi, 5500.0))
        radio.set_filter_hi_hz(int(widened_hi))
        time.sleep(0.5)
        log(f"  opened the SSB filter to {widened_hi:.0f} Hz so the top cells "
            f"are not measured through its skirt")

    for b in cells:
        fc = bt.EQ_CENTRE_HZ[b]
        radio.set_eq_solo(b)
        probe = geom_grid(fc * 0.72, fc * 1.4, n_points)

        freqs, resp, points = [], [], []
        for f in probe:
            m = inj.point(float(f))
            points.append(_point_dict(float(f), inj.plan, m))
            if m.valid:
                freqs.append(float(f))
                resp.append(m.level_dbv)

        if len(freqs) >= 3:
            peak = peak_from_three(freqs, resp)
            lo, hi, q = bandwidth_3db(freqs, resp)
        else:
            peak = lo = hi = q = float("nan")

        # A cell is edge limited when the SSB filter, not the cell, decides its
        # shape. That depends on the filter width in use, so it is judged
        # against the actual passband rather than a fixed list of cells.
        edge_limited = (b in bt.EQ_EDGE_LIMITED_CELLS
                        or fc > 0.85 * widened_hi)
        results.append({
            "index": b,
            "nominal_hz": fc,
            "peak_hz": _clean(peak),
            "bw_3db_hz": _clean(hi - lo if np.isfinite(hi) and np.isfinite(lo) else float("nan")),
            "q": _clean(q),
            "q_expected": _clean(bt.eq_q(b)),
            "edge_limited": edge_limited,
            "points": points,
        })
        log(f"  cell {b:2d} ({fc:7.1f} Hz): peak {peak:7.1f} Hz, Q {q:5.2f}"
            + ("  [edge limited]" if edge_limited else ""))

        if np.isfinite(q) and not edge_limited:
            q_err = abs(q - bt.eq_q(b)) / bt.eq_q(b)
            checks.append(Check(
                f"eq.{b}.q", "eq", f"{fc:.0f} Hz cell has the designed shape",
                100.0 * q_err, 25.0, "percent", q_err <= 0.25,
                message=f"Q {q:.2f} vs {bt.eq_q(b):.2f} expected"))

    radio.set_eq_all([100] * bt.EQ_CELL_COUNT)
    if widened_hi != passband_hi_hz:
        radio.set_filter_hi_hz(int(passband_hi_hz))
        time.sleep(0.3)
    return TestResult("equalizer_cells", checks,
                      {"cells": results, "passband_hi_hz": widened_hi})


# ===========================================================================
#  T7 SSB convolution filter - the control
# ===========================================================================

#: Level at which the SSB filter edge is called. See test_ssb_filter.
SSB_EDGE_DB = -3.0


def test_ssb_filter(radio: Radio, inj: Injector, widths_hz: Sequence[int],
                    baseline_hi_hz: int, log: Callable[[str], None]) -> TestResult:
    """Check the SSB filter edge follows the commanded bandwidth.

    This filter derives its coefficients from the true sample rate and always
    did, so it was never affected by the frozen-table bug. That makes it the
    control: if it shows a shift between rates, the rig or the analysis is
    wrong, not the firmware.
    """
    settings = []
    checks: list[Check] = []
    widest = max(widths_hz)

    for width in [w for w in widths_hz if w != widest]:
        cache: dict[float, float] = {}

        def response_db(f_hz: float, _w=width, _cache=cache) -> float:
            key = round(f_hz, 2)
            if key in _cache:
                return _cache[key]
            radio.set_filter_hi_hz(widest)
            ref = inj.point(f_hz)
            radio.set_filter_hi_hz(_w)
            cur = inj.point(f_hz)
            val = cur.level_dbv - ref.level_dbv if (cur.valid and ref.valid) else float("nan")
            _cache[key] = val
            return val

        # -3 dB rather than -6: measured through a receiver, the tone drops
        # into the noise about 5 dB below the passband, so a -6 dB crossing is
        # outside the dynamic range this rig has at the filter edge.
        edge, trace = find_crossing(response_db, SSB_EDGE_DB,
                                    width * 0.75, min(width * 1.25, 4200.0), 10.0)
        settings.append({
            "fw_hz": width,
            "hi_edge_6db_hz": _clean(edge),
            "bisection_trace": [[round(f, 2), _clean(d)] for f, d in trace],
        })
        log(f"  FW {width} Hz -> {SSB_EDGE_DB:.0f} dB edge at {edge:.1f} Hz")

        if np.isfinite(edge):
            checks.append(Check(
                f"ssb.{width}.edge", "ssb",
                f"SSB filter edge follows FW{width}",
                float(edge), float(width), "hz", abs(edge - width) <= 200.0,
                message=f"{SSB_EDGE_DB:.0f} dB edge at {edge:.1f} Hz for a "
                        f"commanded {width} Hz"))

    radio.set_filter_hi_hz(baseline_hi_hz)
    return TestResult("ssb_filter", checks, {"settings": settings})


# ===========================================================================
#  T8 AM DC blocker
# ===========================================================================

def test_am_dc_blocker(ad2: Ad2, plan: InjectionPlan, settle_s: float,
                       depth_pct: float, min_snr_db: float,
                       log: Callable[[str], None]) -> TestResult:
    """Measure the AM demodulator's DC blocker corner.

    The blocker acts on the recovered envelope, so it can only be probed by
    modulating the carrier and sweeping the modulation frequency - the carrier
    itself sits at DC after demodulation.
    """
    carrier = plan.inject_hz(0.0)
    mods = geom_grid(10.0, 300.0, 12)

    def envelope_db(f_mod: float) -> float:
        ad2.set_am_quadrature(carrier, plan.amplitude_v, plan.phase_sign,
                              f_mod, depth_pct)
        cap = ad2.capture_after(settle_s)
        m = measure_tone(cap, f_mod, scope_range_v=ad2.cfg.scope_range_v,
                         search_bw_hz=max(6.0, f_mod * 0.25),
                         min_snr_db=min_snr_db, max_thd_db=99.0)
        return m.level_dbv if m.valid else float("nan")

    ref = envelope_db(300.0)
    points = []
    for f in mods:
        db = envelope_db(float(f))
        points.append({"f_mod_hz": round(float(f), 2), "resp_db": _clean(db - ref)})

    corner, trace = find_crossing(lambda f: envelope_db(f) - ref, -3.0,
                                  120.0, 12.0, 1.0)
    log(f"  DC blocker -3 dB at {corner:.1f} Hz (design {bt.AM_DC_BLOCKER_CORNER_HZ:.0f} Hz)")

    checks = [Check(
        "am.corner_nominal", "am", "AM DC blocker sits at its design corner",
        _clean(corner), bt.AM_DC_BLOCKER_CORNER_HZ, "hz",
        bool(np.isfinite(corner) and abs(corner - bt.AM_DC_BLOCKER_CORNER_HZ) <= 6.0),
        message=f"{corner:.1f} Hz" if np.isfinite(corner) else "not measured")]

    return TestResult("am_dc_blocker", checks, {
        "corner_3db_hz": _clean(corner),
        "depth_pct": depth_pct,
        "points": points,
        "bisection_trace": [[round(f, 2), _clean(d)] for f, d in trace],
    })


# ===========================================================================
#  Cross-rate comparison - the headline result
# ===========================================================================

def _compare_feature(label: str, group: str, key: str, per_rate: dict,
                     extract: Callable[[dict], list], nominal_of: Callable[[dict], float],
                     id_of: Callable[[dict], str], rate_tol_pct: float,
                     nominal_tol_pct: float,
                     relaxed: Callable[[dict], bool] = lambda _: False,
                     relaxed_tol_pct: float = 8.0) -> tuple[list, list]:
    """Compare one measured feature across sample rates."""
    rates = sorted(per_rate)
    if len(rates) < 2:
        return [], []

    base_rate = max(rates)  # 192000 is the historical reference
    other_rates = [r for r in rates if r != base_rate]

    by_rate = {r: {id_of(item): item for item in extract(per_rate[r])} for r in rates}
    rows, checks = [], []

    for ident in by_rate[base_rate]:
        base_item = by_rate[base_rate][ident]
        base_val = base_item.get(key)
        nominal = nominal_of(base_item)
        measured = {str(base_rate): base_val}
        worst_delta = 0.0
        measurable = base_val is not None

        for r in other_rates:
            item = by_rate.get(r, {}).get(ident)
            val = item.get(key) if item else None
            measured[str(r)] = val
            if val is None or base_val is None:
                measurable = False
                continue
            d = pct_delta(base_val, val)
            if abs(d) > abs(worst_delta):
                worst_delta = d

        tol = relaxed_tol_pct if relaxed(base_item) else nominal_tol_pct
        nominal_err = (pct_delta(nominal, base_val)
                       if base_val is not None and nominal else float("nan"))

        legacy_consistent = (measurable and
                             abs(worst_delta - bt.LEGACY_DELTA_PCT) < 2.0)

        rows.append({
            "id": ident,
            "nominal_hz": nominal,
            key: measured,
            "delta_pct": _clean(worst_delta) if measurable else None,
            "legacy_predicted_delta_pct": bt.LEGACY_DELTA_PCT,
            "legacy_consistent": legacy_consistent,
            "nominal_error_pct": _clean(nominal_err),
            "verdict": ("PASS" if measurable and abs(worst_delta) <= rate_tol_pct
                        else "SKIP" if not measurable else "FAIL"),
        })

        msg = (f"{base_val:.1f} Hz at {base_rate} sps vs "
               f"{measured.get(str(other_rates[0])):.1f} Hz at {other_rates[0]} sps"
               if measurable else "not measured at both rates")
        if legacy_consistent:
            msg += (f" - this matches the {bt.LEGACY_DELTA_PCT:.3f}% shift the "
                    f"frozen coefficient tables used to produce")

        checks.append(Check(
            f"{group}.{ident}.rate_invariance", group,
            f"{label} {nominal:.0f} Hz holds across sample rates",
            _clean(worst_delta) if measurable else None, rate_tol_pct, "percent",
            measurable and abs(worst_delta) <= rate_tol_pct,
            skipped=not measurable, message=msg))

        if measurable:
            checks.append(Check(
                f"{group}.{ident}.nominal", group,
                f"{label} {nominal:.0f} Hz is near its labelled frequency",
                _clean(nominal_err), tol, "percent",
                bool(np.isfinite(nominal_err) and abs(nominal_err) <= tol),
                message=f"measured {base_val:.1f} Hz for a labelled {nominal:.0f} Hz"))

    return rows, checks


def compare_rates(per_rate: dict, rate_tol_pct: float,
                  nominal_tol_pct: float) -> tuple[dict, list]:
    """Roll every per-rate measurement up into the cross-rate verdict."""
    comparison: dict = {}
    checks: list[Check] = []

    rows, cw_checks = _compare_feature(
        "CW filter", "cw", "corner_3db_hz", per_rate,
        extract=lambda d: d.get("cw_filters", {}).get("filters", []),
        nominal_of=lambda i: i["nominal_hz"],
        id_of=lambda i: str(i["index"]),
        rate_tol_pct=rate_tol_pct, nominal_tol_pct=nominal_tol_pct)
    comparison["cw_filters"] = rows
    checks += cw_checks

    rows, eq_checks = _compare_feature(
        "EQ cell", "eq", "peak_hz", per_rate,
        extract=lambda d: d.get("equalizer_cells", {}).get("cells", []),
        nominal_of=lambda i: i["nominal_hz"],
        id_of=lambda i: str(i["index"]),
        rate_tol_pct=rate_tol_pct, nominal_tol_pct=nominal_tol_pct,
        relaxed=lambda i: bool(i.get("edge_limited")))
    comparison["eq_cells"] = rows
    checks += eq_checks

    rows, ssb_checks = _compare_feature(
        "SSB filter", "ssb", "hi_edge_6db_hz", per_rate,
        extract=lambda d: d.get("ssb_filter", {}).get("settings", []),
        nominal_of=lambda i: float(i["fw_hz"]),
        id_of=lambda i: str(i["fw_hz"]),
        rate_tol_pct=rate_tol_pct, nominal_tol_pct=nominal_tol_pct)
    comparison["ssb_filter"] = rows
    checks += ssb_checks

    am = {r: per_rate[r].get("am_dc_blocker") for r in per_rate}
    if all(v and v.get("corner_3db_hz") for v in am.values()) and len(am) > 1:
        rates = sorted(am)
        base = max(rates)
        other = [r for r in rates if r != base][0]
        delta = pct_delta(am[base]["corner_3db_hz"], am[other]["corner_3db_hz"])
        comparison["am_dc_blocker"] = {
            "nominal_hz": bt.AM_DC_BLOCKER_CORNER_HZ,
            "corner_3db_hz": {str(r): am[r]["corner_3db_hz"] for r in rates},
            "delta_pct": _clean(delta),
            "legacy_predicted_delta_pct": bt.LEGACY_DELTA_PCT,
            "verdict": "PASS" if abs(delta) <= 3.0 else "FAIL",
        }
        checks.append(Check(
            "am.corner.rate_invariance", "am",
            "AM DC blocker corner holds across sample rates",
            _clean(delta), 3.0, "percent", abs(delta) <= 3.0,
            message=f"{am[base]['corner_3db_hz']:.1f} Hz vs "
                    f"{am[other]['corner_3db_hz']:.1f} Hz"))

    return comparison, checks
