#!/usr/bin/env python3
"""Offline simulation of the firmware's measured-search ATU tuner (ATUTuneAlgo.cpp).

    python3 search_sim.py              # summary for two build variants
    python3 search_sim.py --bad        # also list the loads it tunes poorly

The tuner itself uses no model. This script stands in for the board with the
fitted model (tuning_sim.Build, every setting of both topologies at once), so a
change to the search can be tried on many loads and builds in seconds before it
is ported to the firmware. It mirrors the firmware planner:

  bypass -> walk the valley in LC (for each coarse L, the best coarse C; stop
  when the valley floor has risen twice) -> if it reaches SWR 1.5, refine it at
  once (pattern search: axis steps, then diagonals, doubling repeats, halving)
  -> otherwise, or if that ends above 1.5, walk CL and refine the lower valley
  not yet refined, then the other. Stop at SWR 1.2 or after the budget.

80-15 m are scored against the best setting of each simulated build. On 10 m that
best usually relies on the largest elements far past resonance, where the model is
known to be wrong (report section 3.5), so 10 m is only checked against bypass.
"""
import sys

import numpy as np

import tuning_sim as T

GRID = [0] + [1 << k for k in range(8)]     # single-element codes


def swr(g):
    g = min(abs(g), 0.999)
    return (1 + g) / (1 - g)


def g_of(s):
    return (s - 1) / (s + 1)


GOOD, TARGET, BYPASS_OK, FAULT = g_of(1.2), g_of(1.5), g_of(1.3), 0.98


class Stop(Exception):
    pass


class Reader:
    """|Gamma| as the bridge reads it, counting readings and never repeating one."""
    def __init__(self, maps, bypass, leak, budget):
        self.maps, self.bypass, self.leak, self.budget = maps, bypass, leak, budget
        self.seen = {}

    def __call__(self, topo, l, c):
        key = (topo, l, c)
        if key not in self.seen:
            if len(self.seen) >= self.budget:
                raise Stop
            g = self.bypass if topo == "BY" else self.maps[topo][l, c]
            self.seen[key] = abs(g + self.leak)
            if topo != "BY" and self.seen[key] <= GOOD:
                raise Stop
        return self.seen[key]

    def best(self):
        return min(self.seen.items(), key=lambda kv: kv[1])


def inner(read, t, L, cprev):
    """Best coarse C at this L: a sweep from 0 (stop after two rises), or a descent
    from the previous L's best C (up while it gains, else down while it gains)."""
    if cprev is None:
        best, bc, rises, last = 9, 0, 0, 9
        for c in GRID:
            g = read(t, L, c)
            if g < best:
                best, bc, rises = g, c, 0
            elif g > last:
                rises += 1
            last = g
            if rises >= 2:
                break
        return bc, best
    i = GRID.index(cprev)
    best, bi = read(t, L, cprev), i
    for d in (+1, -1):
        j = i + d
        while 0 <= j < len(GRID):
            g = read(t, L, GRID[j])
            if g < best - 0.002:
                best, bi, j = g, j, j + d
            else:
                break
        if bi != i:
            break
    return GRID[bi], best


def walk(read, t):
    cprev, floor_best, rises, last, seed = None, 9, 0, 9, None
    for L in GRID:
        c, m = inner(read, t, L, cprev)
        cprev = c
        if m < floor_best:
            floor_best, seed, rises = m, (m, t, L, c), 0
        elif m > last + 0.005:
            rises += 1
        last = m
        if rises >= 2:
            break
    return seed


def refine(read, t, l, c):
    def start(code):
        top = 1
        while (top << 1) <= code:
            top <<= 1
        return top >> 1 if top > 4 else 2

    def ok(x):
        return 0 <= x[0] <= 255 and 0 <= x[1] <= 255

    sL, sC, base = start(l), start(c), read(t, l, c)
    while True:
        moved = False
        for grp in ([(sL, 0), (-sL, 0), (0, sC), (0, -sC)],
                    [(sL, sC), (-sL, -sC), (sL, -sC), (-sL, sC)]):
            cands = [(l + a, c + b) for a, b in grp if ok((l + a, c + b))]
            if not cands:
                continue
            g, x = min((read(t, *x), x) for x in cands)
            if g < base - 0.003:
                dl, dc = x[0] - l, x[1] - c
                l, c, base = x[0], x[1], g
                while ok((l + dl, c + dc)):
                    g2 = read(t, l + dl, c + dc)
                    if g2 < base - 0.003:
                        l, c, base, dl, dc = l + dl, c + dc, g2, 2 * dl, 2 * dc
                    else:
                        break
                moved = True
                break
        if moved:
            continue
        if sL == 1 and sC == 1:
            return
        sL, sC = max(1, sL // 2), max(1, sC // 2)


def tune(read):
    g = read("BY", 0, 0)
    if g <= BYPASS_OK or g > FAULT:
        return
    first = walk(read, "LC")
    if first[0] <= TARGET:
        refine(read, *first[1:])
        if read.best()[1] <= TARGET:
            return
    second = walk(read, "CL")
    for sd in sorted([first, second]):
        if sd is first and first[0] <= TARGET:
            continue                            # already refined
        if sd[0] >= 1.0:
            return
        refine(read, *sd[1:])
        if read.best()[1] <= TARGET:
            return


def run(sigma, leak_hf, leak_10m, budget=120, seed=1, show_bad=False):
    T.rng = np.random.default_rng(seed)
    out = {}
    for f in (3.65e6, 7.1e6, 14.2e6, 21.2e6, 28.4e6):
        rows = []
        for r in (12.5, 25, 100, 200, 400):
            for x in (-100, 0, 100):
                build = T.Build(f, T.variant(sigma))
                zl = r + 1j * x
                maps = {t: np.asarray(v) for t, v in build.g(zl).items()}
                bypass = (zl - 50) / (zl + 50)
                best = min(swr(np.abs(m).min()) for m in maps.values())
                leak = (leak_10m if f > 25e6 else leak_hf) * np.exp(1j)
                rd = Reader(maps, bypass, leak, budget)
                try:
                    tune(rd)
                except Stop:
                    pass
                (t, l, c), _ = rd.best()
                got = swr(bypass if t == "BY" else maps[t][l, c])
                rows.append((got, best, swr(bypass), len(rd.seen)))
                if show_bad and got - max(best, 1.2) > 0.3:
                    print(f"   poor: {f / 1e6:5.2f} MHz {zl}: SWR {got:.2f} ({t} L{l} C{c}), best {best:.2f}")
        out[f] = rows
    return out


def summary(res):
    hf = [o for f, v in res.items() if f < 25e6 for o in v]
    reach = [o for o in hf if o[1] <= 1.4]
    short = np.sort([max(0, o[0] - max(o[1], 1.2)) for o in hf])
    ten = res[28.4e6]
    reads = np.sort([o[3] for v in res.values() for o in v])
    return (f"80-15 m: {sum(o[0] <= 1.5 for o in reach)}/{len(reach)} reachable loads within SWR 1.5, "
            f"90th percentile shortfall {short[int(0.9 * len(short))]:.2f} | "
            f"10 m: {sum(o[0] <= 1.5 for o in ten)}/15 within 1.5, "
            f"{sum(o[0] > o[2] + 0.01 for o in ten)} worse than bypass | "
            f"readings median {np.median(reads):.0f}, 90th {reads[int(0.9 * len(reads))]}, max {reads[-1]}")


if __name__ == "__main__":
    bad = "--bad" in sys.argv
    for name, sigma, lh, l10 in (("5 % build, clean bridge", 0.05, 0.0, 0.0),
                                 ("12 % build, leaky bridge", 0.12, 0.03, 0.10)):
        print(f"{name}: {summary(run(sigma, lh, l10, show_bad=bad))}")
