#!/usr/bin/env python3
"""Plot per-flag duration distributions from flag_timing.py JSON output.

For each flag value with enough events, produces a 2-panel PNG:
    top:    histogram of segment durations (with mean / median / p95 / p99
            marked as vertical lines) — makes bimodal distributions obvious.
    bottom: scatter of duration vs. iteration index — reveals drift or
            clustering of slow events over the capture window.

Also writes a single timeline strip showing the full capture, colored by
flag, useful for spotting where slow events sit in real time.

Usage:
    plot_flag_timing.py PROFILE.json
    plot_flag_timing.py PROFILE.json --out-dir docs/ --prefix baseline \\
        --flag-labels 1=DrawDisplay-other,2=SignalProcessing,3=DrawSpectrumPane

Outputs are written next to the JSON by default. PNG paths are printed to
stdout unless --quiet.
"""

import argparse
import json
import os
import sys

import warnings

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# tight_layout occasionally complains about in-axes legends and grid spec
# height ratios used here; the layout still renders correctly.
warnings.filterwarnings(
    "ignore", message=".*not compatible with tight_layout.*")


# Distinct colors for flag values 0..15. Picked to remain readable when
# adjacent in the timeline strip.
FLAG_COLORS = {
    0: "#cccccc", 1: "#1f77b4", 2: "#ff7f0e", 3: "#2ca02c",
    4: "#d62728", 5: "#9467bd", 6: "#8c564b", 7: "#e377c2",
    8: "#7f7f7f", 9: "#bcbd22", 10: "#17becf",
    11: "#aec7e8", 12: "#ffbb78", 13: "#98df8a",
    14: "#ff9896", 15: "#c5b0d5",
}


def parse_labels(arg):
    out = {}
    if not arg:
        return out
    for item in arg.split(","):
        item = item.strip()
        if "=" not in item:
            continue
        k, v = item.split("=", 1)
        out[int(k.strip())] = v.strip()
    return out


def label_for(flag, labels):
    name = labels.get(flag)
    return f"Flag {flag} ({name})" if name else f"Flag {flag}"


def plot_flag(flag, flag_segments, stats, label, out_path):
    """Two-panel figure: duration histogram + per-iteration scatter."""
    durations_us = np.array(
        [s["duration_s"] * 1e6 for s in flag_segments], dtype=np.float64)
    times_ms = np.array(
        [s["start_time_s"] * 1e3 for s in flag_segments], dtype=np.float64)

    fig, (ax_hist, ax_scat) = plt.subplots(
        2, 1, figsize=(9, 6.5),
        gridspec_kw={"height_ratios": [1.3, 1.0], "hspace": 0.35},
    )

    color = FLAG_COLORS.get(flag, "#444444")

    # ---- Histogram ------------------------------------------------------
    # Auto bin count: sqrt rule, capped, with a floor.
    nbins = int(np.clip(np.sqrt(durations_us.size) * 2.5, 12, 80))
    ax_hist.hist(durations_us, bins=nbins, color=color, alpha=0.75,
                 edgecolor="black", linewidth=0.4)

    markers = [
        ("median_s", "median", "#222222", "--"),
        ("mean_s",   "mean",   "#222222", "-"),
        ("p95_s",    "p95",    "#b00000", ":"),
        ("p99_s",    "p99",    "#b00000", "-."),
    ]
    for key, mlabel, mcolor, style in markers:
        v = stats[key] * 1e6
        ax_hist.axvline(v, color=mcolor, linestyle=style, linewidth=1.2,
                        label=f"{mlabel} {_fmt_us(v)}")

    ax_hist.set_xlabel("Segment duration (µs)")
    ax_hist.set_ylabel("Count")
    ax_hist.set_title(
        f"{label} — distribution  (n={stats['count']}, "
        f"range {_fmt_us(stats['min_s']*1e6)}–{_fmt_us(stats['max_s']*1e6)})"
    )
    ax_hist.grid(True, alpha=0.3)
    ax_hist.legend(loc="upper right", fontsize=8, framealpha=0.85)

    # ---- Per-iteration scatter ------------------------------------------
    ax_scat.scatter(times_ms, durations_us, s=18, color=color,
                    alpha=0.7, edgecolor="black", linewidth=0.3)
    ax_scat.axhline(stats["median_s"] * 1e6, color="#222222",
                    linestyle="--", linewidth=1, label="median")
    ax_scat.axhline(stats["p95_s"] * 1e6, color="#b00000",
                    linestyle=":", linewidth=1, label="p95")
    ax_scat.set_xlabel("Time in capture (ms)")
    ax_scat.set_ylabel("Duration (µs)")
    ax_scat.set_title(f"{label} — duration over time")
    ax_scat.grid(True, alpha=0.3)
    ax_scat.legend(loc="upper right", fontsize=8, framealpha=0.85)

    plt.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)


def plot_timeline(segments, duration_s, labels, out_path):
    """Stacked timeline: one horizontal row per flag value, each row showing
    that flag's segments as filled bars over the capture window. Readable
    even when some flags have short segments that would be invisible in a
    single-row strip.
    """
    flags_present = sorted({s["flag"] for s in segments})
    n_rows = len(flags_present)
    row_for_flag = {f: i for i, f in enumerate(flags_present)}

    fig_height = max(2.0, 0.6 * n_rows + 1.2)
    fig, ax = plt.subplots(figsize=(12, fig_height))

    for s in segments:
        row = row_for_flag[s["flag"]]
        ax.barh(
            row,
            s["duration_s"] * 1000,
            left=s["start_time_s"] * 1000,
            height=0.75,
            color=FLAG_COLORS.get(s["flag"], "#444444"),
            edgecolor="none",
        )

    ax.set_xlim(0, duration_s * 1000)
    ax.set_ylim(-0.6, n_rows - 0.4)
    ax.set_yticks(range(n_rows))
    ax.set_yticklabels([label_for(f, labels) for f in flags_present],
                       fontsize=9)
    ax.invert_yaxis()  # flag 0 at top, increasing flag value downward
    ax.set_xlabel("Time in capture (ms)")
    ax.set_title(f"Flag timeline — {duration_s*1000:.0f} ms capture "
                 f"(one row per flag)")
    ax.grid(True, axis="x", alpha=0.3)
    plt.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)


def _fmt_us(v):
    """Format microseconds with a reasonable precision."""
    if v >= 1000:
        return f"{v/1000:.2f} ms"
    if v >= 100:
        return f"{v:.0f} µs"
    if v >= 10:
        return f"{v:.1f} µs"
    return f"{v:.2f} µs"


def main(argv=None):
    p = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("input", help="Path to JSON file produced by flag_timing.py")
    p.add_argument("--out-dir", default=None,
                   help="Directory for output PNGs (default: same as input).")
    p.add_argument("--prefix", default=None,
                   help="Filename prefix for output PNGs (default: input "
                        "basename without extension).")
    p.add_argument("--flag-labels", default="",
                   help="Comma-separated 'flag=name' pairs for prettier "
                        "titles and legends, e.g. "
                        "'1=DrawDisplay-other,3=DrawSpectrumPane'.")
    p.add_argument("--min-count", type=int, default=2,
                   help="Skip per-flag plots when the flag has fewer than "
                        "this many segments (default 2).")
    p.add_argument("--no-timeline", action="store_true",
                   help="Skip the timeline strip plot.")
    p.add_argument("--quiet", action="store_true",
                   help="Suppress 'wrote ...' messages on stdout.")
    args = p.parse_args(argv)

    with open(args.input) as f:
        data = json.load(f)
    if "error" in data:
        print(f"Input JSON contains an error: {data['error']}",
              file=sys.stderr)
        return 1

    labels = parse_labels(args.flag_labels)
    out_dir = (args.out_dir
               or os.path.dirname(os.path.abspath(args.input))
               or ".")
    prefix = (args.prefix
              or os.path.splitext(os.path.basename(args.input))[0])
    os.makedirs(out_dir, exist_ok=True)

    segments = data.get("segments", [])
    stats_by_flag = data.get("summary", {}).get("flag_segment_stats", {})
    duration_s = data.get("metadata", {}).get("duration_s", 0.0)

    written = []
    for flag_str, stats in stats_by_flag.items():
        if stats.get("count", 0) < args.min_count:
            continue
        flag = int(flag_str)
        flag_segs = [s for s in segments if s["flag"] == flag]
        if len(flag_segs) < args.min_count:
            continue
        out_path = os.path.join(out_dir, f"{prefix}_flag{flag}.png")
        plot_flag(flag, flag_segs, stats, label_for(flag, labels), out_path)
        written.append(out_path)

    if not args.no_timeline and segments:
        out_path = os.path.join(out_dir, f"{prefix}_timeline.png")
        plot_timeline(segments, duration_s, labels, out_path)
        written.append(out_path)

    if not args.quiet:
        for w in written:
            print(w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
