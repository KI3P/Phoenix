#!/usr/bin/env python3
"""Figures for the transmit filter HIL suite. JSON in, PNGs out.

Runs without hardware, so a result saved months ago can be redrawn after the
plotting changes:

    python3 plot_tx_filter_hil.py results/tx_filter_hil_20260729_120000.json

Every figure overlays the sample rates on one set of axes. That is deliberate:
the question these plots answer is whether two curves lie on top of each other,
and side-by-side panels make that the reader's job instead of the plot's.
"""

from __future__ import annotations

import json
import os
import sys
from typing import Sequence

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from tx_filter_hil import bandtable as bt
else:
    from . import bandtable as bt

#: One colour per rate, assigned in descending rate order so 192k is always
#: the same colour across every figure and every run.
RATE_COLOURS = ("#1f77b4", "#d62728", "#2ca02c", "#ff7f0e")


def _rates(doc: dict) -> list:
    return sorted(doc.get("rates", {}), key=lambda r: -int(r))


def _colour(i: int) -> str:
    return RATE_COLOURS[i % len(RATE_COLOURS)]


def _label(rate: str) -> str:
    return f"{int(rate)/1000:.1f} ksps"


def _finish(fig, path: str) -> str:
    fig.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    fig.savefig(path, dpi=130)
    plt.close(fig)
    return path


def _valid_xy(points: Sequence[dict], key: str) -> tuple[np.ndarray, np.ndarray]:
    xs, ys = [], []
    for p in points:
        y = p.get(key)
        if y is not None and p.get("valid", True):
            xs.append(p["f_audio_hz"])
            ys.append(y)
    return np.asarray(xs, dtype=float), np.asarray(ys, dtype=float)


# --- figures ---------------------------------------------------------------

def plot_passband(doc: dict, path: str) -> str:
    """The headline figure: normalised transmit response at every rate.

    Normalised to each rate's own passband reference, because the absolute level
    depends on the transmit gain and says nothing about the filters. The dotted
    line marks where the high corner would sit if the coefficient tables were
    frozen again - the two solid curves should be nowhere near it.
    """
    fig, ax = plt.subplots(figsize=(9, 5.5))
    rates = _rates(doc)

    for i, r in enumerate(rates):
        pb = doc["rates"][r].get("passband", {})
        if not pb:
            continue
        f, level = _valid_xy(pb.get("points", []), "level_dbv")
        ref = pb.get("reference_dbv")
        if f.size == 0 or ref is None:
            continue
        ax.semilogx(f, level - ref, "o-", ms=3, lw=1.3, color=_colour(i),
                    label=f"{_label(r)}  (-3 dB at "
                          f"{pb.get('corner_hi_3db_hz', float('nan')):.0f} Hz)")

    base = doc["rates"].get(rates[0], {}).get("passband", {}) if rates else {}
    corner = base.get("corner_hi_3db_hz")
    if corner:
        ax.axvline(corner, color=_colour(0), ls=":", lw=1, alpha=0.6)
        broken = corner * bt.LEGACY_RATE_RATIO
        ax.axvline(broken, color="k", ls=":", lw=1.4,
                   label=f"where frozen tables would put it ({broken:.0f} Hz, "
                         f"{bt.LEGACY_DELTA_PCT:.2f}%)")

    ax.axhline(-3.0, color="grey", ls="--", lw=0.8)
    ax.text(105, -2.7, "-3 dB", color="grey", fontsize=8)
    window = base.get("passband_window_hz")
    if window:
        ax.axvspan(window[0], window[1], color="grey", alpha=0.08)
        ax.text(np.sqrt(window[0] * window[1]), 2.0, "reference window",
                ha="center", fontsize=8, color="grey")

    ax.set_xlabel("microphone frequency (Hz)")
    ax.set_ylabel("transmit response (dB, normalised to passband)")
    ax.set_title("Transmit audio passband, exciter I/Q output")
    ax.set_ylim(-45, 5)
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower left")
    return _finish(fig, path)


def plot_sideband(doc: dict, path: str) -> str:
    """Opposite-sideband suppression against frequency.

    Checked against a floor rather than for rate invariance: the Hilbert table is
    not regenerated per rate and its usable band is *meant* to scale with Fs, so
    the two curves are expected to differ slightly at the edges.
    """
    fig, ax = plt.subplots(figsize=(9, 5))
    rates = _rates(doc)
    floor = None

    for i, r in enumerate(rates):
        sb = doc["rates"][r].get("sideband_suppression", {})
        pts = sb.get("points", [])
        if not pts:
            continue
        f = np.asarray([p["f_audio_hz"] for p in pts], dtype=float)
        y = np.asarray([p["suppression_db"] if p["suppression_db"] is not None
                        else np.nan for p in pts], dtype=float)
        ax.semilogx(f, y, "o-", ms=3, lw=1.3, color=_colour(i),
                    label=f"{_label(r)}  (worst "
                          f"{sb.get('worst_db', float('nan')):.1f} dB)")
        band = sb.get("hilbert_band_hz")
        if band:
            ax.axvline(band[0], color=_colour(i), ls=":", lw=0.9, alpha=0.5)

    for c in doc.get("checks", []):
        if c["id"] == "sideband.worst" and c["limit"] is not None:
            floor = -c["limit"] if c["limit"] < 0 else c["limit"]
    if floor is not None:
        ax.axhline(floor, color="k", ls="--", lw=1.0,
                   label=f"required floor ({floor:.0f} dB)")

    ax.set_xlabel("microphone frequency (Hz)")
    ax.set_ylabel("wanted over image (dB)")
    ax.set_title("Opposite-sideband suppression\n"
                 "dotted verticals: lower edge of the Hilbert transform's band, "
                 "which scales with Fs by design")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower right")
    return _finish(fig, path)


def plot_alias(doc: dict, path: str) -> str:
    """Out-of-band rejection and any fold-back products.

    The fold point moves with the sample rate - it is a quarter of the audio
    rate - so the two rates are probed over the same input frequencies but their
    products land in different places. Both are drawn against the input
    frequency, with the fold point marked per rate.
    """
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 7), sharex=True)
    rates = _rates(doc)
    drew_fold = False

    # The fold point and the noise floor go in the legend rather than as text on
    # the axes: they are per-rate, and two rates' annotations land close enough
    # together to overprint each other.
    for i, r in enumerate(rates):
        al = doc["rates"][r].get("alias", {})
        pts = al.get("points", [])
        if not pts:
            continue
        f = np.asarray([p["f_audio_hz"] for p in pts], dtype=float)
        direct = np.asarray([p["direct_dbc"] if p["direct_dbc"] is not None
                             else np.nan for p in pts], dtype=float)
        nyq = al.get("nyquist_after_fold_hz")
        floor = al.get("noise_floor_dbc")

        tag = _label(r)
        if nyq:
            tag += f", fold {nyq:.0f} Hz"
        if floor is not None:
            tag += f", floor {floor:.0f} dBc"

        if np.any(np.isfinite(direct)):
            ax1.plot(f, direct, "o-", ms=3, lw=1.3, color=_colour(i), label=tag)

        fold = np.asarray([p["fold_dbc"] if p.get("fold_dbc") is not None
                           else np.nan for p in pts], dtype=float)
        if np.any(np.isfinite(fold)):
            ax2.plot(f, fold, "s-", ms=3, lw=1.3, color=_colour(i), label=tag)
            drew_fold = True

        if nyq:
            for ax in (ax1, ax2):
                ax.axvline(nyq, color=_colour(i), ls=":", lw=1.1, alpha=0.7)
        # A curve sitting on the floor means "nothing measurable", not "exactly
        # this much", so the floor has to be visible to read the plot honestly.
        if floor is not None:
            for ax in (ax1, ax2):
                ax.axhline(floor, color=_colour(i), ls="-.", lw=0.9, alpha=0.55)

    # One limit line per axis, however many rates contributed a check.
    for ax, suffix in ((ax1, "alias.direct_rejection"), (ax2, "alias.fold_back")):
        limit = next((c["limit"] for c in doc.get("checks", [])
                      if c["id"].endswith(suffix) and c["limit"] is not None), None)
        if limit is not None:
            ax.axhline(limit, color="k", ls="--", lw=1.0,
                       label=f"limit ({limit:.0f} dBc)")

    ax1.set_ylabel("direct level (dBc)")
    ax1.set_title("Microphone audio above the transmit passband\n"
                  "top: what comes straight through, below the fold point.  "
                  "bottom: what folds back, above it")
    ax1.grid(True, alpha=0.25)
    ax1.legend(fontsize=7, loc="lower left")

    ax2.set_xlabel("microphone frequency (Hz)")
    ax2.set_ylabel("fold-back product (dBc)")
    ax2.grid(True, alpha=0.25)
    if drew_fold:
        ax2.legend(fontsize=7, loc="lower left")
    else:
        ax2.text(0.5, 0.5, "no fold-back product was measurable",
                 transform=ax2.transAxes, ha="center", fontsize=10, color="grey")
    return _finish(fig, path)


def plot_summary(doc: dict, path: str) -> str:
    """One panel per compared corner: the rate-invariance result at a glance."""
    rows = doc.get("comparison", {}).get("corners", [])
    if not rows:
        rows = []
    fig, ax = plt.subplots(figsize=(9, max(2.5, 0.9 * len(rows) + 1.8)))

    if not rows:
        ax.text(0.5, 0.5, "no cross-rate comparison in this result",
                transform=ax.transAxes, ha="center", fontsize=11, color="grey")
        ax.axis("off")
        return _finish(fig, path)

    labels = [r["label"] for r in rows]
    deltas = [r["delta_pct"] if r["delta_pct"] is not None else np.nan
              for r in rows]
    y = np.arange(len(rows))
    colours = ["#2ca02c" if r["verdict"] == "PASS"
               else "#999999" if r["verdict"] == "SKIP" else "#d62728"
               for r in rows]

    ax.barh(y, deltas, color=colours, height=0.55)
    tol = None
    for c in doc.get("checks", []):
        if c["id"].endswith(".rate_invariance") and c["limit"] is not None:
            tol = c["limit"]
            break
    if tol:
        ax.axvspan(-tol, tol, color="#2ca02c", alpha=0.12)
        ax.axvline(tol, color="#2ca02c", lw=0.9)
        ax.axvline(-tol, color="#2ca02c", lw=0.9,
                   label=f"tolerance +/-{tol:.1f}%")
    ax.axvline(bt.LEGACY_DELTA_PCT, color="k", ls=":", lw=1.5,
               label=f"frozen-table signature ({bt.LEGACY_DELTA_PCT:.3f}%)")
    ax.axvline(0.0, color="grey", lw=0.8)

    ax.set_yticks(y)
    ax.set_yticklabels(labels, fontsize=9)
    ax.invert_yaxis()
    ax.set_xlabel("change between sample rates (%)")
    ax.set_title(f"Transmit filter rate invariance - "
                 f"{doc['summary']['overall']}")
    ax.grid(True, axis="x", alpha=0.25)
    ax.legend(fontsize=8, loc="lower right")
    return _finish(fig, path)


def render_all(doc: dict, out_dir: str, prefix: str) -> list:
    """Render every figure the document supports. Missing data is skipped."""
    made = []
    figures = (
        ("passband", plot_passband),
        ("sideband", plot_sideband),
        ("alias", plot_alias),
        ("summary", plot_summary),
    )
    for name, fn in figures:
        path = os.path.join(out_dir, f"{prefix}_{name}.png")
        try:
            made.append(fn(doc, path))
        except Exception as exc:  # a missing test should not lose the others
            print(f"warning: could not render {name}: {exc}", file=sys.stderr)
    return made


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    for json_path in sys.argv[1:]:
        with open(json_path) as fh:
            doc = json.load(fh)
        out_dir = os.path.dirname(os.path.abspath(json_path))
        prefix = os.path.splitext(os.path.basename(json_path))[0]
        for png in render_all(doc, out_dir, prefix):
            print(png)
    return 0


if __name__ == "__main__":
    sys.exit(main())
