#!/usr/bin/env python3
"""Adjacent-channel power from the CMU200 spectra saved by ``cessb_acp_test.py``.

The receiver: Phoenix's own SSB receive filter, a 257-tap complex FIR
(``CalcCplxFIRCoeffs``, 4-term Blackman-Harris window) at 176.4 ksps / 8 =
22.05 ksps, set to 300-2700 Hz. It is one-sided, so like a real SSB receiver it
also rejects the opposite sideband. A receiver tuned to channel offset ``c``
weights the transmitted spectrum by ``|H(f - c)|^2`` (f relative to our carrier,
positive towards the wanted sideband). The same filter is used for every channel.

Channel power = sum over trace points of power x |H|^2, times point spacing over
the RBW's noise bandwidth (1.065 x RBW for the CMU200's Gaussian filters). The
sweeps of each frequency segment are averaged in linear power first (see
cessb_acp_test.py for why the spectrum is measured in segments). The first and
second halves of every segment's sweeps give two independent estimates: their
difference is the repeatability.

    acp_analysis.py results/cessb_acp_<stamp>.json [--plot]
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys

import numpy as np

RX_FS = 176400.0 / 8
RX_TAPS = 257
CHANNELS = {"lower_alt": -6000.0, "lower": -3000.0, "wanted": 0.0, "upper": 3000.0,
            "upper_alt": 6000.0}


def phoenix_rx_taps(lo_hz: float, hi_hz: float, n: int = RX_TAPS, fs: float = RX_FS) -> np.ndarray:
    """CalcCplxFIRCoeffs with FIR_FILTER_WINDOW 1 (4-term Blackman-Harris)."""
    nfl, nfh = lo_hz / fs, hi_hz / fs
    nfc = (nfh - nfl) / 2.0
    nfs = math.pi * (nfh + nfl)
    i = np.arange(n)
    x = i - 0.5 * (n - 1)
    w = (0.35875 - 0.48829 * np.cos(2 * np.pi * i / (n - 1))
         + 0.14128 * np.cos(4 * np.pi * i / (n - 1)) - 0.01168 * np.cos(6 * np.pi * i / (n - 1)))
    with np.errstate(invalid="ignore", divide="ignore"):
        z = np.where(np.abs(x) < 0.01, 2 * nfc, np.sin(2 * np.pi * x * nfc) / (np.pi * x)) * w
    return z * np.exp(1j * nfs * x)


def butter_power(f, lo_hz, hi_hz, order):
    """Audio-domain Butterworth high-pass x low-pass after an ideal sideband selector."""
    f = np.asarray(f, dtype=float)
    out = np.zeros_like(f)
    pos = f > 0
    out[pos] = 1 / (1 + (lo_hz / f[pos]) ** (2 * order)) / (1 + (f[pos] / hi_hz) ** (2 * order))
    return out


class RxFilter:
    def __init__(self, kind: str = "phoenix", lo_hz: float = 300.0, hi_hz: float = 2700.0,
                 order: int = 6):
        self.kind, self.lo, self.hi, self.order = kind, lo_hz, hi_hz, order
        if kind == "phoenix":
            self.taps = phoenix_rx_taps(lo_hz, hi_hz)
            # normalise to unity at the passband centre
            self.taps /= abs(self._dtft(np.array([(lo_hz + hi_hz) / 2]))[0])

    def _dtft(self, f):
        n = np.arange(len(self.taps))
        return np.exp(-2j * np.pi * np.outer(f, n) / RX_FS) @ self.taps

    def power(self, f_audio) -> np.ndarray:
        """|H|^2 at audio frequency f (Hz, relative to the receiver's carrier)."""
        f = np.asarray(f_audio, dtype=float)
        if self.kind == "phoenix":
            h = np.abs(self._dtft(f)) ** 2
            h[np.abs(f) > RX_FS / 2] = 0.0   # the decimation filters' job
            return h
        if self.kind == "brick":
            return ((f >= self.lo) & (f <= self.hi)).astype(float)
        if self.kind == "butter":
            return butter_power(f, self.lo, self.hi, self.order)
        raise ValueError(self.kind)

    def label(self) -> str:
        if self.kind == "phoenix":
            return f"Phoenix RX FIR {self.lo:.0f}-{self.hi:.0f} Hz"
        if self.kind == "brick":
            return f"brick wall {self.lo:.0f}-{self.hi:.0f} Hz"
        return f"Butterworth {self.order}+{self.order} {self.lo:.0f}-{self.hi:.0f} Hz"


def mw(dbm):
    return 10.0 ** (np.asarray(dbm, dtype=float) / 10.0)


def db10(x):
    return 10.0 * np.log10(np.maximum(np.asarray(x, dtype=float), 1e-30))


def channel_mw(fr, p_mw, filt: RxFilter, offset_hz: float, rbw: float) -> float:
    """Filtered power from one evenly spaced trace (used by the self-tests)."""
    df = float(np.median(np.diff(fr)))
    return float(np.sum(p_mw * filt.power(fr - offset_hz)) * abs(df) / (1.065 * rbw))


def merged(row, half=None):
    """The point's long-term spectrum from its segments: (f, power mW per RBW,
    integration weight df / (1.065 RBW) per point). Each segment's sweeps are
    averaged in linear power and used over its own range. ``half`` 0 or 1 uses
    only pass 0 or pass 1 of every segment that has two."""
    fs, ps, ws = [], [], []
    segs = sorted(row["segments"].items(), key=lambda kv: kv[1]["own"][0])
    for n, (name, d) in enumerate(segs):
        fr = np.asarray(d["f"], float)
        tiles = np.asarray(d["tiles"], float)
        if half is not None:
            passes = np.asarray(d.get("pass", [0] * len(tiles)))
            if (passes == half).any() and len(np.unique(passes)) > 1:
                tiles = tiles[passes == half]       # a segment measured once serves both
        lo, hi = d["own"]
        last = n == len(segs) - 1
        sel = (fr >= lo) & ((fr <= hi) if last else (fr < hi))
        df = abs(float(np.median(np.diff(fr))))
        fs.append(fr[sel])
        ps.append(mw(tiles).mean(axis=0)[sel])
        ws.append(np.full(sel.sum(), df / (1.065 * float(d["rbw"]))))
    return np.concatenate(fs), np.concatenate(ps), np.concatenate(ws)


def covered(f, offset_hz) -> bool:
    """True when the spectrum spans the channel's offset .. offset + 3000 Hz, outside
    which the receiver filter is below -79 dB."""
    return f.min() <= offset_hz + 6.0 and f.max() >= offset_hz + 3000.0 - 6.0


def channel_powers(row, filt: RxFilter, half=None) -> dict:
    f, p, w = merged(row, half)
    return {k: float(np.sum(p * w * filt.power(f - c))) for k, c in CHANNELS.items()
            if covered(f, c)}


def analyse_point(row, filt: RxFilter) -> dict:
    ch = channel_powers(row, filt)
    out = {"label": row["label"], "cessb_db": row.get("cessb_db"), "kind": row.get("kind")}
    for k, v in ch.items():
        out[k + "_dbm"] = float(db10(v))
    names = [k for k in ch if k != "wanted"]
    for k in names:
        out[k + "_acpr_db"] = out[k + "_dbm"] - out["wanted_dbm"]
    if row.get("kind") == "speech":
        halves = [channel_powers(row, filt, h) for h in (0, 1)]
        for k in names:
            out[k + "_acpr_halves"] = [float(db10(c[k] / c["wanted"])) for c in halves]
    if "pep_dbm" in row:
        out.update(pep_dbm=row["pep_dbm"], avg_dbm=row["avg_dbm"], pep_w=row["pep_w"],
                   avg_w=row["avg_w"], papr_db=row["papr_db"])
        for k in ch:
            out[k + "_re_pep_db"] = out[k + "_dbm"] - row["pep_dbm"]
        out["wanted_vs_npower_avg_db"] = out["wanted_dbm"] - row["avg_dbm"]
        f, p, w = merged(row)
        sel = (f >= -3050.0) & (f <= 3500.0)
        out["total_vs_npower_avg_db"] = float(db10(np.sum(p[sel] * w[sel]))) - row["avg_dbm"]
    return out


def load(path):
    with open(path) as fh:
        return json.load(fh)


def analyse(doc, filt: RxFilter) -> dict:
    rows = [analyse_point(r, filt) for r in doc["points"]]
    by_kind = {r["kind"]: r for r in rows if r["kind"] in ("floor", "tx_floor", "tone")}
    speech = [r for r in rows if r["kind"] == "speech"]
    # Floors relative to each speech point's wanted channel
    for r in speech:
        for fk in ("floor", "tx_floor"):
            if fk in by_kind:
                for k in CHANNELS:
                    if k != "wanted" and k + "_dbm" in by_kind[fk]:
                        r[f"{k}_{fk}_db"] = by_kind[fk][k + "_dbm"] - r["wanted_dbm"]
    return {"filter": filt.label(), "rows": rows, "speech": speech, "refs": by_kind}


def fmt_table(res) -> str:
    lines = [f"Receiver filter: {res['filter']}",
             f"{'point':<18}{'PEP W':>7}{'avg W':>7}{'wanted':>8}{'lo alt':>8}{'lower':>8}"
             f"{'upper':>8}{'up alt':>8}  {'upper by half':<16}{'up re PEP':>10}"
             f"{'want-NP':>8}{'tot-NP':>8}"]
    for r in res["rows"]:
        pep = f"{r['pep_w']:7.2f}{r['avg_w']:7.2f}" if "pep_w" in r else " " * 14
        g = lambda k: f"{r[k]:8.1f}" if k in r else " " * 8  # noqa: E731
        ps = r.get("upper_acpr_halves")
        pss = " ".join(f"{v:.1f}" for v in ps) if ps else ""
        lines.append(f"{r['label']:<18}{pep}{r['wanted_dbm']:8.1f}{g('lower_alt_acpr_db')}"
                     f"{g('lower_acpr_db')}{g('upper_acpr_db')}{g('upper_alt_acpr_db')}  "
                     f"{pss:<16}{g('upper_re_pep_db'):>10}{g('wanted_vs_npower_avg_db')}"
                     f"{g('total_vs_npower_avg_db')}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("json")
    ap.add_argument("--filter", default="phoenix", choices=["phoenix", "brick", "butter"])
    ap.add_argument("--lo", type=float, default=300.0)
    ap.add_argument("--hi", type=float, default=2700.0)
    ap.add_argument("--order", type=int, default=6)
    args = ap.parse_args()
    doc = load(args.json)
    res = analyse(doc, RxFilter(args.filter, args.lo, args.hi, args.order))
    print(fmt_table(res))
    return 0


if __name__ == "__main__":
    sys.exit(main())
