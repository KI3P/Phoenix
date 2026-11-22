#!/usr/bin/env python3
"""Plots and an HTML table for the adjacent-channel power test.

    acp_report.py results/cessb_acp_<stamp>.json [--out-prefix PATH]

Writes <prefix>_spectra.png (averaged spectra with the receiver channels),
<prefix>_acpr.png (ratio against CESSB level) and prints the HTML table rows.
"""

from __future__ import annotations

import argparse
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from cessb_hil.acp_analysis import (CHANNELS, RxFilter, analyse, covered, db10,  # noqa: E402
                                   load, merged)


def label_of(r):
    return "Off" if r["cessb_db"] is None else f"{r['cessb_db']} dB"


def stitched(row):
    """The point's long-term spectrum, dBm per RBW, from its segments."""
    f, p, _ = merged(row)
    d = db10(p)
    return f, np.where(d < -140, np.nan, d)     # points the CMU200 returned as invalid


INK, INK2, GRID = "#0b0b0b", "#52514e", "#d9d8d4"
CMU_C, RX_C = "#2a78d6", "#eb6834"                    # categorical slots 1 and 2
RAMP = {0: "#86b6ef", 2: "#6da7ec", 4: "#5598e7", 6: "#2a78d6", 8: "#1c5cab", 10: "#0d366b"}


def style(ax):
    ax.grid(color=GRID, lw=0.6)
    for sp in ("top", "right"):
        ax.spines[sp].set_visible(False)
    for sp in ("left", "bottom"):
        ax.spines[sp].set_color(INK2)
    ax.tick_params(colors=INK2, labelsize=9)


def plot_spectra(doc, res, filt, path, levels=(None, 0, 6, 10)):
    speech = {p["cessb_db"]: p for p in doc["points"] if p.get("kind") == "speech"}
    floor = next((p for p in doc["points"] if p.get("kind") == "tx_floor"), None)
    fig, ax = plt.subplots(figsize=(11, 5.0))
    style(ax)
    ref_pep = speech[None]["pep_dbm"]
    for c in levels:
        if c not in speech:
            continue
        p = speech[c]
        fr, avg = stitched(p)
        col = INK2 if c is None else RAMP.get(c, CMU_C)
        ax.plot(fr, avg - p["pep_dbm"], lw=1.6 if c is None else 1.1, color=col,
                label="CESSB off" if c is None else f"CESSB {c} dB")
    if floor is not None:
        f, p, _ = merged(floor)
        k = 25                       # two sweeps only: smooth over 25 points, in power
        sm = np.convolve(p, np.ones(k) / k, mode="same")
        ax.plot(f[k:-k], db10(sm)[k:-k] - ref_pep, color="#a9a8a2", lw=1.0,
                label="keyed, no audio (smoothed)")
    f0, _ = stitched(speech[None])
    chans = [k for k, c in CHANNELS.items() if covered(f0, c)]
    lo, hi = f0.min(), f0.max()
    ax.set_xlim(lo, hi)
    ax.set_ylim(-125, -5)
    fgrid = np.linspace(lo, hi, 4001)
    ax2 = ax.twinx()               # the receiver passbands, as shading only (no scale)
    names = {"wanted": "wanted", "upper": "upper adjacent", "lower": "lower adjacent",
             "upper_alt": "upper alternate", "lower_alt": "lower alternate"}
    for name in chans:
        c = CHANNELS[name]
        ax2.fill_between(fgrid, 0, filt.power(fgrid - c), color="#86b6ef" if name == "wanted"
                         else "#e9e8e4", alpha=0.45, lw=0)
        ax.text(c + 1500, -122, names[name], ha="center", va="bottom", fontsize=8.5, color=INK2)
    ax2.set_ylim(0, 1.08)
    ax2.set_yticks([])
    ax2.set_zorder(0)
    ax.set_zorder(1)
    ax.patch.set_visible(False)
    for sp in ax2.spines.values():
        sp.set_visible(False)
    ax.set_xlabel("Frequency relative to the carrier, Hz (positive = wanted sideband)", color=INK)
    ax.set_ylabel("dB re PEP, in 100 Hz RBW", color=INK)
    ax.legend(fontsize=8.5, ncol=5, loc="lower center", bbox_to_anchor=(0.5, 1.0),
              frameon=False)
    fig.tight_layout()
    fig.savefig(path, dpi=110, facecolor="white")
    plt.close(fig)


def plot_acpr(res, path, rx=None):
    """Adjacent/wanted ratio against CESSB setting, CMU200 and (optionally) radio 1."""
    order = [None, 0, 2, 4, 6, 8, 10]
    xpos = {c: i for i, c in enumerate(order)}
    labels = ["Off"] + [str(c) for c in order[1:]]
    fig, axs = plt.subplots(1, 2, figsize=(11, 4.3), sharey=True)
    for ax, key, title in ((axs[0], "upper", "Upper adjacent channel (+3 kHz)"),
                           (axs[1], "lower", "Lower adjacent channel (\u22123 kHz)")):
        style(ax)
        sp = [r for r in res["speech"] if r["cessb_db"] in xpos]
        x = np.array([xpos[r["cessb_db"]] for r in sp], float)
        y = np.array([r[key + "_acpr_db"] for r in sp])
        lo = np.array([min([r[key + "_acpr_db"]] + r[key + "_acpr_halves"]) for r in sp])
        hi = np.array([max([r[key + "_acpr_db"]] + r[key + "_acpr_halves"]) for r in sp])
        ax.vlines(x - 0.08, lo, hi, color=CMU_C, lw=2, alpha=0.5)
        ax.plot(x - 0.08, y, "o", color=CMU_C, ms=7, mec="white", mew=1.5,
                label="CMU200 spectrum (bar: pass to pass)")
        if rx is not None:
            rs = [r for r in rx["rows"] if r["kind"] == "speech" and r["cessb_db"] in xpos]
            xr = np.array([xpos[r["cessb_db"]] for r in rs], float) + 0.08
            yr = np.array([r.get(key + "_acpr_corr_db", r[key + "_acpr_db"]) for r in rs])
            ax.plot(xr, yr, "s", color=RX_C, ms=7, mec="white", mew=1.5,
                    label="radio 1 as receiver (floor-corrected)")
        ax.set_xticks(range(len(order)))
        ax.set_xticklabels(labels)
        ax.axvline(0.5, color=GRID, lw=1)
        ax.set_xlabel("CESSB level, dB", color=INK)
        ax.set_title(title, fontsize=10, color=INK)
    axs[0].set_ylabel("Adjacent / wanted channel power, dB", color=INK)
    axs[0].legend(fontsize=8.5, frameon=False, loc="upper left")
    fig.tight_layout()
    fig.savefig(path, dpi=110, facecolor="white")
    plt.close(fig)


def html_rows(res):
    out = []
    for r in res["speech"]:
        def ci(k):
            c = r.get(k + "_acpr_halves")
            return " / ".join(f"{v:.1f}" for v in c) if c else ""
        out.append(
            f"<tr><td>{label_of(r)}</td><td class=\"num\">{r['pep_w']:.2f}</td>"
            f"<td class=\"num\">{r['avg_w']:.2f}</td><td class=\"num\">{r['papr_db']:.1f}</td>"
            f"<td class=\"num\"><strong>{r['upper_acpr_db']:.1f}</strong></td>"
            f"<td class=\"num\">{ci('upper')}</td>"
            f"<td class=\"num\">{r['lower_acpr_db']:.1f}</td><td class=\"num\">{ci('lower')}</td>"
            f"<td class=\"num\">{r['upper_alt_acpr_db']:.1f}</td>"
            f"<td class=\"num\">{r['lower_alt_acpr_db']:.1f}</td>"
            f"<td class=\"num\">{r['upper_re_pep_db']:.1f}</td></tr>")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("json")
    ap.add_argument("--out-prefix")
    ap.add_argument("--rx", help="cessb_acp_rx.py results to overlay")
    args = ap.parse_args()
    doc = load(args.json)
    filt = RxFilter()
    res = analyse(doc, filt)
    prefix = args.out_prefix or os.path.splitext(args.json)[0]
    plot_spectra(doc, res, filt, prefix + "_spectra.png")
    rx = None
    if args.rx:
        from cessb_hil.acp_rx_analysis import analyse as rx_analyse
        with open(args.rx) as fh:
            rx = rx_analyse(__import__("json").load(fh))
    plot_acpr(res, prefix + "_acpr.png", rx)
    print(html_rows(res))
    print(f"wrote {prefix}_spectra.png, {prefix}_acpr.png", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
