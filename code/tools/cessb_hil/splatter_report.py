#!/usr/bin/env python3
"""Table and spectrum plot for a cessb_splatter_matrix.py run.

    ./venv/bin/python cessb_hil/splatter_report.py results/cessb_splatter_<stamp>.json

Writes <stamp>_spectra.png next to the JSON and prints the table as Markdown.
"""

from __future__ import annotations

import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402


def reanalyse(doc):
    """Recompute the band figures from the stored spectra with the current analyse()."""
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from cessb_hil.cessb_splatter_matrix import analyse
    for r in doc["points"]:
        f, p = np.array(r["spectrum"]["f"]), np.array(r["spectrum"]["p"])
        r.update(analyse(f, p, r["pep_dbm"], doc["args"]["rbw_hz"], doc["args"]["span_hz"]))


def label(c):
    return "Off" if c is None else f"{c} dB"


def table(doc) -> str:
    st = doc["single_tone"]
    lines = [f"Single tone (TT1) reference: {st['pep_w']:.2f} W PEP "
             f"({st['re_setting_db']:+.2f} dB re setting)", "",
             "| Mic gain | CESSB | PEP W | avg W | PAPR dB | outside dB re PEP | upper >3 kHz | "
             "lower <-200 Hz | 2f1-f2 | 2f2-f1 | 3f1-2f2 | 3f2-2f1 |",
             "|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for r in doc["points"]:
        lines.append(
            f"| {r['mic_gain_db']} | {label(r['cessb_db'])} | {r['pep_w']:.2f} | {r['avg_w']:.2f} | "
            f"{r['papr_db']:.2f} | {r['outside_re_pep_db']:.1f} | {r['upper_re_pep_db']:.1f} | "
            f"{r['lower_re_pep_db']:.1f} | {r['imd3_low_re_pep_db']:.1f} | "
            f"{r['imd3_high_re_pep_db']:.1f} | {r['imd5_low_re_pep_db']:.1f} | "
            f"{r['imd5_high_re_pep_db']:.1f} |")
    return "\n".join(lines)


def plot(doc, path):
    settings = []
    for r in doc["points"]:
        if r["cessb_db"] not in settings:
            settings.append(r["cessb_db"])
    gains = sorted({r["mic_gain_db"] for r in doc["points"]})
    colours = {g: c for g, c in zip(gains, ["#2a78c8", "#d9822b", "#c23b3b"])}
    tap = doc["args"]["tap_db"]
    fig, axes = plt.subplots(1, len(settings), figsize=(5.2 * len(settings), 4.2), sharey=True)
    axes = np.atleast_1d(axes)
    for ax, c in zip(axes, settings):
        for r in doc["points"]:
            if r["cessb_db"] != c:
                continue
            f = np.array(r["spectrum"]["f"]) / 1e3
            p = np.array(r["spectrum"]["p"]) + tap
            o = np.argsort(f)
            ax.plot(f[o], p[o], lw=0.9, color=colours[r["mic_gain_db"]],
                    label=f"mic {r['mic_gain_db']} dB: {r['pep_w']:.1f} W PEP, "
                          f"outside {r['outside_re_pep_db']:.1f} dB")
        ax.axvspan(0.2, 3.0, color="#888", alpha=0.12, lw=0)
        ax.axhline(10 * np.log10(doc["args"]["power"] * 1000.0) - 6.02, color="#888",
                   lw=0.7, ls=":")
        ax.set_title(f"CESSB {label(c)}")
        ax.set_xlabel("offset from carrier, kHz (+ = wanted sideband)")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=7.5, loc="upper center")
        ax.set_xlim(-10, 10)
    axes[0].set_ylabel(f"dBm at the transmitter output, RBW {doc['args']['rbw_hz']:g} Hz")
    lo = min(min(r["spectrum"]["p"]) for r in doc["points"]) + tap
    axes[0].set_ylim(max(lo - 3, -60), 58)
    fig.tight_layout()
    fig.savefig(path, dpi=110)


def main():
    src = sys.argv[1]
    doc = json.load(open(src))
    reanalyse(doc)
    png = os.path.splitext(src)[0] + "_spectra.png"
    plot(doc, png)
    print(table(doc))
    print(f"\n{png}")


if __name__ == "__main__":
    main()
