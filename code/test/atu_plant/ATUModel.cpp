/*
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix.
If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * @file ATUModel.cpp
 * @brief Circuit model of the K9HZ ATU RF board, ported from tools/atu_vna/model.py
 *
 * Cascade for one setting (port 1 = TX, port 2 = ANT):
 *
 *     line A [coils on line A] -- shunt node (stray + bank) -- line B [coils on line B]
 *
 * Coils sit at fitted delays from TX. In the LC topology all eight are on line A;
 * in CL the node is closer to TX, so L4-L8 are on line B. A coil whose relay is off
 * is an identity, because splitting a uniform line is exact.
 *
 * Evaluating a setting: with the line A product (A1 B1 C1 D1), the line B product
 * (A2 B2 C2 D2) and the node admittance Y,
 *     Yb = (C2 zl + D2) / (A2 zl + B2)            admittance looking into line B
 *     Yt = Yb + Y
 *     Gamma = (P + Q Yt) / (R + S Yt)
 * with P = A1 - 50 C1, Q = B1 - 50 D1, R = A1 + 50 C1, S = B1 + 50 D1. Yb, P, Q, R
 * and S depend only on the L code, so the search computes them once per L code and
 * then needs two complex multiplies per C code.
 */

#include <complex>
#include <math.h>
#include "ATUModel.h"
#include "ATUModelParams_default.h"

typedef std::complex<double> cd;

/// ABCD in double precision, for the per-frequency precompute
struct M2 { cd A, B, C, D; };

static const M2 IDENT = {cd(1, 0), cd(0, 0), cd(0, 0), cd(1, 0)};

static inline M2 Mul(const M2 &a, const M2 &b){
    return {a.A * b.A + a.B * b.C, a.A * b.B + a.B * b.D,
            a.C * b.A + a.D * b.C, a.C * b.B + a.D * b.D};
}

static inline atu_cf ToF(cd z){ return {(float)z.real(), (float)z.imag()}; }

static inline atu_abcd ToF(const M2 &m){ return {ToF(m.A), ToF(m.B), ToF(m.C), ToF(m.D)}; }

///////////////////////////////////////////////////////////////////////////////
// Single-precision complex arithmetic for the per-setting work. Written out
// rather than std::complex<float>, whose operator* calls a slow library routine
// (__mulsc3) without -ffast-math.
///////////////////////////////////////////////////////////////////////////////

static inline atu_cf CAdd(atu_cf a, atu_cf b){ return {a.re + b.re, a.im + b.im}; }
static inline atu_cf CSub(atu_cf a, atu_cf b){ return {a.re - b.re, a.im - b.im}; }
static inline atu_cf CMul(atu_cf a, atu_cf b){
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}
static inline atu_cf CScale(atu_cf a, float s){ return {a.re * s, a.im * s}; }
static inline float CNorm(atu_cf a){ return a.re * a.re + a.im * a.im; }
static inline atu_cf CDiv(atu_cf a, atu_cf b){
    float d = CNorm(b);
    return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
}

float ATUCAbs(atu_cf z){ return sqrtf(CNorm(z)); }

// About 40 KB together
static ATUModelCache sharedCache;
static ATUTopK sharedTopK;
ATUModelCache *ATUSharedModelCache(void){ return &sharedCache; }
ATUTopK *ATUSharedTopK(void){ return &sharedTopK; }

static inline atu_abcd FMul(const atu_abcd &a, const atu_abcd &b){
    return {CAdd(CMul(a.A, b.A), CMul(a.B, b.C)), CAdd(CMul(a.A, b.B), CMul(a.B, b.D)),
            CAdd(CMul(a.C, b.A), CMul(a.D, b.C)), CAdd(CMul(a.C, b.B), CMul(a.D, b.D))};
}

///////////////////////////////////////////////////////////////////////////////
// Elements (model.py: line, z_inductor, inductor_abcd, y_capacitor)
///////////////////////////////////////////////////////////////////////////////

/// Lossy line: delay tau_s, loss alpha_Np at 1 MHz scaled by sqrt(f)
static M2 Line(double f, double Zt, double tau_s, double alpha_Np){
    cd g(alpha_Np * sqrt(f / 1e6), 2.0 * M_PI * f * tau_s);
    cd ch = std::cosh(g), sh = std::sinh(g);
    return {ch, Zt * sh, sh / Zt, ch};
}

/// Inductor pi section: (R1 sqrt(f/1MHz) + jwL) || Cp in series, Cg/2 shunt each end
static M2 Inductor(double f, double L_nH, double Cp_pF, double R1, double Cg_pF){
    double w = 2.0 * M_PI * f;
    cd zs(R1 * sqrt(f / 1e6), w * L_nH * 1e-9);
    cd z = (Cp_pF > 0) ? 1.0 / (1.0 / zs + cd(0, w * Cp_pF * 1e-12)) : zs;
    M2 yg = {cd(1, 0), cd(0, 0), cd(0, w * Cg_pF * 1e-12 / 2.0), cd(1, 0)};
    M2 ser = {cd(1, 0), z, cd(0, 0), cd(1, 0)};
    return Mul(Mul(yg, ser), yg);
}

/// Capacitor branch admittance: 1 / (R + jwLlead + 1/(jwC))
static cd Capacitor(double f, double C_pF, double Llead_nH, double R){
    double w = 2.0 * M_PI * f;
    return 1.0 / (cd(R, w * Llead_nH * 1e-9) + 1.0 / cd(0, w * C_pF * 1e-12));
}

///////////////////////////////////////////////////////////////////////////////
// Per-frequency precompute
///////////////////////////////////////////////////////////////////////////////

/**
 * PRIVATE: Split one line into segments between the coils that sit on it.
 *
 * @param pos Delay of each coil along this line (s), in path order
 * @param n Number of coils on the line
 * @param seg Out: n+1 segment matrices
 */
static void LineSegments(double f, double Zt, double tau, double alpha, double tauTotal,
                         const double *pos, int n, atu_abcd *seg){
    double prev = 0.0;
    for (int k = 0; k < n; k++){
        double t = fmin(fmax(pos[k], 0.0), tau);
        seg[k] = ToF(Line(f, Zt, t - prev, alpha * (t - prev) / tauTotal));
        prev = t;
    }
    seg[n] = ToF(Line(f, Zt, tau - prev, alpha * (tau - prev) / tauTotal));
}

/**
 * PRIVATE: Line product for every L code, with the given coil matrices (double).
 */
static void LineProducts(double f, double Zt, double tau, double alpha, double tauTotal,
                         const double *pos, const uint8_t *order, int n,
                         const M2 *coil, atu_abcd *out){
    M2 seg[ATU_NUM_COILS + 1];
    double prev = 0.0;
    for (int k = 0; k < n; k++){
        double t = fmin(fmax(pos[k], 0.0), tau);
        seg[k] = Line(f, Zt, t - prev, alpha * (t - prev) / tauTotal);
        prev = t;
    }
    seg[n] = Line(f, Zt, tau - prev, alpha * (tau - prev) / tauTotal);

    for (int code = 0; code < ATU_NUM_CODES; code++){
        M2 m = seg[0];
        for (int k = 0; k < n; k++){
            if (code & (1 << order[k])) m = Mul(m, coil[order[k]]);
            m = Mul(m, seg[k + 1]);
        }
        out[code] = ToF(m);
    }
}

void ATUModelPrepare(ATUModelCache *cache, const ATUModelParams *p, double f_Hz){
    const double f = f_Hz;
    const double w = 2.0 * M_PI * f;
    cache->f_Hz = f;

    // Coil pi sections: nominal, and each perturbed set
    M2 coil[ATU_NUM_COILS];
    for (int n = 0; n < ATU_NUM_COILS; n++){
        const ATUIndParams &q = p->ind[n];
        coil[n] = Inductor(f, q.L_nH, q.Cp_pF, q.R1, q.Cg_pF);
        for (int s = 0; s < ATU_NUM_PERT; s++){
            cache->pert[s][n] = ToF(Inductor(f, q.L_nH * ATUModelPerturbation[s][n][0],
                                             q.Cp_pF * ATUModelPerturbation[s][n][1],
                                             q.R1, q.Cg_pF));
        }
    }

    // Bank admittance for every C code: the selected branches share Lsh
    cd yc[ATU_NUM_CAPS];
    for (int n = 0; n < ATU_NUM_CAPS; n++){
        const ATUCapParams &q = p->cap[n];
        yc[n] = Capacitor(f, q.C_pF, fmax(q.Llead_nH - p->Lsh_nH, 0.0), q.R);
    }
    for (int code = 0; code < ATU_NUM_CODES; code++){
        cd sum(0, 0);
        for (int n = 0; n < ATU_NUM_CAPS; n++){
            if (code & (1 << n)) sum += yc[n];
        }
        if ((code == 0) || (p->Lsh_nH <= 0)){
            cache->ycaps[code] = ToF(sum);
        } else {
            cache->ycaps[code] = ToF(1.0 / (1.0 / sum + cd(0, w * p->Lsh_nH * 1e-9)));
        }
    }

    // Coils in path order (by delay from TX), as model.py sorts them
    uint8_t byPos[ATU_NUM_COILS];
    for (int n = 0; n < ATU_NUM_COILS; n++) byPos[n] = (uint8_t)n;
    for (int i = 1; i < ATU_NUM_COILS; i++){       // insertion sort, stable
        uint8_t v = byPos[i];
        int j = i - 1;
        while ((j >= 0) && (p->ind[byPos[j]].t_ns > p->ind[v].t_ns)){
            byPos[j + 1] = byPos[j];
            j--;
        }
        byPos[j + 1] = v;
    }

    for (int topo = 0; topo < ATU_NUM_TOPO; topo++){
        const ATUZeroParams &z = p->zero[topo];
        const double ta = z.tau_a_ns * 1e-9;
        const double tb = z.tau_b_ns * 1e-9;
        const double tt = fmax(ta + tb, 1e-15);
        const double alpha = z.alpha_mNp * 1e-3;

        // Node stray: Cs in series with Ls, to ground
        cache->ys[topo] = ToF(cd(0, w * z.Cs_pF * 1e-12)
                              / (1.0 - w * w * z.Ls_nH * 1e-9 * z.Cs_pF * 1e-12));

        // Which coils are on line A (delay <= tau_a) and which on line B
        double posA[ATU_NUM_COILS], posB[ATU_NUM_COILS];
        int nA = 0, nB = 0;
        for (int k = 0; k < ATU_NUM_COILS; k++){
            int n = byPos[k];
            double t = fmin(p->ind[n].t_ns * 1e-9, tt);
            if (t <= ta){
                cache->orderA[topo][nA] = (uint8_t)n;
                posA[nA++] = t;
            } else {
                cache->orderB[topo][nB] = (uint8_t)n;
                posB[nB++] = t - ta;
            }
        }
        cache->nA[topo] = (uint8_t)nA;
        cache->nB[topo] = (uint8_t)nB;

        LineSegments(f, z.Zta, ta, alpha, tt, posA, nA, cache->segA[topo]);
        LineSegments(f, z.Ztb, tb, alpha, tt, posB, nB, cache->segB[topo]);
        LineProducts(f, z.Zta, ta, alpha, tt, posA, cache->orderA[topo], nA, coil,
                     cache->la[topo]);
        LineProducts(f, z.Ztb, tb, alpha, tt, posB, cache->orderB[topo], nB, coil,
                     cache->lb[topo]);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Evaluating settings
///////////////////////////////////////////////////////////////////////////////

/// Terms that depend only on the line products: Yb and P, Q, R, S (see file comment)
struct LTerms { atu_cf Yb, P, Q, R, S; };

static inline LTerms MakeLTerms(const atu_abcd &la, const atu_abcd &lb, atu_cf zl){
    LTerms t;
    t.Yb = CDiv(CAdd(CMul(lb.C, zl), lb.D), CAdd(CMul(lb.A, zl), lb.B));
    atu_cf c50 = CScale(la.C, ATU_Z0);
    atu_cf d50 = CScale(la.D, ATU_Z0);
    t.P = CSub(la.A, c50);
    t.Q = CSub(la.B, d50);
    t.R = CAdd(la.A, c50);
    t.S = CAdd(la.B, d50);
    return t;
}

static inline atu_cf GammaFromTerms(const LTerms &t, atu_cf Ynode){
    atu_cf yt = CAdd(t.Yb, Ynode);
    return CDiv(CAdd(t.P, CMul(t.Q, yt)), CAdd(t.R, CMul(t.S, yt)));
}

atu_cf ATUModelGamma(const ATUModelCache *cache, uint8_t topo, uint8_t l, uint8_t c, atu_cf zl){
    LTerms t = MakeLTerms(cache->la[topo][l], cache->lb[topo][l], zl);
    return GammaFromTerms(t, CAdd(cache->ys[topo], cache->ycaps[c]));
}

/**
 * PRIVATE: A line product for one L code, from the stored segments and coil matrices.
 */
static atu_abcd CascadeLine(const atu_abcd *seg, const uint8_t *order, int n,
                            const atu_abcd *coil, uint8_t l){
    atu_abcd m = seg[0];
    for (int k = 0; k < n; k++){
        if (l & (1 << order[k])) m = FMul(m, coil[order[k]]);
        m = FMul(m, seg[k + 1]);
    }
    return m;
}

atu_cf ATUModelGammaPerturbed(const ATUModelCache *cache, int set,
                              uint8_t topo, uint8_t l, uint8_t c, atu_cf zl){
    const atu_abcd *coil = cache->pert[set];
    atu_abcd la = CascadeLine(cache->segA[topo], cache->orderA[topo], cache->nA[topo], coil, l);
    atu_abcd lb = CascadeLine(cache->segB[topo], cache->orderB[topo], cache->nB[topo], coil, l);
    LTerms t = MakeLTerms(la, lb, zl);
    return GammaFromTerms(t, CAdd(cache->ys[topo], cache->ycaps[c]));
}

///////////////////////////////////////////////////////////////////////////////
// Search: a bounded max-heap of the best ATU_TOPK candidates, keyed on |Gamma|^2
///////////////////////////////////////////////////////////////////////////////

void ATUTopKReset(ATUTopK *topk){
    topk->n = 0;
    topk->sorted = false;
}

static inline void HeapSiftDown(ATUCandidate *h, int n, int i){
    for (;;){
        int big = i;
        int a = 2 * i + 1, b = a + 1;
        if ((a < n) && (h[a].g > h[big].g)) big = a;
        if ((b < n) && (h[b].g > h[big].g)) big = b;
        if (big == i) return;
        ATUCandidate t = h[i]; h[i] = h[big]; h[big] = t;
        i = big;
    }
}

static inline void HeapPush(ATUTopK *topk, ATUCandidate c){
    ATUCandidate *h = topk->e;
    if (topk->n < ATU_TOPK){
        int i = topk->n++;
        h[i] = c;
        while (i > 0){                              // sift up
            int parent = (i - 1) / 2;
            if (h[parent].g >= h[i].g) break;
            ATUCandidate t = h[i]; h[i] = h[parent]; h[parent] = t;
            i = parent;
        }
    } else if (c.g < h[0].g){                       // replace the worst kept
        h[0] = c;
        HeapSiftDown(h, ATU_TOPK, 0);
    }
}

void ATUModelSearchChunk(const ATUModelCache *cache, atu_cf zl,
                         uint16_t unitFrom, uint16_t unitTo, ATUTopK *topk){
    if (unitTo > ATU_NUM_TOPO * ATU_NUM_CODES) unitTo = ATU_NUM_TOPO * ATU_NUM_CODES;
    for (uint16_t u = unitFrom; u < unitTo; u++){
        uint8_t topo = (uint8_t)(u / ATU_NUM_CODES);
        uint8_t l = (uint8_t)(u % ATU_NUM_CODES);
        LTerms t = MakeLTerms(cache->la[topo][l], cache->lb[topo][l], zl);
        atu_cf base = CAdd(t.Yb, cache->ys[topo]);
        for (int c = 0; c < ATU_NUM_CODES; c++){
            atu_cf yt = CAdd(base, cache->ycaps[c]);
            float num = CNorm(CAdd(t.P, CMul(t.Q, yt)));
            float den = CNorm(CAdd(t.R, CMul(t.S, yt)));
            float g2 = num / den;
            if ((topk->n == ATU_TOPK) && !(g2 < topk->e[0].g)) continue;
            HeapPush(topk, {g2, topo, l, (uint8_t)c});
        }
    }
}

void ATUTopKFinish(ATUTopK *topk){
    // Heap sort ascending in place: repeatedly move the largest to the end
    ATUCandidate *h = topk->e;
    for (int end = topk->n - 1; end > 0; end--){
        ATUCandidate t = h[0]; h[0] = h[end]; h[end] = t;
        HeapSiftDown(h, end, 0);
    }
    for (int i = 0; i < topk->n; i++) h[i].g = sqrtf(h[i].g);
    topk->sorted = true;
}

ATUCandidate ATUModelSearchAll(const ATUModelCache *cache, atu_cf zl, ATUTopK *topk){
    ATUTopKReset(topk);
    ATUModelSearchChunk(cache, zl, 0, ATU_NUM_TOPO * ATU_NUM_CODES, topk);
    ATUTopKFinish(topk);
    return topk->e[0];
}

ATURobustChoice ATUModelRobustPick(const ATUModelCache *cache, atu_cf zl, const ATUTopK *topk){
    ATURobustChoice out;
    out.best = topk->e[0];
    out.worst = 2.0f;
    for (int i = 0; i < topk->n; i++){
        const ATUCandidate &cand = topk->e[i];
        float worst = cand.g;
        if (worst >= out.worst) break;      // sorted: no later candidate can win
        for (int s = 0; s < ATU_NUM_PERT; s++){
            float g = ATUCAbs(ATUModelGammaPerturbed(cache, s, cand.topo, cand.l, cand.c, zl));
            if (g > worst){
                worst = g;
                if (worst >= out.worst) break;
            }
        }
        if (worst < out.worst){
            out.worst = worst;
            out.best = cand;
        }
    }
    return out;
}

