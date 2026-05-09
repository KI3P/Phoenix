/**
 * @file DmrModem.cpp
 * @brief DMR 4FSK modem -- TX + RX implemented; FEC/superframe TBD.
 *
 * TX side (unchanged from previous revision):
 *   - Build RRC FIR at init (α = 0.2, span = 8, normalized so the
 *     upsample+filter cascade has unity DC gain).
 *   - Map MSB-first dibits to {+1, +3, −1, −3} per ETSI §6.1.1.
 *   - Polyphase render the burst, gate to the configured time slot via
 *     DmrSlotIsOurs(millis()).
 *
 * RX side (new):
 *   1. Each incoming audio sample is run through the matched RRC FIR --
 *      same coefficient set as TX, since RRC * RRC = full RC = the
 *      Nyquist pulse.
 *   2. The MF output sample is pushed into a per-burst circular buffer.
 *   3. While unlocked, every sample tries each of the sps possible symbol
 *      phases (φ ∈ 0..sps-1). For each φ, the most recent 24 symbols at
 *      that phase are sliced (against thresholds {−2, 0, +2}) and
 *      compared to the BS-sourced voice SYNC pattern 0x755FD7DF75F7
 *      (ETSI TS 102 361-2 §6.2). When ≥ 20 of 24 symbols match, the
 *      phase locks and the burst-start sample is computed (SYNC starts
 *      at symbol index 54 inside the 132-symbol burst).
 *   4. Once locked, the modem waits for the rest of the burst to flow
 *      into the ring (54 more symbols past the SYNC end), then extracts
 *      all 132 symbols at the locked phase, packs them into 33 bytes
 *      MSB-first, and pushes the burst onto the output queue served by
 *      DmrModemRxPopBurst().
 *   5. After each successful burst the modem re-enters SEARCHING and
 *      reacquires on the next SYNC. A future revision will track the
 *      superframe and accept EMB-only frames between SYNC-bearing ones.
 *
 * Memory: ~28 KB TX (renderBuf + FIR taps) + ~6 KB RX (matched-filter
 * delay line + ring) + a small output queue. Comfortable on Teensy 4.1.
 *
 * Closed-loop sanity test (verified off-Teensy in a host harness): a
 * burst whose middle 6 bytes are the SYNC pattern is rendered via the TX
 * pipeline and fed straight into RxFeed; PopBurst returns the original
 * 33 bytes with all 132 symbols recovered correctly.
 */

#include "SDT.h"
#include "DmrModem.h"
#include "DmrFraming.h"
#include <math.h>
#include <string.h>

namespace {

/* ----- Compile-time bounds ------------------------------------------------ */

/* Compile-time cap on samples-per-symbol. Phoenix's DMR audio path runs
 * at the post-DecimateBy8 rate (192 kHz I/Q → 24 kHz audio → sps = 5),
 * so 12 covers the actual range with margin (up to 57.6 kHz audio).
 * Raising this to 48 would re-enable a 192 kHz audio path but costs
 * ~85 KB across renderBuf, rxRing, mfDelay and firTaps. */
constexpr int   MAX_SPS         = 12;
constexpr int   MAX_FIR_TAPS    = MAX_SPS * DMR_MODEM_RRC_SPAN_SYMBOLS + 1;
constexpr int   MAX_BURST_SYMS  = DMR_BURST_BYTES_MAX * 4;            // 132
constexpr int   MAX_RENDER_LEN  =
    (MAX_BURST_SYMS + DMR_MODEM_RRC_SPAN_SYMBOLS) * MAX_SPS;
constexpr int   SYNC_SYMS       = 24;                                  // 48 bits
constexpr int   SYNC_OFFSET_SYM = 54;                                  // SYNC starts at symbol 54 of 132
/* Cross-correlation acquisition threshold, expressed as a fraction of the
 * ideal correlation (all 24 SYNC symbols at full ±3 amplitude through the
 * matched filter). 0.5 ≈ 6 dB above the noise floor of random data. */
constexpr float SYNC_CORR_FRAC  = 0.5f;
/* SYNC has 24 outer-symbol amplitudes ⇒ Σ symAmp² = 24 * 9 = 216 over the
 * pattern. Multiplied by the cascade gain Σh², that's the ideal corr. */
constexpr float SYNC_IDEAL_NORM = (float)(SYNC_SYMS * 9);

/* ----- Superframe tracking (post-lock continuous extraction) ----- */
/* DMR groups 6 voice frames (A..F) into a 360 ms superframe. Voice frame
 * F carries the full 48-bit SYNC pattern in its middle; frames A..E carry
 * an EMB instead. After we acquire SYNC on a frame F, we know subsequent
 * same-slot bursts arrive every 60 ms (one TDMA frame = 2 × 30 ms slots,
 * we listen to every other slot). 60 ms × 4800 sym/s = 288 symbols. */
constexpr int   INTER_BURST_PERIOD_SYMS  = 288;
constexpr int   SUPERFRAME_BURSTS        = 6;
/* Drop lock if no SYNC has been seen for this many bursts. 12 = 2
 * superframes of silence -- enough to ride through occasional missed
 * frame-F SYNCs without dumping a long call. */
constexpr int   LOCK_TIMEOUT_BURSTS      = 12;
/* Mid-burst SYNC re-validation: how many of the 24 middle symbols must
 * match. 18 / 24 = 75%, low enough to ride a noisy channel without
 * shedding lock prematurely. */
constexpr int   SYNC_REVALIDATE_MIN_HIT  = 18;
constexpr int   RX_RING_LEN     = (MAX_BURST_SYMS + DMR_MODEM_RRC_SPAN_SYMBOLS + 4) * MAX_SPS;
constexpr int   OUT_Q_SIZE      = 4;

/* dibit (b1,b0) → {00:+1, 01:+3, 10:−1, 11:−3} per ETSI §6.1.1. */
constexpr int8_t SYMBOL_MAP[4] = { +1, +3, -1, -3 };

/* BS-sourced voice SYNC: 0x755FD7DF75F7 (ETSI TS 102 361-2 Table 9.2-1). */
constexpr uint8_t BS_VOICE_SYNC[6] = { 0x75, 0x5F, 0xD7, 0xDF, 0x75, 0xF7 };

/* ----- Module state ------------------------------------------------------- */

bool          inited        = false;
float         sampleRate    = 0.0f;
int           sps           = 0;
int           firTapCount   = 0;
float         firTaps[MAX_FIR_TAPS];

/* TX renderer */
/* Pre-rendered TX burst — sequential write at queue time, sequential
 * read out of DmrModemRender. DMAMEM = OCRAM, off the precious DTCM. */
DMAMEM float  renderBuf[MAX_RENDER_LEN];
size_t        renderLen      = 0;
size_t        renderIdx      = 0;
bool          burstPending   = false;
bool          burstRendering = false;

DmrModemState state          = DMR_MODEM_STATE_IDLE;

/* RX matched filter (separate delay line from TX). DMAMEM = OCRAM,
 * frees DTCM for the stack/heap. The FIR walk is sequential, so
 * OCRAM's slightly slower access is hidden by the cache. */
DMAMEM float  mfDelay[MAX_FIR_TAPS] = {0};
int           mfDelayWrPos = 0;

/* RX ring of post-MF samples. DMAMEM — sequential ring access. */
DMAMEM float  rxRing[RX_RING_LEN] = {0};
int           rxRingWr = 0;
uint64_t      rxSampleCount = 0;

/* Decoded SYNC symbol pattern (built at init). */
int8_t        bsVoiceSyncSyms[SYNC_SYMS];

/* Cascade peak-gain Σh² (computed at init). Used to normalize the SYNC
 * cross-correlation acquisition threshold and to AGC the slicer. */
float         cascadeGain  = 1.0f;
float         syncCorrThr  = 0.0f;

/* Running EMA of |matched-filter output|, used by DmrModemRxLevel() to
 * give the home screen a normalized 0..1+ signal-strength reading.
 * α = 0.001 ⇒ effective averaging window ~1000 samples ≈ 41 ms at
 * 24 kHz, smooth enough to ride through symbol-rate variation. */
float         rxLevelEma   = 0.0f;
constexpr float RX_LEVEL_ALPHA = 0.001f;

/* RX lock state. */
bool          rxLocked          = false;
int           rxPhase           = 0;
uint64_t      rxBurstStart      = 0;   // sample index of symbol-0 of in-progress burst
int           rxBurstsSinceSync = 0;   // increments per extracted non-F burst

/* SYNC-time anchor for Tier-II frame sync. Captured at acquisition and
 * refreshed on every SYNC-bearing burst (voice frame F). Cleared when
 * the modem drops back to SEARCHING. */
uint32_t      rxSyncRefUs       = 0;
bool          rxSyncRefValid    = false;

/* SYNC acquisition "settling": after the first threshold cross, keep
 * scanning for one symbol period and commit to the peak. This avoids
 * locking on the leading edge of the cross-correlation plateau. */
bool          syPending       = false;
int           syHoldRemaining = 0;
float         syBestCorr      = 0.0f;
int           syBestPhase     = 0;
uint64_t      syBestEnd       = 0;

/* Output queue. */
struct OutBurst { uint8_t bytes[DMR_BURST_BYTES_MAX]; size_t len; };
OutBurst      outQ[OUT_Q_SIZE];
int           outQHead = 0;
int           outQTail = 0;

/* ----- Helpers ------------------------------------------------------------ */

int buildRrcFir(float *taps, int sps_, float alpha, int span) {
    const int   n      = span * sps_ + 1;
    const int   center = n / 2;
    const float T      = (float)sps_;
    const float pi     = 3.14159265358979f;

    for (int i = 0; i < n; ++i) {
        const float t     = (float)(i - center);
        const float ratio = t / T;
        float       val;
        if (fabsf(t) < 1e-6f) {
            val = 1.0f + alpha * (4.0f / pi - 1.0f);
        } else {
            const float singular_t = T / (4.0f * alpha);
            if (fabsf(fabsf(t) - singular_t) < 1e-3f) {
                const float pi4a = pi / (4.0f * alpha);
                val = (alpha / sqrtf(2.0f)) *
                      ((1.0f + 2.0f / pi) * sinf(pi4a) +
                       (1.0f - 2.0f / pi) * cosf(pi4a));
            } else {
                const float fa = 4.0f * alpha * ratio;
                const float num = sinf(pi * ratio * (1.0f - alpha)) +
                                  fa * cosf(pi * ratio * (1.0f + alpha));
                const float den = pi * ratio * (1.0f - fa * fa);
                val = num / den;
            }
        }
        taps[i] = val;
    }

    float sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += taps[i];
    if (sum > 1e-6f || sum < -1e-6f) {
        const float scale = (float)sps_ / sum;
        for (int i = 0; i < n; ++i) taps[i] *= scale;
    }
    return n;
}

static inline int8_t dibitToSymbol(uint8_t byte, int dibitInByte) {
    const int     shift = (3 - dibitInByte) * 2;
    const uint8_t dibit = (uint8_t)((byte >> shift) & 0x3);
    return SYMBOL_MAP[dibit];
}

void renderBurstToBuffer(const uint8_t *bytes, size_t nBytes) {
    int symCount = (int)nBytes * 4;
    if (symCount > MAX_BURST_SYMS) symCount = MAX_BURST_SYMS;

    int8_t syms[MAX_BURST_SYMS];
    for (int i = 0; i < symCount; ++i) {
        syms[i] = dibitToSymbol(bytes[i / 4], i % 4);
    }

    int outLen = symCount * sps + (firTapCount - 1);
    if (outLen > MAX_RENDER_LEN) outLen = MAX_RENDER_LEN;
    memset(renderBuf, 0, sizeof(float) * (size_t)outLen);

    for (int s = 0; s < symCount; ++s) {
        const float amp = (float)syms[s];
        const int   off = s * sps;
        const int   limit = (firTapCount < (outLen - off))
                                ? firTapCount : (outLen - off);
        for (int k = 0; k < limit; ++k) {
            renderBuf[off + k] += amp * firTaps[k];
        }
    }

    renderLen = (size_t)outLen;
    renderIdx = 0;
}

/* Build the symbol-amplitude version of the SYNC pattern at module init. */
void buildSyncSymbols(void) {
    for (int i = 0; i < SYNC_SYMS; ++i) {
        bsVoiceSyncSyms[i] = dibitToSymbol(BS_VOICE_SYNC[i / 4], i % 4);
    }
}

/* Slice MF output (which sits at amplitude ~symbol*cascadeGain) to the
 * nearest of {-3, -1, +1, +3}. Thresholds are scaled by cascadeGain to
 * match the actual MF output range. */
static inline int8_t sliceSymbol(float v) {
    const float t1 = 2.0f * cascadeGain;
    const float t0 = 0.0f;
    if (v < -t1) return -3;
    if (v <  t0) return -1;
    if (v <  t1) return +1;
    return +3;
}

static inline float ringRead(uint64_t sampleIdx) {
    /* sampleIdx must be within (rxSampleCount - RX_RING_LEN, rxSampleCount]. */
    int idx = (int)(sampleIdx % (uint64_t)RX_RING_LEN);
    return rxRing[idx];
}

/* MF: convolve incoming sample through symmetric RRC FIR. */
static inline float matchedFilterStep(float in) {
    mfDelay[mfDelayWrPos] = in;
    /* Walk the delay line newest-first, multiplying by taps[0..n-1]. */
    float acc = 0.0f;
    int   p   = mfDelayWrPos;
    for (int k = 0; k < firTapCount; ++k) {
        acc += firTaps[k] * mfDelay[p];
        if (--p < 0) p += MAX_FIR_TAPS;
    }
    if (++mfDelayWrPos >= MAX_FIR_TAPS) mfDelayWrPos = 0;
    return acc;
}

/* SYNC correlator. Cross-correlate the matched-filter output against the
 * expected ±3 symbol amplitudes at every candidate phase. Cross-correlation
 * (vs binary slice-and-count) gives a sharp peak at the true symbol
 * decision instant: off-aligned samples are smaller in magnitude and
 * contribute less to the sum.
 *
 * Returns the phase with the largest correlation; outputs that
 * correlation in *bestCorr. The caller decides whether to settle / lock
 * based on syncCorrThr and the cross-sample peak-search policy. */
int searchSyncCorrEndingAt(uint64_t endSample, float *bestCorr) {
    int   bestPhase = -1;
    float bestC     = -1.0f;
    for (int phase = 0; phase < sps; ++phase) {
        float c = 0.0f;
        for (int i = 0; i < SYNC_SYMS; ++i) {
            const uint64_t si = endSample - (uint64_t)phase
                                 - (uint64_t)((SYNC_SYMS - 1 - i) * sps);
            c += ringRead(si) * (float)bsVoiceSyncSyms[i];
        }
        if (c > bestC) { bestC = c; bestPhase = phase; }
    }
    if (bestCorr) *bestCorr = bestC;
    return bestPhase;
}

uint8_t symbolToDibit(int8_t s) {
    /* Inverse of SYMBOL_MAP. */
    if (s == +1) return 0;
    if (s == +3) return 1;
    if (s == -1) return 2;
    return 3;
}

void packBurstFromSymbols(const int8_t syms[MAX_BURST_SYMS],
                          uint8_t bytes[DMR_BURST_BYTES_MAX]) {
    memset(bytes, 0, DMR_BURST_BYTES_MAX);
    for (int s = 0; s < MAX_BURST_SYMS; ++s) {
        const uint8_t dibit = symbolToDibit(syms[s]);
        const int     byteIdx = s / 4;
        const int     shift   = (3 - (s % 4)) * 2;
        bytes[byteIdx] |= (uint8_t)(dibit << shift);
    }
}

void enqueueOutBurst(const uint8_t *bytes) {
    const int next = (outQHead + 1) % OUT_Q_SIZE;
    if (next == outQTail) return;          /* drop on overflow */
    memcpy(outQ[outQHead].bytes, bytes, DMR_BURST_BYTES_MAX);
    outQ[outQHead].len = DMR_BURST_BYTES_MAX;
    outQHead = next;
}

}  // namespace

/* ====================================================================== */
/*                             Public API                                 */
/* ====================================================================== */

DmrModemResult DmrModemInit(float sampleRateHz) {
    if (!(sampleRateHz > 2.0f * (float)DMR_MODEM_SYMBOL_RATE_HZ)) {
        return DMR_MODEM_ERR_RATE;
    }
    const int sps_ = (int)floorf(
        sampleRateHz / (float)DMR_MODEM_SYMBOL_RATE_HZ + 0.5f);
    if (sps_ < 3 || sps_ > MAX_SPS) {
        return DMR_MODEM_ERR_RATE;
    }

    sampleRate     = sampleRateHz;
    sps            = sps_;
    firTapCount    = buildRrcFir(firTaps, sps,
                                 DMR_MODEM_RRC_ALPHA,
                                 DMR_MODEM_RRC_SPAN_SYMBOLS);
    /* Cascade peak gain at the symbol decision instant after TX-RRC * RX-RRC.
     * For two identical symmetric FIRs, this is Σh². We use it both to
     * scale the slicer thresholds and to set the SYNC acquisition
     * threshold. */
    {
        float s = 0.0f;
        for (int i = 0; i < firTapCount; ++i) s += firTaps[i] * firTaps[i];
        cascadeGain = (s > 1e-6f) ? s : 1.0f;
        syncCorrThr = SYNC_CORR_FRAC * SYNC_IDEAL_NORM * cascadeGain;
    }
    buildSyncSymbols();

    /* TX state */
    renderLen = renderIdx = 0;
    burstPending = burstRendering = false;

    /* RX state */
    memset(mfDelay, 0, sizeof(mfDelay));
    mfDelayWrPos = 0;
    memset(rxRing,  0, sizeof(rxRing));
    rxRingWr      = 0;
    rxSampleCount = 0;
    rxLocked      = false;
    rxPhase       = 0;
    rxBurstStart  = 0;
    rxBurstsSinceSync = 0;
    rxSyncRefUs       = 0;
    rxSyncRefValid    = false;
    rxLevelEma    = 0.0f;
    syPending = false;
    syHoldRemaining = 0;
    syBestCorr = 0.0f;
    syBestPhase = 0;
    syBestEnd = 0;
    outQHead = outQTail = 0;

    inited = true;
    state  = DMR_MODEM_STATE_IDLE;
    return DMR_MODEM_OK;
}

void DmrModemShutdown(void) {
    inited         = false;
    sampleRate     = 0.0f;
    sps            = 0;
    firTapCount    = 0;
    renderLen      = 0;
    renderIdx      = 0;
    burstPending   = false;
    burstRendering = false;
    rxLocked       = false;
    state          = DMR_MODEM_STATE_IDLE;
}

void DmrModemApplyConfig(void) {
    /* Slot/addressing changes are picked up live by DmrSlotIsOurs(). */
}

DmrModemState DmrModemGetState(void) {
    return state;
}

/* ----- TX side --------------------------------------------------------- */

DmrModemResult DmrModemTxQueueBurst(const uint8_t *burst, size_t len) {
    if (!inited)                              return DMR_MODEM_ERR_NOT_INIT;
    if (burstPending || burstRendering)       return DMR_MODEM_ERR_BUSY;
    if (burst == nullptr || len == 0
        || len > (size_t)DMR_BURST_BYTES_MAX) return DMR_MODEM_ERR_INTERNAL;

    renderBurstToBuffer(burst, len);
    burstPending = true;
    state        = DMR_MODEM_STATE_TX_PENDING;
    return DMR_MODEM_OK;
}

size_t DmrModemRender(float *audioOut, size_t numSamples) {
    if (audioOut == nullptr) return 0;
    for (size_t i = 0; i < numSamples; ++i) audioOut[i] = 0.0f;

    if (!inited)                          return 0;
    if (!burstPending && !burstRendering) return 0;
    if (!DmrSlotIsOurs(millis()))         return 0;

    if (burstPending) {
        burstPending   = false;
        burstRendering = true;
        state          = DMR_MODEM_STATE_TX_RENDERING;
    }

    const size_t remaining = renderLen - renderIdx;
    const size_t n         = (numSamples < remaining) ? numSamples : remaining;
    for (size_t i = 0; i < n; ++i) {
        audioOut[i] = renderBuf[renderIdx + i];
    }
    renderIdx += n;
    if (renderIdx >= renderLen) {
        burstRendering = false;
        renderIdx = renderLen = 0;
        state = DMR_MODEM_STATE_IDLE;
    }
    return n;
}

bool DmrModemTxBufferAvailable(void) {
    return inited && !burstPending && !burstRendering;
}

/* ----- RX side --------------------------------------------------------- */

void DmrModemRxFeed(const float *audioIn, size_t numSamples) {
    if (!inited || audioIn == nullptr) return;

    for (size_t i = 0; i < numSamples; ++i) {
        /* 1) Matched filter and ring-buffer the result. */
        const float mf = matchedFilterStep(audioIn[i]);
        rxRing[rxRingWr] = mf;
        rxRingWr = (rxRingWr + 1) % RX_RING_LEN;
        const uint64_t curIdx = rxSampleCount;
        ++rxSampleCount;

        /* Track EMA of |MF| for signal-strength reporting. Computed in
         * the same pass to amortize the cost. */
        const float absMf = (mf >= 0.0f) ? mf : -mf;
        rxLevelEma = (1.0f - RX_LEVEL_ALPHA) * rxLevelEma + RX_LEVEL_ALPHA * absMf;

        /* 2) Acquire SYNC. Cross-correlate; settle for one symbol period
         *    after first threshold crossing and lock at the peak. */
        if (!rxLocked) {
            if (rxSampleCount < (uint64_t)(SYNC_SYMS * sps)) continue;

            float corrAtThis = -1.0f;
            const int phaseAtThis = searchSyncCorrEndingAt(curIdx, &corrAtThis);

            if (syPending) {
                if (corrAtThis > syBestCorr) {
                    syBestCorr  = corrAtThis;
                    syBestPhase = phaseAtThis;
                    syBestEnd   = curIdx;
                }
                if (--syHoldRemaining <= 0) {
                    /* Commit. SYNC's last symbol decision is at
                     *   syBestEnd - syBestPhase
                     * and the burst started 54 symbols before SYNC's first
                     * symbol, i.e. at:
                     *   syBestEnd - syBestPhase - (SYNC_SYMS-1 + 54)*sps */
                    rxLocked     = true;
                    rxPhase      = syBestPhase;
                    rxBurstStart = syBestEnd - (uint64_t)syBestPhase
                                  - (uint64_t)((SYNC_SYMS - 1 + SYNC_OFFSET_SYM) * sps);
                    state        = DMR_MODEM_STATE_RX_LOCKED;
                    syPending    = false;
                    /* Capture the network's frame timing so the framing
                     * layer can anchor TX slot phase to it. */
                    rxSyncRefUs    = micros();
                    rxSyncRefValid = true;
                    /* Fall through; we may already have the rest of the burst. */
                }
            } else if (corrAtThis >= syncCorrThr && phaseAtThis >= 0) {
                /* First threshold crossing -- start the settling window. */
                syPending       = true;
                syBestCorr      = corrAtThis;
                syBestPhase     = phaseAtThis;
                syBestEnd       = curIdx;
                syHoldRemaining = sps;        /* one symbol period */
                state           = DMR_MODEM_STATE_RX_SEARCHING;
                continue;                     /* defer burst extraction until lock commits */
            }
        }

        /* 3) Once locked, extract bursts at the predicted superframe
         *    cadence. Voice frame F carries SYNC; A..E carry EMB. We
         *    keep extracting all of them and rely on each burst's own
         *    middle 24 symbols (cross-checked against BS_VOICE_SYNC) to
         *    confirm we still have lock. After LOCK_TIMEOUT_BURSTS
         *    bursts with no SYNC observed we declare the call ended
         *    and re-enter SEARCHING. */
        if (rxLocked) {
            const uint64_t lastDecision =
                rxBurstStart + (uint64_t)((MAX_BURST_SYMS - 1) * sps);
            if (curIdx >= lastDecision) {
                int8_t syms[MAX_BURST_SYMS];
                for (int s = 0; s < MAX_BURST_SYMS; ++s) {
                    const uint64_t si = rxBurstStart + (uint64_t)(s * sps);
                    syms[s] = sliceSymbol(ringRead(si));
                }
                uint8_t bytes[DMR_BURST_BYTES_MAX];
                packBurstFromSymbols(syms, bytes);
                enqueueOutBurst(bytes);

                /* Mid-burst SYNC re-validation: count how many of the 24
                 * middle symbols match BS_VOICE_SYNC. A frame F (SYNC-
                 * bearing) hits ≥ SYNC_REVALIDATE_MIN_HIT; frames A..E
                 * (EMB) and noise miss. */
                int hits = 0;
                for (int i = 0; i < SYNC_SYMS; ++i) {
                    if (syms[SYNC_OFFSET_SYM + i] == bsVoiceSyncSyms[i]) ++hits;
                }
                if (hits >= SYNC_REVALIDATE_MIN_HIT) {
                    rxBurstsSinceSync = 0;
                    /* Re-anchor frame timing to keep slot alignment
                     * tight against drift. */
                    rxSyncRefUs    = micros();
                    rxSyncRefValid = true;
                } else {
                    ++rxBurstsSinceSync;
                }

                /* Schedule the next burst at +60 ms (one TDMA-frame
                 * period). 288 symbols × sps samples per symbol. */
                rxBurstStart += (uint64_t)(INTER_BURST_PERIOD_SYMS * sps);

                /* Drop lock if SYNC has been missing for too long. */
                if (rxBurstsSinceSync >= LOCK_TIMEOUT_BURSTS) {
                    rxLocked          = false;
                    rxBurstsSinceSync = 0;
                    rxSyncRefValid    = false;
                    state             = DMR_MODEM_STATE_RX_SEARCHING;
                }
            }
        }
    }
}

bool DmrModemRxPopBurst(uint8_t *burstOut, size_t *lenOut) {
    if (burstOut == nullptr || lenOut == nullptr) return false;
    if (outQTail == outQHead) { *lenOut = 0; return false; }
    memcpy(burstOut, outQ[outQTail].bytes, DMR_BURST_BYTES_MAX);
    *lenOut  = outQ[outQTail].len;
    outQTail = (outQTail + 1) % OUT_Q_SIZE;
    return true;
}

bool DmrModemRxIsLocked(void) {
    return rxLocked;
}

float DmrModemRxClockPpm(void) {
    return 0.0f;        /* full timing-recovery loop lands later */
}

bool DmrModemGetSyncRef(uint32_t *outRefUs) {
    if (!rxSyncRefValid) return false;
    if (outRefUs != nullptr) *outRefUs = rxSyncRefUs;
    return true;
}

float DmrModemRxLevel(void) {
    /* Mean |MF output| / (2 * cascadeGain). For uniformly random
     * 4FSK symbols ±1, ±3 the expected mean |sym| is (1 + 3) / 2 = 2,
     * so a clean signal at the modem's nominal amplitude reads ~1.0.
     * Below ~0.3 indicates near-noise; over ~1.2 indicates saturation
     * or stronger than nominal. */
    if (cascadeGain < 0.01f) return 0.0f;
    return rxLevelEma / (2.0f * cascadeGain);
}
