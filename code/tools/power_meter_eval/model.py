#!/usr/bin/env python3
"""How well does the Phoenix TX power reading track PEP and average power?

    venv/bin/python power_meter_eval/model.py            (from code/tools)

Offline model of the forward-power measurement on SSB voice, compared against
the true PEP and true average power of the transmitted envelope.

Signal path (reuses code/tools/cessb_model):
    voice/test signal -> mic_to_cessb_node() -> [centred CESSB] -> node (24 kHz)
    node -> cessb_node_to_dac() -> RF envelope at 192 kHz (the "truth")

Meters modelled:
  old      The firmware as of V1.6.0 (LPFBoard.cpp PerformSWRBridgeReading):
           AD8307 log detector, AD7991 sample every ~16 ms, 0.1/0.9 running
           average of the ADC counts (linear in dB), then dBm -> W.
  bridge   The same samples converted to W first, then averaged (1 s mean).
  bpk      Peak hold on the bridge samples (max over 1.5 s): what a bridge-only
           PEP meter would show.
  digital  Block peak and sum of |IQ|^2 at the CESSB node, 100 ms bins,
           PEP = max over 15 bins (1.5 s), AVG = mean over 10 bins (1 s).

Truth at each 4 Hz display instant: PEP = max RF envelope power over the last
1.5 s, AVG = mean RF envelope power over the last 1 s.

Writes results/summary.md, results/summary.json and results/voice_*.png.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np
from scipy import signal
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(TOOLS, "cessb_model"))
sys.path.insert(0, TOOLS)

import cessb  # noqa: E402
import phoenix_tx as tx  # noqa: E402
from cessb_hil import stimuli  # noqa: E402

FS = 192000.0
P_SET_W = 100.0            # power the envelope ceiling (1.0) maps to: PEP setpoint
DRIVE_CESSB_DB = 6.0       # voice peaks this far into the CESSB clipper

# Detector and ADC (LPFBoard.cpp, wiki/theory/log-amp-power-detection.md)
SLOPE_MV_DB = 25.0
INTERCEPT_DBM = -84.0
PATH_LOSS_DB = 46.0        # PAD_ATTENUATION_DB + COUPLER_ATTENUATION_DB
FLOOR_DET_DBM = -75.0      # AD8307 bottom of range, at the detector
VREF_MV = 4096.0
RIPPLE_DB = 0.5            # 2xRF ripple amplitude (about 10 MHz), sampled at a random phase
SAMPLE_MS = 16.0           # bridge update interval (swr_calibration_approach.md)
SAMPLE_JITTER_MS = 2.0
REF_DELAY_MS = 1.5         # reflected channel read after the forward one
GAMMA = 1.0 / 3.0          # SWR 2:1 load for the SWR check
EMA_ALPHA = 0.1

# Display and windows
DISPLAY_MS = 250.0
PEP_WINDOW_S = 1.5
AVG_WINDOW_S = 1.0
BIN_S = 0.1
BLOCK = 256                # node samples per TransmitProcessing() block
TALK_FRACTION = 0.01       # display instants with true AVG below this x P_SET are silence


# --------------------------------------------------------------------------
# Signals
# --------------------------------------------------------------------------

def load_voice(path: str) -> np.ndarray:
    rate, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x[:, 0]
    if rate != 48000:
        raise ValueError(f"{path}: expected 48 kHz, got {rate}")
    return signal.resample_poly(x / 32768.0, 4, 1)


def signals(voice_path: str | None, seconds: float = 10.0) -> dict[str, np.ndarray]:
    t = np.arange(int(FS * seconds)) / FS
    out = {
        "tone": np.sin(2 * np.pi * 1000.0 * t),
        "two_tone": stimuli.two_tone(4096).at_rate(FS, seconds),
        "syllable": stimuli.syllable(4096).at_rate(FS, seconds),
    }
    if voice_path and os.path.exists(voice_path):
        out["voice_W9GR"] = load_voice(voice_path)
    return out


def node_signal(x: np.ndarray, flt: tx.TxFilters, sign: int, use_cessb: bool) -> np.ndarray:
    """Complex envelope at the CESSB node, scaled so the ceiling is 1.0 = P_SET_W."""
    z = tx.mic_to_cessb_node(x, flt)
    skip = int(0.1 * flt.fs24)              # filter start-up
    peak = np.max(np.abs(z[skip:]))
    if not use_cessb:
        return z / peak                     # linear: highest peak just touches the ceiling
    z = z / peak * 10 ** (DRIVE_CESSB_DB / 20)
    return cessb.cessb_c(z, flt.fs24, sign)


# --------------------------------------------------------------------------
# Meters
# --------------------------------------------------------------------------

def det_mv(p_line_w: np.ndarray, rng: np.random.Generator) -> np.ndarray:
    """AD8307 output in mV, as the AD7991 reads it, for an instantaneous line power."""
    p_det_dbm = 10 * np.log10(np.maximum(p_line_w, 1e-15) * 1000) - PATH_LOSS_DB
    p_det_dbm = np.maximum(p_det_dbm, FLOOR_DET_DBM)
    p_det_dbm = p_det_dbm + RIPPLE_DB * np.cos(rng.uniform(0, 2 * np.pi, len(p_line_w)))
    mv = SLOPE_MV_DB * (p_det_dbm - INTERCEPT_DBM)
    return np.clip(np.round(mv), 0, VREF_MV - 1)        # 1 mV per count


def mv_to_w(mv: np.ndarray) -> np.ndarray:
    dbm = mv / SLOPE_MV_DB + INTERCEPT_DBM + PATH_LOSS_DB
    return 10 ** (dbm / 10) / 1000


def run_meters(name: str, z_node: np.ndarray, flt: tx.TxFilters, seed: int = 1) -> dict:
    rng = np.random.default_rng(seed)
    fs24 = flt.fs24
    z_rf = tx.cessb_node_to_dac(z_node, flt)
    p_rf = P_SET_W * np.abs(z_rf) ** 2                  # instantaneous line power, W
    dur = len(p_rf) / FS

    # Bridge samples
    times = []
    t = 0.05
    while t < dur - 0.01:
        times.append(t)
        t += (SAMPLE_MS + rng.uniform(-SAMPLE_JITTER_MS, SAMPLE_JITTER_MS)) / 1000
    times = np.array(times)
    idx_f = (times * FS).astype(int)
    idx_r = np.minimum(((times + REF_DELAY_MS / 1000) * FS).astype(int), len(p_rf) - 1)
    vf = det_mv(p_rf[idx_f], rng)
    vr = det_mv(GAMMA ** 2 * p_rf[idx_r], rng)

    # Old meter: EMA on counts, convert after (LPFBoard.cpp:631-648)
    ema_f = np.zeros_like(vf)
    ema_r = np.zeros_like(vr)
    ef = er = 0.0
    for k in range(len(vf)):
        ef = EMA_ALPHA * vf[k] + (1 - EMA_ALPHA) * ef
        er = EMA_ALPHA * vr[k] + (1 - EMA_ALPHA) * er
        ema_f[k], ema_r[k] = ef, er
    old_pf = mv_to_w(ema_f)
    old_pr = mv_to_w(ema_r)

    # New bridge: per-sample watts
    bf_w = mv_to_w(vf)
    br_w = mv_to_w(vr)

    # Digital: per block peak and sum at the node
    p_node = P_SET_W * np.abs(z_node) ** 2
    nblk = len(p_node) // BLOCK
    blk_peak = p_node[:nblk * BLOCK].reshape(nblk, BLOCK).max(axis=1)
    blk_sum = p_node[:nblk * BLOCK].reshape(nblk, BLOCK).sum(axis=1)
    blk_t = (np.arange(nblk) + 1) * BLOCK / fs24        # time the block completes
    bin_of_blk = np.floor(blk_t / BIN_S).astype(int)
    nbins = bin_of_blk.max() + 1
    bin_peak = np.zeros(nbins)
    bin_sum = np.zeros(nbins)
    bin_n = np.zeros(nbins)
    np.maximum.at(bin_peak, bin_of_blk, blk_peak)
    np.add.at(bin_sum, bin_of_blk, blk_sum)
    np.add.at(bin_n, bin_of_blk, BLOCK)

    # Display instants
    disp = np.arange(PEP_WINDOW_S + 0.5, dur - 0.01, DISPLAY_MS / 1000)
    rows = []
    for td in disp:
        i_end = int(td * FS)
        true_pep = p_rf[int((td - PEP_WINDOW_S) * FS):i_end].max()
        true_avg = p_rf[int((td - AVG_WINDOW_S) * FS):i_end].mean()
        last = np.searchsorted(times, td) - 1
        sel_avg = (times > td - AVG_WINDOW_S) & (times <= td)
        sel_pep = (times > td - PEP_WINDOW_S) & (times <= td)
        cur_bin = int(np.floor(td / BIN_S))            # in progress: use completed + current
        b_pep = slice(max(cur_bin - 14, 0), cur_bin + 1)
        b_avg = slice(max(cur_bin - 9, 0), cur_bin + 1)
        # only blocks completed by td count; recompute the current bin partially
        done = blk_t <= td
        cb = done & (bin_of_blk == cur_bin)
        pk = bin_peak.copy()
        sm = bin_sum.copy()
        nn = bin_n.copy()
        pk[cur_bin] = blk_peak[cb].max() if cb.any() else 0.0
        sm[cur_bin] = blk_sum[cb].sum()
        nn[cur_bin] = BLOCK * cb.sum()
        rows.append(dict(
            t=td, true_pep=true_pep, true_avg=true_avg,
            old=old_pf[last],
            old_swr=_swr(old_pf[last], old_pr[last]),
            bridge_ema=None,
            bridge_avg=bf_w[sel_avg].mean(),
            bridge_swr=_swr(bf_w[sel_avg].mean(), br_w[sel_avg].mean()),
            bridge_pep=bf_w[sel_pep].max(),
            dig_pep=pk[b_pep].max(),
            dig_avg=sm[b_avg].sum() / max(nn[b_avg].sum(), 1),
        ))
    return dict(name=name, rows=rows, p_rf=p_rf, times=times, bf_w=bf_w)


def _swr(pf: float, pr: float) -> float:
    if pf <= 0:
        return float("nan")
    a = min(np.sqrt(max(pr, 0) / pf), 0.999)
    return (1 + a) / (1 - a)


# --------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------

def db(a, b):
    return 10 * np.log10(np.maximum(a, 1e-12) / np.maximum(b, 1e-12))


def summarise(res: dict) -> dict:
    rows = [r for r in res["rows"] if r["true_avg"] > TALK_FRACTION * P_SET_W]
    if not rows:
        return {}
    col = lambda k: np.array([r[k] for r in rows])  # noqa: E731
    tp, ta = col("true_pep"), col("true_avg")
    out = dict(n=len(rows), papr_db=float(np.median(db(tp, ta))))

    def stats(x):
        return dict(median=float(np.median(x)), p10=float(np.percentile(x, 10)),
                    p90=float(np.percentile(x, 90)))

    out["old_vs_pep"] = stats(db(col("old"), tp))
    out["old_vs_avg"] = stats(db(col("old"), ta))
    out["bridge_avg_vs_avg"] = stats(db(col("bridge_avg"), ta))
    out["bridge_pep_vs_pep"] = stats(db(col("bridge_pep"), tp))
    out["dig_pep_vs_pep"] = stats(db(col("dig_pep"), tp))
    out["dig_avg_vs_avg"] = stats(db(col("dig_avg"), ta))
    out["old_swr"] = stats(col("old_swr"))
    out["bridge_swr"] = stats(col("bridge_swr"))
    return out


def plot(res: dict, path: str) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    rows = res["rows"]
    t = np.array([r["t"] for r in rows])
    fig, ax = plt.subplots(figsize=(11, 4.5))
    ax.plot(t, [r["true_pep"] for r in rows], "k-", lw=2, label="true PEP (1.5 s)")
    ax.plot(t, [r["true_avg"] for r in rows], "k--", lw=2, label="true AVG (1 s)")
    ax.plot(t, [r["old"] for r in rows], "r-", label="current meter")
    ax.plot(t, [r["dig_pep"] for r in rows], "b-", alpha=.7, label="digital PEP")
    ax.plot(t, [r["dig_avg"] for r in rows], "b--", alpha=.7, label="digital AVG")
    ax.plot(t, [r["bridge_avg"] for r in rows], "g:", lw=2, label="bridge AVG (linear)")
    ax.plot(t, [r["bridge_pep"] for r in rows], "g-.", alpha=.7, label="bridge peak hold")
    ax.set_yscale("log")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("power (W), PEP setpoint 100 W")
    ax.set_title(res["name"])
    ax.grid(True, which="both", alpha=.3)
    ax.legend(fontsize=8, ncol=4, loc="lower center")
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--wav", default=os.path.join(cessb.DEFAULT_OPENAUDIO, "utility", "W9GR48.WAV"))
    ap.add_argument("--out", default=os.path.join(HERE, "results"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    flt = tx.TxFilters.for_rate(FS)
    sign = tx.wanted_sign(flt)
    summary = {}
    for sname, x in signals(args.wav).items():
        for use_cessb in (False, True):
            if sname == "tone" and use_cessb:
                continue
            key = f"{sname}{'_cessb' if use_cessb else ''}"
            z = node_signal(x, flt, sign, use_cessb)
            res = run_meters(key, z, flt)
            summary[key] = summarise(res)
            if sname.startswith("voice") or sname == "syllable":
                plot(res, os.path.join(args.out, f"{key}.png"))
            print(f"{key:20s} done", flush=True)

    with open(os.path.join(args.out, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1)

    lines = ["# Power meter model: error in dB (median [p10, p90]) over talk instants", "",
             "| signal | PAPR | current vs PEP | current vs AVG | bridge AVG vs AVG | "
             "bridge peak-hold vs PEP | digital PEP vs PEP | digital AVG vs AVG | SWR current | SWR linear |",
             "|---|---|---|---|---|---|---|---|---|---|"]

    def f(s):
        return f"{s['median']:+.1f} [{s['p10']:+.1f}, {s['p90']:+.1f}]"

    def g(s):
        return f"{s['median']:.2f} [{s['p10']:.2f}, {s['p90']:.2f}]"

    for k, s in summary.items():
        if not s:
            continue
        lines.append(f"| {k} | {s['papr_db']:.1f} | {f(s['old_vs_pep'])} | {f(s['old_vs_avg'])} | "
                     f"{f(s['bridge_avg_vs_avg'])} | {f(s['bridge_pep_vs_pep'])} | "
                     f"{f(s['dig_pep_vs_pep'])} | {f(s['dig_avg_vs_avg'])} | "
                     f"{g(s['old_swr'])} | {g(s['bridge_swr'])} |")
    lines += ["", f"True SWR in the model: {(1 + GAMMA) / (1 - GAMMA):.2f}. PEP setpoint {P_SET_W:.0f} W; "
              f"CESSB runs at {DRIVE_CESSB_DB:.0f} dB drive."]
    text = "\n".join(lines) + "\n"
    with open(os.path.join(args.out, "summary.md"), "w") as fh:
        fh.write(text)
    print(text)


if __name__ == "__main__":
    main()
