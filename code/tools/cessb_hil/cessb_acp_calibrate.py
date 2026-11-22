#!/usr/bin/env python3
"""Find the AD2 drive that puts the W9GR clip's peaks at the SSB power setting.

CESSB off. For each AD2 peak amplitude, the clip loops into the microphone input,
the radio is keyed, and the CMU200's NPOWer meter (1 MHz filter) runs over at
least one full pass of the clip: max = PEP, avg = average power.

    AD2 W1 -> radio microphone input
    TX output -> 60 dB tap -> CMU200 RF1;  CAT -> /dev/ttyACM0

CESSB on/off, power and the CESSB level are read first and restored at the end.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time

_TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from cessb_hil import rf as R  # noqa: E402
from cessb_hil.acp_bench import DEFAULT_WAV, HERE, ClipPlayer, npower_long, watts  # noqa: E402
from cessb_hil.cessb_hil_test import (get_cessb, get_mg, get_pl, get_power, query,  # noqa: E402
                                      set_cessb, set_level_db, set_power)
from filter_hil.radio import CatError  # noqa: E402
from tx_filter_hil.tx_passband_rate_compare import SinglePortRadio  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cat-port", default="/dev/ttyACM0")
    ap.add_argument("--rf-input", default="RF1")
    ap.add_argument("--tap-db", type=float, default=60.0)
    ap.add_argument("--power", type=int, default=10)
    ap.add_argument("--amps", default="0.02,0.04", help="AD2 peak volts, comma separated")
    ap.add_argument("--cessb", default="off", help="'off' or a level in dB")
    ap.add_argument("--seconds", type=float, default=14.0, help="NPOWer time per point")
    ap.add_argument("--settle", type=float, default=1.0)
    ap.add_argument("--rest", type=float, default=5.0)
    ap.add_argument("--wav", default=DEFAULT_WAV)
    args = ap.parse_args()
    log = lambda m="": print(m, file=sys.stderr, flush=True)  # noqa: E731
    amps = [float(a) for a in args.amps.split(",")]
    stamp = time.strftime("%Y%m%d_%H%M%S")
    doc = {"started": stamp, "args": vars(args), "points": []}
    restores = []
    with SinglePortRadio(args.cat_port, None, 38400, 115200, 0.5, log=log) as radio, \
            R.Cmu(args.rf_input, 0.0) as cmu:
        try:
            time.sleep(0.3)
            radio.drain_diag()
            fa = int(query(radio, "FA;", r"FA(\d{11});").group(1))
            md = int(query(radio, "MD;", r"MD(\d);").group(1))
            pc0, pr0, pl0, mg0 = get_power(radio), get_cessb(radio), get_pl(radio), get_mg(radio)
            doc["radio_before"] = dict(fa=fa, md=md, pc=pc0, pr=pr0, pl=pl0, mg=mg0)
            log(f"Radio: {fa} Hz MD{md} PC{pc0} PR{int(pr0)} PL{pl0} MG{mg0:03d}")
            if md not in (1, 2):
                log("error: radio not in SSB")
                return 2
            sign = 1 if md == 2 else -1
            restores += [("CESSB", lambda: radio.send(f"PR{int(pr0)};", expect_reply=False)),
                         ("level", lambda: radio.send(f"PL{pl0[0]:03d}{pl0[1]:03d};",
                                                      expect_reply=False)),
                         ("power", lambda: set_power(radio, pc0)),
                         ("unkey", lambda: radio.ptt_release())]
            set_power(radio, args.power)
            if get_power(radio) != args.power:
                raise CatError("power did not take")
            if args.cessb == "off":
                set_cessb(radio, False, 0.3)
            else:
                set_cessb(radio, True, 0.3)
                set_level_db(radio, int(args.cessb), pl0[1])
            for a in amps:
                with ClipPlayer(args.wav, a) as pl:
                    time.sleep(0.5)
                    radio.ptt_press(args.settle)
                    t0 = time.monotonic()
                    try:
                        npw = npower_long(cmu, fa + sign * 1300.0, args.seconds)
                    finally:
                        radio.ptt_release()
                    st = pl.stat()
                row = dict(peak_v=a, pep_dbm=npw.max_dbm, avg_dbm=npw.avg_dbm,
                           pep_w=watts(npw.max_dbm, args.tap_db),
                           avg_w=watts(npw.avg_dbm, args.tap_db), papr_db=npw.papr_db,
                           keyed_s=time.monotonic() - t0, player=st)
                doc["points"].append(row)
                log(f"  {a*1000:6.1f} mV peak: PEP {row['pep_w']:6.2f} W, avg {row['avg_w']:5.2f} W, "
                    f"PAPR {row['papr_db']:5.2f} dB  ({row['keyed_s']:.0f} s, lost {st['lost']})")
                time.sleep(args.rest)
        finally:
            for name, fn in reversed(restores):
                try:
                    fn()
                except Exception as exc:  # noqa: BLE001
                    log(f"WARNING: could not restore {name}: {exc}")
            time.sleep(0.3)
            try:
                doc["radio_after"] = dict(pc=get_power(radio), pr=get_cessb(radio), pl=get_pl(radio))
                log(f"Radio after: {doc['radio_after']}")
            except Exception:  # noqa: BLE001
                pass
            out = os.path.join(HERE, "results", f"cessb_acp_cal_{stamp}.json")
            with open(out, "w") as fh:
                json.dump(doc, fh, indent=1, default=float)
            log(f"Saved {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
