#!/usr/bin/env python3
"""Fit the ATU RF board model to the VNA sweeps and draw the report figures.

    python3 analyse.py [--data DIR] [--out DIR]

Reads <DIR>/atu_<tag>.s2p (written by the saav2 MCP server, S11 and S21 only),
writes fits.json and the PNG figures used in docs/ATU_RF_Board_Characterisation.md.
"""
import argparse
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from s2p import load, db  # noqa: E402
import model as M  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

# Palette: reference categorical slots and the blue ordinal ramp (dataviz skill)
CAT = ["#2a78d6", "#eb6834", "#1baf7a", "#6250d6", "#e34948", "#008300"]
RAMP8 = ["#86b6ef", "#6da7ec", "#5598e7", "#3987e5", "#2a78d6", "#256abf", "#184f95", "#0d366b"]
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3df"
BANDS_MHZ = [(1.8, 2.0), (3.5, 4.0), (5.33, 5.41), (7.0, 7.3), (10.1, 10.15), (14.0, 14.35),
             (18.068, 18.168), (21.0, 21.45), (24.89, 24.99), (28.0, 29.7), (50.0, 54.0)]

plt.rcParams.update({
    "figure.dpi": 110, "savefig.dpi": 150, "font.size": 9.5,
    "axes.edgecolor": INK2, "axes.labelcolor": INK, "axes.titlesize": 10.5,
    "axes.titleweight": "bold", "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.6,
    "axes.spines.top": False, "axes.spines.right": False, "xtick.color": INK2, "ytick.color": INK2,
    "legend.frameon": False, "legend.fontsize": 8.5, "lines.linewidth": 1.6,
    "figure.facecolor": "#fcfcfb", "axes.facecolor": "#fcfcfb", "savefig.facecolor": "#fcfcfb",
})


def smith_grid(ax):
    """Minimal Smith chart: unit circle, constant-R and constant-X arcs."""
    t = np.linspace(0, 2 * np.pi, 400)
    ax.plot(np.cos(t), np.sin(t), color=INK2, lw=0.8)
    for r in (0.2, 0.5, 1, 2, 5):
        c, rad = r / (1 + r), 1 / (1 + r)
        ax.plot(c + rad * np.cos(t), rad * np.sin(t), color=GRID, lw=0.7, zorder=0)
    for x in (0.2, 0.5, 1, 2, 5):
        for sgn in (1, -1):
            zz = 1j * sgn * x + np.concatenate([np.linspace(0, 50, 2000)])
            g = (zz - 1) / (zz + 1)
            ax.plot(g.real, g.imag, color=GRID, lw=0.7, zorder=0)
    ax.axhline(0, color=GRID, lw=0.7, zorder=0)
    ax.set_xlim(-1.05, 1.05)
    ax.set_ylim(-1.05, 1.05)
    ax.set_aspect("equal")
    ax.grid(False)
    ax.set_xticks([])
    ax.set_yticks([])
    for sp in ax.spines.values():
        sp.set_visible(False)
    for r in (0.2, 0.5, 1, 2, 5):
        ax.text((r - 1) / (r + 1), 0.02, f"{50 * r:g}", fontsize=6.5, color=INK2, ha="center", va="bottom")


def bands(ax):
    for lo, hi in BANDS_MHZ:
        ax.axvspan(lo, hi, color="#f0efec", zorder=0, lw=0)


def mhz(f):
    return f / 1e6


def rl(s11):
    return -db(s11)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=os.path.join(HERE, "data"))
    ap.add_argument("--out", default=os.path.join(HERE, "..", "..", "docs", "atu_rf_characterisation"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    L = lambda tag: load(f"atu_{tag}", a.data)  # noqa: E731
    have = lambda tag: os.path.exists(f"{a.data}/atu_{tag}.s2p")  # noqa: E731
    out = {}

    # ---------------------------------------------------------------- zero networks
    f, s11z, s21z = L("zero_LC")
    pz, build = M.fit_zero(f, s11z, s21z)
    zLC = build([pz[k] for k in M.ZERO_KEYS])
    out["zero_LC"] = pz
    _, s11zc, s21zc = L("zero_CL")
    pzc, buildc = M.fit_zero(f, s11zc, s21zc)
    zCL = buildc([pzc[k] for k in M.ZERO_KEYS])
    out["zero_CL"] = pzc
    fb, s11b, s21b = L("bypass")
    pb, _ = M.fit_zero(fb, s11b, s21b)
    out["bypass"] = pb

    # ---------------------------------------------------------------- elements
    ind, cap = {}, {}
    Leff, Ceff = {}, {}
    for n in range(1, 9):
        _, s11, s21 = L(f"L{n}")
        ind[n] = M.fit_inductor(f, s11, s21, zLC, n)
        Z = M.extract_series(f, s21, zLC, ind[n]["t_ns"] * 1e-9, M.NOM_L_uH[n - 1])
        Leff[n] = (Z.imag / (2 * np.pi * f), Z)
        _, s11, s21 = L(f"C{n}")
        cap[n] = M.fit_capacitor(f, s11, s21, zLC, n)
        Y = M.extract_shunt(f, s21, zLC)
        Ceff[n] = (Y.imag / (2 * np.pi * f), Y)
    out["inductors"] = ind
    out["capacitors"] = cap
    _, s11a, s21a = L("Call")
    Lsh = M.fit_bank_inductance(f, s11a, s21a, zLC, cap)
    out["bank"] = {"Lsh_nH": Lsh}
    with open(os.path.join(HERE, "fits.json"), "w") as fh:
        json.dump(out, fh, indent=1, default=float)

    # ---------------------------------------------------------------- fig 1: fixed paths
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.6))
    series = [("bypass", "Bypass (K19/K20 off)", CAT[0]),
              ("zero_LC", "Network in, no L/C, caps at output (LC)", CAT[1]),
              ("zero_CL", "Network in, no L/C, caps at input (CL)", CAT[2])]
    for tag, lab, c in series:
        ff, s11, s21 = L(tag)
        a1.plot(mhz(ff), db(s21), color=c, label=lab)
        a2.plot(mhz(ff), rl(s11), color=c, label=lab)
    if have("bypass_end"):
        ff, s11, s21 = L("bypass_end")
        a1.plot(mhz(ff), db(s21), color=CAT[0], ls=":", lw=1.2, label="Bypass, repeated at end")
        a2.plot(mhz(ff), rl(s11), color=CAT[0], ls=":", lw=1.2, label="Bypass, repeated at end")
    S11m, S21m = M.to_s(M.network(f, zLC))
    a1.plot(mhz(f), db(S21m), color=INK, ls="--", lw=1, label="Model fit (LC)")
    a2.plot(mhz(f), rl(S11m), color=INK, ls="--", lw=1, label="Model fit (LC)")
    for ax in (a1, a2):
        bands(ax)
        ax.set_xlabel("Frequency (MHz)")
        ax.set_xlim(1, 60)
    a1.set_ylabel("|S21| (dB)")
    a1.set_title("Insertion loss of the fixed paths")
    a1.set_ylim(-2, 0.5)
    a2.set_ylabel("Return loss at TX (dB)")
    a2.set_title("Match of the fixed paths")
    a2.set_ylim(0, 50)
    a2.legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(f"{a.out}/fig1_fixed_paths.png")
    plt.close(fig)

    # ---------------------------------------------------------------- fig 2: balanced output
    if have("bal_byp"):
        fig, ax = plt.subplots(figsize=(6, 3.4))
        for (tag, lab), c in zip([("bypass", "Coax output (K18 off)"),
                                  ("bal_byp", "Balun output selected (K18 on), bypass"),
                                  ("bal_in", "Balun output selected (K18 on), network in")], CAT):
            if have(tag):
                ff, s11, s21 = L(tag)
                ax.plot(mhz(ff), db(s21), color=c, label=lab)
        bands(ax)
        ax.set_xlim(1, 60)
        ax.set_xlabel("Frequency (MHz)")
        ax.set_ylabel("|S21| TX to ANT coax (dB)")
        ax.set_title("K18 removes the coax output when the balun is selected")
        ax.legend(loc="lower right")
        fig.tight_layout()
        fig.savefig(f"{a.out}/fig2_balanced_output.png")
        plt.close(fig)

    # ---------------------------------------------------------------- fig 3/5: S21 families
    for kind, fits in (("L", ind), ("C", cap)):
        fig, ax = plt.subplots(figsize=(10, 4.2))
        ff, s11, s21 = L("zero_LC")
        ax.plot(mhz(ff), db(s21), color="#9a9893", lw=1.2, label="None selected")
        for n in range(1, 9):
            ff, s11, s21 = L(f"{kind}{n}")
            nom = M.NOM_L_uH[n - 1] if kind == "L" else M.NOM_C_pF[n - 1]
            unit = "µH" if kind == "L" else "pF"
            ax.plot(mhz(ff), db(s21), color=RAMP8[n - 1], label=f"{kind}{n} {nom:g} {unit}")
            if kind == "L":
                S11p, S21p = M.predict(ff, zLC, {n: fits[n]}, {}, 1 << (n - 1), 0)
            else:
                S11p, S21p = M.predict(ff, zLC, {}, {n: fits[n]}, 0, 1 << (n - 1))
            ax.plot(mhz(ff), db(S21p), color=INK, ls="--", lw=0.7)
        if have(f"{kind}all"):
            ff, s11, s21 = L(f"{kind}all")
            ax.plot(mhz(ff), db(s21), color=CAT[1], lw=1.6,
                    label=f"All eight ({sum(M.NOM_L_uH) if kind == 'L' else sum(M.NOM_C_pF):g} {unit})")
            S11p, S21p = M.predict(ff, zLC, ind, cap, 255 if kind == "L" else 0,
                                   255 if kind == "C" else 0, Lsh)
            ax.plot(mhz(ff), db(S21p), color=CAT[1], ls="--", lw=0.9)
        ax.plot([], [], color=INK, ls="--", lw=0.7, label="Model, fitted values")
        bands(ax)
        ax.set_xlim(1, 60)
        ax.set_ylim(-45, 1)
        ax.set_xlabel("Frequency (MHz)")
        ax.set_ylabel("|S21| (dB)")
        what = "series inductor" if kind == "L" else "shunt capacitor (caps at output, LC)"
        ax.set_title(f"Transmission with one {what} selected at a time")
        ax.legend(loc="lower left", ncol=2)
        fig.tight_layout()
        fig.savefig(f"{a.out}/fig{3 if kind == 'L' else 5}_{kind}_s21.png")
        plt.close(fig)

    # ---------------------------------------------------------------- fig 4: effective L
    fig, ax = plt.subplots(figsize=(10, 4.4))
    for n in range(1, 9):
        le, Z = Leff[n]
        X = Z.imag
        ok = (np.abs(Z) > 2.0)  # resolvable: |Z| at least a couple of ohms
        y = np.where(ok, le * 1e6, np.nan)
        ax.plot(mhz(f), y, color=RAMP8[n - 1], label=f"L{n}")
        ax.axhline(M.NOM_L_uH[n - 1], color=RAMP8[n - 1], ls="--", lw=0.8)
        Lfit = ind[n]["L_nH"] / 1e3
    ax.plot([], [], color=INK2, ls="--", lw=0.8, label="Nominal value")
    bands(ax)
    ax.set_yscale("log")
    ax.set_xlim(1, 60)
    ax.set_ylim(0.02, 40)
    ax.set_xlabel("Frequency (MHz)")
    ax.set_ylabel("Effective inductance (µH)")
    ax.set_title("Inductance each relay adds, extracted at every frequency (|Z| < 2 Ω hidden)")
    ax.legend(loc="upper left", ncol=5)
    fig.tight_layout()
    fig.savefig(f"{a.out}/fig4_L_effective.png")
    plt.close(fig)

    # ---------------------------------------------------------------- fig 6: effective C
    fig, ax = plt.subplots(figsize=(10, 4.4))
    for n in range(1, 9):
        ce, Y = Ceff[n]
        y = ce * 1e12
        y = np.where(y > 0, y, np.nan)
        ax.plot(mhz(f), y, color=RAMP8[n - 1], label=f"C{n}")
        ax.axhline(M.NOM_C_pF[n - 1], color=RAMP8[n - 1], ls="--", lw=0.8)
    ax.plot([], [], color=INK2, ls="--", lw=0.8, label="Nominal value")
    bands(ax)
    ax.set_yscale("log")
    ax.set_xlim(1, 60)
    ax.set_ylim(3, 30000)
    ax.set_xlabel("Frequency (MHz)")
    ax.set_ylabel("Effective capacitance (pF)")
    ax.set_title("Capacitance each relay adds, extracted at every frequency (inductive above series resonance: hidden)")
    ax.legend(loc="upper left", ncol=5)
    fig.tight_layout()
    fig.savefig(f"{a.out}/fig6_C_effective.png")
    plt.close(fig)

    # ---------------------------------------------------------------- fig 7: nominal vs fitted
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 3.4))
    xs = np.arange(1, 9)
    errL = [100 * (ind[n]["L_nH"] / 1e3 / M.NOM_L_uH[n - 1] - 1) for n in xs]
    errC = [100 * (cap[n]["C_pF"] / M.NOM_C_pF[n - 1] - 1) for n in xs]
    for ax, err, lab in ((a1, errL, "L"), (a2, errC, "C")):
        ax.bar(xs, err, color=CAT[0], width=0.6)
        ax.axhline(0, color=INK2, lw=0.8)
        for x, e in zip(xs, err):
            ax.text(x, e + (1 if e >= 0 else -1), f"{e:+.0f}%", ha="center",
                    va="bottom" if e >= 0 else "top", fontsize=8, color=INK)
        ax.set_xticks(xs, [f"{lab}{n}" for n in xs])
        ax.set_ylabel("Fitted vs nominal (%)")
        ax.grid(axis="x", visible=False)
    a1.set_title("Inductors: fitted L vs nominal")
    a2.set_title("Capacitors: fitted C vs nominal")
    lim = max(10, np.nanmax(np.abs(errL + errC)) * 1.25)
    for ax in (a1, a2):
        ax.set_ylim(-lim, lim)
    fig.tight_layout()
    fig.savefig(f"{a.out}/fig7_fit_error.png")
    plt.close(fig)

    # ---------------------------------------------------------------- fig 8: LC vs CL
    if have("combo_LC") and have("combo_CL"):
        lm, cm = 1 << 5, 1 << 4    # L6 2.2 uH, C5 220 pF
        fig = plt.figure(figsize=(10, 4.2))
        a1 = fig.add_subplot(1, 2, 1)
        a2 = fig.add_subplot(1, 2, 2)
        smith_grid(a2)
        marks = [3.65e6, 7.2e6, 14.28e6]
        for tag, lab, c, z in (("combo_LC", "Caps at output (LC)", CAT[0], zLC),
                               ("combo_CL", "Caps at input (CL)", CAT[1], zCL)):
            ff, s11, s21 = L(tag)
            S11p, S21p = M.predict(ff, z, {6: ind[6]}, {5: cap[5]}, lm, cm, Lsh)
            a1.plot(mhz(ff), db(s21), color=c, label=lab)
            a1.plot(mhz(ff), db(S21p), color=c, ls="--", lw=0.9)
            k = ff <= 15e6
            a2.plot(s11[k].real, s11[k].imag, color=c, label=lab)
            a2.plot(S11p[k].real, S11p[k].imag, color=c, ls="--", lw=0.9)
            for fm in marks:
                i = int(np.argmin(np.abs(ff - fm)))
                a2.plot(s11[i].real, s11[i].imag, "o", ms=6, color=c, mec="#fcfcfb", mew=1.5)
                Zi = M.zin_from_s11(s11[i])
                # label placement per point, chosen to keep the labels apart
                off = {("combo_LC", 0): (-9, 4, "right"), ("combo_LC", 1): (-9, 4, "right"),
                       ("combo_LC", 2): (10, -4, "left"), ("combo_CL", 0): (2, 10, "left"),
                       ("combo_CL", 1): (9, -14, "left"), ("combo_CL", 2): (9, -14, "left")}
                dx, dy, ha = off[(tag, marks.index(fm))]
                a2.annotate(f"{fm / 1e6:.1f} MHz\n{Zi.real:.0f}{Zi.imag:+.0f}j Ω", (s11[i].real, s11[i].imag),
                            textcoords="offset points", xytext=(dx, dy), ha=ha, fontsize=7.5, color=INK)
        bands(a1)
        a1.set_xlim(1, 60)
        a1.set_ylim(-40, 1)
        a1.set_xlabel("Frequency (MHz)")
        a1.set_ylabel("|S21| (dB)")
        a1.set_title("Transmission: identical in LC and CL")
        a1.plot([], [], color=INK2, ls="--", lw=0.9, label="Model prediction")
        a1.legend(loc="lower left")
        a2.set_title("Reflection at TX, 1–15 MHz: K17 moves it")
        fig.suptitle("L6 (2.2 µH) + C5 (220 pF), ANT terminated in 50 Ω", fontsize=10.5, fontweight="bold")
        fig.tight_layout()
        fig.savefig(f"{a.out}/fig8_LC_vs_CL.png")
        plt.close(fig)

    # ---------------------------------------------------------------- fig 9: matching demos
    demos = json.load(open(os.path.join(HERE, "match_plan.json"))) if os.path.exists(
        os.path.join(HERE, "match_plan.json")) else []
    if demos and all(have(d["tag"]) for d in demos):
        fig, axes = plt.subplots(1, len(demos), figsize=(3.2 * len(demos), 3.4), sharey=True)
        for ax, d, c in zip(np.atleast_1d(axes), demos, CAT):
            tag = d["tag"] + "_fine" if have(d["tag"] + "_fine") else d["tag"]
            ff, s11, s21 = L(tag)
            z = (build([pz[k] for k in M.ZERO_KEYS], ff) if d["topology"] == "LC"
                 else buildc([pzc[k] for k in M.ZERO_KEYS], ff))
            S11p, _ = M.predict(ff, z, ind, cap, d["lmask"], d["cmask"], Lsh)
            R = d["R_target"]
            for s, ls, lab in ((s11, "-", "Measured"), (S11p, "--", "Model prediction")):
                Zi = M.zin_from_s11(s)
                g = np.abs((Zi - R) / (Zi + R))
                swr = (1 + g) / (1 - g)
                ax.plot(mhz(ff), swr, color=c if ls == "-" else INK, ls=ls, lw=1.6 if ls == "-" else 0.9,
                        label=lab)
            ax.axvline(d["f0"] / 1e6, color=INK2, lw=0.8, ls=":")
            ax.set_xlim(max(1, d["f0"] / 1e6 * 0.4), min(60, d["f0"] / 1e6 * 1.8))
            ax.set_ylim(1, 5)
            ax.set_xlabel("Frequency (MHz)")
            ax.set_title(f"{d['topology']}: 50 Ω → {R:g} Ω at {d['f0'] / 1e6:g} MHz", fontsize=9.5)
        np.atleast_1d(axes)[0].set_ylabel(f"SWR at TX, re target R")
        np.atleast_1d(axes)[0].legend(loc="upper left")
        fig.tight_layout()
        fig.savefig(f"{a.out}/fig9_matching.png")
        plt.close(fig)

    print(json.dumps(out, indent=1, default=lambda x: round(float(x), 4)))


if __name__ == "__main__":
    main()
