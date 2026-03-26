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

#endif