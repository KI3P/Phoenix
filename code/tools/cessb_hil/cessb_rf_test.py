#!/usr/bin/env python3
"""CESSB at the antenna connector: AD2 into the microphone, CMU200 on the output.

The exciter-level suite (``cessb_hil_test.py``) stops at the main board's I/Q
outputs. This one measures the transmitter's RF output, after the RF board's
quadrature modulator, the PA and the low-pass filters, through an attenuating
tap off the dummy load into the CMU200:

    W1        -> the radio's microphone input
    TX output -> dummy load, with a tap (``--tap-db``, 60 dB) -> CMU200 RF1
    CAT       -> /dev/ttyACM0 (single-port USB build)

Measurements: peak and average power from the CMU200's narrow-band power meter
(NPOWer, 1 MHz filter: ``max`` is PEP), and spectra from its analyzer.

1. **Ceiling at RF.** A 1 kHz tone stepped up with CESSB on: PEP rises, then
   holds. The held PEP must be the SSB power setting (through the tap), and flat.
   CESSB off is measured only at levels well below the ceiling, where it must
   agree with on (transparency): the suite never drives the transmitter past
   its power setting with CESSB off.
2. **Power tracking.** A lower power setting moves the held PEP by 10 log(P2/P1).
3. **Stimuli** (two-tone 700 + 1900 Hz, a syllable-shaped "ah"). A linear
   reference with CESSB off just below the ceiling, then CESSB on at set drives
   above it: PEP against the ceiling (overshoot after the PA), talk-power gain
   at equal PEP (PAPR reference minus PAPR on), average power in watts, and
   spectra: odd-order products, opposite sideband, out-of-channel energy.

The transmitter is keyed per section and released between them. Run into a
dummy load. Modulation, CESSB, processor level and power are read first and
restored at the end, on error and on Ctrl-C. The radio must already be on the
band to test (20 m by default; it is not changed, only checked).

Exit codes: 0 pass, 1 fail, 2 error, 3 aborted.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time
from contextlib import contextmanager

import numpy as np

_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from cessb_hil import rf as R  # noqa: E402
from cessb_hil import stimuli as S  # noqa: E402
from cessb_hil.ad2_custom import CessbAd2  # noqa: E402
from cessb_hil.cessb_hil_test import (Checks, get_cessb, get_md, get_pl, get_power,  # noqa: E402
                                      query, set_cessb, set_level_db, set_power)
from filter_hil.ad2 import DwfError  # noqa: E402
from filter_hil.radio import CatError, Radio  # noqa: E402
from tx_filter_hil.ad2 import TxAd2Config  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TONE_HZ = 1000.0
BANDS_HZ = {"160m": (1.8e6, 2.0e6), "80m": (3.5e6, 4.0e6), "60m": (5.25e6, 5.45e6),
            "40m": (7.0e6, 7.3e6), "30m": (10.1e6, 10.15e6), "20m": (14.0e6, 14.35e6),
            "17m": (18.068e6, 18.168e6), "15m": (21.0e6, 21.45e6),
            "12m": (24.89e6, 24.99e6), "10m": (28.0e6, 29.7e6)}


def get_fa(radio: Radio) -> int:
    return int(query(radio, "FA;", r"FA(\d{11});").group(1))


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cat-port", default="/dev/ttyACM0")
    p.add_argument("--cat-baud", type=int, default=38400)
    p.add_argument("--band", default="20m", choices=sorted(BANDS_HZ),
                   help="band the radio must already be on (default: %(default)s)")
    p.add_argument("--rf-input", default="RF1", choices=("RF1", "RF2", "RF4"))
    p.add_argument("--cmu-max-level", type=float, default=0.0,
                   help="CMU200 expected maximum level, dBm (default: %(default)s)")
    p.add_argument("--tap-db", type=float, default=60.0,
                   help="loss from the transmitter output to the CMU200 (default: %(default)s)")
    p.add_argument("--drives", default="0,3,6,9")
    p.add_argument("--spectrum-drives", default="6,9",
                   help="drives at which spectra are taken with CESSB on (default: %(default)s)")
    p.add_argument("--stimuli", default="two_tone,syllable_ah")
    p.add_argument("--ref-db", type=float, default=-1.0,
                   help="linear reference level, CESSB off, dB re the ceiling (default: %(default)s)")
    p.add_argument("--npower-stats", type=int, default=300,
                   help="NPOWer evaluation periods per reading (default: %(default)s)")
    p.add_argument("--min-amplitude", type=float, default=0.01)
    p.add_argument("--min-accurate-amplitude", type=float, default=0.02)
    p.add_argument("--max-amplitude", type=float, default=0.25)
    p.add_argument("--sweep-step-db", type=float, default=1.5)
    p.add_argument("--span-hz", type=float, default=7000.0,
                   help="narrow spectra cover the carrier +/- this (default: %(default)s)")
    p.add_argument("--rbw-hz", type=float, default=100.0)
    p.add_argument("--spectrum-sweeps", type=int, default=8,
                   help="sweeps averaged per spectrum. The syllable is bursty: with 4, its "
                        "out-of-channel ratio scattered by several dB between runs "
                        "(default: %(default)s)")
    p.add_argument("--wide-span-hz", type=float, default=25000.0,
                   help="wide splatter sweep, carrier +/- this; 0 skips (default: %(default)s)")
    p.add_argument("--wide-rbw-hz", type=float, default=300.0)
    p.add_argument("--settle", type=float, default=0.5)
    p.add_argument("--rest", type=float, default=5.0,
                   help="seconds unkeyed between sections (default: %(default)s)")
    p.add_argument("--no-power-check", action="store_true")
    # Limits
    p.add_argument("--max-hold-spread-db", type=float, default=0.5)
    p.add_argument("--max-ceiling-error-db", type=float, default=1.0,
                   help="held PEP vs the power setting; includes the tap's tolerance "
                        "(default: %(default)s)")
    p.add_argument("--max-power-error-db", type=float, default=0.5)
    p.add_argument("--max-transparency-db", type=float, default=0.3)
    p.add_argument("--max-overshoot-db", type=float, default=0.45,
                   help="PEP with CESSB on above the single-tone ceiling: 5 %% in voltage "
                        "(default: %(default)s)")
    p.add_argument("--min-syllable-gain-db", type=float, default=3.0)
    p.add_argument("--max-rise-db", type=float, default=3.0,
                   help="CESSB on may raise the opposite sideband, out-of-channel energy "
                        "and 2f1-f2 by at most this much over the linear reference (CESSB "
                        "off, just below the ceiling). Relative, because at RF those are "
                        "set by the PA's own intermodulation: on the KI3P prototype at 5 W "
                        "the linear two-tone already has 2f1-f2 at -30 dBc (-36 dB re PEP) "
                        "(default: %(default)s)")
    p.add_argument("--max-ooc-re-pep-db", type=float, default=-45.0,
                   help="voice stimuli: out-of-channel power with CESSB on, dB re PEP, when "
                        "no two-tone ran first. Otherwise the limit is the transmitter's own "
                        "linear two-tone figure from the same run: processed voice must "
                        "splatter less than the transmitter's standard IMD test signal "
                        "(default: %(default)s)")
    p.add_argument("--max-ooc-dbc", type=float, default=-35.0,
                   help="absolute guard on out-of-channel energy with CESSB on "
                        "(default: %(default)s)")
    p.add_argument("--out-dir", default=os.path.join(HERE, "results"))
    p.add_argument("--no-plots", action="store_true")
    p.add_argument("-q", "--quiet", action="store_true")
    return p


def run(args) -> int:
    started = time.monotonic()
    log = (lambda m="": None) if args.quiet else (lambda m="": print(m, file=sys.stderr, flush=True))
    drives = [float(d) for d in args.drives.split(",") if d.strip()]
    spec_drives = [float(d) for d in args.spectrum_drives.split(",") if d.strip()]
    wanted = [s.strip() for s in args.stimuli.split(",") if s.strip()]
    stamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(args.out_dir, exist_ok=True)
    checks = Checks(log)
    doc: dict = {"started": stamp, "args": vars(args), "warnings": [], "key_down_s": 0.0}
    restores: list = []

    def restore_all():
        while restores:
            name, fn = restores.pop()
            try:
                fn()
            except Exception as exc:
                print(f"WARNING: could not restore {name}: {exc}", file=sys.stderr)

    try:
        with CessbAd2(TxAd2Config(), log=log) as ad2, \
                Radio(args.cat_port, None, args.cat_baud, 115200, 0.5, log=log) as radio, \
                R.Cmu(args.rf_input, args.cmu_max_level) as cmu:
          try:
            log(f"CMU200: {cmu.identity()}, input {args.rf_input}, tap {args.tap_db:g} dB")
            nbuf = min(4096, ad2.custom_buffer_size())
            stim_all = S.all_stimuli(nbuf)
            unknown = [s for s in wanted if s not in stim_all]
            if unknown:
                print(f"error: unknown stimuli {unknown}", file=sys.stderr)
                return 2

            md0, pr0, pc0, fa = get_md(radio), get_cessb(radio), get_power(radio), get_fa(radio)
            rate = radio.get_sample_rate()
            lo, hi = BANDS_HZ[args.band]
            doc["radio"] = {"md": md0, "cessb": pr0, "power_w": pc0, "rate_hz": rate,
                            "carrier_hz": fa, "band": args.band}
            log(f"Radio: {fa/1e6:.6f} MHz, MD{md0}, CESSB {'on' if pr0 else 'off'}, "
                f"SSB power {pc0} W, {rate} sps")
            if md0 not in (1, 2):
                print("error: put the radio in SSB (USB or LSB) first", file=sys.stderr)
                return 2
            if not lo <= fa <= hi:
                print(f"error: the radio is on {fa/1e6:.4f} MHz, not {args.band}; "
                      f"select the band on the radio first", file=sys.stderr)
                return 2
            sign = 1 if md0 == 2 else -1
            f_meas = fa + sign * 1500.0
            p_set_dbm = 10 * math.log10(pc0 * 1000.0) - args.tap_db

            restores.append(("CESSB", lambda: radio.send(f"PR{1 if pr0 else 0};",
                                                         expect_reply=False)))
            restores.append(("power", lambda: set_power(radio, pc0)))
            pl0 = get_pl(radio)
            restores.append(("processor level", lambda: radio.send(
                f"PL{pl0[0]:03d}{pl0[1]:03d};", expect_reply=False)))
            set_level_db(radio, 0, pl0[1])
            restores.append(("transmit off", lambda: (ad2.awg_off(), radio.ptt_release())))

            @contextmanager
            def keyed():
                radio.ptt_press(1.0)
                t0 = time.monotonic()
                try:
                    yield
                finally:
                    ad2.awg_off()
                    radio.ptt_release()
                    doc["key_down_s"] += time.monotonic() - t0
                    time.sleep(args.rest)

            def npow(on: bool) -> R.NPower:
                set_cessb(radio, on, args.settle)
                return cmu.npower(f_meas, args.npower_stats)

            def spectrum(span, rbw, sweeps, detector="RMS", trace="average"):
                t0 = time.monotonic()
                f, p, _ = cmu.spectrum(fa - span, fa + span, rbw, detector=detector,
                                       ref_dbm=p_set_dbm + 13.0, sweeps=sweeps, trace=trace)
                log(f"    (spectrum +/-{span/1e3:g} kHz, RBW {rbw:g} Hz, {sweeps} sweeps, "
                    f"{detector} {trace}: {time.monotonic() - t0:.0f} s)")
                return R.rel(f, fa, sign), p

            # -- 1. ceiling ---------------------------------------------------------
            log("\n== 1. Ceiling at RF: 1 kHz tone, CESSB on ==")
            set_cessb(radio, True, args.settle)
            sweep = []
            amp, step = args.min_amplitude, 10 ** (args.sweep_step_db / 20)
            with keyed():
                flat = 0
                while amp <= args.max_amplitude * 1.0001:
                    ad2.set_tone(TONE_HZ, amp)
                    time.sleep(args.settle)
                    m = npow(True)
                    sweep.append({"amplitude_v": amp, "on_max_dbm": m.max_dbm,
                                  "on_avg_dbm": m.avg_dbm})
                    rise = m.max_dbm - sweep[-2]["on_max_dbm"] if len(sweep) > 1 else 99
                    log(f"  {amp*1000:7.2f} mV  on: PEP {m.max_dbm:7.2f} dBm, "
                        f"avg {m.avg_dbm:7.2f} dBm  (+{rise:.2f} dB)")
                    flat = flat + 1 if rise < 0.3 else 0
                    if flat >= 3:
                        break
                    amp *= step
            if flat < 3:
                checks.add("ceiling.found", "ceiling", "CESSB holds the tone at a ceiling",
                           None, None, "", False, "never flattened: raise --max-amplitude")
                return 1
            held = [p["on_max_dbm"] for p in sweep[-4:]]
            ceil_dbm = 10 * math.log10(np.mean(R.mw(held)))
            spread = max(held) - min(held)
            ceil_w = 10 ** ((ceil_dbm + args.tap_db) / 10) / 1000
            log(f"  ceiling: {ceil_dbm:.2f} dBm at the tap = {ceil_w:.2f} W PEP "
                f"(setting {pc0} W, {p_set_dbm:.2f} dBm at the tap)")
            checks.add("ceiling.flat", "ceiling", "Held PEP is flat (spread over the last 4 steps)",
                       spread, args.max_hold_spread_db, "dB", spread <= args.max_hold_spread_db)
            err = ceil_dbm - p_set_dbm
            checks.add("ceiling.is_setting", "ceiling",
                       f"Held PEP equals the power setting (through a {args.tap_db:g} dB tap)",
                       err, args.max_ceiling_error_db, "dB", abs(err) <= args.max_ceiling_error_db)

            # CESSB off, only where on is well below the ceiling (so off is too)
            log("  transparency, CESSB off at the points >= 3 dB below the ceiling:")
            with keyed():
                for p in sweep:
                    if p["on_max_dbm"] > ceil_dbm - 3.0:
                        continue
                    ad2.set_tone(TONE_HZ, p["amplitude_v"])
                    time.sleep(args.settle)
                    m = npow(False)
                    p["off_max_dbm"], p["off_avg_dbm"] = m.max_dbm, m.avg_dbm
                    log(f"  {p['amplitude_v']*1000:7.2f} mV  off avg {m.avg_dbm:7.2f} / on "
                        f"{p['on_avg_dbm']:7.2f} dBm  ({m.avg_dbm - p['on_avg_dbm']:+.2f} dB)")
            lin = [p for p in sweep if "off_avg_dbm" in p]
            acc = [p for p in lin if p["amplitude_v"] >= args.min_accurate_amplitude
                   and p["on_max_dbm"] <= ceil_dbm - 6.0]
            worst = max((abs(p["off_avg_dbm"] - p["on_avg_dbm"]) for p in acc), default=None)
            checks.add("ceiling.transparent", "ceiling",
                       "Below the ceiling CESSB on and off agree at RF (worst)",
                       worst, args.max_transparency_db, "dB",
                       worst is not None and worst <= args.max_transparency_db,
                       "" if acc else "no accurate points 6 dB below the ceiling")
            g = float(np.median([R.mw(p["off_max_dbm"]) / p["amplitude_v"] ** 2 for p in lin]))
            knee_v = math.sqrt(R.mw(ceil_dbm) / g)
            log(f"  knee at {knee_v*1000:.1f} mV into the mic input")
            doc["tone_sweep"] = sweep
            doc["ceiling"] = {"pep_dbm": ceil_dbm, "pep_w": ceil_w, "spread_db": spread,
                              "setting_dbm_at_tap": p_set_dbm, "error_db": err,
                              "knee_v": knee_v}

            # -- 2. power tracking --------------------------------------------------
            if not args.no_power_check and pc0 >= 2:
                log("\n== 2. Held PEP follows the power setting ==")
                p2 = max(1, round(pc0 * 0.4))
                ad2_amp = min(2.0 * knee_v, args.max_amplitude)
                with keyed():
                    ad2.set_tone(TONE_HZ, ad2_amp)
                    time.sleep(args.settle)
                    full = npow(True)
                    set_power(radio, p2)
                    time.sleep(args.settle)
                    part = npow(True)
                    set_power(radio, pc0)
                measured = part.max_dbm - full.max_dbm
                expected = 10 * math.log10(p2 / pc0)
                doc["power_tracking"] = {"p_full_w": pc0, "p_low_w": p2,
                                         "full_dbm": full.max_dbm, "low_dbm": part.max_dbm,
                                         "measured_db": measured, "expected_db": expected}
                log(f"  {pc0} W -> {p2} W: PEP moved {measured:+.2f} dB, expected {expected:+.2f}")
                checks.add("ceiling.tracks_power", "ceiling",
                           "Held PEP follows the power setting (error)", measured - expected,
                           args.max_power_error_db, "dB",
                           abs(measured - expected) <= args.max_power_error_db)

            # -- 3. stimuli ---------------------------------------------------------
            from cessb_hil.predict import predict
            doc["stimuli"] = {}
            for name in wanted:
                st = stim_all[name]
                log(f"\n== 3. {name} ==")
                res: dict = {"rows": [], "spectra": {}}
                with keyed():
                    a_probe = 0.5 * knee_v
                    ad2.set_custom(st.samples, st.period_hz, a_probe)
                    time.sleep(args.settle)
                    probe = npow(False)
                    r_s = math.sqrt(R.mw(probe.max_dbm)) / a_probe   # sqrt(mW) of PEP per volt
                    a_ref = math.sqrt(R.mw(ceil_dbm)) * 10 ** (args.ref_db / 20) / r_s
                    ad2.set_custom(st.samples, st.period_hz, a_ref)
                    time.sleep(args.settle)
                    ref = npow(False)
                    if ref.max_dbm > ceil_dbm + 0.5:
                        raise RuntimeError(f"linear reference PEP {ref.max_dbm:.2f} dBm is above "
                                           f"the ceiling: stopping before overdriving")
                    log(f"  reference (off): PEP {ref.max_dbm - ceil_dbm:+.2f} dB re ceiling, "
                        f"PAPR {ref.papr_db:.2f} dB, avg "
                        f"{10 ** ((ref.avg_dbm + args.tap_db) / 10) / 1000:.2f} W")
                    fr, p = spectrum(args.span_hz, args.rbw_hz, args.spectrum_sweeps)
                    res["spectra"]["ref"] = {"f": fr.tolist(), "p": p.tolist()}
                    if args.wide_span_hz > 0 and not st.tones_hz:
                        fw, pw = spectrum(args.wide_span_hz, args.wide_rbw_hz,
                                          args.spectrum_sweeps, "PEAK", "maximum")
                        res["spectra"]["ref_wide"] = {"f": fw.tolist(), "p": pw.tolist()}
                res["reference"] = _row(ref, ceil_dbm, args.tap_db, fr, p, st.tones_hz,
                                        args.rbw_hz)
                if st.tones_hz and "ooc_re_pep_db" in res["reference"]:
                    doc["two_tone_ooc_re_pep_db"] = res["reference"]["ooc_re_pep_db"]
                res["reference"]["amplitude_v"] = a_ref
                model = predict(st, rate, drives)
                res["model"] = model
                for d in drives:
                    a_d = math.sqrt(R.mw(ceil_dbm)) * 10 ** (d / 20) / r_s
                    if a_d > args.max_amplitude:
                        doc["warnings"].append(f"{name} at {d:g} dB needs {a_d*1000:.0f} mV; skipped")
                        continue
                    with keyed():
                        ad2.set_custom(st.samples, st.period_hz, a_d)
                        time.sleep(args.settle)
                        m = npow(True)
                        fr = p = None
                        if d in spec_drives:
                            fr, p = spectrum(args.span_hz, args.rbw_hz, args.spectrum_sweeps)
                            res["spectra"][f"on_{d:g}"] = {"f": fr.tolist(), "p": p.tolist()}
                            if args.wide_span_hz > 0 and not st.tones_hz and d == max(spec_drives):
                                fw, pw = spectrum(args.wide_span_hz, args.wide_rbw_hz,
                                                  args.spectrum_sweeps, "PEAK", "maximum")
                                res["spectra"][f"on_{d:g}_wide"] = {"f": fw.tolist(), "p": pw.tolist()}
                    row = _row(m, ceil_dbm, args.tap_db, fr, p, st.tones_hz, args.rbw_hz)
                    row.update(drive_db=d, amplitude_v=a_d,
                               talk_power_gain_db=res["reference"]["papr_db"] - m.papr_db,
                               model_talk_power_gain_db=model.get(str(d), {}).get("talk_power_gain_db"))
                    res["rows"].append(row)
                    msg = (f"  {d:+4.1f} dB on: PEP {row['pep_re_ceiling_db']:+.2f} dB re ceiling, "
                           f"PAPR {m.papr_db:.2f} dB, talk power {row['talk_power_gain_db']:+.2f} dB "
                           f"(model {row['model_talk_power_gain_db'] or float('nan'):+.2f}), "
                           f"avg {row['avg_w']:.2f} W")
                    if "opposite_dbc" in row:
                        msg += (f"; opposite {row['opposite_dbc']:.1f} dBc, out of channel "
                                f"{row['out_of_channel_dbc']:.1f} dBc")
                    log(msg)
                    if "imd3_low_dbc" in row:
                        log(f"         products dBc: 2f1-f2 {row['imd3_low_dbc']:.1f}, 2f2-f1 "
                            f"{row['imd3_high_dbc']:.1f}, 3f1-2f2 {row['imd5_low_dbc']:.1f}, "
                            f"3f2-2f1 {row['imd5_high_dbc']:.1f}")
                    tag = f"{name}@{d:g}dB"
                    checks.add(f"hold.{tag}", "hold", f"{tag}: PEP over the ceiling, CESSB on",
                               row["pep_re_ceiling_db"], args.max_overshoot_db, "dB",
                               row["pep_re_ceiling_db"] <= args.max_overshoot_db)
                    if "opposite_dbc" in row:
                        ref_row = res["reference"]
                        checks.add(f"ooc.{tag}", "spectrum", f"{tag}: out of channel, CESSB on",
                                   row["out_of_channel_dbc"], args.max_ooc_dbc, "dBc",
                                   row["out_of_channel_dbc"] <= args.max_ooc_dbc)
                        if st.tones_hz:
                            keys = [("out_of_channel_dbc", "out of channel"),
                                    ("imd3_low_dbc", "2f1-f2 (opposite sideband)")]
                        else:
                            # Voice: in dB re PEP against the transmitter's own two-tone
                            # IMD. Not relative to the voice's linear reference: that is
                            # within the PA's linear range, and a ratio to the wanted band
                            # power favours the denser signal.
                            keys = [("opposite_dbc", "opposite sideband")]
                            lim = doc.get("two_tone_ooc_re_pep_db", args.max_ooc_re_pep_db)
                            src = ("the linear two-tone's, this run" if
                                   "two_tone_ooc_re_pep_db" in doc else "--max-ooc-re-pep-db")
                            checks.add(f"ooc_pep.{tag}", "spectrum",
                                       f"{tag}: out-of-channel power re PEP, CESSB on "
                                       f"(limit: {src})", row["ooc_re_pep_db"], lim, "dB",
                                       row["ooc_re_pep_db"] <= lim)
                        for k, label in keys:
                            rise = row[k] - ref_row[k]
                            checks.add(f"rise.{k}.{tag}", "spectrum",
                                       f"{tag}: {label}, CESSB on minus linear reference",
                                       rise, args.max_rise_db, "dB", rise <= args.max_rise_db)
                    if name.startswith("syllable") and abs(d - 6.0) < 1e-6:
                        checks.add(f"gain.{tag}", "talk power",
                                   f"{tag}: talk-power gain at equal PEP, at RF",
                                   row["talk_power_gain_db"], args.min_syllable_gain_db, "dB",
                                   row["talk_power_gain_db"] >= args.min_syllable_gain_db)
                doc["stimuli"][name] = res
            log(f"\nKey-down time {doc['key_down_s']:.0f} s")
          finally:
            restore_all()
            doc["restored"] = True
    except (DwfError, CatError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\naborted - AWG silenced, PTT dropped and settings restored", file=sys.stderr)
        return 3

    doc["checks"] = [c.as_dict() for c in checks.items]
    doc["overall"] = checks.overall
    doc["duration_s"] = time.monotonic() - started
    base = os.path.join(args.out_dir, f"cessb_rf_{stamp}")
    with open(base + ".json", "w") as fh:
        json.dump(doc, fh, indent=1, default=lambda o: o.item() if hasattr(o, "item") else str(o))
    from cessb_hil import rf_report
    pngs = []
    if not args.no_plots:
        try:
            pngs = rf_report.plots(doc, base)
        except Exception as exc:
            doc["warnings"].append(f"plots failed: {exc}")
    with open(base + ".md", "w") as fh:
        fh.write(rf_report.markdown(doc, pngs))
    n_fail = sum(not c.passed for c in checks.items)
    print(f"\n{doc['overall']}: {len(checks.items) - n_fail}/{len(checks.items)} checks "
          f"passed. Report: {base}.md", file=sys.stderr)
    return 0 if doc["overall"] == "PASS" else 1


def _row(m: R.NPower, ceil_dbm: float, tap_db: float, fr, p, tones, rbw_hz: float) -> dict:
    row = {"pep_dbm": m.max_dbm, "avg_dbm": m.avg_dbm, "papr_db": m.papr_db,
           "pep_re_ceiling_db": m.max_dbm - ceil_dbm,
           "avg_w": 10 ** ((m.avg_dbm + tap_db) / 10) / 1000,
           "pep_w": 10 ** ((m.max_dbm + tap_db) / 10) / 1000}
    if fr is not None:
        s = R.spectrum_stats(fr, p, rbw_hz=rbw_hz)
        row.update(opposite_dbc=s.opposite_dbc, out_of_channel_dbc=s.out_of_channel_dbc,
                   carrier_dbc=s.carrier_dbc, worst_ooc_dbc=s.worst_ooc_dbc,
                   ooc_re_pep_db=R.db10(s.ooc_mw) - ceil_dbm)
        if tones:
            row.update(R.two_tone_rf(fr, p, *tones))
    return row


def main() -> int:
    return run(build_parser().parse_args())


if __name__ == "__main__":
    sys.exit(main())
