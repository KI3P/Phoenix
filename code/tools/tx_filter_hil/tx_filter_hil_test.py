#!/usr/bin/env python3
"""Verify the Phoenix transmit filters hold their frequencies across sample rates.

Injects a tone into the radio's microphone input with an Analog Discovery 2,
captures the exciter's I and Q outputs synchronously, and measures where the
transmit audio filters actually sit - at every sample rate under test.

    W1          -> the radio's microphone input
    Scope Ch1   -> exciter I output   (order is auto-detected)
    Scope Ch2   -> exciter Q output
    /dev/ttyACM1 -> CAT control at 38400
    /dev/ttyACM0 -> diagnostic port at 115200 (needed for the ED settings dump)

The radio is keyed over CAT for the duration of each sweep. Nothing here limits
RF output, so run it with the exciter feeding the scope only, or with a dummy
load fitted.

Exit codes: 0 pass, 1 fail, 2 error (no hardware, or preflight refused to run),
3 aborted (radio may be left keyed - the suite unkeys on the way out, but check).

See README.md for wiring detail and how to read the output.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

import numpy as np

# Allow running as a script from anywhere, not only as a package module. The
# parent directory has to be on the path either way: this suite imports the CAT
# plumbing and the scalar curve fitting from the sibling filter_hil package.
_TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

if __package__ in (None, ""):
    from tx_filter_hil import bandtable as bt
    from tx_filter_hil import report as report_mod
    from tx_filter_hil import tests as T
    from tx_filter_hil.ad2 import DwfError, TxAd2, TxAd2Config
    from tx_filter_hil.measure import geom_grid
else:
    from . import bandtable as bt
    from . import report as report_mod
    from . import tests as T
    from .ad2 import DwfError, TxAd2, TxAd2Config
    from .measure import geom_grid

from filter_hil.radio import (MOD_LSB, MOD_USB, CatError, Radio,  # noqa: E402
                              RadioStateGuard)

REPO_ROOT = os.path.dirname(os.path.dirname(_TOOLS_DIR))
ALL_TESTS = ("wiring", "level", "passband", "sideband", "alias", "carrier")
DEFAULT_TESTS = ALL_TESTS


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    g = p.add_argument_group("selection")
    g.add_argument("--rates", default="192000,176400",
                   help="sample rates to test, in order (default: %(default)s)")
    g.add_argument("--tests", default=",".join(DEFAULT_TESTS),
                   help=f"tests to run, from {','.join(ALL_TESTS)} "
                        f"(default: %(default)s)")
    g.add_argument("--modulation", choices=("usb", "lsb"), default="usb",
                   help="sideband to transmit on for the run (default: %(default)s)")

    g = p.add_argument_group("serial")
    g.add_argument("--cat-port", default="/dev/ttyACM1")
    g.add_argument("--diag-port", default="/dev/ttyACM0")
    g.add_argument("--cat-baud", type=int, default=38400)
    g.add_argument("--diag-baud", type=int, default=115200)
    g.add_argument("--cat-timeout", type=float, default=0.5)

    g = p.add_argument_group("analog discovery")
    g.add_argument("--scope-rate", type=float, default=100000.0,
                   help="scope sample rate, Hz. Deliberately not a divisor of the "
                        "radio's 192 kHz output rate, so DAC images do not alias "
                        "onto the measured tone (default: %(default)s)")
    g.add_argument("--capture", type=float, default=0.25,
                   help="seconds per capture (default: %(default)s)")
    g.add_argument("--scope-range", type=float, default=5.0,
                   help="scope input range, volts peak to peak (default: %(default)s)")
    g.add_argument("--scope-offset", type=float, default=1.6,
                   help="scope input offset, volts. The exciter outputs sit on a "
                        "DC bias near this (default: %(default)s)")
    g.add_argument("--awg-offset", type=float, default=0.0)
    g.add_argument("--force-swap", choices=("yes", "no"), default=None,
                   help="skip wiring detection and force whether Ch1 is on Q")
    g.add_argument("--force-sideband", type=int, choices=(1, -1), default=None,
                   help="force which side of DC the wanted energy is on")

    g = p.add_argument_group("drive level")
    g.add_argument("--start-amplitude", type=float, default=0.005,
                   help="initial AWG amplitude into the mic input, volts peak "
                        "(default: %(default)s)")
    g.add_argument("--max-amplitude", type=float, default=0.25,
                   help="never drive the microphone input harder than this, volts "
                        "peak. It is a millivolt-level port with a preamplifier "
                        "in front of it (default: %(default)s)")
    g.add_argument("--amp-step-db", type=float, default=3.0)
    g.add_argument("--min-snr", type=float, default=25.0,
                   help="a point below this is not used in fits (default: %(default)s)")
    g.add_argument("--max-thd", type=float, default=-30.0,
                   help="stop raising the drive once distortion passes this "
                        "(default: %(default)s)")
    g.add_argument("--mic-gain", type=float, default=None,
                   help="set the radio's mic gain to this many dB for the run, "
                        "-40 to +30. Default: leave it alone")

    g = p.add_argument_group("timing")
    g.add_argument("--settle", type=float, default=0.25,
                   help="seconds after changing the tone (default: %(default)s)")
    g.add_argument("--rate-settle", type=float, default=2.0,
                   help="seconds after a sample rate change (default: %(default)s)")
    g.add_argument("--ptt-settle", type=float, default=1.0,
                   help="seconds after keying before the first capture "
                        "(default: %(default)s)")
    g.add_argument("--passband-points", type=int, default=41,
                   help="points in the passband sweep, 100 Hz to 4 kHz "
                        "(default: %(default)s)")
    g.add_argument("--alias-points", type=int, default=17,
                   help="points in the out-of-band sweep (default: %(default)s)")

    g = p.add_argument_group("tolerances")
    g.add_argument("--rate-tol-pct", type=float, default=2.5,
                   help="primary criterion: how far a corner may move between "
                        "rates. Looser than the receive suite's 1.5 because both "
                        "generated transmit stages are 48-tap Kaiser designs, "
                        "which do not scale exactly: evaluating the tap sets "
                        "directly gives 1.2%% of movement on a correct radio, and "
                        "the firmware's own offline test allows 2%%. The bug being "
                        "hunted is -8.125%% (default: %(default)s)")
    g.add_argument("--nominal-tol-pct", type=float, default=8.0,
                   help="secondary: distance from the offline stage measurement. "
                        "Loose because the equaliser bank and the codec front end "
                        "are in series and cannot be removed (default: %(default)s)")
    g.add_argument("--min-suppression", type=float, default=25.0,
                   help="required opposite-sideband suppression across the "
                        "passband, dB (default: %(default)s)")
    g.add_argument("--min-oob-rejection", type=float, default=30.0,
                   help="required rejection of microphone audio between the "
                        "stopband boundary and the fold point, dB. The design "
                        "predicts about 52 dB there, so this leaves margin "
                        "against a regression while staying clear of the rig's "
                        "own noise floor (default: %(default)s)")
    g.add_argument("--max-fold-dbc", type=float, default=-50.0,
                   help="loudest fold-back product allowed, dBc (default: %(default)s)")
    g.add_argument("--max-carrier-dbc", type=float, default=-20.0,
                   help="loudest carrier residue allowed, dBc (default: %(default)s)")

    g = p.add_argument_group("output")
    g.add_argument("--out-dir", default=None,
                   help="default: results/ next to this script")
    g.add_argument("--prefix", default="tx_filter_hil")
    g.add_argument("--json", default=None)
    g.add_argument("--markdown", default=None)
    g.add_argument("--no-plots", action="store_true")
    g.add_argument("-v", "--verbose", action="store_true")
    g.add_argument("-q", "--quiet", action="store_true")

    g = p.add_argument_group("safety")
    g.add_argument("--no-restore", action="store_true",
                   help="leave the radio in the test configuration")
    g.add_argument("--dry-run", action="store_true",
                   help="validate arguments and the report path without hardware")
    return p


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
    modulation = MOD_USB if args.modulation == "usb" else MOD_LSB

    for r in rates:
        if r not in bt.SAMPLE_RATES_HZ:
            print(f"error: {r} is not a CAT-selectable sample rate "
                  f"(choose from {bt.SAMPLE_RATES_HZ})", file=sys.stderr)
            return 2
    unknown = [t for t in selected if t not in ALL_TESTS]
    if unknown:
        print(f"error: unknown test(s) {', '.join(unknown)}; choose from "
              f"{', '.join(ALL_TESTS)}", file=sys.stderr)
        return 2
    if args.mic_gain is not None and not -40.0 <= args.mic_gain <= 30.0:
        print(f"error: --mic-gain {args.mic_gain} is outside the -40 to +30 dB "
              f"the MG command covers", file=sys.stderr)
        return 2

    if args.scope_rate > 250000:
        log("WARNING: scope rates above 250 kHz drop samples in record mode.")
    if abs(192000.0 % args.scope_rate) < 1.0 or abs(176400.0 % args.scope_rate) < 1.0:
        log(f"WARNING: {args.scope_rate:.0f} Hz divides one of the radio's output "
            f"rates. Exciter DAC images will alias onto the measured tone and "
            f"limit the sideband suppression that can be read.")

    config = {k: v for k, v in vars(args).items()}
    config["resolved"] = {"json": json_path, "markdown": md_path,
                          "rates": rates, "tests": selected}

    if args.dry_run:
        doc = report_mod.build_tx_document(
            provenance=report_mod.collect_provenance(argv, REPO_ROOT),
            config=config, rig={}, baseline={}, final={}, state_restored=True,
            residual_diff={}, per_rate={}, comparison={}, checks=[],
            duration_s=time.monotonic() - started,
            warnings=["dry run: no hardware touched"])
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
    key_down_s = 0.0

    cfg = TxAd2Config(scope_rate_hz=args.scope_rate, capture_s=args.capture,
                      scope_range_v=args.scope_range,
                      scope_offset_v=args.scope_offset,
                      awg_offset_v=args.awg_offset)

    try:
        with TxAd2(cfg, log=log) as ad2, Radio(
                args.cat_port, args.diag_port, args.cat_baud, args.diag_baud,
                args.cat_timeout, log=log) as radio:

            ad2.configure_scope()

            log("\n== Preflight ==")
            pre, ed = T.run_preflight(radio, log)
            all_checks += pre.checks
            baseline_summary = dict(pre.data["ed"])
            baseline_summary["id"] = pre.data["id"]

            if ed.sample_rate_hz not in bt.SAMPLE_RATES_HZ:
                print(f"error: radio reports sample rate {ed.sample_rate_hz}, which "
                      f"this suite cannot restore. Set it from the menu first.",
                      file=sys.stderr)
                return 2

            keyed = False
            key_down_start = 0.0

            def key() -> None:
                nonlocal keyed, key_down_start
                if not keyed:
                    radio.ptt_press(args.ptt_settle)
                    key_down_start = time.monotonic()
                    keyed = True

            def unkey() -> None:
                """Drop PTT and account for the time spent transmitting."""
                nonlocal keyed, key_down_s
                if keyed:
                    key_down_s += time.monotonic() - key_down_start
                    keyed = False
                radio.ptt_release()

            with RadioStateGuard(radio, ed, log=log) as guard:
                if args.no_restore:
                    guard.restored = True  # registered restores will be skipped

                guard.on_restore("sample rate",
                                 lambda r=ed.sample_rate_hz: radio.set_sample_rate(
                                     r, args.rate_settle))
                mod0 = ed.active_modulation
                guard.on_restore("modulation", lambda m=mod0: radio.set_modulation(m))
                if args.mic_gain is not None:
                    guard.on_restore(
                        "mic gain",
                        lambda v=ed.mic_gain_db: radio.set_mic_gain_pct(
                            Radio.mic_gain_pct_from_db(v)))
                    radio.set_mic_gain_pct(Radio.mic_gain_pct_from_db(args.mic_gain))
                    log(f"Mic gain set to {args.mic_gain:.0f} dB "
                        f"(was {ed.mic_gain_db} dB)")

                # Registered last so it runs *first*: RadioStateGuard replays its
                # restores newest to oldest, and every restore above assumes the
                # radio is receiving. Changing the sample rate mid-transmission in
                # particular reconfigures the I2S clock under a running chain.
                guard.on_restore("transmit off", unkey)

                # TXGain scales the exciter output by a factor derived from the
                # requested power, so a power change mid-run would move every
                # level measured. It is read, reported and left alone.
                power_w = ed.active_power_ssb
                log(f"Requested SSB power {power_w:.0f} W - left untouched; "
                    f"TXGain derives the exciter drive from it")

                radio.set_modulation(modulation)
                time.sleep(0.5)

                passband_grid = geom_grid(100.0, 4400.0, args.passband_points)

                # -- rig characterisation, once, at the starting rate ------
                key()
                swap = (args.force_swap == "yes") if args.force_swap else False
                sideband_sign = args.force_sideband or 1
                plan = T.TxPlan(rate_hz=ed.sample_rate_hz, modulation=modulation,
                                swap=swap, sideband_sign=sideband_sign,
                                amplitude_v=args.start_amplitude)

                if "wiring" in selected:
                    log("\n== Detecting the wiring order and sideband ==")
                    res, swap, sideband_sign = T.detect_wiring(
                        ad2, radio, plan, args.settle, args.min_suppression,
                        args.max_amplitude, modulation, log)
                    all_checks += res.checks
                    rig.update(res.data)
                    fatal = next((c for c in res.checks
                                  if c.id == "rig.suppression"), None)
                    if fatal is not None and not fatal.passed:
                        log("Cannot proceed without both exciter outputs "
                            "connected in quadrature.")
                        ad2.awg_off()
                        return 1
                plan.swap, plan.sideband_sign = swap, sideband_sign
                plan.modulation = modulation

                amplitude = args.start_amplitude
                if "level" in selected:
                    log("\n== Choosing a microphone drive level ==")
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
                    "scope_offset_v": args.scope_offset,
                    "power_out_ssb_w": power_w,
                    "mic_gain_db": (args.mic_gain if args.mic_gain is not None
                                    else ed.mic_gain_db),
                    "modulation": args.modulation,
                })

                # -- per rate ----------------------------------------------
                for rate in rates:
                    log(f"\n===== {rate} sps =====")
                    if rate != radio.get_sample_rate():
                        # ChangeSampleRate() reconfigures the I2S clock and
                        # rebuilds every filter. Doing that mid-transmission is
                        # not something the firmware is asked to survive, so drop
                        # PTT around it.
                        unkey()
                        radio.set_sample_rate(rate, args.rate_settle)
                    plan.rate_hz = rate
                    key()

                    bucket: dict = {
                        "rate_hz": rate,
                        "audio_rate_hz": bt.audio_rate_hz(rate),
                        "hilbert_rate_hz": bt.hilbert_rate_hz(rate),
                        "fold_hz": bt.fold_frequency_hz(rate),
                    }
                    inj = T.Injector(ad2, plan, args.settle, args.min_snr,
                                     args.max_thd)

                    passband = None
                    if "passband" in selected:
                        log("-- Transmit audio passband --")
                        passband = T.test_passband(inj, passband_grid,
                                                   args.nominal_tol_pct, log)
                        all_checks += passband.checks
                        bucket["passband"] = passband.data

                    if "sideband" in selected and passband is not None:
                        log("-- Opposite-sideband suppression --")
                        res = T.test_sideband_suppression(
                            passband, rate, args.min_suppression, log)
                        all_checks += res.checks
                        bucket["sideband_suppression"] = res.data

                    if "carrier" in selected and passband is not None:
                        log("-- Carrier leakage --")
                        res = T.test_carrier(passband, args.max_carrier_dbc, log)
                        all_checks += res.checks
                        bucket["carrier"] = res.data

                    if "alias" in selected:
                        if passband is None:
                            log("-- Out-of-band and fold-back: skipped, needs the "
                                "passband reference --")
                            warnings.append("the alias test needs the passband "
                                            "sweep for its reference level")
                        else:
                            log("-- Out-of-band rejection and fold-back --")
                            ref_dbv = passband.data.get("reference_dbv")
                            if ref_dbv is None:
                                warnings.append(f"no passband reference at {rate} "
                                                f"sps; alias test skipped")
                            else:
                                ref_v = 10.0 ** (ref_dbv / 20.0)
                                # From where the stopband actually starts - below
                                # that the response is on the transition skirt and
                                # is meant to be only partly attenuated - up to
                                # just under the audio Nyquist, above which the
                                # codec's own decimation rather than the transmit
                                # chain decides what happens.
                                top = 0.92 * bt.audio_rate_hz(rate) / 2.0
                                alias_grid = np.linspace(
                                    bt.TX_STOPBAND_FROM_HZ, top,
                                    args.alias_points)
                                res = T.test_alias(
                                    inj, plan, ref_v, alias_grid,
                                    args.min_oob_rejection, args.max_fold_dbc, log)
                                all_checks += res.checks
                                bucket["alias"] = res.data

                    per_rate[str(rate)] = bucket

                # -- cross-rate comparison ---------------------------------
                log("\n== Comparing rates ==")
                comparison, cmp_checks = T.compare_rates(
                    per_rate, args.rate_tol_pct, args.nominal_tol_pct)
                all_checks += cmp_checks

                ad2.awg_off()
                unkey()

            restored = guard.restored and not args.no_restore
            rig["key_down_s"] = key_down_s
            log(f"\nTotal key-down time: {key_down_s:.0f} s")

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
        print("\naborted - the AWG has been silenced and PTT dropped, but check "
              "that the radio is receiving", file=sys.stderr)
        return 3

    doc = report_mod.build_tx_document(
        provenance=report_mod.collect_provenance(argv, REPO_ROOT,
                                                 dwf_version=ad2.version),
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
                from tx_filter_hil.plot_tx_filter_hil import render_all
            else:
                from .plot_tx_filter_hil import render_all
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
    return run(args, ["tx_filter_hil_test.py"] + argv)


if __name__ == "__main__":
    sys.exit(main())
