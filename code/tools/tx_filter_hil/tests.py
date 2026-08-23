"""The individual transmit hardware tests.

The transmit chain is measured differently from the receive chain, and the
difference is worth stating plainly because it changes what the results mean.

In the receive suite every response is the *difference* between two captures with
a filter engaged and bypassed, so the whole analog path cancels. Nothing in the
transmit chain can be bypassed over CAT: there is no command for the transmit
equaliser, and the two generated FIR stages either side of the Hilbert transform
are unconditional. So what is measured here is the **composite** response of the
whole chain, microphone input to exciter output, including the codec's own
front end.

That is not the handicap it sounds like, because the criterion that matters is
rate invariance. Nothing in the analog path changes when the sample rate changes,
so it divides out of a comparison between rates just as cleanly as it would out
of a bypass difference. What the composite measurement does cost is absolute
accuracy, and every absolute check below is given a loose tolerance for that
reason.

The bug being hunted is the same one: filters whose corners are specified in
hertz but whose coefficients were frozen for one audio rate. Run at another rate
every corner scaled by the ratio of the rates, **-8.125 %** between 176.4 and
192 ksps. The tolerance is 1.5 %, so there is a factor of five between correct
and broken.
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable, Optional, Sequence

import numpy as np

from filter_hil.radio import MOD_LSB, MOD_NAMES, MOD_USB, CatError, EdSnapshot, Radio

from . import bandtable as bt
from .ad2 import IqCapture, TxAd2
from .measure import (IqMeasurement, corner_from_sweep, dominant_line, geom_grid,
                      measure_iq_tone, passband_reference_db, pct_delta,
                      spur_level_dbc, sweep_ripple_db, to_db, worst_in_band)

#: Window inside which the transmit passband is taken to be flat. Above the
#: lowest equaliser cell at 198 Hz and well below the 2.76 kHz corner.
PASSBAND_LO_HZ = 500.0
PASSBAND_HI_HZ = 1800.0


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
            "value": clean(self.value), "limit": clean(self.limit),
            "units": self.units, "passed": bool(self.passed),
            "skipped": bool(self.skipped), "message": self.message,
        }


@dataclass
class TestResult:
    name: str
    checks: list = field(default_factory=list)
    data: dict = field(default_factory=dict)
    skipped: bool = False
    reason: str = ""


def clean(x):
    """JSON has no NaN; represent unmeasurable values as null."""
    if x is None:
        return None
    if isinstance(x, (bool, np.bool_)):
        return bool(x)
    if isinstance(x, (int, np.integer)):
        return int(x)
    return float(x) if np.isfinite(x) else None


@dataclass
class TxPlan:
    """How the rig is wired and driven, as discovered at run time.

    ``swap`` says whether scope Ch1 is on Q rather than I, and ``sideband_sign``
    which side of DC the wanted energy lands on once that swap is applied. Both
    are found by experiment rather than assumed, because both depend on how the
    bench happens to be wired today and neither is knowable from the firmware.
    """

    rate_hz: int
    modulation: int = MOD_USB
    swap: bool = False
    sideband_sign: int = 1
    amplitude_v: float = 0.05

    @property
    def audio_rate_hz(self) -> float:
        return bt.audio_rate_hz(self.rate_hz)

    @property
    def fold_hz(self) -> float:
        return bt.fold_frequency_hz(self.rate_hz)


class Injector:
    """Drives one microphone frequency and reads the exciter output back."""

    def __init__(self, ad2: TxAd2, plan: TxPlan, settle_s: float,
                 min_snr_db: float, max_thd_db: float) -> None:
        self.ad2 = ad2
        self.plan = plan
        self.settle_s = settle_s
        self.min_snr_db = min_snr_db
        self.max_thd_db = max_thd_db

    def capture_at(self, f_audio_hz: float) -> IqCapture:
        """Drive the microphone at one frequency and return the raw capture."""
        self.ad2.set_tone(f_audio_hz, self.plan.amplitude_v)
        return self.ad2.capture_after(self.settle_s)

    def point(self, f_audio_hz: float) -> tuple[IqMeasurement, IqCapture]:
        cap = self.capture_at(f_audio_hz)
        m = measure_iq_tone(cap, f_audio_hz, swap=self.plan.swap,
                            sideband_sign=self.plan.sideband_sign,
                            scope_range_v=self.ad2.cfg.scope_range_v,
                            scope_offset_v=self.ad2.cfg.scope_offset_v,
                            min_snr_db=self.min_snr_db,
                            max_thd_db=self.max_thd_db)
        return m, cap

    def sweep(self, freqs_hz: Iterable[float]) -> list[tuple[IqMeasurement, IqCapture]]:
        return [self.point(float(f)) for f in freqs_hz]


def point_dict(m: IqMeasurement) -> dict:
    """One measured point, in the shape the report and the plots consume."""
    d = {
        "f_audio_hz": round(m.f_target_hz, 3),
        "f_measured_hz": clean(m.f_measured_hz),
        "level_dbv": clean(m.level_dbv),
        "suppression_db": clean(m.suppression_db),
        "carrier_dbc": clean(m.carrier_dbc),
        "snr_db": clean(m.snr_db),
        "thd_db": clean(m.thd_db),
        "clipped": m.clipped,
        "valid": m.valid,
    }
    if m.reason:
        d["reason"] = m.reason
    return d


# ===========================================================================
#  T0 preflight
# ===========================================================================

def run_preflight(radio: Radio, log: Callable[[str], None]) -> tuple[TestResult, EdSnapshot]:
    """Confirm the radio answers and is in a state that can be keyed."""
    checks: list[Check] = []

    ident = radio.expect("ID;")
    checks.append(Check("rig.cat", "rig", "Radio answers on the CAT port",
                        None, None, "", True, message=f"ID reply {ident}"))

    ed = radio.dump_ed()
    log(f"Radio: rate {ed.sample_rate_hz} sps, band {ed.current_band[ed.active_vfo]}, "
        f"mode {ed.modulation_name}, mic gain {ed.mic_gain_db} dB, "
        f"SSB power {ed.active_power_ssb:.0f} W")

    # TX_write only dispatches PTT_PRESSED from SSB_RECEIVE and KEY_PRESSED from
    # CW_RECEIVE. A CW key-down transmits a carrier from the sidetone oscillator,
    # not processed microphone audio, so it exercises none of the filters here.
    # AM and SAM reach the same SSB transmit state but SidebandSelection only
    # special-cases USB, so the sideband would be whatever LSB gives - measurable
    # but confusing. The run forces USB or LSB either way.
    ssb = ed.active_modulation in (MOD_USB, MOD_LSB)
    checks.append(Check(
        "rig.ssb_mode", "rig", "Radio is on a sideband mode the transmit chain "
        "processes microphone audio in",
        float(ed.active_modulation), None, "modulation", True,
        message=(f"{ed.modulation_name}" if ssb else
                 f"{ed.modulation_name}; the run will switch to USB and switch "
                 f"back afterwards")))

    checks.append(Check(
        "rig.sample_rate_known", "rig",
        "Radio reports a CAT-selectable sample rate",
        float(ed.sample_rate_hz), None, "hz",
        ed.sample_rate_hz in bt.SAMPLE_RATES_HZ,
        message=f"{ed.sample_rate_hz} sps"))

    return TestResult("preflight", checks, {"ed": ed.summary(), "id": ident}), ed


# ===========================================================================
#  T1 wiring and sideband
# ===========================================================================

def detect_wiring(ad2: TxAd2, radio: Radio, plan: TxPlan, settle_s: float,
                  min_suppression_db: float, amplitude_v: float,
                  modulation: int,
                  log: Callable[[str], None]) -> tuple[TestResult, bool, int]:
    """Find which scope input is on I, and which side of DC the sideband is on.

    Two unknowns, and unlike the receive rig they cannot be separated by trying
    combinations and seeing which one produces a signal - every combination
    produces a perfectly good tone. All that changes is which side of DC it lands
    on, and one capture cannot tell "Ch1 is on I, upper sideband" from "Ch1 is on
    Q, lower sideband".

    What separates them is the radio's own sideband switch. ``SidebandSelection``
    inverts I for USB and leaves LSB alone, so commanding USB conjugates the
    transmitted signal and moves the tone to the other side of DC. Exchanging the
    scope inputs conjugates it too - but the operator is not going to rewire the
    bench between two captures, so the *change* between USB and LSB isolates the
    radio's contribution from the wiring's.

    So: capture in LSB, capture in USB. The tone must swap sides. Which sign LSB
    produced then says which input is on I. The radio is left on ``modulation``,
    which is the sideband the rest of the run measures on.
    """
    probe_hz = 1000.0
    # Detection runs at the approved ceiling: picking a side is a comparison
    # between two strong lines, and a tone buried in noise gives an ambiguous
    # sign. Auto-levelling brings the drive back down immediately afterwards.
    plan.amplitude_v = amplitude_v
    trials: dict[str, dict] = {}

    for mod in (MOD_LSB, MOD_USB):
        radio.set_modulation(mod)
        time.sleep(0.5)
        ad2.set_tone(probe_hz, amplitude_v)
        cap = ad2.capture_after(settle_s)
        # Read the raw capture without applying a swap: the swap is what is being
        # determined, and dominant_line only needs to know where the energy is.
        f_signed, amp = dominant_line(cap, swap=False,
                                      f_min_hz=probe_hz * 0.5,
                                      f_max_hz=probe_hz * 1.5)
        name = MOD_NAMES[mod]
        trials[name] = {
            "modulation": mod,
            "f_signed_hz": clean(f_signed),
            "amplitude_v": clean(amp),
            "level_dbv": clean(to_db(amp)),
            "sign": int(np.sign(f_signed)) if amp > 0 else 0,
        }
        log(f"  {name}: {to_db(amp):7.1f} dBV at {f_signed:+8.1f} Hz "
            f"(Ch1 taken as I)")

    lsb, usb = trials["LSB"], trials["USB"]
    flipped = (lsb["sign"] != 0 and usb["sign"] != 0
               and lsb["sign"] != usb["sign"])

    # LSB leaves I alone, so with Ch1 genuinely on I the analytic signal is
    # I + jQ as built by the chain and the tone sits on the positive side.
    swap = lsb["sign"] < 0
    wiring = ("Ch1 -> I, Ch2 -> Q" if not swap else
              "Ch1 -> Q, Ch2 -> I (compensated in software)")

    # Settle on the sideband the run will use, and take the wanted side from a
    # capture with the resolved swap in place rather than predicting it.
    # SidebandSelection's inversion says USB should come out on the negative side
    # once Ch1 is on I, but an inverting stage anywhere in the exciter's analog
    # output would undo that without being a fault, so the sign is measured.
    plan.swap = swap
    plan.modulation = modulation
    radio.set_modulation(modulation)
    time.sleep(0.5)
    ad2.set_tone(probe_hz, amplitude_v)
    cap = ad2.capture_after(settle_s)
    f_resolved, _ = dominant_line(cap, swap=swap, f_min_hz=probe_hz * 0.5,
                                  f_max_hz=probe_hz * 1.5)
    plan.sideband_sign = -1 if f_resolved < 0 else 1

    # The same capture gives the suppression figure, which is the number that says
    # whether both channels are really connected and in quadrature.
    m = measure_iq_tone(cap, probe_hz, swap=plan.swap,
                        sideband_sign=plan.sideband_sign,
                        scope_range_v=ad2.cfg.scope_range_v,
                        scope_offset_v=ad2.cfg.scope_offset_v,
                        min_snr_db=0.0, max_thd_db=99.0)
    expected_sign = -1 if modulation == MOD_USB else 1
    mod_name = MOD_NAMES[modulation]
    log(f"  resolved: {wiring}, wanted side "
        f"{'+' if plan.sideband_sign > 0 else '-'}f in {mod_name}"
        + ("" if plan.sideband_sign == expected_sign
           else " (opposite to the firmware's convention - an inverting stage "
                "somewhere in the exciter output)")
        + f", suppression {m.suppression_db:.1f} dB")

    checks = [Check(
        "rig.sideband_flip", "rig",
        "Commanding the other sideband moves the transmitted tone across DC",
        None, None, "", flipped,
        message=("the tone changed sides between LSB and USB, so the wiring "
                 "order is unambiguous" if flipped else
                 "the tone did not change sides between LSB and USB. Either only "
                 "one of Ch1/Ch2 is connected, or the radio did not change "
                 "modulation, or it is not actually transmitting"))]

    # A single connected channel gives a real signal, whose spectrum is symmetric
    # about DC: suppression near 0 dB. That is the failure this catches, and it is
    # the one thing about the rig the sign test alone cannot see.
    checks.append(Check(
        "rig.suppression", "rig",
        "Both exciter outputs are connected and in quadrature",
        clean(m.suppression_db), min_suppression_db,
        "db", bool(m.suppression_db >= min_suppression_db),
        message=(f"{m.suppression_db:.1f} dB of opposite-sideband suppression at "
                 f"{probe_hz:.0f} Hz" if m.suppression_db >= min_suppression_db else
                 f"only {m.suppression_db:.1f} dB between the two sides of DC. A "
                 f"real signal - one channel missing, or both probes on the same "
                 f"output - reads close to 0 dB")))

    data = {
        "swap": swap,
        "wiring": wiring,
        "sideband_sign": plan.sideband_sign,
        "sideband_sign_expected": expected_sign,
        "sideband_sign_as_expected": plan.sideband_sign == expected_sign,
        "sideband_flipped": flipped,
        "measured_on": mod_name,
        "suppression_at_1k_db": clean(m.suppression_db),
        "detect_amplitude_v": amplitude_v,
        "dc_ch1_v": clean(m.dc_ch1_v),
        "dc_ch2_v": clean(m.dc_ch2_v),
        "trials": trials,
    }
    return TestResult("wiring", checks, data), swap, plan.sideband_sign


# ===========================================================================
#  T2 drive level
# ===========================================================================

def autolevel(ad2: TxAd2, plan: TxPlan, settle_s: float, start_v: float,
              max_v: float, step_db: float, min_snr_db: float, max_thd_db: float,
              log: Callable[[str], None]) -> tuple[TestResult, float]:
    """Find the smallest microphone drive that gives a clean measurement.

    Steps up only, and never past ``max_v``. The microphone input is a
    millivolt-level port with a preamplifier in front of it, so the useful window
    is narrow: too little and the tone sits in the codec's noise, too much and the
    preamplifier clips and the harmonics contaminate the fold-back test.

    Distortion is what stops the search, not just noise. Unlike the receive rig -
    where the harmonic bins hold receiver noise as much as any real distortion -
    the transmit measurement is taken straight off a DAC and its THD figure is
    trustworthy, so it is used as the ceiling.
    """
    probe_hz = 1000.0
    amplitude = start_v
    trials = []
    chosen = None

    while amplitude <= max_v * 1.0001:
        plan.amplitude_v = amplitude
        ad2.set_tone(probe_hz, amplitude)
        cap = ad2.capture_after(settle_s)
        m = measure_iq_tone(cap, probe_hz, swap=plan.swap,
                            sideband_sign=plan.sideband_sign,
                            scope_range_v=ad2.cfg.scope_range_v,
                            scope_offset_v=ad2.cfg.scope_offset_v,
                            min_snr_db=min_snr_db, max_thd_db=max_thd_db,
                            thd_invalidates=True)
        trials.append({"amplitude_v": round(amplitude, 6), **point_dict(m)})
        log(f"  {amplitude*1000:6.1f} mV -> {m.level_dbv:6.1f} dBV, "
            f"SNR {m.snr_db:5.1f} dB, THD {m.thd_db:6.1f} dB, "
            f"suppression {m.suppression_db:5.1f} dB, peak {m.peak_v:.3f} V")

        if m.snr_db >= min_snr_db and m.thd_db <= max_thd_db and not m.clipped:
            chosen = amplitude
            break
        amplitude *= 10.0 ** (step_db / 20.0)

    if chosen is None:
        chosen = min(amplitude, max_v)
        msg = (f"never reached {min_snr_db:.0f} dB SNR at under {max_thd_db:.0f} dB "
               f"THD below {max_v*1000:.0f} mV; raise --max-amplitude, or the "
               f"radio's mic gain with --mic-gain")
        passed = False
    else:
        msg = f"{chosen*1000:.1f} mV"
        passed = True
    plan.amplitude_v = chosen

    check = Check("rig.drive_level", "rig",
                  "A microphone drive giving adequate SNR without distorting",
                  chosen * 1000.0, max_v * 1000.0, "mv", passed, message=msg)
    return TestResult("autolevel", [check], {"amplitude_v": chosen,
                                             "trials": trials}), chosen


# ===========================================================================
#  T3 passband
# ===========================================================================

def test_passband(inj: Injector, grid_hz: np.ndarray, nominal_tol_pct: float,
                  log: Callable[[str], None]) -> TestResult:
    """Sweep the transmit audio response and extract its corners.

    This is the headline measurement. The high corner is set by
    ``TXInterpolateBy2Again``'s filter, specified as 3039.6 Hz at -6 dB and
    landing near 2.76 kHz at -3 dB, which is one of the two stages regenerated per
    sample rate. The low corner is set by the equaliser bank, whose lowest cell
    sits at 198 Hz and which is regenerated too.

    Both are measured as composites, with the codec's own response and the
    microphone input's AC coupling in series and no way to divide them out. The
    absolute checks are loose for that reason. The rate comparison in
    :func:`compare_rates` is not, and is where the answer actually comes from.
    """
    points = []
    freqs, levels, suppression, carrier = [], [], [], []

    for f in grid_hz:
        m, _ = inj.point(float(f))
        points.append(point_dict(m))
        if m.valid:
            freqs.append(float(f))
            levels.append(m.level_dbv)
            suppression.append(m.suppression_db)
            carrier.append(m.carrier_dbc)

    ref_db = passband_reference_db(freqs, levels, PASSBAND_LO_HZ, PASSBAND_HI_HZ)
    ripple = sweep_ripple_db(freqs, levels, PASSBAND_LO_HZ, PASSBAND_HI_HZ)
    mid = 0.5 * (PASSBAND_LO_HZ + PASSBAND_HI_HZ)

    hi_3db = corner_from_sweep(freqs, levels, ref_db - 3.0, side="high", from_hz=mid)
    hi_6db = corner_from_sweep(freqs, levels, ref_db - 6.0, side="high", from_hz=mid)
    lo_3db = corner_from_sweep(freqs, levels, ref_db - 3.0, side="low", from_hz=mid)

    log(f"  passband reference {ref_db:.1f} dBV, ripple {ripple:.2f} dB")
    log(f"  -3 dB corners: {lo_3db:.1f} Hz low, {hi_3db:.1f} Hz high "
        f"(-6 dB at {hi_6db:.1f} Hz)")

    checks: list[Check] = []
    valid_in_band = [p for p in points
                     if PASSBAND_LO_HZ <= p["f_audio_hz"] <= PASSBAND_HI_HZ
                     and p["valid"]]
    in_band = [p for p in points
               if PASSBAND_LO_HZ <= p["f_audio_hz"] <= PASSBAND_HI_HZ]
    frac = len(valid_in_band) / len(in_band) if in_band else 0.0
    checks.append(Check(
        "passband.coverage", "passband",
        "Most of the passband gives a usable measurement",
        100.0 * frac, 95.0, "percent", frac >= 0.95,
        message=f"{len(valid_in_band)}/{len(in_band)} points valid between "
                f"{PASSBAND_LO_HZ:.0f} and {PASSBAND_HI_HZ:.0f} Hz"))

    # A passband flat to a small fraction of a decibel across an octave and a
    # half is not a passband, it is a limiter. The equaliser's reconstruction
    # ripples by a few tenths of a decibel, so some variation is expected.
    checks.append(Check(
        "passband.ripple", "passband",
        "Transmit passband is flat, but not suspiciously flat",
        clean(ripple), 3.0, "db",
        bool(np.isfinite(ripple) and ripple <= 3.0),
        message=(f"{ripple:.2f} dB peak to peak" if np.isfinite(ripple)
                 else "not measured")))

    nominal_hi = bt.TX_AUDIO_CORNER_3DB_HZ
    hi_err = pct_delta(nominal_hi, hi_3db)
    checks.append(Check(
        "passband.hi_corner_nominal", "passband",
        f"High corner is near the {nominal_hi:.0f} Hz the offline stage "
        f"measurement gives",
        clean(hi_err), nominal_tol_pct, "percent",
        bool(np.isfinite(hi_err) and abs(hi_err) <= nominal_tol_pct),
        skipped=not np.isfinite(hi_3db),
        message=(f"{hi_3db:.1f} Hz measured against {nominal_hi:.0f} Hz "
                 f"({hi_err:+.1f}%); the equaliser bank is in series and cannot "
                 f"be taken out over CAT" if np.isfinite(hi_3db)
                 else "no -3 dB crossing inside the sweep")))

    nominal_lo = bt.TX_AUDIO_LOW_CORNER_HZ
    checks.append(Check(
        "passband.lo_corner_sane", "passband",
        f"Low corner is somewhere near the {nominal_lo:.0f} Hz lowest "
        f"equaliser cell",
        clean(lo_3db), None, "hz",
        bool(np.isfinite(lo_3db) and 100.0 <= lo_3db <= 400.0),
        skipped=not np.isfinite(lo_3db),
        message=(f"{lo_3db:.1f} Hz; the microphone input's AC coupling is in "
                 f"series with the equaliser here, so this is a sanity bound "
                 f"rather than a specification" if np.isfinite(lo_3db)
                 else "no -3 dB crossing below the passband inside the sweep")))

    return TestResult("passband", checks, {
        "points": points,
        "reference_dbv": clean(ref_db),
        "ripple_db": clean(ripple),
        "corner_lo_3db_hz": clean(lo_3db),
        "corner_hi_3db_hz": clean(hi_3db),
        "corner_hi_6db_hz": clean(hi_6db),
        "passband_window_hz": [PASSBAND_LO_HZ, PASSBAND_HI_HZ],
        "suppression_points": [{"f_audio_hz": round(f, 2), "suppression_db": clean(s)}
                               for f, s in zip(freqs, suppression)],
        "carrier_points": [{"f_audio_hz": round(f, 2), "carrier_dbc": clean(c)}
                           for f, c in zip(freqs, carrier)],
    })


# ===========================================================================
#  T4 sideband suppression
# ===========================================================================

def test_sideband_suppression(passband: TestResult, rate_hz: int,
                              min_suppression_db: float,
                              log: Callable[[str], None]) -> TestResult:
    """Judge the opposite-sideband suppression measured during the sweep.

    No extra captures: suppression comes free with every point of the passband
    sweep, because both sides of DC are present in the same complex spectrum.

    What this measures is the Hilbert transform's quadrature accuracy, the per-band
    I/Q amplitude and phase correction, and any gain difference between the two
    exciter outputs - lumped together, because from the exciter's terminals they
    are not separable.

    Unlike everything else here, this is checked against a **floor** rather than
    for rate invariance. The Hilbert table is not regenerated per rate, and that
    is correct: a Hilbert transformer's usable band is a fraction of its sample
    rate, so one fixed table is the same design at any rate and its edges are
    *meant* to move with Fs. Requiring the suppression curve to hold still in
    hertz would fail correct firmware.
    """
    pts = passband.data.get("suppression_points", [])
    freqs = [p["f_audio_hz"] for p in pts]
    supp = [p["suppression_db"] for p in pts]

    f_worst, worst = worst_in_band(freqs, supp, PASSBAND_LO_HZ,
                                   bt.TX_AUDIO_CORNER_3DB_HZ)
    f_best, best = worst_in_band(freqs, supp, PASSBAND_LO_HZ,
                                 bt.TX_AUDIO_CORNER_3DB_HZ, best=True)
    log(f"  worst {worst:.1f} dB at {f_worst:.0f} Hz, "
        f"best {best:.1f} dB at {f_best:.0f} Hz")

    checks = [Check(
        "sideband.worst", "sideband",
        "Opposite sideband is suppressed across the transmit passband",
        clean(worst), min_suppression_db, "db",
        bool(np.isfinite(worst) and worst >= min_suppression_db),
        skipped=not np.isfinite(worst),
        message=(f"worst case {worst:.1f} dB at {f_worst:.0f} Hz"
                 if np.isfinite(worst) else "not measured"))]

    return TestResult("sideband_suppression", checks, {
        "worst_db": clean(worst),
        "worst_at_hz": clean(f_worst),
        "best_db": clean(best),
        "best_at_hz": clean(f_best),
        "hilbert_band_hz": list(bt.hilbert_band_hz(rate_hz)),
        "points": pts,
    })


# ===========================================================================
#  T5 out-of-band rejection and fold-back
# ===========================================================================

#: How close to the measured noise floor a spur reading may sit before it is
#: reported as floor limited rather than as a real level.
FLOOR_MARGIN_DB = 3.0


def measure_noise_floor_dbc(ad2: TxAd2, plan: TxPlan, settle_s: float,
                            reference_v: float, probe_hz: Sequence[float]) -> float:
    """Spur level the rig reads with nothing driving the microphone.

    Measured exactly the way a spur is measured - the same windowed peak search
    over the same width - so the two are directly comparable. Without this a
    reading of "-33 dBc" is ambiguous: it could be a real product or it could be
    the floor, and the difference decides whether a pass means anything.
    """
    ad2.set_tone(1000.0, 0.0)
    cap = ad2.capture_after(settle_s)
    levels = [spur_level_dbc(cap, plan.swap, f, reference_v) for f in probe_hz]
    finite = [v for v in levels if np.isfinite(v)]
    return max(finite) if finite else float("nan")


def test_alias(inj: Injector, plan: TxPlan, reference_v: float,
               grid_hz: np.ndarray, min_rejection_db: float,
               max_fold_dbc: float, log: Callable[[str], None]) -> TestResult:
    """Drive the microphone above the passband and look for what comes through.

    Two different things happen depending on where the probe sits, and they are
    checked over different frequency ranges rather than lumped together:

    * **Direct rejection**, between the stopband boundary and the fold point.
      Here the tone can still reach the output at its own frequency and must be
      attenuated. Below :data:`bandtable.TX_STOPBAND_FROM_HZ` it is on the
      transition skirt - the cascade is only about 20 dB down at 3.45 kHz, and
      correctly so - which is why the sweep starts where the stopband starts.
    * **Fold-back**, above the fold point. ``TXDecimateBy2Again`` halves the
      audio rate, so a tone above a quarter of that rate cannot appear at its own
      frequency at all: it can only reappear folded, at 192 ksps a microphone
      tone at 8 kHz landing on 4 kHz. This is the failure ``TX_DECIMATE3_FC_HZ``
      was added to fix - the table it replaced was flat well past the fold point.
      The fold frequencies scale with the sample rate, so where to look changes
      per rate.

    Both are read against the rig's own noise floor, measured with the microphone
    silent, and reported as floor limited when they sit on it.

    One honest caveat, which the report repeats: the equaliser bank runs *before*
    the decimator and its highest cell is at 4 kHz, so it attenuates an 8 kHz
    input substantially on its own. A clean result here therefore proves nothing
    folds back into the transmitted audio - which is what matters operationally -
    but it does not by itself prove the decimator's own stopband is intact. The
    offline test ``TransmitChain176k.DecimatorStopsBeforeTheFoldPoint`` covers
    that directly, by evaluating the tap set at the fold point.
    """
    points = []
    checks: list[Check] = []
    fold = plan.fold_hz
    nyquist = bt.nyquist_after_fold_hz(plan.rate_hz)

    floor_dbc = measure_noise_floor_dbc(
        inj.ad2, plan, inj.settle_s, reference_v,
        [0.35 * nyquist, 0.6 * nyquist, 0.85 * nyquist])
    log(f"  noise floor {floor_dbc:.1f} dBc with the microphone silent")

    worst_direct = -999.0
    worst_direct_at = float("nan")
    worst_fold = -999.0
    worst_fold_at = float("nan")

    for f in grid_hz:
        f = float(f)
        m, cap = inj.point(f)

        # Above the fold point there is nothing at f to measure: the stream after
        # the decimation does not reach that far. Only the folded product exists.
        folds = f > nyquist
        direct_dbc = (m.level_dbv - to_db(reference_v)) if not folds else float("nan")

        f_alias = bt.alias_of_hz(f, plan.rate_hz)
        fold_dbc = (spur_level_dbc(cap, plan.swap, f_alias, reference_v)
                    if folds and abs(f_alias - f) > 100.0 else float("nan"))

        points.append({
            "f_audio_hz": round(f, 2),
            "region": "fold" if folds else "direct",
            "direct_dbc": clean(direct_dbc),
            "f_alias_hz": clean(f_alias) if folds else None,
            "fold_dbc": clean(fold_dbc),
            "snr_db": clean(m.snr_db),
            "clipped": m.clipped,
        })
        if folds:
            log(f"  {f:7.0f} Hz -> folds to {f_alias:6.0f} Hz at "
                f"{fold_dbc:7.1f} dBc")
        else:
            log(f"  {f:7.0f} Hz -> direct {direct_dbc:7.1f} dBc")

        if np.isfinite(direct_dbc) and direct_dbc > worst_direct:
            worst_direct, worst_direct_at = direct_dbc, f
        if np.isfinite(fold_dbc) and fold_dbc > worst_fold:
            worst_fold, worst_fold_at = fold_dbc, f

    def floor_limited(value: float) -> bool:
        return bool(np.isfinite(floor_dbc) and np.isfinite(value)
                    and value <= floor_dbc + FLOOR_MARGIN_DB)

    if worst_direct > -998.0:
        limited = floor_limited(worst_direct)
        checks.append(Check(
            "alias.direct_rejection", "alias",
            f"Microphone audio between {bt.TX_STOPBAND_FROM_HZ:.0f} Hz and the "
            f"{nyquist:.0f} Hz fold point is not transmitted",
            clean(worst_direct), -min_rejection_db, "dbc",
            bool(worst_direct <= -min_rejection_db),
            message=f"worst {worst_direct:.1f} dBc at {worst_direct_at:.0f} Hz "
                    f"(limit {-min_rejection_db:.0f} dBc"
                    + (f", at the {floor_dbc:.1f} dBc noise floor, so the true "
                       f"rejection is greater" if limited else "")
                    + f"). The design predicts about "
                      f"{bt.TX_STOPBAND_PREDICTED_DB:.0f} dB here"))
    else:
        checks.append(Check(
            "alias.direct_rejection", "alias",
            "Microphone audio above the transmit passband is not transmitted",
            None, -min_rejection_db, "dbc", True, skipped=True,
            message=f"no probe frequency fell between "
                    f"{bt.TX_STOPBAND_FROM_HZ:.0f} Hz and the {nyquist:.0f} Hz "
                    f"fold point"))

    if worst_fold > -998.0:
        limited = floor_limited(worst_fold)
        checks.append(Check(
            "alias.fold_back", "alias",
            f"Nothing folds back into the passband when the audio rate halves "
            f"about {fold/2:.0f} Hz",
            clean(worst_fold), max_fold_dbc, "dbc",
            bool(worst_fold <= max_fold_dbc),
            message=f"worst fold-back product {worst_fold:.1f} dBc, from a "
                    f"{worst_fold_at:.0f} Hz input landing at "
                    f"{bt.alias_of_hz(worst_fold_at, plan.rate_hz):.0f} Hz "
                    f"(limit {max_fold_dbc:.0f} dBc"
                    + (f", at the {floor_dbc:.1f} dBc noise floor, so nothing "
                       f"measurable folded" if limited else "") + ")"))
    else:
        checks.append(Check(
            "alias.fold_back", "alias",
            "Nothing folds back into the passband when the audio rate halves",
            None, max_fold_dbc, "dbc", True, skipped=True,
            message=f"no probe frequency above the {nyquist:.0f} Hz fold point "
                    f"produced a measurable product"))

    return TestResult("alias", checks, {
        "points": points,
        "reference_v": clean(reference_v),
        "noise_floor_dbc": clean(floor_dbc),
        "fold_hz": clean(fold),
        "nyquist_after_fold_hz": clean(nyquist),
        "stopband_from_hz": bt.TX_STOPBAND_FROM_HZ,
        "worst_direct_dbc": clean(worst_direct) if worst_direct > -998.0 else None,
        "worst_direct_at_hz": clean(worst_direct_at),
        "worst_direct_floor_limited": floor_limited(worst_direct),
        "worst_fold_dbc": clean(worst_fold) if worst_fold > -998.0 else None,
        "worst_fold_at_hz": clean(worst_fold_at),
        "worst_fold_floor_limited": floor_limited(worst_fold),
    })


# ===========================================================================
#  T6 carrier leakage
# ===========================================================================

def test_carrier(passband: TestResult, max_carrier_dbc: float,
                 log: Callable[[str], None]) -> TestResult:
    """Judge the DC sitting on the exciter outputs.

    ``PlayIQData`` adds ``ED.DCOffsetI`` and ``DCOffsetQ`` to every block to null
    the transmitter's carrier, so DC on these outputs is deliberate and its
    residual is what the carrier calibration achieved. Nothing about it is
    sample-rate dependent, so this is reported as a diagnostic and checked only
    against a generous ceiling - a large residue here would raise the noise floor
    of every other measurement in the run.
    """
    pts = passband.data.get("carrier_points", [])
    values = [p["carrier_dbc"] for p in pts if p["carrier_dbc"] is not None]
    worst = max(values) if values else float("nan")
    median = float(np.median(values)) if values else float("nan")
    log(f"  carrier residue {median:.1f} dBc median, {worst:.1f} dBc worst")

    checks = [Check(
        "carrier.residue", "carrier",
        "Carrier nulling leaves the exciter outputs close to their bias",
        clean(worst), max_carrier_dbc, "dbc",
        bool(np.isfinite(worst) and worst <= max_carrier_dbc),
        skipped=not np.isfinite(worst),
        message=(f"worst {worst:.1f} dBc, median {median:.1f} dBc relative to the "
                 f"transmitted tone" if np.isfinite(worst) else "not measured"))]

    return TestResult("carrier", checks, {
        "worst_dbc": clean(worst),
        "median_dbc": clean(median),
        "points": pts,
    })


# ===========================================================================
#  Cross-rate comparison - the headline result
# ===========================================================================

def _compare_scalar(label: str, group: str, ident: str, per_rate: dict,
                    extract: Callable[[dict], Optional[float]],
                    nominal_hz: Optional[float], rate_tol_pct: float,
                    nominal_tol_pct: float) -> tuple[Optional[dict], list]:
    """Compare one scalar frequency across sample rates."""
    rates = sorted(per_rate)
    if len(rates) < 2:
        return None, []

    base_rate = max(rates)  # 192000 is the historical reference
    other_rates = [r for r in rates if r != base_rate]

    measured = {r: extract(per_rate[r]) for r in rates}
    base_val = measured[base_rate]
    worst_delta = 0.0
    measurable = base_val is not None

    for r in other_rates:
        val = measured[r]
        if val is None or base_val is None:
            measurable = False
            continue
        d = pct_delta(base_val, val)
        if abs(d) > abs(worst_delta):
            worst_delta = d

    legacy_consistent = (measurable
                         and abs(worst_delta - bt.LEGACY_DELTA_PCT) < 2.0)
    nominal_err = (pct_delta(nominal_hz, base_val)
                   if base_val is not None and nominal_hz else float("nan"))

    row = {
        "id": ident,
        "label": label,
        "nominal_hz": nominal_hz,
        "value_hz": {str(r): clean(measured[r]) for r in rates},
        "delta_pct": clean(worst_delta) if measurable else None,
        "legacy_predicted_delta_pct": bt.LEGACY_DELTA_PCT,
        "legacy_consistent": legacy_consistent,
        "nominal_error_pct": clean(nominal_err),
        "verdict": ("PASS" if measurable and abs(worst_delta) <= rate_tol_pct
                    else "SKIP" if not measurable else "FAIL"),
    }

    msg = (f"{base_val:.1f} Hz at {base_rate} sps vs "
           f"{measured[other_rates[0]]:.1f} Hz at {other_rates[0]} sps"
           if measurable else "not measured at both rates")
    if legacy_consistent:
        msg += (f" - this matches the {bt.LEGACY_DELTA_PCT:.3f}% shift the frozen "
                f"coefficient tables used to produce")

    checks = [Check(
        f"{group}.{ident}.rate_invariance", group,
        f"{label} holds across sample rates",
        clean(worst_delta) if measurable else None, rate_tol_pct, "percent",
        bool(measurable and abs(worst_delta) <= rate_tol_pct),
        skipped=not measurable, message=msg)]

    if measurable and nominal_hz:
        checks.append(Check(
            f"{group}.{ident}.nominal", group,
            f"{label} is near its expected {nominal_hz:.0f} Hz",
            clean(nominal_err), nominal_tol_pct, "percent",
            bool(np.isfinite(nominal_err) and abs(nominal_err) <= nominal_tol_pct),
            message=f"measured {base_val:.1f} Hz against {nominal_hz:.0f} Hz"))

    return row, checks


def compare_rates(per_rate: dict, rate_tol_pct: float,
                  nominal_tol_pct: float) -> tuple[dict, list]:
    """Roll every per-rate measurement up into the cross-rate verdict."""
    comparison: dict = {"corners": []}
    checks: list[Check] = []

    for ident, label, key, nominal in (
            ("hi_3db", "Transmit audio high corner (-3 dB)",
             "corner_hi_3db_hz", bt.TX_AUDIO_CORNER_3DB_HZ),
            ("hi_6db", "Transmit audio high corner (-6 dB)",
             "corner_hi_6db_hz", None),
            ("lo_3db", "Transmit audio low corner (-3 dB)",
             "corner_lo_3db_hz", bt.TX_AUDIO_LOW_CORNER_HZ)):
        row, row_checks = _compare_scalar(
            label, "corner", ident, per_rate,
            extract=lambda d, k=key: d.get("passband", {}).get(k),
            nominal_hz=nominal, rate_tol_pct=rate_tol_pct,
            # The low corner has the microphone input's AC coupling in series and
            # the -6 dB corner has no independently measured reference, so only
            # the -3 dB high corner is judged on absolute accuracy at full
            # strength.
            nominal_tol_pct=nominal_tol_pct if ident == "hi_3db" else 25.0)
        if row is not None:
            comparison["corners"].append(row)
            checks += row_checks

    # Sideband suppression is not compared for rate invariance in hertz - the
    # Hilbert table is meant to scale with Fs - but a large change in the worst
    # case between rates still says something went wrong, so it is reported.
    supp = {r: per_rate[r].get("sideband_suppression", {}).get("worst_db")
            for r in per_rate}
    if len([v for v in supp.values() if v is not None]) > 1:
        rates = sorted(supp)
        base = max(rates)
        other = [r for r in rates if r != base][0]
        comparison["sideband_suppression"] = {
            "worst_db": {str(r): clean(supp[r]) for r in rates},
            "delta_db": clean(supp[other] - supp[base]),
        }

    fold = {r: per_rate[r].get("alias", {}).get("worst_fold_dbc") for r in per_rate}
    if any(v is not None for v in fold.values()):
        comparison["fold_back"] = {"worst_dbc": {str(r): clean(fold[r])
                                                for r in sorted(fold)}}

    carrier = {r: per_rate[r].get("carrier", {}).get("worst_dbc") for r in per_rate}
    if any(v is not None for v in carrier.values()):
        comparison["carrier"] = {"worst_dbc": {str(r): clean(carrier[r])
                                              for r in sorted(carrier)}}

    return comparison, checks
