#!/usr/bin/env python3
"""Simulate ATU tuning strategies with the fitted model of the measured board.

    python3 tuning_sim.py            # sections 1-3: ambiguity, other builds, ladder
    python3 tuning_sim.py extra      # sections 4-5: calibration accuracy, step order
    python3 tuning_sim.py robust     # section 7: plain vs restricted vs robust selection
    python3 tuning_sim.py algorithm  # section 6: the proposed algorithm end to end

Questions:
 1. Does one SWR reading at one frequency determine the best setting?
 2. Does a setting chosen with this board's model work on another build whose
    hand-wound inductors differ, and what does it take to recover?

The model gives each relay setting's full two-port (ABCD), so the impedance seen
at TX with an antenna Z_ant on ANT is Zin = (A Z_ant + B) / (C Z_ant + D).
"""
import json
import os
import sys

import numpy as np
from scipy.optimize import least_squares

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from s2p import load, Z0  # noqa: E402
import model as M  # noqa: E402

rng = np.random.default_rng(1)
FITS = json.load(open(os.path.join(HERE, "fits.json")))
IND = {int(k): v for k, v in FITS["inductors"].items()}
CAP = {int(k): v for k, v in FITS["capacitors"].items()}
LSH = FITS["bank"]["Lsh_nH"]
ZP = {}
for topo in ("LC", "CL"):
    f, s11, s21 = load(f"atu_zero_{topo}", os.path.join(HERE, "data"))
    p, build = M.fit_zero(f, s11, s21)
    ZP[topo] = (build, [p[k] for k in M.ZERO_KEYS])
I2 = (1.0, 0.0, 0.0, 1.0)


def settings_abcd(f0, ind, topo):
    """ABCD of all 256 x 256 settings of one topology at f0, shape (256, 256)."""
    build, p = ZP[topo]
    z = build(p, np.array([f0]))
    f = np.array([f0])
    ta, tb = z["tau_a"], z["tau_b"]
    tt = ta + tb
    lm = np.arange(256)[:, None]
    cm = np.arange(256)[None, :]
    # bank admittance for every C mask
    ys = np.zeros((1, 256), complex)
    for n, c in CAP.items():
        yc = M.y_capacitor(f, c["C_pF"], max(c["Llead_nH"] - LSH, 0.0), c["R"])[0]
        ys = ys + np.where(cm & (1 << (n - 1)), yc, 0)
    with np.errstate(divide="ignore", invalid="ignore"):
        ybank = np.where(ys == 0, 0, 1 / (1 / ys + 1j * 2 * np.pi * f0 * LSH * 1e-9))
    ybank = ybank + z["Ys"][0]

    def path(Zt, t0, t1, items):
        m = tuple(np.ones((256, 1), complex) * v for v in I2)
        pos = t0
        for t, n, el in items:
            seg = M.line(f, Zt, t - pos, z["alpha"] * (t - pos) / tt)
            m = M.mul(m, tuple(v[0] for v in seg))
            on = (lm & (1 << (n - 1))) > 0
            m = M.mul(m, tuple(np.where(on, e[0], i) for e, i in zip(el, I2)))
            pos = t
        seg = M.line(f, Zt, t1 - pos, z["alpha"] * (t1 - pos) / tt)
        return M.mul(m, tuple(v[0] for v in seg))

    items = sorted((min(p_["t_ns"] * 1e-9, tt), n,
                    M.inductor_abcd(f, p_["L_nH"], p_["Cp_pF"], p_["R1"], p_.get("Cg_pF", 0)))
                   for n, p_ in ind.items())
    la = path(z["Zta"], 0.0, ta, [(t, n, e) for t, n, e in items if t <= ta])
    lb = path(z["Ztb"], 0.0, tb, [(t - ta, n, e) for t, n, e in items if t > ta])
    one = np.ones_like(ybank)
    sh = (one, 0 * ybank, ybank, one)
    return M.mul(M.mul(la, sh), lb)


def gamma(abcd, zl):
    A, B, C, D = abcd
    zin = (A * zl + B) / (C * zl + D)
    return (zin - Z0) / (zin + Z0)


def swr(g):
    g = np.minimum(np.abs(g), 0.9999)
    return (1 + g) / (1 - g)


class Build:
    """One ATU build at one frequency: all settings' ABCD for both topologies."""
    def __init__(self, f0, ind):
        self.f0 = f0
        self.ind = ind
        self.net = {t: settings_abcd(f0, ind, t) for t in ("LC", "CL")}

    def g(self, zl, topo=None, lm=None, cm=None):
        if topo is None:
            return {t: gamma(self.net[t], zl) for t in self.net}
        return gamma(tuple(v[lm, cm] for v in self.net[topo]), zl)

    def best(self, zl):
        out = None
        for t, g in self.g(zl).items():
            i = np.unravel_index(np.argmin(np.abs(g)), g.shape)
            if out is None or abs(g[i]) < abs(out[3]):
                out = (t, int(i[0]), int(i[1]), g[i])
        return out


def variant(sigma):
    v = {}
    for n, p in IND.items():
        q = dict(p)
        q["L_nH"] = p["L_nH"] * (1 + rng.normal(0, sigma))
        q["Cp_pF"] = p["Cp_pF"] * (1 + rng.normal(0, sigma))
        v[n] = q
    return v


def hill_climb(build, zl, topo, lm, cm, steps=(1, 2, 4), noise=0.0, max_meas=10**6):
    """Closed-loop refinement with SWR readings only: try neighbouring codes.
    Readings carry relative error `noise`; returns the true SWR of the final setting."""
    def read(l, c):
        return abs(build.g(zl, topo, l, c)) * (1 + rng.normal(0, noise) if noise else 1)
    cur = read(lm, cm)
    n_meas = 1
    while n_meas < max_meas:
        cand = []
        for d in steps:
            for dl, dc in ((d, 0), (-d, 0), (0, d), (0, -d)):
                l2, c2 = lm + dl, cm + dc
                if 0 <= l2 < 256 and 0 <= c2 < 256:
                    cand.append((read(l2, c2), l2, c2))
        n_meas += len(cand)
        g, l2, c2 = min(cand)
        if g >= cur:
            break
        cur, lm, cm = g, l2, c2
    return swr(build.g(zl, topo, lm, cm)), n_meas


def solve_load(ref, probes, meas):
    """Find Z_ant from SWR magnitudes measured at known probe settings."""
    def res(x):
        zl = x[0] + 1j * x[1]
        return [abs(ref.g(zl, t, l, c)) - m for (t, l, c), m in zip(probes, meas)]
    best = None
    for r0 in (10, 50, 300, 1000):
        for x0 in (-300, -50, 0, 50, 300):
            r = least_squares(res, [r0, x0], bounds=([0.5, -3000], [5000, 3000]))
            if best is None or r.cost < best.cost:
                best = r
    return best.x[0] + 1j * best.x[1]


def pick_probes(f0):
    """Probe settings: bypass-like (nothing selected), one series L, one shunt C,
    chosen so each element's reactance is near 50 ohm at f0."""
    w = 2 * np.pi * f0
    nl = min(range(1, 9), key=lambda n: abs(np.log(w * IND[n]["L_nH"] * 1e-9 / 50)))
    nc = min(range(1, 9), key=lambda n: abs(np.log(w * CAP[n]["C_pF"] * 1e-12 * 50)))
    return [("LC", 0, 0), ("LC", 1 << (nl - 1), 0), ("LC", 0, 1 << (nc - 1)),
            ("CL", 1 << (nl - 1), 1 << (nc - 1))]


def random_loads(n, swr_max=10):
    """Loads spread evenly over the Smith chart out to swr_max."""
    gmax = (swr_max - 1) / (swr_max + 1)
    r = gmax * np.sqrt(rng.uniform(0.05, 1, n))
    g = r * np.exp(1j * rng.uniform(0, 2 * np.pi, n))
    return Z0 * (1 + g) / (1 - g)


def hill_climb_sorted(build, zl, topo, lm, cm, steps=(1, 2, 4)):
    """As hill_climb, but L and C move through this build's settings sorted by
    actual inductance / capacitance (from its calibration), not by binary code."""
    lord = np.argsort([sum(build.ind[n]["L_nH"] for n in range(1, 9) if c & (1 << (n - 1)))
                       for c in range(256)])
    cord = np.argsort([sum(CAP[n]["C_pF"] for n in range(1, 9) if c & (1 << (n - 1)))
                       for c in range(256)])
    li, ci = int(np.where(lord == lm)[0][0]), int(np.where(cord == cm)[0][0])
    cur = abs(build.g(zl, topo, lm, cm))
    n_meas = 1
    while True:
        cand = []
        for d in steps:
            for dl, dc in ((d, 0), (-d, 0), (0, d), (0, -d)):
                l2, c2 = li + dl, ci + dc
                if 0 <= l2 < 256 and 0 <= c2 < 256:
                    cand.append((abs(build.g(zl, topo, int(lord[l2]), int(cord[c2]))), l2, c2))
        n_meas += len(cand)
        g, l2, c2 = min(cand)
        if g >= cur:
            return swr(cur), n_meas
        cur, li, ci = g, l2, c2


def calibration_accuracy(f_list=(7.1e6, 14.2e6, 28.4e6), sigmas=(0.0, 0.01, 0.02, 0.05), nb=8, nl=4):
    """Open-loop result on a build whose true inductors differ from the model by sigma
    (i.e. the residual error of a per-build calibration)."""
    print("\n=== 4. How accurate must a per-build inductor calibration be? ===")
    print("  SWR when the setting is computed from a model whose L/Cp are off by sigma"
          " (median / 90th percentile, loads with SWR <= 10)")
    loads = random_loads(nb * nl)
    for f0 in f_list:
        ref = Build(f0, IND)
        row = []
        for s in sigmas:
            out = []
            for b in range(nb):
                true = Build(f0, variant(s)) if s > 0 else ref
                for zl in loads[b * nl:(b + 1) * nl]:
                    t, lm, cm, _ = ref.best(zl)
                    out.append(swr(true.g(zl, t, lm, cm)))
            row.append(f"{np.median(out):.2f} / {np.percentile(out, 90):.2f}")
        print(f"  {f0 / 1e6:5.1f} MHz  " + "   ".join(f"sigma {int(s * 100)}%: {r}" for s, r in zip(sigmas, row)))


def hill_climb_compare(f_list=(7.1e6, 14.2e6, 28.4e6), nb=8, nl=4):
    print("\n=== 5. Refinement: binary-code steps vs steps in sorted-value order ===")
    loads = random_loads(nb * nl)
    for f0 in f_list:
        ref = Build(f0, IND)
        a, b_, na, nb_ = [], [], [], []
        for b in range(nb):
            other = Build(f0, variant(0.05))
            for zl in loads[b * nl:(b + 1) * nl]:
                t, lm, cm, _ = ref.best(zl)
                s, n = hill_climb(other, zl, t, lm, cm)
                a.append(s); na.append(n)
                s, n = hill_climb_sorted(other, zl, t, lm, cm)
                b_.append(s); nb_.append(n)
        best = []
        print(f"  {f0 / 1e6:5.1f} MHz  binary code: SWR {np.median(a):.2f} / {np.percentile(a, 90):.2f},"
              f" readings {np.median(na):.0f} (max {max(na)})   sorted order: SWR {np.median(b_):.2f} /"
              f" {np.percentile(b_, 90):.2f}, readings {np.median(nb_):.0f} (max {max(nb_)})")


def perturbed(base, sigma):
    out = {}
    for n, p in base.items():
        q = dict(p)
        q["L_nH"] = p["L_nH"] * (1 + rng.normal(0, sigma))
        q["Cp_pF"] = p["Cp_pF"] * (1 + rng.normal(0, sigma))
        out[n] = q
    return out


class Tuner:
    """The proposed algorithm, using only SWR magnitudes from the radio's bridge.

    model: the build model the firmware holds; pert: the same model with its
    inductors perturbed, for robust selection; probes: fixed probe settings.
    """
    def __init__(self, f0, model_ind, n_pert=6, sigma_pert=0.02, K=300):
        self.f0 = f0
        self.model = Build(f0, model_ind)
        self.pert = [Build(f0, perturbed(model_ind, sigma_pert)) for _ in range(n_pert)]
        self.K = K
        self.probes = pick_probes(f0)

    def robust_best(self, zl):
        cands = []
        for topo, G in self.model.g(zl).items():
            a = np.abs(G).ravel()
            for i in np.argpartition(a, self.K)[:self.K]:
                cands.append((a[i], topo, int(i // 256), int(i % 256)))
        cands.sort()
        best = None
        for g0, topo, l, c in cands[:self.K]:
            worst = max([g0] + [abs(p.g(zl, topo, l, c)) for p in self.pert])
            if best is None or worst < best[0]:
                best = (worst, topo, l, c, g0)
        return best[1], best[2], best[3], best[4]

    def solve(self, readings):
        def res(x):
            zl = x[0] + 1j * x[1]
            return [abs(self.model.g(zl, *s)) - m for s, m in readings]
        best = None
        for r0 in (10, 50, 300, 1000):
            for x0 in (-300, -50, 0, 50, 300):
                r = least_squares(res, [r0, x0], bounds=([0.5, -3000], [5000, 3000]))
                if best is None or r.cost < best.cost:
                    best = r
        return best.x[0] + 1j * best.x[1]

    def tune(self, plant, zl, noise=0.03, target_swr=1.5, max_jumps=4, polish_above=None):
        """plant: the real build. Returns (final SWR, number of SWR readings, stage).
        polish_above: if the accepted setting reads above this SWR, finish with a short
        hill-climb (steps of 1 and 2 codes)."""
        def read(setting):
            return min(abs(plant.g(zl, *setting)) * (1 + rng.normal(0, noise)), 0.999)
        readings = [(s, read(s)) for s in self.probes]
        tg = (target_swr - 1) / (target_swr + 1)
        setting = None
        for j in range(max_jumps):
            zest = self.solve(readings)
            topo, l, c, gpred = self.robust_best(zest)
            setting = (topo, l, c)
            m = read(setting)
            readings.append((setting, m))
            # accept if good enough, or as good as the model says is achievable here
            if m <= tg or m <= abs(gpred) + 0.05:
                if polish_above and swr(m) > polish_above:
                    s, n = hill_climb(plant, zl, *setting, steps=(1, 2), noise=noise)
                    return s, len(readings) + n - 1, f"jump {j + 1} + polish"
                return swr(plant.g(zl, *setting)), len(readings), f"jump {j + 1}"
        s, n = hill_climb(plant, zl, *setting, noise=noise)
        return s, len(readings) + n - 1, "hill-climb"


def robust_compare(f_list=(3.65e6, 7.1e6, 14.2e6, 28.4e6), nb=8, nl=4, srf_frac=0.6):
    """Known Z_ant; setting chosen with this board's model; applied to other builds."""
    print("\n=== 7. Choosing the setting: plain best vs restricted vs robust (known Z) ===")
    print("  SWR on other builds: median / 90th percentile / max (32 loads, 8 builds)")
    srfL = {n: 1 / (2 * np.pi * np.sqrt(p["L_nH"] * 1e-9 * p["Cp_pF"] * 1e-12)) for n, p in IND.items()}
    srfC = {n: 1 / (2 * np.pi * np.sqrt(p["C_pF"] * 1e-12 * p["Llead_nH"] * 1e-9)) for n, p in CAP.items()}
    loads = random_loads(nb * nl)
    for f0 in f_list:
        tuner = Tuner(f0, IND)
        ref = tuner.model
        okL = [n for n in IND if f0 < srf_frac * srfL[n]]
        okC = [n for n in CAP if f0 < srf_frac * srfC[n]]
        lok = np.array([all(not (c >> (n - 1)) & 1 or n in okL for n in range(1, 9)) for c in range(256)])
        cok = np.array([all(not (c >> (n - 1)) & 1 or n in okC for n in range(1, 9)) for c in range(256)])
        allow = lok[:, None] & cok[None, :]
        out = {s: {"plain": [], "restricted": [], "robust": []} for s in (0.02, 0.05)}
        for s in (0.02, 0.05):
            for b in range(nb):
                other = Build(f0, variant(s))
                for zl in loads[b * nl:(b + 1) * nl]:
                    t, lm, cm, _ = ref.best(zl)
                    out[s]["plain"].append(swr(other.g(zl, t, lm, cm)))
                    best = None
                    for topo, G in ref.g(zl).items():
                        a = np.where(allow, np.abs(G), 9)
                        i = np.unravel_index(np.argmin(a), a.shape)
                        if best is None or a[i] < best[0]:
                            best = (a[i], topo, int(i[0]), int(i[1]))
                    out[s]["restricted"].append(swr(other.g(zl, *best[1:])))
                    t, lm, cm, _ = tuner.robust_best(zl)
                    out[s]["robust"].append(swr(other.g(zl, t, lm, cm)))
        print(f"  {f0 / 1e6:5.2f} MHz (restricted: L{okL} C{okC})")
        for s in (0.02, 0.05):
            row = "   ".join(f"{k} {np.median(v):.2f} / {np.percentile(v, 90):.2f} / {max(v):.2f}"
                             for k, v in out[s].items())
            print(f"     builds sigma {int(s * 100)}%: {row}")


def end_to_end(f_list=(3.65e6, 7.1e6, 14.2e6, 28.4e6), nb=10, nl=4):
    print("\n=== 6. Proposed algorithm end to end (other builds, sigma 5 %; SWR readings +-3 %) ===")
    print("  SWR columns: median / 90th percentile / max over 40 loads (SWR <= 10) on 10 random builds")
    loads = random_loads(nb * nl)
    for f0 in f_list:
        ref = Build(f0, IND)
        keys = ("uncal", "cal", "uncal_pol", "cal_pol")
        res = {k: [] for k in ("best",) + keys + tuple("n_" + k for k in keys)}
        stages = {k: [] for k in keys}
        tuner_uncal = Tuner(f0, IND)
        for b in range(nb):
            true_ind = variant(0.05)
            plant = Build(f0, true_ind)
            tuner_cal = Tuner(f0, perturbed(true_ind, 0.02))   # model calibrated to within 2 %
            for zl in loads[b * nl:(b + 1) * nl]:
                res["best"].append(swr(plant.best(zl)[3]))
                for key, tuner, pol in (("uncal", tuner_uncal, None), ("cal", tuner_cal, None),
                                        ("uncal_pol", tuner_uncal, 1.2), ("cal_pol", tuner_cal, 1.2)):
                    s, n, st = tuner.tune(plant, zl, polish_above=pol)
                    res[key].append(s)
                    res["n_" + key].append(n)
                    stages[key].append(st)

        def q(k):
            a = np.array(res[k])
            return f"{np.median(a):.2f} / {np.percentile(a, 90):.2f} / {a.max():.2f}"

        def hc(k):
            return sum(1 for s in stages[k] if s == "hill-climb")
        print(f"  {f0 / 1e6:5.2f} MHz  best achievable (median / 90% / max SWR) {q('best')}")
        for k, lab in (("uncal", "reference model, jump only     "), ("cal", "calibrated 2 %, jump only      "),
                       ("uncal_pol", "reference model, jump + polish "), ("cal_pol", "calibrated 2 %, jump + polish  ")):
            print(f"     {lab}: SWR {q(k)}  readings median {np.median(res['n_' + k]):.0f} "
                  f"(90% {np.percentile(res['n_' + k], 90):.0f}, max {max(res['n_' + k])})  fallback hill-climb {hc(k)}/{len(loads)}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "extra":
        calibration_accuracy()
        hill_climb_compare()
        return
    if len(sys.argv) > 1 and sys.argv[1] == "algorithm":
        end_to_end()
        return
    if len(sys.argv) > 1 and sys.argv[1] == "robust":
        robust_compare()
        return
    print("=== 1. One SWR reading does not identify the load ===")
    f0 = 7.1e6
    ref = Build(f0, IND)
    for g in (0.5, -0.5, 0.5j, -0.5j):
        zl = Z0 * (1 + g) / (1 - g)
        t, lm, cm, gb = ref.best(zl)
        print(f"  Z_ant = {zl.real:6.1f}{zl.imag:+6.1f}j  (SWR 3.0)  best: {t} L-code {lm:3d} C-code {cm:3d}"
              f"  -> SWR {swr(gb):.2f}")

    print("\n=== 2. Builds with different inductors (sigma = 5 % on L and Cp) ===")
    loads = random_loads(40)
    for f0 in (3.65e6, 7.1e6, 14.2e6, 28.4e6):
        ref = Build(f0, IND)
        probes = pick_probes(f0)
        res = {k: [] for k in ("ideal_same", "ideal_other", "transplant", "refined", "n_refine",
                                "probe_same", "probe_other", "probe_refined", "n_probe_refine")}
        for b in range(10):
            other = Build(f0, variant(0.05))
            for zl in loads[b * 4:(b + 1) * 4]:
                t, lm, cm, g = ref.best(zl)
                res["ideal_same"].append(swr(g))
                res["ideal_other"].append(swr(other.best(zl)[3]))
                tr = swr(other.g(zl, t, lm, cm))
                res["transplant"].append(tr)
                s, n = hill_climb(other, zl, t, lm, cm)
                res["refined"].append(s)
                res["n_refine"].append(n)
                # probing with SWR only (+-3 % reading error), model of the reference build
                for name, bld in (("probe_same", ref), ("probe_other", other)):
                    meas = [abs(bld.g(zl, *pr)) * (1 + rng.normal(0, 0.03)) for pr in probes]
                    zest = solve_load(ref, probes, meas)
                    t2, l2, c2, _ = ref.best(zest)
                    res[name].append(swr(bld.g(zl, t2, l2, c2)))
                    if name == "probe_other":
                        s, n = hill_climb(other, zl, t2, l2, c2)
                        res["probe_refined"].append(s)
                        res["n_probe_refine"].append(n + len(probes))

        def q(k):
            a = np.array(res[k])
            return f"median {np.median(a):.2f}  90% {np.percentile(a, 90):.2f}  max {a.max():.2f}"
        print(f"\n  f = {f0 / 1e6:g} MHz, 40 loads with SWR up to 10, 10 random builds")
        print(f"    best achievable, measured build       : {q('ideal_same')}")
        print(f"    best achievable, other builds         : {q('ideal_other')}")
        print(f"    known Z, this board's model, same board : (= best achievable)")
        print(f"    known Z, this board's model, other build: {q('transplant')}")
        print(f"      + SWR hill-climb from there         : {q('refined')}  "
              f"(readings: median {np.median(res['n_refine']):.0f}, max {max(res['n_refine'])})")
        print(f"    4 SWR probes -> Z -> setting, same board : {q('probe_same')}")
        print(f"    4 SWR probes -> Z -> setting, other build: {q('probe_other')}")
        print(f"      + SWR hill-climb from there         : {q('probe_refined')}  "
              f"(readings incl. probes: median {np.median(res['n_probe_refine']):.0f}, "
              f"max {max(res['n_probe_refine'])})")

    print("\n=== 3. The inductor ladder: binary code vs actual inductance (this build) ===")
    L = [IND[n]["L_nH"] / 1e3 for n in range(1, 9)]
    for n in range(2, 9):
        below = sum(L[:n - 1])
        print(f"  L{n} = {L[n - 1]:.3f} uH vs L1..L{n - 1} = {below:.3f} uH -> "
              f"{'gap' if L[n - 1] > below else 'OVERLAP'} {abs(L[n - 1] - below):.3f} uH")


if __name__ == "__main__":
    main()
