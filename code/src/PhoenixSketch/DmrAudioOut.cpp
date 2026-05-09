/**
 * @file DmrAudioOut.cpp
 * @brief 8 kHz AMBE PCM → DSP-rate float speaker audio.
 *
 * Implementation: integer-factor polyphase interpolator. AMBE+2 voice is
 * already bandlimited to ~3.5 kHz at 8 kHz (the codec's design passband),
 * so a modest 16-tap-per-phase windowed-sinc lowpass with ~3.6 kHz cutoff
 * gives transparent upsampling. The output ring is sized at 4 800 samples
 * (200 ms at 24 kHz) so the burst-paced producer (one PCM frame every
 * 20 ms, three per voice burst, roughly every 60 ms) and the steady DSP
 * consumer (256 samples every ~10.7 ms at 24 kHz) stay decoupled.
 */

#include "SDT.h"
#include "DmrAudioOut.h"
#include "AmbeDvsi.h"
#include <math.h>
#include <string.h>

namespace {

constexpr int   AMBE_RATE_HZ      = AMBE_PCM_SAMPLE_RATE_HZ;     /* 8 kHz */
constexpr int   AMBE_FRAME_SAMPS  = AMBE_PCM_SAMPLES_PER_FRAME;  /* 160   */
/* Compile-time cap on the integer upsample factor (DSP rate / 8 kHz).
 * Phoenix's default 192 kHz I/Q → 24 kHz audio gives factor 3; even an
 * exotic 64 kHz audio path is factor 8, so 8 is a safe ceiling and
 * saves ~5 KB on FIR-tap storage vs the original UPSAMPLE_MAX=24. */
constexpr int   UPSAMPLE_MAX      = 8;
constexpr int   TAPS_PER_PHASE    = 16;
constexpr int   FIR_LEN_MAX       = UPSAMPLE_MAX * TAPS_PER_PHASE; /* 128 */
/* Output ring sized for ~80 ms of 24 kHz audio. Comfortably absorbs the
 * burst-paced producer (one PCM frame every 20 ms, three per voice
 * burst) versus the steady DSP consumer (256 samples every ~10.7 ms).
 * Original 4 800 = 200 ms was over-budget for ~14 KB savings. */
constexpr int   RING_SIZE         = 1920;
constexpr int   UP_SCRATCH_MAX    = AMBE_FRAME_SAMPS * UPSAMPLE_MAX;

bool   ready          = false;
int    upFactor       = 1;
float  taps[FIR_LEN_MAX]      = {0};   /* small + per-sample; keep in DTCM for speed */
int    firTotalTaps   = 0;

/* History of input PCM samples for the polyphase filter. */
float  history[TAPS_PER_PHASE] = {0};  /* tiny; keep in DTCM */
int    historyWr      = 0;

/* Output ring — sequential write/read. DMAMEM = OCRAM, off DTCM. */
DMAMEM float  ring[RING_SIZE] = {0};
int    ringHead       = 0;       /* next write */
int    ringTail       = 0;       /* next read  */

inline bool ringEmpty() { return ringHead == ringTail; }
inline bool ringFull()  { return ((ringHead + 1) % RING_SIZE) == ringTail; }

inline void ringPush(float v) {
    if (ringFull()) {
        /* Drop oldest to keep newest -- bounded latency under producer overrun. */
        ringTail = (ringTail + 1) % RING_SIZE;
    }
    ring[ringHead] = v;
    ringHead = (ringHead + 1) % RING_SIZE;
}

inline bool ringPop(float &out) {
    if (ringEmpty()) return false;
    out = ring[ringTail];
    ringTail = (ringTail + 1) % RING_SIZE;
    return true;
}

/* Build a length-N windowed-sinc lowpass with Hann window. cutoffNorm is
 * the normalized cutoff frequency relative to the OUTPUT (upsampled)
 * sample rate, in [0, 0.5]. */
void buildLowpass(float *h, int len, float cutoffNorm) {
    const int   center = len / 2;
    const float pi     = 3.14159265358979f;
    float       sum    = 0.0f;
    for (int i = 0; i < len; ++i) {
        const float n = (float)(i - center);
        float       s;
        if (fabsf(n) < 1e-6f) {
            s = 2.0f * cutoffNorm;
        } else {
            s = sinf(2.0f * pi * cutoffNorm * n) / (pi * n);
        }
        const float w = 0.5f * (1.0f - cosf(2.0f * pi * (float)i / (float)(len - 1)));
        h[i] = s * w;
        sum += h[i];
    }
    /* Normalize for unity DC gain at OUTPUT rate, then scale by upFactor
     * so the polyphase form (which sees the input-rate sample stream
     * without zero-stuffing) preserves amplitude. */
    if (sum > 1e-6f) {
        const float k = 1.0f / sum;
        for (int i = 0; i < len; ++i) h[i] *= k;
    }
}

/* Upsample one input PCM sample x (8 kHz) into upFactor output samples
 * (DSP rate) via polyphase decomposition. taps[p + k*upFactor] selects
 * phase p tap k. */
void interpolateOne(float x, float *outSamples) {
    /* Push x into history (newest at historyWr; oldest one slot ahead). */
    history[historyWr] = x;
    historyWr = (historyWr + 1) % TAPS_PER_PHASE;

    for (int p = 0; p < upFactor; ++p) {
        float acc = 0.0f;
        int   r   = historyWr;
        for (int k = 0; k < TAPS_PER_PHASE; ++k) {
            if (--r < 0) r += TAPS_PER_PHASE;
            acc += history[r] * taps[p + k * upFactor];
        }
        outSamples[p] = acc * (float)upFactor; /* compensate for poly-decomp gain */
    }
}

/* Drain any AMBE PCM frames waiting in AmbeDvsi's output queue, upsample
 * them, and push into the ring. */
void pumpFromAmbe(void) {
    int16_t  pcm[AMBE_FRAME_SAMPS];
    while (AmbePopPcm20ms(pcm)) {
        float scratch[UP_SCRATCH_MAX];
        int   k = 0;
        for (int i = 0; i < AMBE_FRAME_SAMPS; ++i) {
            const float x = (float)pcm[i] / 32768.0f;
            interpolateOne(x, &scratch[k]);
            k += upFactor;
        }
        for (int i = 0; i < k; ++i) ringPush(scratch[i]);
    }
}

}  // namespace

bool DmrAudioInit(float dspAudioRateHz) {
    /* AMBE PCM rate is 8 kHz fixed. Require integer-multiple DSP rates. */
    if (!(dspAudioRateHz >= (float)AMBE_RATE_HZ)) return (ready = false);
    const int factor = (int)floorf(dspAudioRateHz / (float)AMBE_RATE_HZ + 0.5f);
    if (factor < 1 || factor > UPSAMPLE_MAX) return (ready = false);
    if (fabsf((float)factor * (float)AMBE_RATE_HZ - dspAudioRateHz) > 0.5f) {
        /* Non-integer ratio: refuse and ask a future commit to add a
         * fractional resampler. */
        return (ready = false);
    }

    upFactor      = factor;
    firTotalTaps  = upFactor * TAPS_PER_PHASE;

    /* Cutoff just below 4 kHz / dspRate so the upsample image at f_in/2
     * = 4 kHz is well-attenuated. Use 90 % of Nyquist of the INPUT rate
     * to leave a transition band. cutoff in normalized output-rate
     * units: 0.9 * 4 kHz / dspRate = 0.45 / upFactor. */
    const float cutoff = 0.45f / (float)upFactor;
    buildLowpass(taps, firTotalTaps, cutoff);

    memset(history, 0, sizeof(history));
    historyWr = 0;
    ringHead  = 0;
    ringTail  = 0;
    ready     = true;
    return true;
}

size_t DmrAudioPumpAndFill(float *dst, size_t n) {
    if (dst == nullptr) return 0;
    if (!ready) {
        for (size_t i = 0; i < n; ++i) dst[i] = 0.0f;
        return n;
    }
    pumpFromAmbe();
    for (size_t i = 0; i < n; ++i) {
        float v;
        dst[i] = ringPop(v) ? v : 0.0f;
    }
    return n;
}

void DmrAudioReset(void) {
    memset(history, 0, sizeof(history));
    historyWr = 0;
    ringHead  = 0;
    ringTail  = 0;
}
