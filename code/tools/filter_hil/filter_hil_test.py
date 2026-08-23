#!/usr/bin/env python3
"""Verify the Phoenix receive filters hold their frequencies across sample rates.

Injects a quadrature tone into the radio's I/Q receive inputs with an Analog
Discovery 2, reads the demodulated audio back off the speaker output, and
measures where each filter actually sits - at every sample rate under test.

    W1, W2      -> the radio's I and Q receive inputs (order is auto-detected)
    Scope Ch1   -> the speaker output
    /dev/ttyACM1 -> CAT control at 38400
    /dev/ttyACM0 -> diagnostic port at 115200 (needed for the ED settings dump)

Exit codes: 0 pass, 1 fail, 2 error (no hardware, or preflight refused to run),
3 aborted (radio state may be dirty).

See README.md for wiring detail and how to read the output.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

import numpy as np

# Allow running as a script from anywhere, not only as a package module.
if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from filter_hil import bandtable as bt
    from filter_hil import report as report_mod
    from filter_hil import tests as T
    from filter_hil.ad2 import Ad2, Ad2Config, DwfError
    from filter_hil.measure import geom_grid
    from filter_hil.radio import (CatError, EdSnapshot, Radio, RadioStateGuard,
                                  MOD_AM, MOD_NAMES)
else:
    from . import bandtable as bt
    from . import report as report_mod
    from . import tests as T
    from .ad2 import Ad2, Ad2Config, DwfError
    from .measure import geom_grid
    from .radio import (CatError, EdSnapshot, Radio, RadioStateGuard,
                        MOD_AM, MOD_NAMES)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ALL_TESTS = ("iq", "level", "map", "ref", "cw", "eq", "ssb", "am")
DEFAULT_TESTS = ("iq", "level", "map", "ref", "cw", "eq", "ssb")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    g = p.add_argument_group("selection")
    g.add_argument("--rates", default="192000,176400",
                   help="sample rates to test, in order (default: %(default)s)")
    g.add_argument("--tests", default=",".join(DEFAULT_TESTS),
                   help=f"tests to run, from {','.join(ALL_TESTS)} (default: %(default)s)")
    g.add_argument("--cw-filters", default="0,1,2,3,4",
                   help="CW filter indices to measure (default: %(default)s)")
    g.add_argument("--eq-cells", default="all",
                   help="equaliser cells to measure, or 'all' (default: %(default)s)")
    g.add_argument("--fw-widths", default="1800,2400,2800,3000",
                   help="SSB filter widths to check, Hz (default: %(default)s)")

    g = p.add_argument_group("serial")
    g.add_argument("--cat-port", default="/dev/ttyACM1")
    g.add_argument("--diag-port", default="/dev/ttyACM0")
    g.add_argument("--cat-baud", type=int, default=38400)
    g.add_argument("--diag-baud", type=int, default=115200)
    g.add_argument("--cat-timeout", type=float, default=0.5)

    g = p.add_argument_group("analog discovery")
    g.add_argument("--scope-rate", type=float, default=96000.0,
                   help="scope sample rate, Hz. Record mode gets unreliable "
                        "above ~250 kHz (default: %(default)s)")
    g.add_argument("--capture", type=float, default=0.25,
                   help="seconds per capture (default: %(default)s)")
    g.add_argument("--scope-range", type=float, default=5.0,
                   help="scope input range, volts. The speaker output needs "
                        "headroom for the receiver noise (default: %(default)s)")
    g.add_argument("--awg-offset", type=float, default=0.0)
    g.add_argument("--force-phase", type=int, choices=(1, -1), default=None,
                   help="skip quadrature detection and force this sense")
    g.add_argument("--force-sideband", type=int, choices=(1, -1), default=None,
                   help="force the sideband: +1 takes audio from Fs/4 + f (USB), "
                        "-1 from Fs/4 - f (LSB). Normally detected.")

    g = p.add_argument_group("drive level")
    g.add_argument("--start-amplitude", type=float, default=0.010,
                   help="initial AWG amplitude, volts peak (default: %(default)s)")
    g.add_argument("--max-amplitude", type=float, default=1.0,
                   help="never drive the radio harder than this, volts peak. "
                        "Measured on the bench the recovered tone sits only "
                        "15-20 dB over the receiver's own noise even here, so "
                        "lowering it costs dynamic range (default: %(default)s)")
    g.add_argument("--amp-step-db", type=float, default=3.0)
    g.add_argument("--min-snr", type=float, default=12.0,
                   help="a point below this is not used in fits. The rig is "
                        "limited by the receiver noise, not the scope "
                        "(default: %(default)s)")
    g.add_argument("--max-thd", type=float, default=-40.0)
    g.add_argument("--min-image-rejection", type=float, default=15.0)
    g.add_argument("--volume", type=int, default=30,
                   help="AF volume percent for the run. The audio output rises "
                        "very steeply with this: 54 clipped a 5 V scope range "
                        "outright (default: %(default)s)")

    g = p.add_argument_group("timing")
    g.add_argument("--settle", type=float, default=0.20,
                   help="seconds after changing the stimulus (default: %(default)s)")
    g.add_argument("--rate-settle", type=float, default=2.0,
                   help="seconds after a sample rate change (default: %(default)s)")
    g.add_argument("--eq-points", type=int, default=9)
    g.add_argument("--grid-points", type=int, default=31)
    g.add_argument("--cw-tol-hz", type=float, default=5.0)

    g = p.add_argument_group("tolerances")
    g.add_argument("--rate-tol-pct", type=float, default=1.5,
                   help="primary criterion: how far a frequency may move between "
                        "rates (default: %(default)s)")
    g.add_argument("--nominal-tol-pct", type=float, default=4.0,
                   help="secondary: distance from the labelled frequency "
                        "(default: %(default)s)")
    g.add_argument("--am-depth", type=float, default=50.0)

    g = p.add_argument_group("output")
    g.add_argument("--out-dir", default=None,
                   help="default: results/ next to this script")
    g.add_argument("--prefix", default="filter_hil")
    g.add_argument("--json", default=None)
    g.add_argument("--markdown", default=None)
    g.add_argument("--no-plots", action="store_true")
    g.add_argument("-v", "--verbose", action="store_true")
    g.add_argument("-q", "--quiet", action="store_true")

    g = p.add_argument_group("safety")
    g.add_argument("--allow-agc-on", action="store_true",
                   help="run even with AGC engaged. It compresses the very "
                        "differences being measured; results will be unreliable")
    g.add_argument("--no-restore", action="store_true",
                   help="leave the radio in the test configuration")
    g.add_argument("--dry-run", action="store_true",
                   help="validate arguments and the report path without hardware")
    return p


def _int_list(text: str, upper: int) -> list:
    if text.strip().lower() == "all":
        return list(range(upper))
    return [int(x) for x in text.split(",") if x.strip()]


def run(args, argv) -> int:
    started = time.monotonic()
    quiet = args.quiet

    def log(msg: str = "") -> None:
        if not quiet:
            print(msg, file=sys.stderr)

    out_dir = args.out_dir or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                           "results")
    stamp = time.strftime("%Y%m%d_%H%M%S")
    json_path = args.json or os.path.join(out_dir, f"{args.prefix}_{stamp}.json")
    md_path = args.markdown or os.path.join(out_dir, f"{args.prefix}_{stamp}.md")

    rates = [int(r) for r in args.rates.split(",") if r.strip()]
    selected = [t.strip() for t in args.tests.split(",") if t.strip()]
    cw_indices = _int_list(args.cw_filters, len(bt.CW_FILTER_NOMINAL_HZ))
    eq_cells = _int_list(args.eq_cells, bt.EQ_CELL_COUNT)
    fw_widths = [int(x) for x in args.fw_widths.split(",") if x.strip()]

    for r in rates:
        if r not in bt.SAMPLE_RATES_HZ:
            print(f"error: {r} is not a CAT-selectable sample rate "
                  f"(choose from {bt.SAMPLE_RATES_HZ})", file=sys.stderr)
            return 2

    if args.scope_rate > 250000:
        log("WARNING: scope rates above 250 kHz drop samples in record mode.")

    config = {k: v for k, v in vars(args).items()}
    config["resolved"] = {"json": json_path, "markdown": md_path,
                          "rates": rates, "tests": selected}

    if args.dry_run:
        doc = report_mod.build_document(
            provenance=report_mod.collect_provenance(argv, REPO_ROOT),
            config=config, rig={}, baseline={}, final={}, state_restored=True,
            residual_diff={}, per_rate={}, comparison={}, checks=[],
            duration_s=time.monotonic() - started, warnings=["dry run: no hardware touched"])
        report_mod.write_json(doc, json_path)
        report_mod.write_markdown(doc, md_path)
        print(f"Dry run OK. Wrote {json_path} and {md_path}")
        return 0

    warnings: list[str] = []
    per_rate: dict = {}
    all_checks: list = []
    rig: dict = {}
    baseline_summary: dict = {}
    final_summary: dict = {}
    residual: dict = {}
    restored = False

    cfg = Ad2Config(scope_rate_hz=args.scope_rate, capture_s=args.capture,
                    scope_range_v=args.scope_range, awg_offset_v=args.awg_offset)

    try:
        with Ad2(cfg, log=log) as ad2, Radio(
                args.cat_port, args.diag_port, args.cat_baud, args.diag_baud,
                args.cat_timeout, log=log) as radio:

            ad2.configure_scope()

            log("\n== Preflight ==")
            pre, ed = T.run_preflight(radio, ad2, args.allow_agc_on, log)
            all_checks += pre.checks
            baseline_summary = dict(pre.data["ed"])
            baseline_summary["id"] = pre.data["id"]

            if not ed.agc_off:
                if not args.allow_agc_on:
                    print("\nerror: AGC is on. It compresses the amplitude "
                          "differences this suite measures, so every filter skirt "
                          "would read flat. Turn AGC off in the radio's menu, or "
                          "pass --allow-agc-on to proceed anyway.", file=sys.stderr)
                    return 2
                warnings.append("AGC was ON - every measurement here is unreliable")

            if ed.sample_rate_hz not in bt.SAMPLE_RATES_HZ:
                print(f"error: radio reports sample rate {ed.sample_rate_hz}, which "
                      f"this suite cannot restore. Set it from the menu first.",
                      file=sys.stderr)
                return 2

            with RadioStateGuard(radio, ed, log=log) as guard:
                if args.no_restore:
                    guard.restored = True  # registered restores will be skipped

                # Register undo before changing anything.
                guard.on_restore("sample rate",
                                 lambda r=ed.sample_rate_hz: radio.set_sample_rate(
                                     r, args.rate_settle))
                guard.on_restore("CW filter",
                                 lambda v=ed.cw_filter_index: radio.set_cw_filter(v))
                guard.on_restore("equaliser",
                                 lambda v=list(ed.equalizer_rec): radio.set_eq_all(v))
                guard.on_restore("noise reduction",
                                 lambda v=ed.nr_option: radio.set_nr(v))
                guard.on_restore("notch", lambda v=ed.notch_on: radio.set_notch(v))
                guard.on_restore("volume",
                                 lambda v=ed.audio_volume: radio.set_volume_pct(v))
                mod0 = ed.modulation[ed.active_vfo]
                guard.on_restore("modulation", lambda m=mod0: radio.set_modulation(m))

                # A quiet, unprocessed baseline for measurement.
                radio.set_nr(0)
                radio.set_notch(0)
                if args.volume is not None:
                    radio.set_volume_pct(args.volume)

                try:
                    baseline_hi = radio.get_filter_hi_hz()
                except CatError:
                    baseline_hi = max(fw_widths)
                    warnings.append("could not read the filter bandwidth back "
                                    "(FW;); it will be restored to "
                                    f"{baseline_hi} Hz")
                guard.on_restore("filter bandwidth",
                                 lambda v=baseline_hi: radio.set_filter_hi_hz(v))

                # The fine tune shifts the demodulation centre away from Fs/4,
                # often by kilohertz. Without it every injection misses the
                # passband entirely.
                plan = T.InjectionPlan(rate_hz=ed.sample_rate_hz,
                                       fine_tune_hz=ed.active_fine_tune_hz,
                                       amplitude_v=args.start_amplitude)
                log(f"Demodulation centre: |Fs/4 {bt.fs_over_4_hz(ed.sample_rate_hz):.0f} "
                    f"{ed.active_fine_tune_hz:+.0f} fine tune| = {plan.centre_hz:.0f} Hz")

                # -- rig characterisation, once, at the starting rate ------
                phase_sign = args.force_phase or 1
                sideband_sign = args.force_sideband or 1
                if "iq" in selected:
                    log("\n== Detecting the wiring order and sideband ==")
                    res, phase_sign, sideband_sign = T.detect_iq_order(
                        ad2, plan, args.settle, args.min_image_rejection,
                        args.force_phase, args.max_amplitude, log)
                    all_checks += res.checks
                    rig.update(res.data)
                    if not res.checks[0].passed:
                        log("Cannot proceed without a usable quadrature sense.")
                        return 1
                plan.phase_sign = phase_sign
                plan.sideband_sign = sideband_sign

                amplitude = args.start_amplitude
                if "level" in selected:
                    log("\n== Choosing a drive level ==")
                    res, amplitude = T.autolevel(
                        ad2, plan, args.settle, args.start_amplitude,
                        args.max_amplitude, args.amp_step_db, args.min_snr,
                        args.max_thd, log)
                    all_checks += res.checks
                    if not res.checks[0].passed:
                        warnings.append(res.checks[0].message)
                plan.amplitude_v = amplitude

                rig.update({
                    "drive_amplitude_v": amplitude,
                    "scope_rate_hz": args.scope_rate,
                    "capture_s": args.capture,
                    "scope_range_v": args.scope_range,
                })

                grid = geom_grid(120.0, 4400.0, args.grid_points)

                # -- per rate ----------------------------------------------
                for rate in rates:
                    log(f"\n===== {rate} sps =====")
                    if rate != radio.get_sample_rate():
                        radio.set_sample_rate(rate, args.rate_settle)
                    plan.rate_hz = rate
                    plan.fine_tune_hz = radio.dump_ed().active_fine_tune_hz
                    plan.sidetone_hz = 0.0
                    plan.mapping_correction_hz = 0.0

                    bucket: dict = {
                        "rate_hz": rate,
                        "fs_over_4_hz": bt.fs_over_4_hz(rate),
                        "audio_rate_hz": bt.audio_rate_hz(rate),
                    }

                    inj = T.Injector(ad2, plan, args.settle, args.min_snr, args.max_thd)

                    if "map" in selected:
                        log("-- Fs/4 mapping --")
                        res = T.calibrate_mapping(ad2, plan, args.settle, log)
                        all_checks += res.checks
                        bucket["mapping"] = res.data
                        if not all(c.passed for c in res.checks):
                            log("Mapping failed; skipping the rest of this rate.")
                            bucket["skipped"] = "mapping_failed"
                            per_rate[str(rate)] = bucket
                            continue

                    reference = None
                    if "ref" in selected:
                        log("-- Reference sweep --")
                        reference = T.reference_sweep(inj, grid, log)
                        all_checks += reference.checks
                        bucket["reference_sweep"] = reference.data

                    if "ssb" in selected:
                        log("-- SSB filter (control) --")
                        res = T.test_ssb_filter(radio, inj, fw_widths, baseline_hi, log)
                        all_checks += res.checks
                        bucket["ssb_filter"] = res.data

                    if "eq" in selected and reference is not None:
                        log("-- Equaliser cells --")
                        res = T.test_equalizer_cells(radio, inj, reference,
                                                     eq_cells, args.eq_points,
                                                     baseline_hi, log)
                        all_checks += res.checks
                        bucket["equalizer_cells"] = res.data

                    if "cw" in selected:
                        log("-- CW audio filters --")
                        radio.enter_cw()
                        time.sleep(0.5)
                        # Bypass the filter under test before calibrating, or the
                        # mapping probes are measured through it and the ones
                        # outside its passband come back as noise.
                        radio.set_cw_filter(bt.CW_FILTER_OFF)
                        plan.sidetone_hz = radio.sidetone_shift_hz(ed, in_cw=True)
                        plan.mapping_correction_hz = 0.0
                        cw_map = T.calibrate_mapping(ad2, plan, args.settle, log)
                        bucket["cw_mapping"] = cw_map.data
                        if all(c.passed for c in cw_map.checks):
                            res = T.test_cw_filters(radio, inj, cw_indices,
                                                    args.cw_tol_hz, baseline_hi,
                                                    log)
                            all_checks += res.checks
                            bucket["cw_filters"] = res.data
                        else:
                            all_checks += cw_map.checks
                            log("CW mapping failed; skipping the CW filters.")
                        radio.set_modulation(mod0)
                        time.sleep(0.5)
                        plan.sidetone_hz = 0.0
                        plan.mapping_correction_hz = 0.0

                    if "am" in selected:
                        if ed.modulation[ed.active_vfo] == MOD_AM or True:
                            log("-- AM DC blocker --")
                            radio.set_modulation(MOD_AM)
                            time.sleep(0.5)
                            am_plan = T.InjectionPlan(
                                rate_hz=rate, fine_tune_hz=plan.fine_tune_hz,
                                phase_sign=plan.phase_sign,
                                sideband_sign=plan.sideband_sign,
                                amplitude_v=plan.amplitude_v)
                            res = T.test_am_dc_blocker(
                                ad2, am_plan, args.settle, args.am_depth,
                                args.min_snr, log)
                            all_checks += res.checks
                            bucket["am_dc_blocker"] = res.data
                            radio.set_modulation(mod0)
                            time.sleep(0.5)

                    per_rate[str(rate)] = bucket

                # -- cross-rate comparison ---------------------------------
                log("\n== Comparing rates ==")
                comparison, cmp_checks = T.compare_rates(
                    per_rate, args.rate_tol_pct, args.nominal_tol_pct)
                all_checks += cmp_checks

                ad2.awg_off()

            restored = guard.restored and not args.no_restore
            if not args.no_restore:
                residual = guard.verify()
                try:
                    final_summary = radio.dump_ed().summary()
                except CatError:
                    final_summary = {}
                if guard.failures:
                    warnings += guard.failures
            else:
                warnings.append("--no-restore: the radio was left in the test "
                                "configuration")

    except DwfError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except CatError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\naborted - the AWG has been silenced, but the radio may still be "
              "in the test configuration", file=sys.stderr)
        return 3

    doc = report_mod.build_document(
        provenance=report_mod.collect_provenance(argv, REPO_ROOT,
                                                 dwf_version=cfg and ""),
        config=config, rig=rig, baseline=baseline_summary, final=final_summary,
        state_restored=restored, residual_diff=residual, per_rate=per_rate,
        comparison=comparison, checks=all_checks,
        duration_s=time.monotonic() - started, warnings=warnings)

    report_mod.write_json(doc, json_path)
    doc["artifacts"]["json"] = json_path

    png_paths: list = []
    if not args.no_plots:
        try:
            if __package__ in (None, ""):
                from filter_hil.plot_filter_hil import render_all
            else:
                from .plot_filter_hil import render_all
            png_paths = render_all(doc, out_dir, f"{args.prefix}_{stamp}")
            doc["artifacts"]["png"] = png_paths
        except Exception as exc:
            warnings.append(f"could not render plots: {exc}")
            doc["warnings"] = warnings

    report_mod.write_markdown(doc, md_path, png_paths)
    doc["artifacts"]["markdown"] = md_path
    report_mod.write_json(doc, json_path)

    report_mod.print_summary(doc)
    return 0 if doc["summary"]["overall"] == "PASS" else 1


def main() -> int:
    argv = sys.argv[1:]
    args = build_parser().parse_args(argv)
    return run(args, ["filter_hil_test.py"] + argv)


if __name__ == "__main__":
    sys.exit(main())
