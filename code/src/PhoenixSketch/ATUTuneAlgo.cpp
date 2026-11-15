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
 * @file ATUTuneAlgo.cpp
 * @brief The automatic tuning program (ATU_PROG_TUNE): a measured search
 *
 * The bridge gives |Gamma| only, and the tuner knows nothing about the network
 * beyond its relay codes: no model, no element values, no calibration. It ranks
 * settings by measured |Gamma| and keeps the best one it has read:
 *
 *   1. Memory: a setting stored for this band and antenna near this frequency is
 *      read first, and kept if it reads within 0.1 of the SWR it was stored with and
 *      within the target.
 *   2. Bypass: read with the network out. A good enough match stays bypassed; a
 *      nearly total reflection (an open or a short) is an antenna fault.
 *   3. Walk the valley, in each topology (LC, then CL). An L-network's |Gamma| has
 *      a long, narrow valley in (L, C) that starts near small L and climbs towards
 *      the match; with C fixed, L alone usually just makes things worse, so a sweep
 *      of one element at a time misses it. Instead, for each coarse L code
 *      {0, 1, 2, 4, ..., 128}, find the best coarse C (a sweep from 0 for the first
 *      L, then a short descent from the previous L's best C), and stop once that
 *      valley floor has risen twice. The elements step by about 2.3x in value, so
 *      the single-element codes are a log-spaced grid whatever the coils' values.
 *   4. Refine from the lowest point of the better topology's valley: pattern search
 *      in code space. Read the four steps along the axes; if none gains, the four
 *      diagonals; move to the best that gains, then keep going the same way with a
 *      doubling step while that gains. When nothing gains, halve both steps. The
 *      steps start at half each code's top set bit (2 for a code below 8). If that
 *      ends short of the target, refine from the other topology's valley too.
 *   5. Stop at SRCH_GOOD_SWR, when refining is done, or after SRCH_MAX_READINGS, and
 *      leave the best setting read. Store it.
 *
 * A setting is never read twice: every reading is in the record, and the planner
 * looks a setting up there before asking for it. An element used past its own
 * resonance simply reads badly and is passed over.
 *
 * next() is pull-based: it walks the search, and wherever it needs the |Gamma| of a
 * setting that has not been read it returns ATU_STEP_MEASURE; once the engine has
 * read it, the next call finds it in the record and walks on. Each call is
 * O(readings) with no heavy arithmetic.
 */

#include "SDT.h"

#ifdef ATU_PRESENT

#include "ATUTune.h"

#define SRCH_BYPASS_SWR         1.3f    // stay bypassed at or below this
#define SRCH_GOOD_SWR           1.2f    // stop searching at or below this
#define SRCH_TARGET_SWR         1.5f    // ATU_OK at or below this, else ATU_FAIL_NO_MATCH
#define SRCH_FAULT_GAMMA        0.98f   // bypass above this (SWR 99): antenna fault
#define SRCH_MIN_GAIN           0.003f  // a refine step must lower |Gamma| by more than this
#define SRCH_DESCENT_GAIN       0.002f  // a valley-walk descent step must gain more than this
#define SRCH_FLOOR_RISE         0.005f  // the valley floor has risen if it goes up by more
#define SRCH_FLOOR_RISES        2       // stop walking after this many rises
#define SRCH_SWEEP_RISES        2       // stop a C sweep after this many rises
#define SRCH_MAX_READINGS       120     // about 13 s at today's reading time
#define SRCH_MEMORY_SWR_MARGIN  0.1f    // keep a stored setting reading within this of its SWR
#define SRCH_COARSE_ITEMS       9       // codes 0, 1, 2, 4, ..., 128

static inline float SwrToGamma(float swr){ return (swr - 1.0f) / (swr + 1.0f); }
static inline float GammaToSwr(float g){
    float gc = fminf(g, 0.999f);
    return (1.0f + gc) / (1.0f - gc);
}

enum SearchPhase { S_MEMORY, S_BYPASS, S_WALK, S_REFINE, S_DONE };
enum InnerMode { IN_SWEEP, IN_DESCEND };

#define MAX_CAND 4

static struct {
    SearchPhase phase;
    int32_t band, antenna, f_kHz;   // where the result is stored
    uint32_t memSetting;            // the stored setting tried first
    float memSwr;                   // its stored SWR (0 if not measured)

    // Valley walk, one topology at a time
    uint8_t topo;                   // 0 = LC, 1 = CL
    uint8_t outer;                  // index of the coarse L code
    InnerMode inner;
    int8_t j;                       // index of the coarse C code being read
    int8_t dir;                     // descent: 0 reading the start, then +1, then -1
    int8_t c0;                      // descent start (the previous L's best C index)
    int8_t innerBest;               // index of the best C at this L
    float innerBestG;
    float innerLastG;               // sweep: the last reading
    uint8_t innerRises;             // sweep: rises past the minimum
    bool haveFloor;                 // a previous L's best C exists
    float floorBest, floorLast;
    uint8_t floorRises;
    uint32_t seed[2];               // lowest point of each topology's valley
    float seedG[2];
    bool refined[2];

    // Refine
    uint8_t l, c, sL, sC;
    float baseG;
    uint8_t group;                  // 0: steps along the axes; 1: diagonals
    uint32_t cand[MAX_CAND];
    uint8_t nCand, candIdx;
    bool pattern;                   // repeating the last move
    int16_t dL, dC;
} srch;

static inline uint8_t CoarseCode(int item){ return item ? (uint8_t)(1u << (item - 1)) : 0; }

static inline uint32_t Pack(uint8_t topo, uint8_t l, uint8_t c){
    return ATU_PACK(true, false, topo == 1, l, c);
}

static inline bool InRange(int l, int c){ return (l >= 0) && (l <= 255) && (c >= 0) && (c <= 255); }

/// Half the top set bit of a code, at least 2
static uint8_t StartStep(uint8_t code){
    uint8_t top = 1;
    while ((top << 1) <= code) top <<= 1;
    return (top > 4) ? (uint8_t)(top >> 1) : 2;
}

/**
 * PRIVATE: |Gamma| of a setting if it has been read
 * @return false if it has not been read yet
 */
static bool Lookup(const ATUTuneRecord *rec, uint32_t setting, float *g){
    for (int i = 0; i < rec->n; i++){
        if (rec->setting[i] == setting){ *g = rec->gamma[i]; return true; }
    }
    return false;
}

/// PRIVATE: The best setting read so far (bypass if nothing has been read)
static uint32_t BestRead(const ATUTuneRecord *rec, float *g){
    uint32_t best = ATU_PACKED_BYPASS;
    *g = 2.0f;
    for (int i = 0; i < rec->n; i++){
        if (rec->gamma[i] < *g){ *g = rec->gamma[i]; best = rec->setting[i]; }
    }
    return best;
}

static inline ATUStep Measure(uint32_t setting){ return {ATU_STEP_MEASURE, setting, ATU_OK}; }

/// PRIVATE: Finish with this setting on the relays, and store it
static ATUStep Done(ATUTuneRecord *rec, uint32_t setting, float g){
    srch.phase = S_DONE;
    rec->finalGamma = g;
    float swr = GammaToSwr(g);
    ATUMemoryStore(srch.band, srch.antenna, setting, srch.f_kHz, swr);
    return {ATU_STEP_DONE, setting, (swr <= SRCH_TARGET_SWR) ? ATU_OK : ATU_FAIL_NO_MATCH};
}

/// PRIVATE: Finish with the best setting read
static ATUStep DoneBest(ATUTuneRecord *rec){
    float g;
    uint32_t s = BestRead(rec, &g);
    return Done(rec, s, g);
}

/// PRIVATE: Start the inner search (the best C) at the current coarse L
static void StartInner(void){
    srch.innerBestG = 2.0f;
    if (!srch.haveFloor){
        srch.inner = IN_SWEEP;
        srch.j = 0;
        srch.innerLastG = 2.0f;
        srch.innerRises = 0;
    } else {
        srch.inner = IN_DESCEND;
        srch.j = srch.c0;
        srch.dir = 0;
    }
}

/// PRIVATE: Start walking a topology's valley
static void StartWalk(uint8_t topo){
    srch.phase = S_WALK;
    srch.topo = topo;
    srch.outer = 0;
    srch.haveFloor = false;
    srch.floorBest = 2.0f;
    srch.floorLast = 2.0f;
    srch.floorRises = 0;
    srch.seedG[topo] = 2.0f;
    StartInner();
}

/// PRIVATE: Build the candidates of one refine round (axis steps or diagonals)
static void StartRound(void){
    srch.nCand = 0;
    srch.candIdx = 0;
    srch.pattern = false;
    int sL = srch.sL, sC = srch.sC;
    const int axL[MAX_CAND] = {+sL, -sL, 0, 0}, axC[MAX_CAND] = {0, 0, +sC, -sC};
    const int dgL[MAX_CAND] = {+sL, -sL, +sL, -sL}, dgC[MAX_CAND] = {+sC, -sC, -sC, +sC};
    const int *dl = srch.group ? dgL : axL, *dc = srch.group ? dgC : axC;
    for (int i = 0; i < MAX_CAND; i++){
        int nl = srch.l + dl[i], nc = srch.c + dc[i];
        if (InRange(nl, nc)) srch.cand[srch.nCand++] = Pack(srch.topo, (uint8_t)nl, (uint8_t)nc);
    }
}

/// PRIVATE: Start refining from a topology's valley
static void StartRefine(uint8_t topo){
    srch.phase = S_REFINE;
    srch.refined[topo] = true;
    srch.topo = topo;
    srch.l = ATU_PACKED_L(srch.seed[topo]);
    srch.c = ATU_PACKED_C(srch.seed[topo]);
    srch.sL = StartStep(srch.l);
    srch.sC = StartStep(srch.c);
    srch.baseG = srch.seedG[topo];
    srch.group = 0;
    StartRound();
}

/**
 * PRIVATE: Advance the inner search (the best coarse C at the current coarse L)
 * @return true when it has finished (innerBest/innerBestG hold the result);
 *         false with *s set to a setting that must be read first
 */
static bool InnerStep(const ATUTuneRecord *rec, uint32_t *s){
    uint8_t L = CoarseCode(srch.outer);
    float g;
    for (;;){
        if (srch.inner == IN_SWEEP){
            // From C = 0 up, until it has risen twice past its minimum
            if (srch.j >= SRCH_COARSE_ITEMS) return true;
            *s = Pack(srch.topo, L, CoarseCode(srch.j));
            if (!Lookup(rec, *s, &g)) return false;
            if (g < srch.innerBestG){
                srch.innerBestG = g;
                srch.innerBest = srch.j;
                srch.innerRises = 0;
            } else if (g > srch.innerLastG){
                srch.innerRises++;
            }
            srch.innerLastG = g;
            srch.j++;
            if (srch.innerRises >= SRCH_SWEEP_RISES) return true;
            continue;
        }
        // Descent from c0: up while it gains; if up gained nothing, down while it gains
        if ((srch.j >= 0) && (srch.j < SRCH_COARSE_ITEMS)){
            *s = Pack(srch.topo, L, CoarseCode(srch.j));
            if (!Lookup(rec, *s, &g)) return false;
            if (srch.dir == 0){
                srch.innerBestG = g;
                srch.innerBest = srch.j;
                srch.dir = +1;
                srch.j = (int8_t)(srch.c0 + 1);
                continue;
            }
            if (g < srch.innerBestG - SRCH_DESCENT_GAIN){
                srch.innerBestG = g;
                srch.innerBest = srch.j;
                srch.j = (int8_t)(srch.j + srch.dir);
                continue;
            }
        }
        if ((srch.dir == +1) && (srch.innerBest == srch.c0)){
            srch.dir = -1;
            srch.j = (int8_t)(srch.c0 - 1);
            continue;
        }
        return true;
    }
}

static void TuneBegin(ATUTuneRecord *rec, uint32_t operatorSetting, double f_Hz){
    memset(&srch, 0, sizeof(srch));
    srch.phase = S_BYPASS;
    srch.band = ATUCurrentBand();
    srch.antenna = ATUCurrentAntenna();
    srch.f_kHz = (int32_t)lround(f_Hz / 1000.0);
    int32_t memF_kHz;
    if (ATUMemoryGet(srch.band, srch.antenna, &srch.memSetting, &memF_kHz, &srch.memSwr) &&
        (fabsf((float)(memF_kHz - srch.f_kHz)) <= ATU_MEMORY_FREQ_FRACTION * (float)srch.f_kHz)){
        srch.phase = S_MEMORY;
    }
}

static ATUStep TuneNext(ATUTuneRecord *rec){
    float g;
    if ((srch.phase == S_WALK) || (srch.phase == S_REFINE)){
        // Stop as soon as anything read is good enough, or the readings run out
        float bestG;
        BestRead(rec, &bestG);
        if ((bestG <= SwrToGamma(SRCH_GOOD_SWR)) || (rec->n >= SRCH_MAX_READINGS)) return DoneBest(rec);
    }
    // Each pass of this loop either asks for a reading, finishes, or moves the
    // search on using readings already taken; the bound only guards against a
    // logic error.
    for (int guard = 0; guard < 8 * ATU_MAX_READINGS; guard++){
        switch (srch.phase){
            case S_MEMORY:{
                if (!Lookup(rec, srch.memSetting, &g)) return Measure(srch.memSetting);
                float swr = GammaToSwr(g);
                bool keep = (swr <= SRCH_TARGET_SWR) &&
                            ((srch.memSwr <= 0) || (swr <= srch.memSwr + SRCH_MEMORY_SWR_MARGIN));
                if (keep) return Done(rec, srch.memSetting, g);
                srch.phase = S_BYPASS;
                break;
            }
            case S_BYPASS:{
                if (!Lookup(rec, ATU_PACKED_BYPASS, &g)) return Measure(ATU_PACKED_BYPASS);
                if (g <= SwrToGamma(SRCH_BYPASS_SWR)) return Done(rec, ATU_PACKED_BYPASS, g);
                if (g > SRCH_FAULT_GAMMA) return {ATU_STEP_FAIL, 0, ATU_FAIL_ANTENNA};
                StartWalk(0);
                break;
            }
            case S_WALK:{
                uint32_t s;
                if (!InnerStep(rec, &s)) return Measure(s);
                // The valley floor at this L
                float m = srch.innerBestG;
                if (m < srch.seedG[srch.topo]){
                    srch.seedG[srch.topo] = m;
                    srch.seed[srch.topo] = Pack(srch.topo, CoarseCode(srch.outer), CoarseCode(srch.innerBest));
                }
                if (m < srch.floorBest){
                    srch.floorBest = m;
                    srch.floorRises = 0;
                } else if (m > srch.floorLast + SRCH_FLOOR_RISE){
                    srch.floorRises++;
                }
                srch.floorLast = m;
                srch.haveFloor = true;
                srch.c0 = srch.innerBest;
                if ((srch.floorRises < SRCH_FLOOR_RISES) && (++srch.outer < SRCH_COARSE_ITEMS)){
                    StartInner();
                    break;
                }
                // End of this topology's walk
                if (srch.topo == 0){
                    StartWalk(1);
                    break;
                }
                StartRefine((srch.seedG[1] < srch.seedG[0]) ? 1 : 0);
                break;
            }
            case S_REFINE:{
                if (rec->n >= SRCH_MAX_READINGS) return DoneBest(rec);
                if (srch.pattern){
                    // Repeat the last move, doubled, while it gains
                    int nl = srch.l + srch.dL, nc = srch.c + srch.dC;
                    if (InRange(nl, nc)){
                        uint32_t s = Pack(srch.topo, (uint8_t)nl, (uint8_t)nc);
                        if (!Lookup(rec, s, &g)) return Measure(s);
                        if (g < srch.baseG - SRCH_MIN_GAIN){
                            srch.l = (uint8_t)nl;
                            srch.c = (uint8_t)nc;
                            srch.baseG = g;
                            srch.dL *= 2;
                            srch.dC *= 2;
                            break;
                        }
                    }
                    srch.group = 0;
                    StartRound();
                    break;
                }
                if (srch.candIdx < srch.nCand){
                    uint32_t s = srch.cand[srch.candIdx];
                    if (!Lookup(rec, s, &g)) return Measure(s);
                    if (g <= SwrToGamma(SRCH_GOOD_SWR)) return Done(rec, s, g);
                    srch.candIdx++;
                    break;
                }
                // End of a round: move to the best candidate if it gains
                uint32_t bestS = 0;
                float bestG = 2.0f;
                for (int i = 0; i < srch.nCand; i++){
                    if (Lookup(rec, srch.cand[i], &g) && (g < bestG)){ bestG = g; bestS = srch.cand[i]; }
                }
                if (bestG < srch.baseG - SRCH_MIN_GAIN){
                    srch.dL = (int16_t)(ATU_PACKED_L(bestS) - srch.l);
                    srch.dC = (int16_t)(ATU_PACKED_C(bestS) - srch.c);
                    srch.l = ATU_PACKED_L(bestS);
                    srch.c = ATU_PACKED_C(bestS);
                    srch.baseG = bestG;
                    srch.pattern = true;
                    break;
                }
                if (srch.group == 0){
                    srch.group = 1;             // the axes gave nothing: try the diagonals
                } else if ((srch.sL > 1) || (srch.sC > 1)){
                    srch.sL = (srch.sL > 1) ? (uint8_t)(srch.sL >> 1) : 1;
                    srch.sC = (srch.sC > 1) ? (uint8_t)(srch.sC >> 1) : 1;
                    srch.group = 0;
                } else {
                    // Refined as far as it goes. Try the other topology if short of the target.
                    float best;
                    BestRead(rec, &best);
                    uint8_t other = srch.topo ? 0 : 1;
                    if ((best > SwrToGamma(SRCH_TARGET_SWR)) && !srch.refined[other] &&
                        (srch.seedG[other] < 1.0f)){
                        StartRefine(other);
                        break;
                    }
                    return DoneBest(rec);
                }
                StartRound();
                break;
            }
            case S_DONE:
            default:
                return DoneBest(rec);
        }
    }
    return DoneBest(rec);
}

static void TuneReport(ATUTuneRecord *rec, uint32_t setting, float gamma){}

const ATUPlanner ATUTunePlanner = {TuneBegin, TuneNext, TuneReport};

#endif // ATU_PRESENT
