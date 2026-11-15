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
 *   1. Bypass: read with the network out. A good enough match stays bypassed; a
 *      nearly total reflection (an open or a short) is an antenna fault.
 *   2. Walk the valley, in each topology (LC, then CL). An L-network's |Gamma| has
 *      a long, narrow valley in (L, C) that starts near small L and climbs towards
 *      the match; with C fixed, L alone usually just makes things worse, so a sweep
 *      of one element at a time misses it. Instead, for each coarse L code
 *      {0, 1, 2, 4, ..., 128}, find the best coarse C (a sweep from 0 for the first
 *      L, then a short descent from the previous L's best C), and stop once that
 *      valley floor has risen twice. The elements step by about 2.3x in value, so
 *      the single-element codes are a log-spaced grid whatever the coils' values.
 *   3. Refine from the lowest point of a valley: pattern search in code space. Read
 *      the four steps along the axes; if none gains, the four diagonals; move to the
 *      best that gains, then keep going the same way with a doubling step while that
 *      gains. When nothing gains, halve both steps. The steps start at half each
 *      code's top set bit (2 for a code below 8).
 *      If LC's valley already reaches the target SWR, it is refined at once, and CL
 *      is walked only if that ends short of the target. Otherwise both valleys are
 *      walked and the lower one refined first; the other is refined too if the first
 *      ends short of the target.
 *   4. Stop at SRCH_GOOD_SWR, when refining is done, or after SRCH_MAX_READINGS, and
 *      leave the best setting read. Store it for this band and antenna.
 *
 * A tune always searches. The stored setting for a band and antenna is already put
 * on the relays when the band or antenna is selected (ATUApplyStoredIfChanged), so
 * asking for a tune means that setting is not good enough any more: a retune.
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
#define SRCH_COARSE_ITEMS       9       // codes 0, 1, 2, 4, ..., 128

#define TOPO_LC                 0       // capacitor bank at the output (antenna) side
#define TOPO_CL                 1       // capacitor bank at the input (transmitter) side
#define UNREAD_GAMMA            2.0f    // above any |Gamma|: "nothing read yet" in a minimum
#define GROUP_AXES              0       // refine round: one element steps
#define GROUP_DIAGONALS         1       // refine round: both elements step

static inline float SwrToGamma(float swr){ return (swr - 1.0f) / (swr + 1.0f); }
static inline float GammaToSwr(float g){
    float gc = fminf(g, 0.999f);
    return (1.0f + gc) / (1.0f - gc);
}

enum SearchPhase { S_BYPASS, S_WALK, S_REFINE, S_DONE };
enum InnerMode { IN_SWEEP, IN_DESCEND };

#define MAX_CAND 4

// The state of the search. "Coarse" codes are the single-element codes 0, 1, 2, 4,
// ..., 128, numbered 0 to SRCH_COARSE_ITEMS - 1 (CoarseCode()). "Outer" is the walk
// along L; "inner" is the search for the best C at one L.
static struct {
    SearchPhase phase;
    int32_t band, antenna, f_kHz;   // where the result is stored
    bool bal;                       // the antenna is on the balun output: every setting keeps it
    uint32_t bypass;                // the network out, on this antenna's output

    // Valley walk, one topology at a time
    uint8_t topo;                   // TOPO_LC or TOPO_CL (also used by the refine)
    uint8_t outer;                  // index of the coarse L code
    InnerMode inner;
    int8_t j;                       // index of the coarse C code being read
    int8_t dir;                     // descent: 0 reading the start, then +1, then -1
    int8_t c0;                      // descent start (the previous L's best C index)
    int8_t innerBest;               // index of the best C at this L
    float innerBestG;               // its |Gamma|: the valley floor at this L
    float innerLastG;               // sweep: the last reading
    uint8_t innerRises;             // sweep: rises past the minimum
    bool haveFloor;                 // a previous L's best C exists
    float floorBest, floorLast;     // the lowest valley floor so far, and the previous L's
    uint8_t floorRises;             // times the floor has risen since its lowest
    uint32_t seed[2];               // lowest point of each topology's valley
    float seedG[2];                 // its |Gamma| (UNREAD_GAMMA if not walked)
    bool refined[2];                // refining from this topology's seed has started
    bool walked[2];                 // this topology's valley walk has finished

    // Refine (pattern search around l, c)
    uint8_t l, c;                   // the base point: the best setting of this refine so far
    uint8_t sL, sC;                 // the step in each code
    float baseG;                    // |Gamma| at the base point
    uint8_t group;                  // GROUP_AXES or GROUP_DIAGONALS
    uint32_t cand[MAX_CAND];        // the settings of this round
    uint8_t nCand, candIdx;         // how many, and the next one to look at
    bool pattern;                   // repeating the last move, doubled
    int16_t dL, dC;                 // that move
} srch;

static inline uint8_t CoarseCode(int item){ return item ? (uint8_t)(1u << (item - 1)) : 0; }

static inline uint32_t Pack(uint8_t topo, uint8_t l, uint8_t c){
    return ATU_PACK(true, srch.bal, topo == TOPO_CL, l, c);
}

static inline bool InRange(int l, int c){ return (l >= 0) && (l <= 255) && (c >= 0) && (c <= 255); }

/// A topology's valley walk found a lowest point (readings are at most 0.999)
static inline bool HaveSeed(uint8_t topo){ return srch.seedG[topo] < 1.0f; }

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
    uint32_t best = srch.bypass;
    *g = UNREAD_GAMMA;
    for (int i = 0; i < rec->n; i++){
        if (rec->gamma[i] < *g){ *g = rec->gamma[i]; best = rec->setting[i]; }
    }
    return best;
}

///////////////////////////////////////////////////////////////////////////////
// Steps handed to the engine
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// Starting each part of the search
///////////////////////////////////////////////////////////////////////////////

/// PRIVATE: Start the inner search (the best C) at the current coarse L
static void StartInner(void){
    srch.innerBestG = UNREAD_GAMMA;
    if (!srch.haveFloor){
        // The first L of a walk: nothing to start from, so sweep C from 0
        srch.inner = IN_SWEEP;
        srch.j = 0;
        srch.innerLastG = UNREAD_GAMMA;
        srch.innerRises = 0;
    } else {
        // Later Ls: the valley is continuous, so start at the previous L's best C
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
    srch.floorBest = UNREAD_GAMMA;
    srch.floorLast = UNREAD_GAMMA;
    srch.floorRises = 0;
    srch.seedG[topo] = UNREAD_GAMMA;
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
    const int *dl = (srch.group == GROUP_DIAGONALS) ? dgL : axL;
    const int *dc = (srch.group == GROUP_DIAGONALS) ? dgC : axC;
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
    srch.group = GROUP_AXES;
    StartRound();
}

///////////////////////////////////////////////////////////////////////////////
// The inner search: the best coarse C at the current coarse L
//
// Both forms return true when they have finished (innerBest/innerBestG hold the
// result), or false with *s set to a setting that must be read first. They are
// called again after that reading and carry on from where they were.
///////////////////////////////////////////////////////////////////////////////

/// PRIVATE: Sweep C up from 0 until |Gamma| has risen SRCH_SWEEP_RISES times past its minimum
static bool SweepC(const ATUTuneRecord *rec, uint32_t *s){
    uint8_t L = CoarseCode(srch.outer);
    float g;
    while ((srch.j < SRCH_COARSE_ITEMS) && (srch.innerRises < SRCH_SWEEP_RISES)){
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
    }
    return true;
}

/// PRIVATE: Descend from c0: up while it gains; if up gained nothing, down while it gains
static bool DescendC(const ATUTuneRecord *rec, uint32_t *s){
    uint8_t L = CoarseCode(srch.outer);
    float g;
    for (;;){
        if ((srch.j >= 0) && (srch.j < SRCH_COARSE_ITEMS)){
            *s = Pack(srch.topo, L, CoarseCode(srch.j));
            if (!Lookup(rec, *s, &g)) return false;
            if (srch.dir == 0){
                // The start point is read: now try upwards
                srch.innerBestG = g;
                srch.innerBest = srch.j;
                srch.dir = +1;
                srch.j = (int8_t)(srch.c0 + 1);
                continue;
            }
            if (g < srch.innerBestG - SRCH_DESCENT_GAIN){
                // A gain: take it and keep going the same way
                srch.innerBestG = g;
                srch.innerBest = srch.j;
                srch.j = (int8_t)(srch.j + srch.dir);
                continue;
            }
        }
        // No gain at j, or j ran off the end of the codes
        if ((srch.dir == +1) && (srch.innerBest == srch.c0)){
            // Up gained nothing at all: try downwards
            srch.dir = -1;
            srch.j = (int8_t)(srch.c0 - 1);
            continue;
        }
        return true;
    }
}

static bool InnerStep(const ATUTuneRecord *rec, uint32_t *s){
    return (srch.inner == IN_SWEEP) ? SweepC(rec, s) : DescendC(rec, s);
}

///////////////////////////////////////////////////////////////////////////////
// The phases
//
// Each phase function makes one move. It returns true with *out set when the
// engine has something to do (take a reading, or finish), and false when the
// search has moved on using readings already taken, so that TuneNext() should
// call the (possibly new) phase again.
///////////////////////////////////////////////////////////////////////////////

/// PRIVATE: Read with the network out: stay bypassed, report a fault, or start the search
static bool BypassStep(ATUTuneRecord *rec, ATUStep *out){
    float g;
    if (!Lookup(rec, srch.bypass, &g)){
        *out = Measure(srch.bypass);
        return true;
    }
    if (g <= SwrToGamma(SRCH_BYPASS_SWR)){
        *out = Done(rec, srch.bypass, g);
        return true;
    }
    if (g > SRCH_FAULT_GAMMA){
        *out = {ATU_STEP_FAIL, 0, ATU_FAIL_ANTENNA};
        return true;
    }
    StartWalk(TOPO_LC);
    return false;
}

/// PRIVATE: A topology's valley walk has ended: decide what comes next
static bool AfterWalk(ATUTuneRecord *rec, ATUStep *out){
    srch.walked[srch.topo] = true;
    if (srch.topo == TOPO_LC){
        // A valley that already reaches the target: refine it before walking CL
        if (srch.seedG[TOPO_LC] <= SwrToGamma(SRCH_TARGET_SWR)) StartRefine(TOPO_LC);
        else StartWalk(TOPO_CL);
        return false;
    }
    // CL has been walked
    if (srch.refined[TOPO_LC]){
        // LC was refined and ended short of the target (or CL would not have been walked)
        if (HaveSeed(TOPO_CL)){
            StartRefine(TOPO_CL);
            return false;
        }
        *out = DoneBest(rec);
        return true;
    }
    // Neither is refined yet: start with the lower valley
    StartRefine((srch.seedG[TOPO_CL] < srch.seedG[TOPO_LC]) ? TOPO_CL : TOPO_LC);
    return false;
}

/// PRIVATE: One move of the valley walk
static bool WalkStep(ATUTuneRecord *rec, ATUStep *out){
    uint32_t s;
    if (!InnerStep(rec, &s)){
        *out = Measure(s);
        return true;
    }
    // The inner search has finished: innerBestG is the valley floor at this L
    float floor = srch.innerBestG;
    if (floor < srch.seedG[srch.topo]){
        srch.seedG[srch.topo] = floor;
        srch.seed[srch.topo] = Pack(srch.topo, CoarseCode(srch.outer), CoarseCode(srch.innerBest));
    }
    if (floor < srch.floorBest){
        srch.floorBest = floor;
        srch.floorRises = 0;
    } else if (floor > srch.floorLast + SRCH_FLOOR_RISE){
        srch.floorRises++;
    }
    srch.floorLast = floor;
    srch.haveFloor = true;
    srch.c0 = srch.innerBest;
    // On to the next L, unless the floor has risen enough or the Ls have run out
    if ((srch.floorRises < SRCH_FLOOR_RISES) && (++srch.outer < SRCH_COARSE_ITEMS)){
        StartInner();
        return false;
    }
    return AfterWalk(rec, out);
}

/// PRIVATE: A refine can go no further: finish, or go on to the other topology
static bool AfterRefine(ATUTuneRecord *rec, ATUStep *out){
    float best;
    BestRead(rec, &best);
    if (best > SwrToGamma(SRCH_TARGET_SWR)){
        // Short of the target: the other topology may do better
        if (!srch.walked[TOPO_CL]){
            StartWalk(TOPO_CL);
            return false;
        }
        uint8_t other = (srch.topo == TOPO_LC) ? TOPO_CL : TOPO_LC;
        if (!srch.refined[other] && HaveSeed(other)){
            StartRefine(other);
            return false;
        }
    }
    *out = DoneBest(rec);
    return true;
}

/// PRIVATE: One move of the refine (pattern search)
static bool RefineStep(ATUTuneRecord *rec, ATUStep *out){
    float g;
    if (srch.pattern){
        // Repeat the last move, doubled, while it gains
        int nl = srch.l + srch.dL, nc = srch.c + srch.dC;
        if (InRange(nl, nc)){
            uint32_t s = Pack(srch.topo, (uint8_t)nl, (uint8_t)nc);
            if (!Lookup(rec, s, &g)){
                *out = Measure(s);
                return true;
            }
            if (g < srch.baseG - SRCH_MIN_GAIN){
                srch.l = (uint8_t)nl;
                srch.c = (uint8_t)nc;
                srch.baseG = g;
                srch.dL *= 2;
                srch.dC *= 2;
                return false;
            }
        }
        // It stopped gaining (or left the codes): a new round about the base point
        srch.group = GROUP_AXES;
        StartRound();
        return false;
    }

    // Read this round's candidates, one at a time
    if (srch.candIdx < srch.nCand){
        uint32_t s = srch.cand[srch.candIdx];
        if (!Lookup(rec, s, &g)){
            *out = Measure(s);
            return true;
        }
        srch.candIdx++;
        return false;
    }

    // End of a round: move to the best candidate if it gains
    uint32_t bestS = 0;
    float bestG = UNREAD_GAMMA;
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
        return false;
    }

    // Nothing gained
    if (srch.group == GROUP_AXES){
        srch.group = GROUP_DIAGONALS;       // the axes gave nothing: try the diagonals
        StartRound();
        return false;
    }
    if ((srch.sL > 1) || (srch.sC > 1)){
        // Nor did the diagonals: halve the steps
        srch.sL = (srch.sL > 1) ? (uint8_t)(srch.sL >> 1) : 1;
        srch.sC = (srch.sC > 1) ? (uint8_t)(srch.sC >> 1) : 1;
        srch.group = GROUP_AXES;
        StartRound();
        return false;
    }
    return AfterRefine(rec, out);           // the steps are 1: refined as far as it goes
}

///////////////////////////////////////////////////////////////////////////////
// The planner
///////////////////////////////////////////////////////////////////////////////

static void TuneBegin(ATUTuneRecord *rec, uint32_t operatorSetting, double f_Hz){
    memset(&srch, 0, sizeof(srch));
    srch.phase = S_BYPASS;
    srch.band = ATUCurrentBand();
    srch.antenna = ATUCurrentAntenna();
    srch.bal = ATUAntennaBalun(srch.antenna);
    srch.bypass = ATUBypassFor(srch.antenna);
    srch.f_kHz = (int32_t)lround(f_Hz / 1000.0);
}

static ATUStep TuneNext(ATUTuneRecord *rec){
    if ((srch.phase == S_WALK) || (srch.phase == S_REFINE)){
        // Stop as soon as anything read is good enough, or the readings run out.
        // No reading arrives during a call, so once here is enough.
        float bestG;
        BestRead(rec, &bestG);
        if ((bestG <= SwrToGamma(SRCH_GOOD_SWR)) || (rec->n >= SRCH_MAX_READINGS)) return DoneBest(rec);
    }
    // Each pass either gives the engine a step or moves the search on using
    // readings already taken; the bound only guards against a logic error.
    for (int guard = 0; guard < 8 * ATU_MAX_READINGS; guard++){
        ATUStep step;
        bool haveStep;
        switch (srch.phase){
            case S_BYPASS: haveStep = BypassStep(rec, &step); break;
            case S_WALK:   haveStep = WalkStep(rec, &step);   break;
            case S_REFINE: haveStep = RefineStep(rec, &step); break;
            case S_DONE:
            default:       return DoneBest(rec);
        }
        if (haveStep) return step;
    }
    return DoneBest(rec);
}

static void TuneReport(ATUTuneRecord *rec, uint32_t setting, float gamma){}

const ATUPlanner ATUTunePlanner = {TuneBegin, TuneNext, TuneReport};

#endif // ATU_PRESENT
