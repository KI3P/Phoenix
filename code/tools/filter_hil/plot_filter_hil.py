#!/usr/bin/env python3
"""Render figures from a filter HIL results file.

Takes the JSON written by ``filter_hil_test.py`` and produces the PNGs the
Markdown report embeds. Kept separate from the measurement so figures can be
regenerated - restyled, re-cropped, argued about - long after the rig has been
packed away, which is the same split as flag_timing.py and plot_flag_timing.py.

    python3 plot_filter_hil.py results/filter_hil_20260728_101500.json
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from filter_hil import bandtable as bt
else:
    from . import bandtable as bt

DPI = 110
RATE_STYLE = {0: dict(ls="-", lw=1.8), 1: dict(ls="--", lw=1.3)}


def _footer(fig, doc: dict) -> None:
    """Stamp provenance under every figure so a stray PNG is still traceable."""
    p = doc.get("provenance", {})
    rig = doc.get("rig", {})
    bits = [
        doc.get("generated_utc", ""),
        f"commit {p.get('git_commit', '?')}" + ("+dirty" if p.get("git_dirty") else ""),
        rig.get("wiring", ""),
        f"drive {rig.get('drive_amplitude_v', 0)*1000:.0f} mV",
    ]
    fig.text(0.5, 0.012, "  |  ".join(b for b in bits if b), ha="center", fontsize=7,
             bbox=dict(boxstyle="round", facecolor="wheat", alpha=0.5))


def _rates(doc: dict) -> list:
    return sorted(doc.get("rates", {}), key=lambda r: -int(r))


def _save(fig, out_dir: str, name: str, paths: list) -> None:
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name)
    fig.savefig(path, dpi=DPI, bbox_inches="tight")
    plt.close(fig)
    paths.append(path)
    print(path)


def plot_cw_overlay(doc: dict, out_dir: str, prefix: str, paths: list) -> None:
    """Both rates on one axis, with the shift the old firmware would have shown.

    This is the figure to look at first: if the solid and dashed curves for a
    given filter lie on top of each other and well away from the dotted
    prediction, the filters are tracking the sample rate correctly.
    """
    rates = _rates(doc)
    have = [r for r in rates if doc["rates"][r].get("cw_filters")]
    if not have:
        return

    fig, ax = plt.subplots(figsize=(11, 6.5))
    colors = plt.cm.viridis(np.linspace(0, 0.85, len(bt.CW_FILTER_NOMINAL_HZ)))

    for ri, rate in enumerate(have):
        for f in doc["rates"][rate]["cw_filters"]["filters"]:
            k = f["index"]
            # Merge the bisection probes into the coarse sweep. A 12 pole
            # Chebyshev falls from 0 to -40 dB between adjacent coarse points,
            # so without them the plot shows a flat line and a cliff; the
            # bisection is exactly where the interesting detail was measured.
            pts = [(p["f_audio_hz"], p["resp_db"]) for p in f["points"]
                   if p["resp_db"] is not None]
            pts += [(t[0], t[1]) for t in f.get("bisection_trace", [])
                    if t[1] is not None]
            if not pts:
                continue
            pts = sorted(set(pts))
            x, y = zip(*sorted(pts))
            ax.semilogx(x, y, color=colors[k], **RATE_STYLE.get(ri, RATE_STYLE[1]),
                        label=f"{f['nominal_hz']:.0f} Hz @ {int(rate)//1000}k")

    # Where the corners would sit if the coefficient tables were still frozen.
    if len(have) > 1:
        base = have[0]
        for f in doc["rates"][base]["cw_filters"]["filters"]:
            c = f.get("corner_3db_hz")
            if c:
                ax.axvline(c * bt.LEGACY_RATE_RATIO, color="red", ls=":", lw=1.0, alpha=0.6)
        ax.plot([], [], color="red", ls=":", lw=1.0,
                label=f"where the old frozen tables would put them "
                      f"({bt.LEGACY_DELTA_PCT:.2f}%)")

    ax.axhline(-3.0, color="k", lw=0.8, alpha=0.5)
    ax.set_xlabel("audio frequency (Hz)")
    ax.set_ylabel("filter response (dB)")
    ax.set_ylim(-45, 6)
    ax.set_xlim(200, 5000)
    ax.grid(alpha=0.3, which="both")
    ax.legend(fontsize=7, ncol=2, loc="lower left")
    solid = f"{int(have[0])//1000}k solid"
    dashed = f", {int(have[1])//1000}k dashed" if len(have) > 1 else ""
    ax.set_title(f"CW audio filters at both sample rates ({solid}{dashed})")
    _footer(fig, doc)
    _save(fig, out_dir, f"{prefix}_cw_overlay.png", paths)


def plot_eq_centres(doc: dict, out_dir: str, prefix: str, paths: list) -> None:
    """Measured versus labelled centre, and the error per cell."""
    rates = _rates(doc)
    have = [r for r in rates if doc["rates"][r].get("equalizer_cells")]
    if not have:
        return

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 8))

    for ri, rate in enumerate(have):
        cells = doc["rates"][rate]["equalizer_cells"]["cells"]
        nom = [c["nominal_hz"] for c in cells if c["peak_hz"]]
        meas = [c["peak_hz"] for c in cells if c["peak_hz"]]
        ax1.loglog(nom, meas, "o" if ri == 0 else "s", ms=5,
                   label=f"{int(rate)//1000}k sps", alpha=0.8)
        err = [100.0 * (m - n) / n for n, m in zip(nom, meas)]
        idx = [c["index"] for c in cells if c["peak_hz"]]
        ax2.plot(idx, err, "o-" if ri == 0 else "s--", ms=4,
                 label=f"{int(rate)//1000}k sps", alpha=0.85)

    lim = [150, 5000]
    ax1.plot(lim, lim, "k-", lw=0.8, alpha=0.5, label="ideal")
    ax1.set_xlabel("labelled centre (Hz)")
    ax1.set_ylabel("measured peak (Hz)")
    ax1.set_title("Equaliser cell centres")
    ax1.grid(alpha=0.3, which="both")
    ax1.legend(fontsize=8)

    tol = doc.get("config", {}).get("nominal_tol_pct", 4.0)
    ax2.axhspan(-tol, tol, color="green", alpha=0.12,
                label=f"nominal tolerance +/-{tol:.0f}%")
    ax2.axhline(bt.LEGACY_DELTA_PCT, color="red", ls=":", lw=1.2,
                label=f"old frozen-table shift ({bt.LEGACY_DELTA_PCT:.2f}%)")
    ax2.axhline(0.0, color="k", lw=0.8)
    ax2.set_xlabel("equaliser cell")
    ax2.set_ylabel("error vs labelled (%)")
    ax2.set_xticks(range(bt.EQ_CELL_COUNT))
    ax2.grid(alpha=0.3)
    ax2.legend(fontsize=8)

    _footer(fig, doc)
    plt.tight_layout(rect=[0, 0.035, 1, 1])
    _save(fig, out_dir, f"{prefix}_eq_centres.png", paths)


def plot_eq_cells(doc: dict, out_dir: str, prefix: str, paths: list) -> None:
    """The response of every cell, per rate."""
    for rate in _rates(doc):
        cells = doc["rates"][rate].get("equalizer_cells", {}).get("cells")
        if not cells:
            continue
        fig, ax = plt.subplots(figsize=(11, 6))
        colors = plt.cm.turbo(np.linspace(0, 1, bt.EQ_CELL_COUNT))
        for c in cells:
            pts = [(p["f_audio_hz"], p["level_dbv"]) for p in c["points"]
                   if p.get("level_dbv") is not None and p.get("valid")]
            if len(pts) < 2:
                continue
            x, y = zip(*sorted(pts))
            y = np.asarray(y) - max(y)
            ax.semilogx(x, y, color=colors[c["index"]], lw=1.4)
            if c["peak_hz"]:
                ax.axvline(c["nominal_hz"], color=colors[c["index"]], lw=0.6, alpha=0.4)
        ax.set_xlabel("audio frequency (Hz)")
        ax.set_ylabel("normalised response (dB)")
        ax.set_ylim(-25, 3)
        ax.grid(alpha=0.3, which="both")
        ax.set_title(f"Equaliser cells at {int(rate)//1000} ksps "
                     f"(vertical lines are the labelled centres)")
        _footer(fig, doc)
        _save(fig, out_dir, f"{prefix}_eq_cells_{rate}.png", paths)


def plot_ssb_control(doc: dict, out_dir: str, prefix: str, paths: list) -> None:
    """The control chart: commanded bandwidth against measured edge."""
    rates = _rates(doc)
    have = [r for r in rates if doc["rates"][r].get("ssb_filter")]
    if not have:
        return

    fig, ax = plt.subplots(figsize=(9, 6))
    for ri, rate in enumerate(have):
        rows = doc["rates"][rate]["ssb_filter"]["settings"]
        x = [r["fw_hz"] for r in rows if r["hi_edge_6db_hz"]]
        y = [r["hi_edge_6db_hz"] for r in rows if r["hi_edge_6db_hz"]]
        ax.plot(x, y, "o-" if ri == 0 else "s--", ms=6,
                label=f"{int(rate)//1000}k sps")
    lo, hi = 1500, 3400
    ax.plot([lo, hi], [lo, hi], "k-", lw=0.8, alpha=0.5, label="ideal")
    ax.set_xlabel("commanded bandwidth, FW (Hz)")
    ax.set_ylabel("measured -6 dB edge (Hz)")
    ax.set_title("SSB convolution filter - the control\n"
                 "This filter always derived its coefficients from the true sample "
                 "rate,\nso a shift here means the rig or the analysis is wrong, "
                 "not the firmware")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    _footer(fig, doc)
    plt.tight_layout(rect=[0, 0.04, 1, 1])
    _save(fig, out_dir, f"{prefix}_ssb_control.png", paths)


def plot_am(doc: dict, out_dir: str, prefix: str, paths: list) -> None:
    """AM DC blocker envelope response."""
    rates = _rates(doc)
    have = [r for r in rates if doc["rates"][r].get("am_dc_blocker")]
    if not have:
        return
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for ri, rate in enumerate(have):
        d = doc["rates"][rate]["am_dc_blocker"]
        pts = [(p["f_mod_hz"], p["resp_db"]) for p in d["points"]
               if p["resp_db"] is not None]
        if not pts:
            continue
        x, y = zip(*sorted(pts))
        ax.semilogx(x, y, "o-" if ri == 0 else "s--", ms=4,
                    label=f"{int(rate)//1000}k sps "
                          f"(-3 dB at {d.get('corner_3db_hz', float('nan')):.1f} Hz)")
    ax.axhline(-3.0, color="k", lw=0.8, alpha=0.6)
    ax.axvline(bt.AM_DC_BLOCKER_CORNER_HZ, color="green", ls="--", lw=1.0,
               label=f"design corner {bt.AM_DC_BLOCKER_CORNER_HZ:.0f} Hz")
    ax.set_xlabel("modulation frequency (Hz)")
    ax.set_ylabel("envelope response (dB)")
    ax.set_title("AM demodulator DC blocker")
    ax.grid(alpha=0.3, which="both")
    ax.legend(fontsize=8)
    _footer(fig, doc)
    _save(fig, out_dir, f"{prefix}_am_dc_blocker.png", paths)


def plot_summary(doc: dict, out_dir: str, prefix: str, paths: list) -> None:
    """Every cross-rate result on one chart, against the two reference lines."""
    comp = doc.get("comparison", {})
    rows = []
    for group, key, label in (("cw_filters", "corner_3db_hz", "CW"),
                              ("eq_cells", "peak_hz", "EQ"),
                              ("ssb_filter", "hi_edge_6db_hz", "SSB")):
        for r in comp.get(group, []):
            if r.get("delta_pct") is not None:
                rows.append((f"{label} {r['nominal_hz']:.0f}", r["delta_pct"],
                             r["verdict"]))
    if not rows:
        return

    labels, deltas, verdicts = zip(*rows)
    fig, ax = plt.subplots(figsize=(10, max(5, 0.28 * len(rows) + 2)))
    y = np.arange(len(rows))
    colors = ["tab:green" if v == "PASS" else "tab:red" for v in verdicts]
    ax.barh(y, deltas, color=colors, alpha=0.85)
    # A row that measured exactly zero would otherwise draw no bar at all and
    # read as missing data rather than as the best possible result.
    ax.scatter(deltas, y, s=14, color=colors, zorder=3)

    tol = doc.get("config", {}).get("rate_tol_pct", 1.5)
    ax.axvspan(-tol, tol, color="green", alpha=0.12, label=f"tolerance +/-{tol}%")
    ax.axvline(0.0, color="k", lw=1.0)
    ax.axvline(bt.LEGACY_DELTA_PCT, color="red", ls=":", lw=1.4,
               label=f"old frozen-table shift ({bt.LEGACY_DELTA_PCT:.3f}%)")

    ax.set_yticks(y)
    ax.set_yticklabels(labels, fontsize=7)
    ax.invert_yaxis()
    ax.set_xlabel("frequency change between sample rates (%)")
    s = doc["summary"]
    ax.set_title(f"Rate independence: {s['overall']} "
                 f"({s['passed']} passed, {s['failed']} failed, {s['skipped']} skipped)")
    ax.grid(alpha=0.3, axis="x")
    ax.legend(fontsize=8, loc="lower right")
    _footer(fig, doc)
    plt.tight_layout(rect=[0, 0.04, 1, 1])
    _save(fig, out_dir, f"{prefix}_summary.png", paths)


def plot_iq_detect(doc: dict, out_dir: str, prefix: str, paths: list) -> None:
    """The two quadrature trials, for when the wiring detection misbehaves."""
    trials = doc.get("rig", {}).get("trials")
    if not trials:
        return
    fig, ax = plt.subplots(figsize=(9, 4.5))
    names, levels, colors = [], [], []
    chosen_phase = doc["rig"].get("phase_sign")
    chosen_side = doc["rig"].get("sideband_sign")
    for key, t in sorted(trials.items()):
        ph, side = t.get("phase_sign", 0), t.get("sideband_sign", 0)
        names.append(f"W2 {ph*90:+d}\n{'above' if side > 0 else 'below'}")
        levels.append(t.get("level_dbv") or -140.0)
        colors.append("tab:green" if (ph == chosen_phase and side == chosen_side)
                      else "tab:blue")
    ax.bar(names, levels, color=colors, alpha=0.85)
    ax.set_xlabel("W2 phase / side of the demodulation centre")
    ax.set_ylabel("recovered audio level (dBV)")
    rej = doc["rig"].get("image_rejection_db")
    ax.set_title(f"I/Q wiring detection\n{doc['rig'].get('wiring', '')}"
                 + (f" - image rejection {rej:.1f} dB" if rej else ""))
    ax.grid(alpha=0.3, axis="y")
    _footer(fig, doc)
    plt.tight_layout(rect=[0, 0.06, 1, 1])
    _save(fig, out_dir, f"{prefix}_iq_detect.png", paths)


RENDERERS = {
    "summary": plot_summary,
    "cw": plot_cw_overlay,
    "eq": lambda *a: (plot_eq_centres(*a), plot_eq_cells(*a)),
    "ssb": plot_ssb_control,
    "am": plot_am,
    "iq": plot_iq_detect,
}


def render_all(doc: dict, out_dir: str, prefix: str,
               only: list | None = None) -> list:
    """Render every figure the document supports; returns the PNG paths."""
    paths: list = []
    for name, fn in RENDERERS.items():
        if only and name not in only:
            continue
        try:
            fn(doc, out_dir, prefix, paths)
        except Exception as exc:
            print(f"warning: could not render {name}: {exc}", file=sys.stderr)
    return paths


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("json_file")
    p.add_argument("--out-dir", default=None)
    p.add_argument("--prefix", default=None)
    p.add_argument("--only", default=None,
                   help=f"comma-separated subset of {','.join(RENDERERS)}")
    args = p.parse_args()

    with open(args.json_file) as fh:
        doc = json.load(fh)

    out_dir = args.out_dir or os.path.dirname(os.path.abspath(args.json_file))
    prefix = args.prefix or os.path.splitext(os.path.basename(args.json_file))[0]
    only = [x.strip() for x in args.only.split(",")] if args.only else None

    paths = render_all(doc, out_dir, prefix, only)
    if not paths:
        print("no figures rendered - does the results file contain any measurements?",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
