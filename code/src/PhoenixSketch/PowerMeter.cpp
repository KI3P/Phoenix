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
 * Transmit power meter: PEP and average power on SSB voice. See PowerMeter.h.
 * No hardware and no radio state: the callers pass the time, band and samples.
 */

#include "SDT.h"

struct PowerMeterBin {
    float32_t digPeak_W;   // highest instantaneous digital power
    float32_t digSum_W;    // sum of the digital powers
    uint32_t digN;         // digital samples
    float32_t fwdSum_W;    // sum of the bridge forward readings
    float32_t refSum_W;    // sum of the bridge reflected readings
    uint32_t bridgeN;      // bridge readings
};

static struct PowerMeterBin bins[PM_NBINS];
static uint32_t curBin = 0;        // absolute bin index (time / PM_BIN_MS) of the newest bin
static bool started = false;
static float32_t k_dB[PM_CONTEXTS];
static int32_t context = 0;
static float32_t heldSWR = 1.0f;

static void ClearBins(void){
    memset(bins, 0, sizeof(bins));
}

void PowerMeterReset(void){
    ClearBins();
    started = false;
    curBin = 0;
    context = 0;
    heldSWR = 1.0f;
    for (int32_t i = 0; i < PM_CONTEXTS; i++) k_dB[i] = 0.0f;
}

void PowerMeterSetContext(int32_t band, bool pa100W){
    int32_t c = 2 * band + (pa100W ? 1 : 0);
    if (c < 0 || c >= PM_CONTEXTS) c = 0;
    if (c != context){
        context = c;
        ClearBins();
        started = false;
    }
}

/** Sums over the n bins ending at absolute bin index last */
static void WindowSums(uint32_t last, uint32_t n, struct PowerMeterBin *s){
    memset(s, 0, sizeof(*s));
    for (uint32_t i = 0; i < n; i++){
        const struct PowerMeterBin *b = &bins[(last - i) % PM_NBINS];
        if (b->digPeak_W > s->digPeak_W) s->digPeak_W = b->digPeak_W;
        s->digSum_W += b->digSum_W;
        s->digN += b->digN;
        s->fwdSum_W += b->fwdSum_W;
        s->refSum_W += b->refSum_W;
        s->bridgeN += b->bridgeN;
    }
}

/** True if every one of the n bins ending at last holds digital and bridge samples */
static bool WindowFull(uint32_t last, uint32_t n){
    for (uint32_t i = 0; i < n; i++){
        const struct PowerMeterBin *b = &bins[(last - i) % PM_NBINS];
        if (b->digN == 0 || b->bridgeN == 0) return false;
    }
    return true;
}

/**
 * A bin has just been completed: move k toward the bridge/digital ratio of the
 * window ending there, if that window is a steady carrier.
 *
 * Only a steady envelope: each AD7991 reading carries 15-19 % of the conversion
 * before it, about 0.5 ms earlier, through the AD8307's 12.5 kOhm output. That
 * averages the log of a fast-moving envelope over a fraction of a millisecond, and
 * on the bench (radio 2, 2026-11-22) left the bridge's mean in watts 0.8-1.1 dB low
 * on two tones against 0.45 dB on a single tone. Speech was in between. A steady
 * carrier (tune, single tone, FT8) has no such error, so k learns only from those,
 * and can follow them quickly.
 */
static void FinishBin(uint32_t done){
    if (!WindowFull(done, PM_AVG_BINS)) return;     // key-up or key-down in the window
    struct PowerMeterBin s;
    WindowSums(done, PM_AVG_BINS, &s);
    if (s.bridgeN < PM_K_MIN_BRIDGE_N) return;
    float32_t dig = s.digSum_W / (float32_t)s.digN;
    float32_t bridge = s.fwdSum_W / (float32_t)s.bridgeN;
    if (dig < PM_K_MIN_AVG_W || bridge <= 0.0f) return;
    if (s.digPeak_W > dig * powf(10.0f, PM_K_MAX_PAPR_DB / 10.0f)) return;   // not steady
    float32_t r_dB = 10.0f * log10f(bridge / dig);
    float32_t k = k_dB[context] + (r_dB - k_dB[context]) * ((float32_t)PM_BIN_MS / (float32_t)PM_K_TAU_MS);
    if (k > PM_K_LIMIT_DB) k = PM_K_LIMIT_DB;
    if (k < -PM_K_LIMIT_DB) k = -PM_K_LIMIT_DB;
    k_dB[context] = k;
}

/** Bring the newest bin up to now, completing and clearing bins on the way */
static void Advance(uint32_t now_ms){
    uint32_t idx = now_ms / PM_BIN_MS;
    if (!started){
        ClearBins();
        curBin = idx;
        started = true;
        return;
    }
    // Signed, so the millis() wrap is just another step forward. A sample stamped a
    // little before the current bin (another caller's millis() read a moment
    // earlier) goes into the current bin rather than clearing the ring.
    int32_t ahead = (int32_t)(idx - curBin);
    if (ahead <= 0) return;
    if (ahead > PM_NBINS){
        FinishBin(curBin);
        ClearBins();
        curBin = idx;
        return;
    }
    while (curBin != idx){
        FinishBin(curBin);
        curBin++;
        memset(&bins[curBin % PM_NBINS], 0, sizeof(struct PowerMeterBin));
    }
}

float32_t PowerMeterDigitalScale(float32_t calPoint_W, float32_t correction_dB, float32_t ceiling){
    return calPoint_W / (ceiling * ceiling * powf(10.0f, correction_dB / 10.0f));
}

void PowerMeterBlockPower(const float32_t *I, const float32_t *Q, uint32_t n, float32_t scale,
                          float32_t *peak_W, float32_t *sum_W){
    float32_t peak = 0.0f;
    float32_t sum = 0.0f;
    for (uint32_t i = 0; i < n; i++){
        float32_t p = I[i] * I[i] + Q[i] * Q[i];
        sum += p;
        if (p > peak) peak = p;
    }
    *peak_W = peak * scale;
    *sum_W = sum * scale;
}

void PowerMeterAddDigitalBlock(float32_t peak_W, float32_t sum_W, uint32_t n, uint32_t now_ms){
    Advance(now_ms);
    struct PowerMeterBin *b = &bins[curBin % PM_NBINS];
    if (peak_W > b->digPeak_W) b->digPeak_W = peak_W;
    b->digSum_W += sum_W;
    b->digN += n;
}

void PowerMeterAddBridgeSample(float32_t fwd_W, float32_t ref_W, uint32_t now_ms){
    Advance(now_ms);
    struct PowerMeterBin *b = &bins[curBin % PM_NBINS];
    b->fwdSum_W += fwd_W;
    b->refSum_W += ref_W;
    b->bridgeN++;
}

static float32_t Correction(void){
    return powf(10.0f, k_dB[context] / 10.0f);
}

bool PowerMeterHasDigital(uint32_t now_ms){
    if (!started) return false;
    Advance(now_ms);
    struct PowerMeterBin s;
    WindowSums(curBin, PM_PEP_BINS, &s);
    return s.digN > 0;
}

float32_t PowerMeterPEP_W(uint32_t now_ms){
    if (!started) return 0.0f;
    Advance(now_ms);
    struct PowerMeterBin s;
    WindowSums(curBin, PM_PEP_BINS, &s);
    return s.digPeak_W * Correction();
}

float32_t PowerMeterAvg_W(uint32_t now_ms){
    if (!started) return 0.0f;
    Advance(now_ms);
    struct PowerMeterBin s;
    WindowSums(curBin, PM_AVG_BINS, &s);
    if (s.digN == 0) return 0.0f;
    return s.digSum_W / (float32_t)s.digN * Correction();
}

float32_t PowerMeterBridgeAvg_W(uint32_t now_ms){
    if (!started) return 0.0f;
    Advance(now_ms);
    struct PowerMeterBin s;
    WindowSums(curBin, PM_AVG_BINS, &s);
    if (s.bridgeN == 0) return 0.0f;
    return s.fwdSum_W / (float32_t)s.bridgeN;
}

float32_t PowerMeterSWR(uint32_t now_ms){
    if (!started) return heldSWR;
    Advance(now_ms);
    struct PowerMeterBin s;
    WindowSums(curBin, PM_AVG_BINS, &s);
    if (s.bridgeN == 0) return heldSWR;
    float32_t fwd = s.fwdSum_W / (float32_t)s.bridgeN;
    float32_t ref = s.refSum_W / (float32_t)s.bridgeN;
    if (fwd < PM_SWR_MIN_W) return heldSWR;
    float32_t a = sqrtf((ref > 0.0f ? ref : 0.0f) / fwd);
    if (a > 0.999f) a = 0.999f;
    heldSWR = (1.0f + a) / (1.0f - a);
    return heldSWR;
}

float32_t PowerMeterCorrection_dB(void){
    return k_dB[context];
}
