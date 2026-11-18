#!/usr/bin/env python3
"""CESSB on the real radio: AD2 into the microphone, exciter I/Q captured.

Drives the microphone input from the AD2's W1 with repeatable test signals and
captures the exciter's I and Q outputs on Ch1/Ch2, with CESSB switched off and
on over CAT (``PR0;`` / ``PR1;``) at every point. Wiring as for
tx_filter_hil:

    W1        -> the radio's microphone input
    Scope Ch1 -> exciter I output   (order is auto-detected)
    Scope Ch2 -> exciter Q output
    CAT       -> /dev/ttyACM0 by default: the single-port USB build shares it

What it measures, CESSB off and on:

1. **Ceiling.** A 1 kHz tone stepped up in level. Off, the output follows the
   input; on, it follows until the ceiling and then holds there. The held level
   is the ceiling, and every later level is quoted against it.
2. **Power tracking.** The ceiling must move with the SSB power setting, since
   TXGain maps it onto the requested power (PEP). Half the power: -3.01 dB.
3. **Stimuli at set drives above the ceiling** (0, 3, 6, 9 dB by default): a
   700 + 1900 Hz two-tone, a steady vowel and a syllable-shaped vowel (the voice
   stand-in). For each: peak envelope against the ceiling, talk-power gain at
   equal peak, opposite-sideband and out-of-channel energy, and for the two-tone
   the odd-order products. The offline model's prediction for the same signal is
   reported alongside.

The radio is keyed throughout. Run it into a dummy load. Settings it touches
(modulation, CESSB, mic gain if asked, power for the tracking check) are read
first and restored at the end, on Ctrl-C and at exit.

Exit codes: 0 pass, 1 fail, 2 error, 3 aborted.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time

import numpy as np

_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from cessb_hil import analysis as A  # noqa: E402
from cessb_hil import stimuli as S  # noqa: E402
from cessb_hil.ad2_custom import CessbAd2  # noqa: E402
from filter_hil.ad2 import DwfError  # noqa: E402
from filter_hil.radio import MOD_LSB, MOD_USB, CatError, Radio  # noqa: E402
from tx_filter_hil import tests as T  # noqa: E402
from tx_filter_hil.ad2 import TxAd2Config  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(_TOOLS))
TONE_HZ = 1000.0
MD_TO_MOD = {1: MOD_LSB, 2: MOD_USB}


# ---------------------------------------------------------------------------
# CAT helpers. The single-port build shares the port with debug output, so
# replies are found by pattern rather than taken whole.
# ---------------------------------------------------------------------------

def query(radio: Radio, cmd: str, pattern: str, tries: int = 3) -> re.Match:
    last = None
    for _ in range(tries):
        reply = radio.send(cmd, expect_reply=True) or ""
        last = reply
        m = re.search(pattern, reply)
        if m:
            return m
        time.sleep(0.1)
    raise CatError(f"{cmd!r}: no reply matching {pattern!r} (last {last!r})")


def get_cessb(radio: Radio) -> bool:
    return query(radio, "PR;", r"PR([01]);").group(1) == "1"


def set_cessb(radio: Radio, on: bool, settle_s: float) -> None:
    radio.send(f"PR{1 if on else 0};", expect_reply=False)
    time.sleep(settle_s)
    if get_cessb(radio) != on:
        raise CatError(f"the radio did not switch CESSB {'on' if on else 'off'} "
                       f"(PR command missing? flash the CESSB Phase 2 firmware)")


def get_power(radio: Radio) -> int:
    return int(query(radio, "PC;", r"PC(\d{3});").group(1))


def set_power(radio: Radio, watts: int) -> None:
    radio.send(f"PC{int(watts):03d};", expect_reply=False)
    time.sleep(0.2)


def get_md(radio: Radio) -> int:
    return int(query(radio, "MD;", r"MD(\d);").group(1))


def get_mg(radio: Radio) -> int:
    return int(query(radio, "MG;", r"MG(\d{3});").group(1))


# ---------------------------------------------------------------------------

class Checks:
    def __init__(self, log):
        self.items: list[T.Check] = []
        self.log = log

    def add(self, ident, group, desc, value, limit, units, passed, message=""):
        c = T.Check(ident, group, desc, value, limit, units, bool(passed), message=message)
        self.items.append(c)
        mark = "PASS" if passed else "FAIL"
        v = "" if value is None else f" {value:.3g}{units and ' ' + units}"
        self.log(f"  [{mark}] {desc}:{v}" + (f" ({message})" if message else ""))

    @property
    def overall(self) -> str:
        return "PASS" if all(c.passed for c in self.items) else "FAIL"


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cat-port", default="/dev/ttyACM0")
    p.add_argument("--cat-baud", type=int, default=38400)
    p.add_argument("--modulation", choices=("usb", "lsb"), default="usb")
    p.add_argument("--drives", default="0,3,6,9",
                   help="dB above the ceiling to drive each stimulus (default: %(default)s)")
    p.add_argument("--stimuli", default="two_tone,vowel_ah,syllable_ah")
    p.add_argument("--mic-gain", type=float, default=None,
                   help="set mic gain (dB, 0..30) for the run; restored afterwards")
    p.add_argument("--min-amplitude", type=float, default=0.005,
                   help="lowest AWG peak volts in the tone sweep (default: %(default)s)")
    p.add_argument("--min-accurate-amplitude", type=float, default=0.02,
                   help="AWG levels below this are not trusted for the transparency "
                        "check: at a few millivolts the AD2's output moves by +/-1 dB "
                        "between captures (measured), CESSB on or off "
                        "(default: %(default)s)")
    p.add_argument("--max-amplitude", type=float, default=0.25,
                   help="never drive the mic input harder than this, volts peak "
                        "(default: %(default)s)")
    p.add_argument("--sweep-step-db", type=float, default=1.5)
    p.add_argument("--detect-amplitude", type=float, default=0.02)
    p.add_argument("--scope-rate", type=float, default=100000.0)
    p.add_argument("--capture", type=float, default=0.5,
                   help="seconds per capture; two loops of the syllable (default: %(default)s)")
    p.add_argument("--scope-range", type=float, default=5.0)
    p.add_argument("--scope-offset", type=float, default=1.6)
    p.add_argument("--settle", type=float, default=0.3)
    p.add_argument("--no-power-check", action="store_true")
    # Limits
    p.add_argument("--max-overshoot-pct", type=float, default=5.0,
                   help="peak envelope allowed above the ceiling with CESSB on "
                        "(model: 1.3 %%; the rest is allowance for the analog "
                        "exciter) (default: %(default)s)")
    p.add_argument("--max-transparency-db", type=float, default=0.3)
    p.add_argument("--max-hold-spread-db", type=float, default=1.0)
    p.add_argument("--max-power-error-db", type=float, default=0.5)
    p.add_argument("--max-imd-opposite-dbc", type=float, default=-40.0,
                   help="limit on 2f1-f2 (opposite sideband) with CESSB on. The DSP "
                        "gives -62.7 dBc at 192k and -55.8 at 176.4k (gtest); the "
                        "bench adds a fixed spur and the image floor "
                        "(default: %(default)s)")
    p.add_argument("--max-opposite-dbc", type=float, default=-25.0,
                   help="gross-failure guard on the opposite sideband with CESSB on. "
                        "Not tighter, because on the bench this reads the exciter's own "
                        "image floor, which on the KI3P prototype moves between about "
                        "-40 and -63 dBc from one capture to the next, CESSB on or off "
                        "(2026-11-18). A wrongly centred stage fails it (default: %(default)s)")
    p.add_argument("--max-ooc-dbc", type=float, default=-45.0)
    p.add_argument("--min-syllable-gain-db", type=float, default=3.0,
                   help="talk-power gain required of the voice stand-in at 6 dB "
                        "drive (model: 4.1 dB) (default: %(default)s)")
    p.add_argument("--out-dir", default=os.path.join(HERE, "results"))
    p.add_argument("--no-plots", action="store_true")
    p.add_argument("-q", "--quiet", action="store_true")
    return p


def run(args) -> int:
    started = time.monotonic()
    log = (lambda m="": None) if args.quiet else (lambda m="": print(m, file=sys.stderr))
    drives = [float(d) for d in args.drives.split(",") if d.strip()]
    wanted_stimuli = [s.strip() for s in args.stimuli.split(",") if s.strip()]
    stamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(args.out_dir, exist_ok=True)
    checks = Checks(log)
    doc: dict = {"started": stamp, "args": vars(args), "warnings": []}

    cfg = TxAd2Config(scope_rate_hz=args.scope_rate, capture_s=args.capture,
                      scope_range_v=args.scope_range, scope_offset_v=args.scope_offset)
    restores: list = []

    def restore_all():
        while restores:
            name, fn = restores.pop()
            try:
                fn()
            except Exception as exc:          # keep going: every restore matters
                print(f"WARNING: could not restore {name}: {exc}", file=sys.stderr)

    try:
        with CessbAd2(cfg, log=log) as ad2, Radio(args.cat_port, None, args.cat_baud,
                                                 115200, 0.5, log=log) as radio:
          try:
            ad2.configure_scope()
            nbuf = min(4096, ad2.custom_buffer_size())
            stim_all = S.all_stimuli(nbuf)
            unknown = [s for s in wanted_stimuli if s not in stim_all]
            if unknown:
                print(f"error: unknown stimuli {unknown}; choose from {list(stim_all)}",
                      file=sys.stderr)
                return 2

            # -- state to restore --------------------------------------------
            md0, pr0, pc0 = get_md(radio), get_cessb(radio), get_power(radio)
            rate = radio.get_sample_rate()
            doc["radio"] = {"md": md0, "cessb": pr0, "power_w": pc0, "rate_hz": rate}
            log(f"Radio: MD{md0}, CESSB {'on' if pr0 else 'off'}, SSB power {pc0} W, "
                f"{rate} sps")
            if md0 not in MD_TO_MOD:
                print("error: put the radio in SSB (USB or LSB) first", file=sys.stderr)
                return 2
            restores.append(("modulation", lambda: radio.send(f"MD{md0};", expect_reply=False)))
            restores.append(("CESSB", lambda: radio.send(f"PR{1 if pr0 else 0};",
                                                         expect_reply=False)))
            restores.append(("power", lambda: set_power(radio, pc0)))
            if args.mic_gain is not None:
                mg0 = get_mg(radio)
                restores.append(("mic gain", lambda: radio.set_mic_gain_pct(mg0)))
                radio.set_mic_gain_pct(Radio.mic_gain_pct_from_db(args.mic_gain))
                log(f"Mic gain set to {args.mic_gain:.0f} dB (MG {mg0} restored after)")
            # Registered last, so it runs first: everything above assumes receive
            restores.append(("transmit off", lambda: (ad2.awg_off(), radio.ptt_release())))

            modulation = MOD_USB if args.modulation == "usb" else MOD_LSB
            set_cessb(radio, False, args.settle)
            radio.set_modulation(modulation)
            time.sleep(0.5)
            radio.ptt_press(1.0)
            key_t0 = time.monotonic()

            # -- wiring --------------------------------------------------------
            log("\n== Wiring and sideband ==")
            plan = T.TxPlan(rate_hz=rate, modulation=modulation)
            res, swap, sign = T.detect_wiring(ad2, radio, plan, args.settle, 25.0,
                                              args.detect_amplitude, modulation, log)
            for c in res.checks:
                checks.add(c.id, c.group, c.description, c.value, c.limit, c.units,
                           c.passed, c.message)
            doc["wiring"] = res.data
            if not all(c.passed for c in res.checks):
                log("Cannot continue without both exciter outputs in quadrature.")
                return 1

            def capture_z(on: bool):
                set_cessb(radio, on, args.settle)
                cap = ad2.capture()
                clipped = cap.excursion(args.scope_offset) > 0.95 * args.scope_range / 2
                return cap.analytic(swap), clipped, cap.clean

            # -- 1. ceiling ------------------------------------------------------
            log("\n== 1. Ceiling: 1 kHz tone level sweep, CESSB off and on ==")
            sweep = []
            amp = args.min_amplitude
            step = 10 ** (args.sweep_step_db / 20)
            limited_run = 0
            while amp <= args.max_amplitude * 1.0001:
                ad2.set_tone(TONE_HZ, amp)
                time.sleep(args.settle)
                z_off, clip_off, ok_off = capture_z(False)
                z_on, clip_on, ok_on = capture_z(True)
                e_off = A.median_envelope(z_off, args.scope_rate)
                e_on = A.median_envelope(z_on, args.scope_rate)
                sweep.append({"amplitude_v": amp, "off_v": e_off, "on_v": e_on,
                              "off_clipped": clip_off, "on_clipped": clip_on,
                              "clean": ok_off and ok_on})
                log(f"  {amp*1000:7.2f} mV  off {e_off:.4f} V{' CLIP' if clip_off else ''}"
                    f"   on {e_on:.4f} V   ({A.db(e_off/e_on):+5.2f} dB)")
                limited_run = limited_run + 1 if e_off > e_on * 10 ** (6 / 20) else 0
                if clip_off or limited_run >= 3:
                    break
                amp *= step
            doc["tone_sweep"] = sweep
            usable = [p for p in sweep if not p["off_clipped"]]
            ceil = A.find_ceiling([p["on_v"] for p in usable], [p["off_v"] for p in usable])
            doc["ceiling"] = ceil
            checks.add("ceiling.found", "ceiling",
                       "CESSB holds a 1 kHz tone at a ceiling", None, None, "",
                       ceil["found"],
                       "" if ceil["found"] else
                       "the output never limited: raise --max-amplitude or --mic-gain")
            if not ceil["found"]:
                return 1
            C = ceil["ceiling_v"]
            log(f"  ceiling: {C:.4f} V envelope at the exciter")
            checks.add("ceiling.hold_flat", "ceiling",
                       "Held output is flat above the ceiling (spread)",
                       ceil["limited_spread_db"], args.max_hold_spread_db, "dB",
                       ceil["limited_spread_db"] <= args.max_hold_spread_db)
            below = [p for p in usable if p["off_v"] <= C * 10 ** (-6 / 20)
                     and p["amplitude_v"] >= args.min_accurate_amplitude]
            worst = max((abs(A.db(p["on_v"] / p["off_v"])) for p in below), default=None)
            checks.add("ceiling.transparent", "ceiling",
                       "Below the ceiling CESSB on and off agree (worst)",
                       worst, args.max_transparency_db, "dB",
                       worst is not None and worst <= args.max_transparency_db,
                       "" if below else "no accurate sweep points 6 dB below the ceiling")
            lin = [p for p in below if p["amplitude_v"] > 0]
            g_tone = float(np.median([p["off_v"] / p["amplitude_v"] for p in lin])) if lin \
                else usable[0]["off_v"] / usable[0]["amplitude_v"]
            knee_v = C / g_tone
            doc["tone_gain_v_per_v"] = g_tone
            doc["knee_amplitude_v"] = knee_v
            log(f"  knee at {knee_v*1000:.1f} mV into the mic input")

            # -- 2. power tracking -------------------------------------------------
            if not args.no_power_check and pc0 >= 2:
                log("\n== 2. Ceiling follows the SSB power setting ==")
                a_hold = min(knee_v * 2.0, args.max_amplitude)
                ad2.set_tone(TONE_HZ, a_hold)
                set_cessb(radio, True, args.settle)
                e_full = A.median_envelope(ad2.capture_after(args.settle).analytic(swap),
                                           args.scope_rate)
                p_half = pc0 // 2
                set_power(radio, p_half)
                e_half = A.median_envelope(ad2.capture_after(args.settle).analytic(swap),
                                           args.scope_rate)
                set_power(radio, pc0)
                measured = A.db(e_half / e_full)
                expected = 10 * np.log10(p_half / pc0)
                err = measured - expected
                doc["power_tracking"] = {"p_full_w": pc0, "p_half_w": p_half,
                                         "measured_db": measured, "expected_db": expected}
                log(f"  {pc0} W -> {p_half} W: ceiling moved {measured:+.2f} dB, "
                    f"expected {expected:+.2f} dB")
                checks.add("ceiling.tracks_power", "ceiling",
                           "Ceiling moves with the power setting (error)",
                           err, args.max_power_error_db, "dB",
                           abs(err) <= args.max_power_error_db)
            elif not args.no_power_check:
                doc["warnings"].append("power tracking skipped: SSB power below 2 W")

            # -- 3. stimuli ----------------------------------------------------------
            from cessb_hil.predict import predict
            doc["stimuli"] = {}
            for name in wanted_stimuli:
                st = stim_all[name]
                log(f"\n== 3. {name} ==")
                # Find this stimulus's off-path peak per AWG volt at a linear level.
                a_probe = 0.5 * knee_v
                ad2.set_custom(st.samples, st.period_hz, a_probe)
                time.sleep(args.settle)
                z_ref, _, _ = capture_z(False)
                ref = A.envelope_stats(z_ref, args.scope_rate, sign)
                r_s = ref.peak / a_probe
                log(f"  reference (off, linear): peak {ref.peak/C:.3f} x ceiling, "
                    f"PAPR {ref.papr_db:.2f} dB, opposite {ref.opposite_dbc:.1f} dBc")
                model = predict(st, rate, drives)
                rows = []
                for d in drives:
                    a_d = C * 10 ** (d / 20) / r_s
                    if a_d > args.max_amplitude:
                        doc["warnings"].append(f"{name} at {d:g} dB needs {a_d*1000:.0f} mV, "
                                               f"above --max-amplitude; skipped")
                        log(f"  {d:+4.1f} dB: needs {a_d*1000:.0f} mV - skipped")
                        continue
                    ad2.set_custom(st.samples, st.period_hz, a_d)
                    time.sleep(args.settle)
                    row = {"drive_db": d, "amplitude_v": a_d}
                    for on in (False, True):
                        z, clipped, clean = capture_z(on)
                        s_ = A.envelope_stats(z, args.scope_rate, sign)
                        r = {"peak_x_ceiling": s_.peak / C, "papr_db": s_.papr_db,
                             "talk_power_gain_db": ref.wanted_papr_db - s_.wanted_papr_db,
                             "opposite_dbc": s_.opposite_dbc,
                             "out_of_channel_dbc": s_.out_of_channel_dbc,
                             "scope_clipped": clipped, "clean": clean}
                        if st.tones_hz:
                            r.update(A.two_tone_products(z, args.scope_rate, sign, *st.tones_hz))
                        row["on" if on else "off"] = r
                    m = model.get(str(d), {})
                    row["model"] = m
                    rows.append(row)
                    on, off = row["on"], row["off"]
                    log(f"  {d:+4.1f} dB: peak off {off['peak_x_ceiling']:.2f} / on "
                        f"{on['peak_x_ceiling']:.3f} x ceiling (model {m.get('peak', float('nan')):.3f}); "
                        f"talk power on {on['talk_power_gain_db']:+.2f} dB (model "
                        f"{m.get('talk_power_gain_db', float('nan')):+.2f}); opposite "
                        f"{on['opposite_dbc']:.1f} dBc; out of channel {on['out_of_channel_dbc']:.1f} dBc")
                    if st.tones_hz:
                        log(f"         products on: 2f1-f2 {on['imd3_low_dbc']:.1f}, "
                            f"2f2-f1 {on['imd3_high_dbc']:.1f}, its mirror (audio "
                            f"distortion) {on['imd3_low_mirror_dbc']:.1f} dBc; off: "
                            f"{off['imd3_low_dbc']:.1f}, {off['imd3_high_dbc']:.1f}, "
                            f"{off['imd3_low_mirror_dbc']:.1f} dBc")
                doc["stimuli"][name] = {"reference": {"peak_x_ceiling": ref.peak / C,
                                                      "papr_db": ref.papr_db,
                                                      "opposite_dbc": ref.opposite_dbc,
                                                      "out_of_channel_dbc": ref.out_of_channel_dbc,
                                                      "awg_v_per_peak": 1 / r_s},
                                        "rows": rows, "model": model}
                for row in rows:
                    on, d = row["on"], row["drive_db"]
                    tag = f"{name}@{d:g}dB"
                    checks.add(f"hold.{tag}", "hold", f"{tag}: peak with CESSB on",
                               100 * (on["peak_x_ceiling"] - 1), args.max_overshoot_pct,
                               "% over", on["peak_x_ceiling"] <= 1 + args.max_overshoot_pct / 100)
                    checks.add(f"ooc.{tag}", "spectrum", f"{tag}: out-of-channel, CESSB on",
                               on["out_of_channel_dbc"], args.max_ooc_dbc, "dBc",
                               on["out_of_channel_dbc"] <= args.max_ooc_dbc)
                    if st.tones_hz:
                        checks.add(f"imd.{tag}", "spectrum",
                                   f"{tag}: 2f1-f2 in the opposite sideband, CESSB on",
                                   on["imd3_low_dbc"], args.max_imd_opposite_dbc, "dBc",
                                   on["imd3_low_dbc"] <= args.max_imd_opposite_dbc)
                    else:
                        checks.add(f"opp.{tag}", "spectrum",
                                   f"{tag}: opposite sideband, CESSB on (gross-failure guard)",
                                   on["opposite_dbc"], args.max_opposite_dbc, "dBc",
                                   on["opposite_dbc"] <= args.max_opposite_dbc)
                    if name.startswith("syllable") and abs(d - 6.0) < 1e-6:
                        checks.add(f"gain.{tag}", "talk power",
                                   f"{tag}: talk-power gain at equal peak",
                                   on["talk_power_gain_db"], args.min_syllable_gain_db,
                                   "dB", on["talk_power_gain_db"] >= args.min_syllable_gain_db)

            ad2.awg_off()
            radio.ptt_release()
            doc["key_down_s"] = time.monotonic() - key_t0
            log(f"\nKey-down time {doc['key_down_s']:.0f} s")
          finally:
            # Inside the with-block, so the CAT port is still open for the restores
            restore_all()
            doc["restored"] = True

    except (DwfError, CatError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\naborted - AWG silenced, PTT dropped and settings restored; check "
              "the radio is receiving", file=sys.stderr)
        return 3

    doc["checks"] = [c.as_dict() for c in checks.items]
    doc["overall"] = checks.overall
    doc["duration_s"] = time.monotonic() - started
    base = os.path.join(args.out_dir, f"cessb_hil_{stamp}")
    with open(base + ".json", "w") as fh:
        json.dump(doc, fh, indent=1, default=lambda o: o.item() if hasattr(o, "item") else str(o))
    pngs = []
    if not args.no_plots:
        try:
            from cessb_hil.report import plots
            pngs = plots(doc, base)
        except Exception as exc:
            doc["warnings"].append(f"plots failed: {exc}")
    from cessb_hil.report import markdown
    with open(base + ".md", "w") as fh:
        fh.write(markdown(doc, pngs))
    n_fail = sum(not c.passed for c in checks.items)
    print(f"\n{doc['overall']}: {len(checks.items) - n_fail}/{len(checks.items)} checks "
          f"passed. Report: {base}.md", file=sys.stderr)
    return 0 if doc["overall"] == "PASS" else 1


def main() -> int:
    return run(build_parser().parse_args())


if __name__ == "__main__":
    sys.exit(main())
