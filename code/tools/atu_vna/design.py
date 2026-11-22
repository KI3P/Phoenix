#!/usr/bin/env python3
"""Choose ATU relay settings for the matching demonstrations, from the fitted model.

    python3 design.py [--data DIR]

For each target (topology, frequency, R) it searches all 256 x 256 L/C masks for
the setting whose predicted input impedance at TX, with ANT terminated in 50 ohm,
is closest to R. Writes match_plan.json, which analyse.py reads for figure 9.

By the conjugate-match theorem a lossless network that turns 50 ohm at ANT into
R at TX also matches a source of R at TX to 50 ohm at ANT, so these settings show
the network's impedance transformation in both directions.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from s2p import load  # noqa: E402
import model as M  # noqa: E402

TARGETS = [
    ("LC", 3.6e6, 12.5), ("CL", 3.6e6, 200.0),
    ("LC", 14.2e6, 12.5), ("CL", 14.2e6, 200.0),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=os.path.join(HERE, "data"))
    a = ap.parse_args()
    fits = json.load(open(os.path.join(HERE, "fits.json")))
    ind = {int(k): v for k, v in fits["inductors"].items()}
    cap = {int(k): v for k, v in fits["capacitors"].items()}
    Lsh = fits["bank"]["Lsh_nH"]
    zero = {}
    for topo in ("LC", "CL"):
        f, s11, s21 = load(f"atu_zero_{topo}", a.data)
        p, build = M.fit_zero(f, s11, s21)
        zero[topo] = (f, build([p[k] for k in M.ZERO_KEYS]))

    plan = []
    for topo, f0, R in TARGETS:
        f, z = zero[topo]
        i = int(np.argmin(np.abs(f - f0)))
        fi = f[i:i + 1]
        zi = {k: (v[i:i + 1] if isinstance(v, np.ndarray) else v) for k, v in z.items()}
        best = None
        for lm in range(256):
            for cm in range(256):
                s11, _ = M.predict(fi, zi, ind, cap, lm, cm, Lsh)
                Zin = M.zin_from_s11(s11[0])
                g = abs((Zin - R) / (Zin + R))
                if best is None or g < best[0]:
                    best = (g, lm, cm, Zin)
        g, lm, cm, Zin = best
        swr = (1 + g) / (1 - g)
        Lsum = sum(M.NOM_L_uH[n] for n in range(8) if lm & (1 << n))
        Csum = sum(M.NOM_C_pF[n] for n in range(8) if cm & (1 << n))
        tag = f"match_{topo}_{f0 / 1e6:g}MHz_{R:g}".replace(".", "p")
        print(f"{tag}: LM {lm} CM {cm}  (nominal {Lsum:.2f} uH, {Csum:g} pF; grid point {f[i] / 1e6:.3f} MHz)"
              f"  predicted Zin {Zin.real:.1f}{Zin.imag:+.1f}j  SWR re {R:g} = {swr:.2f}")
        plan.append(dict(tag=tag, topology=topo, f0=float(f[i]), R_target=R, lmask=lm, cmask=cm,
                         predicted_zin=[float(Zin.real), float(Zin.imag)], predicted_swr=float(swr)))
    json.dump(plan, open(os.path.join(HERE, "match_plan.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
