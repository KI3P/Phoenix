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

#ifndef ATUMODEL_H
#define ATUMODEL_H

#include <stdint.h>

/**
 * @file ATUModel.h
 * @brief Circuit model of the K9HZ ATU RF board, for the automatic tuner
 *
 * A port of code/tools/atu_vna/model.py. Every relay setting is a cascade of ABCD
 * matrices: line A, then a shunt node (stray plus the capacitor bank), then line B,
 * with each selected inductor inserted as a pi section at its fitted position along
 * the lines. See code/docs/ATU_RF_Board_Characterisation.md, sections 2-3 and 6.
 *
 * Pure functions with no hardware access. The per-frequency precompute runs in
 * double precision; evaluating a setting runs in single precision.
 */

#define ATU_TOPO_LC     0       // capacitor bank at the antenna side (K17 off)
#define ATU_TOPO_CL     1       // capacitor bank at the transmitter side (K17 on)
#define ATU_NUM_TOPO    2
#define ATU_NUM_CODES   256     // 8-bit L and C masks
#define ATU_NUM_COILS   8
#define ATU_NUM_CAPS    8
#define ATU_NUM_PERT    6       // perturbed coil sets used by robust selection
#define ATU_TOPK        300     // candidates kept by the search for robust selection
#define ATU_Z0          50.0f

/// Fixed paths with no L or C selected: line A, node stray, line B (fitted per topology)
typedef struct {
    double Zta;         // line A characteristic impedance, ohm
    double tau_a_ns;    // line A delay; inductors closer to TX than this sit on line A
    double Cs_pF;       // node stray capacitance...
    double Ls_nH;       // ...in series with this inductance, to ground
    double Ztb;         // line B characteristic impedance, ohm
    double tau_b_ns;    // line B delay
    double alpha_mNp;   // total line loss at 1 MHz, scaled by sqrt(f), shared out by delay
} ATUZeroParams;

/// One series inductor: (R1*sqrt(f/1MHz) + jwL) || Cp, with Cg/2 to ground at each end
typedef struct {
    double L_nH;        // inductance the relay adds
    double Cp_pF;       // self capacitance plus open contact
    double R1;          // series resistance at 1 MHz, ohm
    double t_ns;        // position: delay from the TX port
    double Cg_pF;       // capacitance to ground
} ATUIndParams;

/// One shunt capacitor branch: R + jwLlead + 1/(jwC)
typedef struct {
    double C_pF;
    double Llead_nH;    // whole lead path; Lsh of it is shared by the bank
    double R;
} ATUCapParams;

typedef struct {
    ATUZeroParams zero[ATU_NUM_TOPO];
    ATUIndParams ind[ATU_NUM_COILS];
    ATUCapParams cap[ATU_NUM_CAPS];
    double Lsh_nH;      // lead inductance shared by the whole capacitor bank
} ATUModelParams;

typedef struct { float re, im; } atu_cf;
typedef struct { atu_cf A, B, C, D; } atu_abcd;

/// The model evaluated at one frequency. About 38 KB: keep it in static storage.
typedef struct {
    double f_Hz;
    atu_abcd la[ATU_NUM_TOPO][ATU_NUM_CODES];   // line A with each L code's coils
    atu_abcd lb[ATU_NUM_TOPO][ATU_NUM_CODES];   // line B with each L code's coils
    atu_cf ycaps[ATU_NUM_CODES];                // bank admittance for each C code
    atu_cf ys[ATU_NUM_TOPO];                    // node stray admittance
    // Line segments between coil positions, for cascades with other coil values
    atu_abcd segA[ATU_NUM_TOPO][ATU_NUM_COILS + 1];
    atu_abcd segB[ATU_NUM_TOPO][ATU_NUM_COILS + 1];
    uint8_t orderA[ATU_NUM_TOPO][ATU_NUM_COILS];  // coil index at each line A position
    uint8_t orderB[ATU_NUM_TOPO][ATU_NUM_COILS];
    uint8_t nA[ATU_NUM_TOPO];                     // number of coils on line A
    uint8_t nB[ATU_NUM_TOPO];
    atu_abcd pert[ATU_NUM_PERT][ATU_NUM_COILS];   // perturbed coil pi sections
} ATUModelCache;

/// One relay setting and its predicted |Gamma|
typedef struct {
    float g;            // |Gamma| (squared while inside the search heap)
    uint8_t topo;
    uint8_t l;
    uint8_t c;
} ATUCandidate;

/// The best settings found so far by a search
typedef struct {
    ATUCandidate e[ATU_TOPK];
    uint16_t n;
    bool sorted;        // true after ATUTopKFinish(): e[] ascending by |Gamma|
} ATUTopK;

/// A setting chosen by robust selection
typedef struct {
    ATUCandidate best;  // the setting; best.g is the nominal model's |Gamma|
    float worst;        // its worst |Gamma| over the nominal and perturbed models
} ATURobustChoice;

extern const ATUModelParams ATUModelDefaultParams;
extern const float ATUModelPerturbation[ATU_NUM_PERT][ATU_NUM_COILS][2];

/**
 * @brief Evaluate the model at one frequency
 * @param cache Filled in; about 38 KB, so pass static storage, never a local
 * @param p Model parameters (ATUModelDefaultParams, or a calibrated copy)
 * @param f_Hz Frequency
 * @note Takes a few ms on the Teensy; do it once per tune, not per setting
 */
void ATUModelPrepare(ATUModelCache *cache, const ATUModelParams *p, double f_Hz);

/**
 * @brief Reflection coefficient at TX for one setting
 * @param cache Prepared with ATUModelPrepare()
 * @param topo ATU_TOPO_LC or ATU_TOPO_CL
 * @param l Inductor mask, bit n = L(n+1)
 * @param c Capacitor mask, bit n = C(n+1)
 * @param zl Load impedance on ANT, ohm
 */
atu_cf ATUModelGamma(const ATUModelCache *cache, uint8_t topo, uint8_t l, uint8_t c, atu_cf zl);

/**
 * @brief Reflection coefficient at TX for one setting, with perturbed coils
 * @param set Perturbed coil set, 0 to ATU_NUM_PERT-1
 * @note Recomputes the setting's cascade from the line segments
 */
atu_cf ATUModelGammaPerturbed(const ATUModelCache *cache, int set,
                              uint8_t topo, uint8_t l, uint8_t c, atu_cf zl);

/// |z|
float ATUCAbs(atu_cf z);

/**
 * @brief The firmware's one model cache, in RAM2 (DMAMEM)
 * @note Shared by the tuner and the AX benchmark; only one of them runs at a time
 */
ATUModelCache *ATUSharedModelCache(void);
ATUTopK *ATUSharedTopK(void);

/// Empty the candidate list before a new search
void ATUTopKReset(ATUTopK *topk);

/**
 * @brief Search part of the setting space for a load, keeping the best ATU_TOPK
 * @param unitFrom First unit to search. A unit is one topology and L code:
 *        unit = topo * 256 + l, so the whole space is units 0 to 511.
 * @param unitTo One past the last unit to search
 * @note Each unit evaluates all 256 C codes. Call it in chunks to spread a search
 *       over several main-loop passes, then call ATUTopKFinish().
 */
void ATUModelSearchChunk(const ATUModelCache *cache, atu_cf zl,
                         uint16_t unitFrom, uint16_t unitTo, ATUTopK *topk);

/// Sort the candidates ascending and convert them to |Gamma|. Call after the last chunk.
void ATUTopKFinish(ATUTopK *topk);

/**
 * @brief Search every setting for a load
 * @return The best setting by the nominal model; topk holds the best ATU_TOPK, sorted
 */
ATUCandidate ATUModelSearchAll(const ATUModelCache *cache, atu_cf zl, ATUTopK *topk);

/**
 * @brief Robust selection (report section 6.4)
 *
 * Of the candidates in topk (sorted, by ATUTopKFinish()), choose the one whose worst
 * |Gamma| over the nominal model and the ATU_NUM_PERT perturbed coil sets is lowest.
 * Such a setting stays good on a build whose coils differ a little from the model.
 */
ATURobustChoice ATUModelRobustPick(const ATUModelCache *cache, atu_cf zl, const ATUTopK *topk);

#endif // ATUMODEL_H
