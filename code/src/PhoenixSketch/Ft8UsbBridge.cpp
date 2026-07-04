// Ft8UsbBridge.cpp
//
// USB (WSJT-X, ~44.1 kHz)  ->  Phoenix SDR TX path (sampleRate, e.g. 192 kHz)
// Linear resampler with a float FIFO.
//
// This version adds a simple "warm-up" phase:
//   - The first few calls to Ft8UsbBridge_GetSamples() return pure silence.
//   - After that, we only send fully interpolated data plus a small
//     "hold last sample" tail if we ever run short.
// Goal: avoid the ugly "slow motor" pulsing on the very first TUNE.

#include <Audio.h>
#include <string.h>
#include "Ft8UsbBridge.h"
#include "Config.h"

#if defined(T41_USB_AUDIO) && !defined(T41_CORE_HAS_USB_AUDIO_PUSH_BLOCK)
#warning "T41: core_mods/usb_audio.{cpp,h} not applied -- FT8 RX audio-to-PC path disabled (stub); TX still works. See code/docs/EXPERIMENT_LOG.md."
#endif

static volatile bool g_ft8TxActive = false;

// ─────────────────────────────────────────────────────────────
// RX PATH: 192kHz DSP audio → resample → FIFO → USB to PC
//
// None of this touches AudioInputUSB/AudioOutputUSB directly, so it's kept
// unconditional (safe to call regardless of board USB Type). The one spot
// that does need the real USB audio interface -- the final handoff to
// usb_audio_push_block() inside Ft8UsbBridge_DrainToUSB() -- is already
// gated on T41_CORE_HAS_USB_AUDIO_PUSH_BLOCK, which usb_audio.h only
// defines when the interface actually exists (see core_mods/usb_audio.h).
// ─────────────────────────────────────────────────────────────
static const uint32_t RX_FIFO_SIZE = 16384;
DMAMEM static float g_rxFifo[RX_FIFO_SIZE];
static volatile uint32_t g_rxWriteIdx = 0;
static volatile uint32_t g_rxReadIdx  = 0;

static inline uint32_t RxNextIdx(uint32_t i) { return (i + 1u) % RX_FIFO_SIZE; }

static inline bool RxFifoPush(float x) {
    uint32_t next = RxNextIdx(g_rxWriteIdx);
    if (next == g_rxReadIdx) return false;
    g_rxFifo[g_rxWriteIdx] = x;
    g_rxWriteIdx = next;
    return true;
}

static inline bool RxFifoPop(float *x) {
    if (g_rxReadIdx == g_rxWriteIdx) return false;
    *x = g_rxFifo[g_rxReadIdx];
    g_rxReadIdx = RxNextIdx(g_rxReadIdx);
    return true;
}

static inline uint32_t RxFifoCount(void) {
    if (g_rxWriteIdx >= g_rxReadIdx) return g_rxWriteIdx - g_rxReadIdx;
    return RX_FIFO_SIZE - g_rxReadIdx + g_rxWriteIdx;
}

void Ft8UsbBridge_PutRxSamples(const float *in, uint32_t count)
{
    if (!in || count < 2) return;
    static float srcPos = 0.0f;
    const float step = 192000.0f / 44100.0f;
    while (srcPos < (float)(count - 1)) {
        uint32_t i0 = (uint32_t)srcPos;
        uint32_t i1 = i0 + 1;
        float frac = srcPos - (float)i0;
        float x = in[i0] + frac * (in[i1] - in[i0]);
        if (x >  1.0f) x =  1.0f;
        if (x < -1.0f) x = -1.0f;
        RxFifoPush(x);
        srcPos += step;
    }
    srcPos -= (float)(count - 1);
}

void Ft8UsbBridge_DrainToUSB(void)
{

    if (g_ft8TxActive) return;
    if (RxFifoCount() < AUDIO_BLOCK_SAMPLES) return;

#if defined(T41_CORE_HAS_USB_AUDIO_PUSH_BLOCK)
    int16_t left_buf[AUDIO_BLOCK_SAMPLES];
    int16_t right_buf[AUDIO_BLOCK_SAMPLES];

    for (int k = 0; k < AUDIO_BLOCK_SAMPLES; k++) {
        float x = 0.0f;
        RxFifoPop(&x);
        if (x >  1.0f) x =  1.0f;
        if (x < -1.0f) x = -1.0f;
        int16_t s = (int16_t)(x * 32767.0f);
        left_buf[k] = s;
        right_buf[k] = s;
    }
    usb_audio_push_block(left_buf, right_buf);
#else
    // No core patch available (or no USB audio interface in this build):
    // there's no way to deliver these samples to the PC. Drop them so the
    // RX FIFO doesn't fill and stall; TX (PC -> radio) is unaffected since
    // it doesn't depend on the core patch.
    float x;
    for (int k = 0; k < AUDIO_BLOCK_SAMPLES; k++) RxFifoPop(&x);
#endif
}

void Ft8UsbBridge_SetTxActive(bool on)
{
    g_ft8TxActive = on;
}

// ─────────────────────────────────────────────────────────────
// TX PATH: USB (WSJT-X) → resample → Phoenix SDR
//
// This is the one part of the bridge that genuinely needs a USB-audio-
// capable board (AudioInputUSB/AudioRecordQueue require AUDIO_INTERFACE,
// which usb_audio.h only defines for USB Type = Audio / Serial+MIDI+Audio).
// Ft8UsbBridge_Init()/Ft8UsbBridge_GetSamples() are called unconditionally
// from elsewhere in the project (Globals.cpp, DSP.cpp), so when the
// interface isn't available they fall back to safe stubs below instead of
// requiring every caller to match this exact gate.
// ─────────────────────────────────────────────────────────────
#if defined(T41_USB_AUDIO) && (defined(USB_AUDIO) || defined(USB_MIDI_AUDIO_SERIAL))

// -----------------------------------------------------------------------------
// USB audio objects
// We use LEFT channel only from WSJT-X (WSJT usually sends same audio on L/R).
// -----------------------------------------------------------------------------
AudioInputUSB      g_usbIn;
AudioRecordQueue   g_usbQueueL;
AudioRecordQueue   g_usbQueueR;
AudioConnection    g_pcUsbToQueueL(g_usbIn, 0, g_usbQueueL, 0);
AudioConnection    g_pcUsbToQueueR(g_usbIn, 1, g_usbQueueR, 0);

// -----------------------------------------------------------------------------
// Internal SRC state
// -----------------------------------------------------------------------------

// SDR sample rate (e.g., 192000.0f). Set in Ft8UsbBridge_Init().
static float g_sdrSampleRate = 192000.0f;

// Teensy USB audio nominal sample rate.
// AUDIO_SAMPLE_RATE_EXACT ≈ 44117.64706 Hz.
static const float kUsbSampleRate = AUDIO_SAMPLE_RATE_EXACT;

// FIFO for buffering USB-rate samples (float, mono).
// 4096 samples is ~90 ms at 44.1 kHz – plenty of cushion.
static float    g_usbFifo[4096];
static uint32_t g_fifoCount = 0;    // number of valid samples in g_usbFifo[]
static float    g_srcPhase  = 0.0f; // fractional index in FIFO (in USB samples)

// Track the last valid audio sample so we can avoid sudden zeros mid-stream.
static float    g_lastSample    = 0.0f;
static bool     g_haveSample    = false;

// Simple "warm-up" counter: how many complete SDR blocks we've produced
// since Ft8UsbBridge_Init() was called.
static uint32_t g_blocksSinceInit = 0;

// -----------------------------------------------------------------------------
// FIFO helpers
// -----------------------------------------------------------------------------

// Compact FIFO after "consumed" samples have been used.
static void fifo_consume(uint32_t consumed)
{
    if (consumed == 0 || g_fifoCount == 0) return;

    if (consumed >= g_fifoCount) {
        g_fifoCount = 0;
        return;
    }

    memmove(g_usbFifo,
            g_usbFifo + consumed,
            (g_fifoCount - consumed) * sizeof(float));

    g_fifoCount -= consumed;
}

// Pull as many 128-sample USB blocks as we can and append them to FIFO.
static void pump_usb_to_fifo()
{
    const uint32_t blockSamples = 128;
    const uint32_t fifoCapacity = (uint32_t)(sizeof(g_usbFifo) / sizeof(g_usbFifo[0]));

    while (g_usbQueueL.available() > 0 &&
           g_usbQueueR.available() > 0 &&
           (g_fifoCount + blockSamples) <= fifoCapacity)
    {
        int16_t *pL = g_usbQueueL.readBuffer();
        int16_t *pR = g_usbQueueR.readBuffer();
        if (!pL || !pR) break;

        for (uint32_t i = 0; i < blockSamples; ++i) {
            float l = (float)pL[i] / 32768.0f;
            float r = (float)pR[i] / 32768.0f;
            g_usbFifo[g_fifoCount + i] = 0.5f * (l + r);
        }

        g_fifoCount += blockSamples;
        g_usbQueueL.freeBuffer();
        g_usbQueueR.freeBuffer();
    }
}


// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void Ft8UsbBridge_Init(float sdrSampleRateHz)
{
    g_sdrSampleRate = (sdrSampleRateHz > 0.0f) ? sdrSampleRateHz : 192000.0f;

    g_fifoCount       = 0;
    g_srcPhase        = 0.0f;
    g_lastSample      = 0.0f;
    g_haveSample      = false;
    g_blocksSinceInit = 0;

    // Optional but helpful: reset queues so we start clean
    g_usbQueueL.end();
    g_usbQueueR.end();
    g_usbQueueL.begin();
    g_usbQueueR.begin();
}


// Get "outCount" SDR-rate samples for the TX path.
// Caller (DSP.cpp) will:
//   - Put this into data->I,
//   - Zero data->Q,
//   - Run the TX chain (or direct-to-IQ, depending on your variant).
//
// We always fill the whole buffer. On startup we deliberately output
// a few blocks of silence so the USB FIFO can "stabilize" before the
// rig ever sees actual RF drive.
bool Ft8UsbBridge_GetSamples(float *out, uint32_t outCount)
{
    if (!out || outCount == 0) {
        return false;
    }

    // --------------------------------------------------------------
    // 0) WARM-UP PHASE
    //
    // On the very first TUNE, USB audio may only have just started to
    // arrive from WSJT-X. To avoid any chance of block-by-block
    // "motorboating", we deliberately send a few blocks of *pure
    // silence* before we ever send any real tone.
    //
    // At 192 kHz with 2048-sample blocks, each block is ~10.7 ms.
    // 4 blocks ≈ 43 ms of silence – inaudible in an FT8 TX but enough
    // to get a stable FIFO.
    // --------------------------------------------------------------
    const uint32_t WARMUP_BLOCKS = 4;

    if (g_blocksSinceInit < WARMUP_BLOCKS)
    {
        // Keep draining USB queues so they don’t overflow while we “warm up”
        pump_usb_to_fifo();

        memset(out, 0, outCount * sizeof(float));
        g_blocksSinceInit++;
        return true;
    }


    // Bring in fresh USB data.
    pump_usb_to_fifo();

    // How many input (USB) samples to advance per output (SDR) sample.
    // E.g. for 44.1k -> 192k: step ≈ 0.2299
    const float step = kUsbSampleRate / g_sdrSampleRate;

    float    phase    = g_srcPhase;
    uint32_t produced = 0;

    // --------------------------------------------------------------
    // 1) Normal interpolation from FIFO
    // --------------------------------------------------------------
    while (produced < outCount && g_fifoCount > 1) {
        uint32_t i0 = (uint32_t)phase;

        // Need two samples for interpolation.
        if (i0 + 1 >= g_fifoCount) {
            break;
        }

        float frac = phase - (float)i0;
        float s0   = g_usbFifo[i0];
        float s1   = g_usbFifo[i0 + 1];

        float s = s0 + frac * (s1 - s0);  // linear interpolation

        out[produced++] = s;

        // Remember last valid sample so we can avoid sharp transitions
        // if the FIFO ever runs slightly short mid-block.
        g_lastSample = s;
        g_haveSample = true;

        phase += step;
    }

    // How many whole input samples did we consume?
    uint32_t consumed = (uint32_t)phase;
    g_srcPhase = phase - (float)consumed;

    if (consumed > 0) {
        fifo_consume(consumed);
    }

    // --------------------------------------------------------------
    // 2) Tail fill: if we ever underrun after warm-up, fill the rest
    //    of the block with the last valid sample. For a continuous
    //    WSJT tone this should be rare and effectively inaudible.
    // --------------------------------------------------------------
    if (produced < outCount) {
        float fill = (g_haveSample ? g_lastSample : 0.0f);

        for (uint32_t i = produced; i < outCount; ++i) {
            out[i] = fill;
        }
    }

    g_blocksSinceInit++;
    return true;
}

#else // !(defined(T41_USB_AUDIO) && (defined(USB_AUDIO) || defined(USB_MIDI_AUDIO_SERIAL)))

// No USB audio interface in this build (T41_USB_AUDIO disabled, or a board
// USB Type without Audio -- e.g. plain/Dual/Triple Serial). Stub out the
// TX-path entry points so callers don't need to know which USB Type is
// selected.
void Ft8UsbBridge_Init(float sdrSampleRateHz)
{
    (void)sdrSampleRateHz;
}

bool Ft8UsbBridge_GetSamples(float *out, uint32_t outCount)
{
    if (!out || outCount == 0) return false;
    memset(out, 0, outCount * sizeof(float));
    return true;
}

#endif
