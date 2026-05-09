/**
 * @file DmrTxAudio.cpp
 * @brief mic audio → AMBE → DMR voice burst → modem queue.
 *
 * State machine on the producer side (one of two sides of DmrModem):
 *
 *   1. Decimate mic audio from the DSP rate (typically 24 kHz) to 8 kHz
 *      via a 48-tap windowed-sinc lowpass; output every M-th input sample
 *      (M = micRate / 8 kHz, typically 3).
 *
 *   2. Accumulate 160 samples = 20 ms into a PCM frame buffer; ship via
 *      AmbeEncodePcm20ms when full. The codec on the ThumbDV runs over
 *      USB; encoded 49-bit AMBE+2 frames come back via AmbePopChannel.
 *
 *   3. Accumulate 3 AMBE frames per voice burst (3 × 20 ms = 60 ms,
 *      matching the DMR TDMA frame). Build the burst with
 *      DmrTxBuildVoiceFrame (Golay(23,12) + Hamming(15,11) FEC + voice
 *      info area split around SYNC).
 *
 *   4. Hold the burst in a 4-deep ring while DmrModem is busy rendering
 *      the previous one. Each feed drains as much as possible from the
 *      ring into DmrModemTxQueueBurst until the modem reports busy or
 *      the ring is empty.
 *
 *   5. Detect a new call (gap > 50 ms since last feed) and queue an LC
 *      header burst ahead of the first voice burst so receivers can
 *      announce src / dst / Talk Group from dmrConfig.
 *
 * Static memory: ~1.6 KB (FIR taps + history + PCM accumulator + 4-deep
 * burst ring). Tiny on Teensy 4.1.
 */

#include "SDT.h"
#include "DmrTxAudio.h"
#include "DmrModem.h"
#include "DmrFraming.h"
#include "AmbeDvsi.h"
#include <math.h>
#include <string.h>

namespace {

constexpr int   AMBE_RATE_HZ         = AMBE_PCM_SAMPLE_RATE_HZ;     /* 8 kHz  */
constexpr int   AMBE_FRAME_SAMPS     = AMBE_PCM_SAMPLES_PER_FRAME;  /* 160    */
/* Compile-time cap on the integer decimation factor (mic rate / 8 kHz).
 * Phoenix's default 24 kHz mic-side path is factor 3; 8 covers up to
 * 64 kHz mic audio with margin and saves ~5 KB on FIR storage vs the
 * original DECIM_MAX=24. */
constexpr int   DECIM_MAX            = 8;
constexpr int   FIR_TAPS_PER_PHASE   = 16;
constexpr int   FIR_LEN_MAX          = DECIM_MAX * FIR_TAPS_PER_PHASE; /* 128 */
constexpr int   PENDING_RING_DEPTH   = 4;
constexpr uint32_t NEW_CALL_GAP_MS   = 50;

/* ----- Decimator state ----- */
bool   ready          = false;
int    decimFactor    = 1;
int    decimCounter   = 0;
int    firLen         = 0;
float  firTaps[FIR_LEN_MAX]   = {0};   /* small + per-sample; keep in DTCM */
/* Decimator delay line — sequential write, sequential FIR walk on output.
 * DMAMEM = OCRAM, off DTCM. */
DMAMEM float  firHistory[FIR_LEN_MAX] = {0};
int    firHistoryWr   = 0;

/* ----- 8 kHz int16 PCM accumulator ----- */
int16_t pcmBuf[AMBE_FRAME_SAMPS] = {0};
int     pcmCount      = 0;

/* ----- AMBE channel-frame accumulator (3 frames per voice burst) ----- */
uint8_t ambeFrames[DMR_AMBE_FRAMES_PER_BURST][AMBE_DMR_BYTES_PER_FRAME] = {{0}};
int     ambeCount     = 0;

/* ----- Pending-burst ring (drains into DmrModemTxQueueBurst) ----- */
struct PendingBurst { uint8_t bytes[DMR_BURST_BYTES_MAX]; size_t len; };
PendingBurst burstRing[PENDING_RING_DEPTH];
int          ringHead = 0;   /* next write */
int          ringTail = 0;   /* next read  */

inline bool ringEmpty() { return ringHead == ringTail; }
inline bool ringFull()  { return ((ringHead + 1) % PENDING_RING_DEPTH) == ringTail; }

void ringPush(const uint8_t *bytes, size_t len) {
    if (ringFull()) {
        /* Drop oldest to bound latency under producer overrun. */
        ringTail = (ringTail + 1) % PENDING_RING_DEPTH;
    }
    memcpy(burstRing[ringHead].bytes, bytes, DMR_BURST_BYTES_MAX);
    burstRing[ringHead].len = len;
    ringHead = (ringHead + 1) % PENDING_RING_DEPTH;
}

bool ringPeek(uint8_t **outBytes, size_t *outLen) {
    if (ringEmpty()) return false;
    *outBytes = burstRing[ringTail].bytes;
    *outLen   = burstRing[ringTail].len;
    return true;
}

void ringAdvance() {
    if (!ringEmpty()) ringTail = (ringTail + 1) % PENDING_RING_DEPTH;
}

/* ----- Call lifecycle tracking ----- */
uint32_t lastFedMs    = 0;
bool     lcSentThisCall = false;

/* ----- Lowpass FIR construction (Hann-windowed sinc) ----- */
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
    if (sum > 1e-6f) {
        const float k = 1.0f / sum;
        for (int i = 0; i < len; ++i) h[i] *= k;
    }
}

/* Push one input sample; if M-th sample, return true with FIR output. */
bool decimStep(float in, float *out) {
    firHistory[firHistoryWr] = in;
    firHistoryWr = (firHistoryWr + 1) % FIR_LEN_MAX;
    ++decimCounter;
    if (decimCounter < decimFactor) return false;
    decimCounter = 0;

    float acc = 0.0f;
    int   p   = firHistoryWr;
    for (int k = 0; k < firLen; ++k) {
        if (--p < 0) p += FIR_LEN_MAX;
        acc += firTaps[k] * firHistory[p];
    }
    *out = acc;
    return true;
}

/* Try to flush the burst ring into the modem; stop on busy/error. */
void drainRingToModem(void) {
    uint8_t *bytes;
    size_t   len;
    while (DmrModemTxBufferAvailable() && ringPeek(&bytes, &len)) {
        const DmrModemResult r = DmrModemTxQueueBurst(bytes, len);
        if (r == DMR_MODEM_OK) {
            ringAdvance();
        } else {
            break;
        }
    }
}

void detectNewCall(void) {
    const uint32_t now = millis();
    if (now - lastFedMs > NEW_CALL_GAP_MS) {
        /* Gap > NEW_CALL_GAP_MS ⇒ treat next feed as fresh call. */
        pcmCount      = 0;
        ambeCount     = 0;
        ringHead      = ringTail = 0;
        lcSentThisCall = false;
    }
    lastFedMs = now;
}

void queueLcHeaderIfNeeded(void) {
    if (lcSentThisCall) return;
    uint8_t hdr[DMR_BURST_BYTES_MAX];
    size_t  hlen = 0;
    if (DmrTxBuildLcHeaderBurst(hdr, &hlen)) {
        ringPush(hdr, hlen);
        lcSentThisCall = true;
    }
}

}  // namespace

/* ====================================================================== */
/*                              Public API                                */
/* ====================================================================== */

bool DmrTxAudioInit(float micAudioRateHz) {
    if (!(micAudioRateHz >= (float)AMBE_RATE_HZ)) return (ready = false);
    const int factor = (int)floorf(micAudioRateHz / (float)AMBE_RATE_HZ + 0.5f);
    if (factor < 1 || factor > DECIM_MAX) return (ready = false);
    if (fabsf((float)factor * (float)AMBE_RATE_HZ - micAudioRateHz) > 0.5f) {
        return (ready = false);  /* non-integer ratio not supported here */
    }

    decimFactor   = factor;
    firLen        = factor * FIR_TAPS_PER_PHASE;

    /* Cutoff just below 4 kHz / micRate so 8 kHz aliasing images are
     * suppressed. 0.45 / decimFactor in normalized input-rate units. */
    const float cutoff = 0.45f / (float)decimFactor;
    buildLowpass(firTaps, firLen, cutoff);

    memset(firHistory, 0, sizeof(firHistory));
    firHistoryWr  = 0;
    decimCounter  = 0;
    pcmCount      = 0;
    ambeCount     = 0;
    ringHead      = ringTail = 0;
    lcSentThisCall = false;
    lastFedMs     = 0;
    ready         = true;
    return true;
}

void DmrTxAudioReset(void) {
    pcmCount       = 0;
    ambeCount      = 0;
    ringHead       = ringTail = 0;
    lcSentThisCall = false;
    /* Don't clear FIR history -- decimator stabilizes faster across calls. */
}

void DmrTxAudioFeed(const float *micAudio, size_t numSamples) {
    if (!ready || micAudio == nullptr || numSamples == 0) return;

    detectNewCall();

    /* 1) Decimate mic audio to 8 kHz; accumulate PCM; encode every 20 ms. */
    for (size_t i = 0; i < numSamples; ++i) {
        float out8k;
        if (!decimStep(micAudio[i], &out8k)) continue;
        /* Convert normalized float to int16 with saturation. */
        float scaled = out8k * 32767.0f;
        if (scaled >  32767.0f) scaled =  32767.0f;
        if (scaled < -32768.0f) scaled = -32768.0f;
        pcmBuf[pcmCount++] = (int16_t)scaled;
        if (pcmCount >= AMBE_FRAME_SAMPS) {
            pcmCount = 0;
            (void)AmbeEncodePcm20ms(pcmBuf);   /* drops if codec not ready */
        }
    }

    /* 2) Drain encoded frames; build a voice burst every 3 frames. */
    uint8_t frame[AMBE_DMR_BYTES_PER_FRAME];
    while (AmbePopChannel(frame)) {
        memcpy(ambeFrames[ambeCount], frame, AMBE_DMR_BYTES_PER_FRAME);
        ++ambeCount;
        if (ambeCount >= DMR_AMBE_FRAMES_PER_BURST) {
            ambeCount = 0;
            /* Send LC at the head of the call so receivers can announce. */
            queueLcHeaderIfNeeded();
            uint8_t burst[DMR_BURST_BYTES_MAX];
            size_t  blen = 0;
            if (DmrTxBuildVoiceFrame(ambeFrames, burst, &blen)) {
                ringPush(burst, blen);
            }
        }
    }

    /* 3) Submit pending bursts to the modem until it reports busy. */
    drainRingToModem();
}
