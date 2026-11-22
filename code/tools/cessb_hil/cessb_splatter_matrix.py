#!/usr/bin/env python3
"""Two-tone splatter matrix at RF: mic gain x CESSB setting, CMU200 on the output.

Uses the firmware's own two-tone source (CAT ``TT2;``: 700 + 1900 Hz injected at
the microphone mixers) instead of the AD2. For this test the firmware carried a
temporary change (2026-11-22, since reverted) that scales the two-tone by the
mic gain, 10^((MG - 20)/20): at 20 dB mic gain its PEP is the power setting, as
a generator in the mic jack would be after the codec's mic preamp. The single
tone (``TT1;``) is not scaled; it is measured first as the PEP reference.

    TX output -> 60 dB coupler (through to the dummy load) -> CMU200 RF1
    CAT       -> /dev/ttyACM0 (single-port USB build)

At every point: NPOWer (1 MHz filter; max = PEP, avg = average) and one RMS
spectrum, carrier +/- ``--span-hz``, averaged over ``--sweeps`` sweeps. From the
spectrum: the tones, the odd-order products, and integrated band powers:

* ``outside``: everything outside the 200-3000 Hz channel except the carrier
  region: below -200 Hz plus above 3000 Hz, to the span edge. dB re PEP. The
  headline splatter figure.
* ``upper``: above 3000 Hz (beyond the wanted sideband's edge; 2f2-f1 is at 3100 Hz).
* ``lower``: below -200 Hz (the opposite side; 2f1-f2 is at -500 Hz).
* ``opposite``: -3000 to -200 Hz, and ``ooc``: |f| >= 3.3 kHz, as in cessb_rf_test.py.

Mic gain, CESSB on/off and level, power, and the TX source are read first and
restored at the end, on error and on Ctrl-C.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time

import numpy as np

_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from cessb_hil import rf as R  # noqa: E402
from cessb_hil.cessb_hil_test import (get_cessb, get_md, get_mg, get_pl, get_power,  # noqa: E402
                                      query, set_cessb, set_level_db, set_power)
from cessb_hil.cessb_rf_test import get_fa  # noqa: E402
from filter_hil.radio import CatError, Radio  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def mg_code(gain_db: int) -> int:
    """CAT MG value (0-100) that MG_write turns into gain_db: int(m*0.7 - 40)."""
    for m in range(101):
        if int(m * 70.0 / 100.0 - 40.0) == gain_db:
            return m
    raise ValueError(f"no MG code gives {gain_db} dB")


def mg_read_code(gain_db: int) -> int:
    """What MG_read reports for gain_db: int((g + 40)*100/70). Not the inverse of
    MG_write (20 dB is written as MG086 but read back as MG085)."""
    return int((gain_db + 40) * 100.0 / 70.0)


def mg_db(read_code: int) -> int:
    """The gain behind an MG_read reply."""
    for g in range(-40, 31):
        if mg_read_code(g) == read_code:
            return g
    raise ValueError(f"MG{read_code:03d} is not an MG_read reply")


def set_mg(radio: Radio, gain_db: int) -> None:
    code = mg_code(gain_db)
    radio.send(f"MG{code:03d};", expect_reply=False)
    time.sleep(0.2)
    got = get_mg(radio)
    if got != mg_read_code(gain_db):
        raise CatError(f"MG did not take: asked {gain_db} dB (MG{code:03d}), read MG{got:03d}")


def set_tt(radio: Radio, src: int) -> None:
    radio.send(f"TT{src};", expect_reply=False)
    time.sleep(0.2)
    got = int(query(radio, "TT;", r"TT(\d);").group(1))
    if got != src:
        raise CatError(f"TT did not take: asked {src}, read {got}")


def int_mw(fr, p, lo, hi, rbw):
    return R.integrated_mw(fr, p, lo, hi, rbw)


def analyse(fr, p, pep_dbm, rbw, edge):
    two = R.two_tone_rf(fr, p, 700.0, 1900.0)
    opp = int_mw(fr, p, -3000.0, -200.0, rbw)
    ooc = int_mw(fr, p, 3300.0, edge, rbw) + int_mw(fr, p, -edge, -3300.0, rbw)
    upper = int_mw(fr, p, 3000.0, edge, rbw)
    lower = int_mw(fr, p, -edge, -200.0, rbw)
    outside = upper + lower
    wanted = int_mw(fr, p, 200.0, 3000.0, rbw)
    out = {
        "wanted_dbm": R.db10(wanted),
        "outside_re_pep_db": R.db10(outside) - pep_dbm,
        "upper_re_pep_db": R.db10(upper) - pep_dbm,
        "lower_re_pep_db": R.db10(lower) - pep_dbm,
        "opposite_re_pep_db": R.db10(opp) - pep_dbm,
        "ooc_re_pep_db": R.db10(ooc) - pep_dbm,
        "outside_dbm": R.db10(outside),
        "carrier_re_pep_db": R.tone_dbm(fr, p, 0.0) - pep_dbm,
    }
    for k in ("imd3_low", "imd3_high", "imd5_low", "imd5_high", "imd7_low", "imd7_high"):
        out[k + "_re_pep_db"] = R.tone_dbm(fr, p, two[k + "_hz"]) - pep_dbm
    out["tone1_dbm"], out["tone2_dbm"] = two["tone1_dbm"], two["tone2_dbm"]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cat-port", default="/dev/ttyACM0")
    ap.add_argument("--rf-input", default="RF1")
    ap.add_argument("--tap-db", type=float, default=60.0)
    ap.add_argument("--power", type=int, default=5, help="SSB power setting, W")
    ap.add_argument("--mic-gains", default="10,15,20")
    ap.add_argument("--cessb", default="off,3,6", help="'off' or a level in dB")
    ap.add_argument("--span-hz", type=float, default=10000.0)
    ap.add_argument("--rbw-hz", type=float, default=100.0)
    ap.add_argument("--sweeps", type=int, default=8)
    ap.add_argument("--npower-stats", type=int, default=300)
    ap.add_argument("--settle", type=float, default=1.0)
    ap.add_argument("--rest", type=float, default=10.0)
    ap.add_argument("--out-dir", default=os.path.join(HERE, "results"))
    args = ap.parse_args()

    gains = [int(g) for g in args.mic_gains.split(",")]
    settings = [None if c.strip() == "off" else int(c) for c in args.cessb.split(",")]
    stamp = time.strftime("%Y%m%d_%H%M%S")
    os.makedirs(args.out_dir, exist_ok=True)
    log = lambda m="": print(m, file=sys.stderr, flush=True)  # noqa: E731
    doc = {"started": stamp, "args": vars(args), "points": [], "key_down_s": 0.0}
    restores = []

    with Radio(args.cat_port, None, 38400, 115200, 0.5, log=log) as radio, \
            R.Cmu(args.rf_input, 0.0) as cmu:
        try:
            md0, pr0, pc0, fa = get_md(radio), get_cessb(radio), get_power(radio), get_fa(radio)
            mg0, pl0 = get_mg(radio), get_pl(radio)
            tt0 = int(query(radio, "TT;", r"TT(\d);").group(1))
            doc["radio_before"] = {"md": md0, "cessb": pr0, "power_w": pc0, "fa": fa,
                                   "mg": mg0, "pl": pl0, "tt": tt0}
            log(f"CMU200: {cmu.identity()}")
            log(f"Radio: {fa/1e6:.6f} MHz MD{md0}, PC{pc0}, MG{mg0:03d} ({mg_db(mg0)} dB), "
                f"PR{int(pr0)}, PL{pl0[0]:03d}{pl0[1]:03d}, TT{tt0}")
            if md0 not in (1, 2):
                log("error: put the radio in SSB first")
                return 2
            sign = 1 if md0 == 2 else -1
            restores += [("TX source", lambda: radio.send(f"TT{tt0};", expect_reply=False)),
                         ("mic gain", lambda: set_mg(radio, mg_db(mg0))),
                         ("CESSB", lambda: radio.send(f"PR{int(pr0)};", expect_reply=False)),
                         ("level", lambda: radio.send(f"PL{pl0[0]:03d}{pl0[1]:03d};",
                                                      expect_reply=False)),
                         ("power", lambda: set_power(radio, pc0)),
                         ("unkey", lambda: radio.ptt_release())]
            set_power(radio, args.power)
            if get_power(radio) != args.power:
                raise CatError("power setting did not take")
            p_set_dbm = 10 * math.log10(args.power * 1000.0) - args.tap_db
            f_meas = fa + sign * 1300.0

            def measure(label):
                radio.ptt_press(args.settle)
                t0 = time.monotonic()
                try:
                    npw = cmu.npower(f_meas, args.npower_stats)
                    f, p, _ = cmu.spectrum(fa - args.span_hz, fa + args.span_hz, args.rbw_hz,
                                           detector="RMS", ref_dbm=p_set_dbm + 13.0,
                                           sweeps=args.sweeps, trace="average")
                finally:
                    radio.ptt_release()
                    doc["key_down_s"] += time.monotonic() - t0
                log(f"  {label}: keyed {time.monotonic() - t0:.0f} s")
                time.sleep(args.rest)
                return npw, R.rel(f, fa, sign), p

            # Reference: the unscaled single tone, whose PEP is the power setting
            log("\n== Reference: single 1 kHz tone (TT1), CESSB off ==")
            set_cessb(radio, False, 0.3)
            set_tt(radio, 1)
            npw, fr, p = measure("single tone")
            doc["single_tone"] = {"pep_dbm": npw.max_dbm, "avg_dbm": npw.avg_dbm,
                                  "pep_w": 10 ** ((npw.max_dbm + args.tap_db) / 10) / 1000,
                                  "re_setting_db": npw.max_dbm - p_set_dbm,
                                  "spectrum": {"f": fr.tolist(), "p": p.tolist()}}
            log(f"  PEP {npw.max_dbm:.2f} dBm at the tap = {doc['single_tone']['pep_w']:.2f} W "
                f"({npw.max_dbm - p_set_dbm:+.2f} dB re the {args.power} W setting)")

            set_tt(radio, 2)
            for c in settings:
                if c is None:
                    set_cessb(radio, False, 0.3)
                else:
                    set_cessb(radio, True, 0.3)
                    set_level_db(radio, c, pl0[1])
                for g in gains:
                    set_mg(radio, g)
                    label = f"MG {g} dB, CESSB {'off' if c is None else f'{c} dB'}"
                    log(f"\n== {label} ==")
                    npw, fr, p = measure(label)
                    row = {"mic_gain_db": g, "cessb_db": c,
                           "pep_dbm": npw.max_dbm, "avg_dbm": npw.avg_dbm, "papr_db": npw.papr_db,
                           "pep_w": 10 ** ((npw.max_dbm + args.tap_db) / 10) / 1000,
                           "avg_w": 10 ** ((npw.avg_dbm + args.tap_db) / 10) / 1000,
                           "pep_re_setting_db": npw.max_dbm - p_set_dbm}
                    row.update(analyse(fr, p, npw.max_dbm, args.rbw_hz, args.span_hz))
                    row["spectrum"] = {"f": fr.tolist(), "p": p.tolist()}
                    doc["points"].append(row)
                    log(f"  PEP {row['pep_w']:.2f} W ({row['pep_re_setting_db']:+.2f} dB), avg "
                        f"{row['avg_w']:.2f} W, PAPR {row['papr_db']:.2f} dB; outside channel "
                        f"{row['outside_re_pep_db']:.1f} dB re PEP; IMD3 "
                        f"{row['imd3_low_re_pep_db']:.1f}/{row['imd3_high_re_pep_db']:.1f}, IMD5 "
                        f"{row['imd5_low_re_pep_db']:.1f}/{row['imd5_high_re_pep_db']:.1f} dB re PEP")
        except KeyboardInterrupt:
            log("aborted")
            return 3
        finally:
            for name, fn in reversed(restores):
                try:
                    fn()
                except Exception as exc:  # noqa: BLE001
                    log(f"WARNING: could not restore {name}: {exc}")
            time.sleep(0.3)
            try:
                doc["radio_after"] = {"mg": get_mg(radio), "pr": get_cessb(radio),
                                      "pl": get_pl(radio), "pc": get_power(radio)}
            except Exception:  # noqa: BLE001
                pass
            out = os.path.join(args.out_dir, f"cessb_splatter_{stamp}.json")
            with open(out, "w") as fh:
                json.dump(doc, fh, indent=1, default=float)
            log(f"\nKey-down {doc['key_down_s']:.0f} s. Saved {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
