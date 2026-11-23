#!/usr/bin/env python3
"""Channel power ratios from cessb_acp_rx.py (radio 1 as the receiver).

The ratio is the mean-square audio in each channel over the mean-square audio in
the wanted channel, both through radio 1's own SSB filter at the same fixed gain.
"floor-corrected" subtracts radio 1's own noise in that channel (measured with
the transmitter unkeyed), and the margin says how far above that noise the
reading was.

    acp_rx_analysis.py results/cessb_acp_rx_<stamp>.json
"""

from __future__ import annotations

import json
import math
import sys


def db(x):
    return 10 * math.log10(max(x, 1e-30))


def analyse(doc) -> dict:
    floor = next((p for p in doc["points"] if p["kind"] == "rx_floor"), None)
    rows = []
    for p in doc["points"]:
        ch = p["channels"]
        w = ch["wanted"]["power"]
        r = {"label": p["label"], "kind": p["kind"], "cessb_db": p.get("cessb_db"),
             "wanted_dbfs": db(w), "wanted_peak_dbfs": 20 * math.log10(max(ch["wanted"]["peak"], 1e-10))}
        for k, v in ch.items():
            if k == "wanted":
                continue
            r[k + "_acpr_db"] = db(v["power"] / w)
            if floor is not None and p is not floor:
                fl = floor["channels"][k]["power"]
                fw = floor["channels"]["wanted"]["power"]
                r[k + "_margin_db"] = db(v["power"] / fl)
                r[k + "_acpr_corr_db"] = db(max(v["power"] - fl, 1e-30) / max(w - fw, 1e-30))
        rows.append(r)
    return {"rows": rows, "tone_cal": doc.get("tone_cal"), "label": doc["args"].get("label", "")}


def fmt(res) -> str:
    names = ["lower_alt", "lower", "upper", "upper_alt"]
    out = [f"Receiver: radio 1 {res['label']}  tone cal {res['tone_cal']}",
           f"{'point':<16}{'wanted dBFS':>12}{'peak':>7}" + "".join(f"{n:>22}" for n in names)]
    for r in res["rows"]:
        cells = []
        for n in names:
            if n + "_acpr_db" not in r:
                cells.append(" " * 22)
                continue
            s = f"{r[n + '_acpr_db']:.1f}"
            if n + "_acpr_corr_db" in r:
                s += f" ({r[n + '_acpr_corr_db']:.1f}, +{r[n + '_margin_db']:.0f})"
            cells.append(f"{s:>22}")
        out.append(f"{r['label']:<16}{r['wanted_dbfs']:12.1f}{r['wanted_peak_dbfs']:7.1f}"
                   + "".join(cells))
    out.append("cells: ratio dB (floor-corrected, margin over radio 1's floor dB)")
    return "\n".join(out)


def main() -> int:
    with open(sys.argv[1]) as fh:
        doc = json.load(fh)
    print(fmt(analyse(doc)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
