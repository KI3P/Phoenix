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
 * USB audio transport for DIGITAL mode.
 *
 * ## Why this file exists
 *
 * The stock Teensy AudioInputUSB / AudioOutputUSB objects (the same ones
 * OpenAudio's USB_Audio_F32.h wraps) do everything we need: descriptors, DMA,
 * the isochronous ISRs, and the asynchronous feedback endpoint that keeps the
 * host's clock and ours in step. None of that is reimplemented here.
 *
 * What does not work is putting them in the AudioConnection graph directly. The
 * graph is clocked by the I2S DMA, not by USB: software_isr() runs once per
 * AUDIO_BLOCK_SAMPLES of I2S, which at 176.4 ksps is 1378.125 Hz. The USB audio
 * objects must be updated at 44100/128 = 344.53 Hz. Overfed 4:1, AudioOutputUSB
 * only holds two blocks and discards the rest, and AudioInputUSB's feedback
 * control loop is calibrated for the nominal rate.
 *
 * At 176.4 ksps that ratio is exactly 4:1, which is the whole reason DIGITAL
 * mode forces this sample rate. So we take the USB objects out of the graph
 * clock (AudioStream::update_all skips any object whose protected `active` flag
 * is false) and drive their update() from a pacer that does real work on every
 * fourth call.
 *
 * ## Data flow
 *
 *   radio -> PC:  DSP block (512 samples @ 44.1 kHz)
 *                   -> USBAudioWriteRx -> usbPlayL/R -> usbOut -> host
 *   PC -> radio:  host -> usbIn -> usbRecL/R -> USBAudioReadTx -> DSP block
 *
 * The AudioPlayQueue / AudioRecordQueue objects are the buffering, so there is
 * no ring buffer here: the DSP delivers four blocks at once every 11.6 ms while
 * the pacer moves one block every 2.9 ms.
 *
 * ## A note on style
 *
 * Phoenix is written as C-style C++ - free functions over file-scope data - and
 * this file follows that. The two places it cannot are forced by the Teensy
 * Audio library, and both are as small as they can be made:
 *
 *   1. Clearing AudioStream::active needs a derived type, because the flag is
 *      protected. Hence the four one-line Deactivatable* structs below.
 *   2. Getting into update_all() at all needs an AudioStream subclass with an
 *      update() method. Hence USBAudioPacer, whose state is a file-scope
 *      variable and whose body is an ordinary out-of-line function.
 *
 * @see USBAudio.h for the public API
 */
#include "SDT.h"
#include "USBAudio.h"

#ifdef AUDIO_INTERFACE

#include <Audio.h>

// The core's usb_audio.cpp exports these but does not declare them in a header.
extern volatile uint32_t usb_audio_underrun_count;
extern volatile uint32_t usb_audio_overrun_count;

/** Depth of the radio-to-host play queue, in AUDIO_BLOCK_SAMPLES blocks.
 *
 *  The producer and consumer are rate-matched on average (both 44,100 samples/s)
 *  but not in phase: the DSP delivers four blocks at once every 11.6 ms, while
 *  the pacer takes one block every 2.9 ms. So the queue swings by four blocks
 *  every DSP period no matter what, and anything that delays the main loop -
 *  a display refresh is the obvious one - drains it further.
 *
 *  This started at 8 with no prefill, which left the queue living at a depth of
 *  0-4 blocks, i.e. no underrun margin at all. A 12 s capture then contained 147
 *  dropouts totalling 903 ms, every one an exact multiple of AUDIO_BLOCK_SAMPLES
 *  and spaced about 133 ms apart: AudioOutputUSB::update() substitutes a silence
 *  block whenever its input queue is empty, so an empty play queue is heard as
 *  digital silence, not as a glitch.
 *
 *  24 blocks prefilled to half depth gives ~35 ms of margin in both directions -
 *  three times the worst observed stall - for ~35 ms of added latency, which is
 *  nothing to an FT8 decoder. */
#define USB_PLAY_QUEUE_BLOCKS 24

/** Blocks of silence queued at start-up so the first stall has something to eat. */
#define USB_PLAY_QUEUE_PREFILL (USB_PLAY_QUEUE_BLOCKS / 2)

/** Blocks the record queue may accumulate before USBAudioReadTx() discards the
 *  oldest. AudioRecordQueue has no built-in cap and will happily swallow the
 *  whole audio memory pool if the main loop stalls. */
#define USB_REC_QUEUE_CAP 12

/** AudioStream blocks per DSP block. 512 / 128 = 4, and also the pacer divisor. */
#define USB_BLOCKS_PER_DSP_BLOCK (USB_AUDIO_BLOCK_SAMPLES / AUDIO_BLOCK_SAMPLES)

// ---------------------------------------------------------------------------
// Audio graph objects
//
// AudioStream::active is protected, so only a derived type may clear it. These
// four structs add nothing else. Clearing the flag removes the object from
// update_all(), which is what lets DriveUSBAudioObjects() below run them at the
// USB rate instead of the I2S rate.
// ---------------------------------------------------------------------------
struct DeactivatableInputUSB    : public AudioInputUSB    { void Deactivate(void){ active = false; } };
struct DeactivatableOutputUSB   : public AudioOutputUSB   { void Deactivate(void){ active = false; } };
struct DeactivatablePlayQueue   : public AudioPlayQueue   { void Deactivate(void){ active = false; } };
struct DeactivatableRecordQueue : public AudioRecordQueue { void Deactivate(void){ active = false; } };

static DeactivatableInputUSB    usbIn;               // host -> radio (transmit audio)
static DeactivatableOutputUSB   usbOut;              // radio -> host (receive audio)
static DeactivatablePlayQueue   usbPlayL, usbPlayR;  // staging for usbOut
static DeactivatableRecordQueue usbRecL, usbRecR;    // staging from usbIn

static AudioConnection usbPatch1(usbPlayL, 0, usbOut, 0);
static AudioConnection usbPatch2(usbPlayR, 0, usbOut, 1);
static AudioConnection usbPatch3(usbIn, 0, usbRecL, 0);
static AudioConnection usbPatch4(usbIn, 1, usbRecR, 0);

static volatile bool usbAudioRunning = false;
static volatile uint8_t usbPacerCount = 0;

// Transmit-path health counters. The USB->exciter path joins 128-sample blocks
// pulled from the record queue; if that queue ever runs dry or has to be trimmed,
// the join is discontinuous and the discontinuity lands on the air as sidebands
// at the block rate. These say which is happening.
static volatile uint32_t usbTxUnderruns = 0;   // ReadTx found fewer than 4 blocks
static volatile uint32_t usbTxTrimmed = 0;     // blocks discarded for exceeding the cap
static volatile uint32_t usbTxCalls = 0;       // ReadTx calls, to normalise the above
static volatile uint8_t  usbRecDepthMin = 255;
static volatile uint8_t  usbRecDepthMax = 0;

/** Scratch for scaling receive audio on its way to the host, so the caller's
 *  buffer - which still has to feed the speaker - is left untouched. */
static float32_t usbRxScratch[AUDIO_BLOCK_SAMPLES];

/**
 * Run one USB audio block in and one out.
 *
 * Called from the pacer at 44100/AUDIO_BLOCK_SAMPLES Hz. The six objects are all
 * out of update_all(), so this is the only thing driving them, and the order
 * here is their data dependency order.
 */
static void DriveUSBAudioObjects(void){
    // radio -> host: staged blocks into the USB output object
    usbPlayL.update();
    usbPlayR.update();
    usbOut.update();
    // host -> radio: USB input object into the staging queues
    usbIn.update();
    usbRecL.update();
    usbRecR.update();

    // Bound the record queue here rather than only in USBAudioReadTx(), which is
    // called only while transmitting. Left to itself the queue fills for the whole
    // time we are receiving: it was measured pinned at 208 of AudioRecordQueue's
    // 209-block ceiling, holding ~418 of the 500 audio blocks between the two
    // channels and handing the first transmit ~0.6 s of stale audio to discard.
    while (usbRecL.available() > USB_REC_QUEUE_CAP) {
        usbRecL.readBuffer();
        usbRecL.freeBuffer();
    }
    while (usbRecR.available() > USB_REC_QUEUE_CAP) {
        usbRecR.readBuffer();
        usbRecR.freeBuffer();
    }
}

/**
 * Divides the audio library's graph clock down to the USB audio block rate.
 *
 * This is the only object in this file left in update_all(). It runs in the
 * audio ISR at Fs/AUDIO_BLOCK_SAMPLES and does real work on every fourth call,
 * i.e. at exactly 44100/128 Hz when the radio is at 176.4 ksps.
 */
struct USBAudioPacer : public AudioStream {
    USBAudioPacer(void) : AudioStream(0, NULL) { active = true; }
    void update(void);
};

static USBAudioPacer usbPacer;

void USBAudioPacer::update(void){
    if (++usbPacerCount < USB_BLOCKS_PER_DSP_BLOCK)
        return;
    usbPacerCount = 0;
    if (!usbAudioRunning)
        return;
    DriveUSBAudioObjects();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void USBAudioInit(void){
    // AudioConnection::connect() sets active = true on both ends, so this has to
    // happen after the static AudioConnections above are constructed - which is
    // why it lives here and not in the constructors. It also has to happen at
    // startup rather than on entry to digital mode: until it does, update_all()
    // is driving these objects at the I2S block rate.
    usbIn.Deactivate();
    usbOut.Deactivate();
    usbPlayL.Deactivate();
    usbPlayR.Deactivate();
    usbRecL.Deactivate();
    usbRecR.Deactivate();
}

void USBAudioBegin(void){
    // Defensive: harmless if USBAudioInit() already ran, and guarantees the
    // objects are off the graph clock before we start feeding them.
    USBAudioInit();

    usbPlayL.setMaxBuffers(USB_PLAY_QUEUE_BLOCKS);
    usbPlayR.setMaxBuffers(USB_PLAY_QUEUE_BLOCKS);
    // Never stall the main loop waiting on the host to drain a queue
    usbPlayL.setBehaviour(AudioPlayQueue::NON_STALLING);
    usbPlayR.setBehaviour(AudioPlayQueue::NON_STALLING);

    AudioNoInterrupts();
    usbRecL.clear();
    usbRecR.clear();
    usbRecL.begin();
    usbRecR.begin();
    usbPacerCount = 0;
    usbTxUnderruns = 0; usbTxTrimmed = 0; usbTxCalls = 0;
    usbRecDepthMin = 255; usbRecDepthMax = 0;
    usbAudioRunning = true;
    AudioInterrupts();

    // Prefill with silence so the queue starts at half depth rather than empty.
    // Without this the very first main-loop stall is heard as a dropout, and the
    // queue never gets a chance to build up a cushion of its own.
    for (unsigned i = 0; i < USB_PLAY_QUEUE_PREFILL; i++) {
        int16_t *bufferL = usbPlayL.getBuffer();
        int16_t *bufferR = usbPlayR.getBuffer();
        if (bufferL == NULL || bufferR == NULL)
            break;
        memset(bufferL, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        memset(bufferR, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        usbPlayL.playBuffer();
        usbPlayR.playBuffer();
    }
}

void USBAudioEnd(void){
    AudioNoInterrupts();
    usbAudioRunning = false;
    usbRecL.end();
    usbRecR.end();
    usbRecL.clear();
    usbRecR.clear();
    AudioInterrupts();

    // AudioPlayQueue has no clear(), and its stop() is declared in the header but
    // never defined in this version of the Audio library. Drain it by hand
    // instead, so re-entering digital mode does not start by sending the host a
    // burst of stale audio. The queue is bounded at USB_PLAY_QUEUE_BLOCKS, so
    // that many updates empty it. Safe to call from the main loop: the pacer sees
    // usbAudioRunning false and leaves these objects alone.
    for (unsigned i = 0; i < USB_PLAY_QUEUE_BLOCKS; i++)
        DriveUSBAudioObjects();
}

void USBAudioWriteRx(const float32_t *src, size_t n){
    if (!usbAudioRunning || n != USB_AUDIO_BLOCK_SAMPLES)
        return;

    // Scale into scratch rather than in place: the caller is still going to run
    // the second interpolation stage over this same buffer to feed the speaker.
    float32_t level = ED.digitalRxLevel / 100.0f;

    for (unsigned i = 0; i < USB_BLOCKS_PER_DSP_BLOCK; i++) {
        int16_t *bufferL = usbPlayL.getBuffer();
        int16_t *bufferR = usbPlayR.getBuffer();
        if (bufferL == NULL || bufferR == NULL)
            return; // audio memory exhausted; drop the rest of this block

        // Teensy's CMSIS declares pSrc non-const even though it only reads it
        arm_scale_f32((float32_t *)&src[AUDIO_BLOCK_SAMPLES * i], level,
                      usbRxScratch, AUDIO_BLOCK_SAMPLES);
        arm_float_to_q15(usbRxScratch, bufferL, AUDIO_BLOCK_SAMPLES);
        // Digital receive audio is mono; the host sees it on both channels
        memcpy(bufferR, bufferL, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));

        // NON_STALLING: a non-zero return means the queue is full and the block
        // is retained for a retry. We simply refill it next time round, which
        // drops the stale audio in favour of the newer audio - the right trade
        // for a live stream.
        usbPlayL.playBuffer();
        usbPlayR.playBuffer();
    }
}

bool USBAudioReadTx(float32_t *dst, size_t n){
    if (!usbAudioRunning || n != USB_AUDIO_BLOCK_SAMPLES)
        return false;

    // AudioRecordQueue has no maximum depth of its own, so bound it here or a
    // stalled main loop would drain the audio memory pool. Discard oldest first.
    int depth = usbRecL.available();
    usbTxCalls++;
    if (depth < usbRecDepthMin) usbRecDepthMin = (depth > 255) ? 255 : (uint8_t)depth;
    if (depth > usbRecDepthMax) usbRecDepthMax = (depth > 255) ? 255 : (uint8_t)depth;

    while (usbRecL.available() > USB_REC_QUEUE_CAP) {
        usbRecL.readBuffer();
        usbRecL.freeBuffer();
        usbTxTrimmed++;
    }
    while (usbRecR.available() > USB_REC_QUEUE_CAP) {
        usbRecR.readBuffer();
        usbRecR.freeBuffer();
    }

    if (usbRecL.available() < (int)USB_BLOCKS_PER_DSP_BLOCK ||
        usbRecR.available() < (int)USB_BLOCKS_PER_DSP_BLOCK) {
        usbTxUnderruns++;
        return false; // underrun; caller substitutes silence
    }

    for (unsigned i = 0; i < USB_BLOCKS_PER_DSP_BLOCK; i++) {
        int16_t *bufferL = usbRecL.readBuffer();
        arm_q15_to_float(bufferL, &dst[AUDIO_BLOCK_SAMPLES * i], AUDIO_BLOCK_SAMPLES);
        usbRecL.freeBuffer();
        // Transmit audio is mono - take the left channel and discard the right,
        // but drain both in lockstep or the right queue grows without bound.
        usbRecR.readBuffer();
        usbRecR.freeBuffer();
    }
    return true;
}

bool USBAudioIsRunning(void){
    return usbAudioRunning;
}

bool USBAudioHostActive(void){
    return usb_audio_receive_setting != 0;
}

float USBAudioHostVolume(void){
    return usbIn.volume();
}

void USBAudioGetStats(uint32_t *underruns, uint32_t *overruns){
    if (underruns)
        *underruns = usb_audio_underrun_count;
    if (overruns)
        *overruns = usb_audio_overrun_count;
}

void USBAudioGetTxStats(uint32_t *calls, uint32_t *underruns, uint32_t *trimmed,
                        uint8_t *depthMin, uint8_t *depthMax){
    if (calls)     *calls = usbTxCalls;
    if (underruns) *underruns = usbTxUnderruns;
    if (trimmed)   *trimmed = usbTxTrimmed;
    if (depthMin)  *depthMin = usbRecDepthMin;
    if (depthMax)  *depthMax = usbRecDepthMax;
}

#else // !AUDIO_INTERFACE

// Built without a USB audio interface. DIGITAL mode is not reachable from the
// UI or CAT in this configuration, but these stubs keep every call site free of
// conditional compilation.

void USBAudioInit(void){}
void USBAudioBegin(void){}
void USBAudioEnd(void){}
bool USBAudioIsRunning(void){ return false; }
void USBAudioWriteRx(const float32_t *src, size_t n){ (void)src; (void)n; }
bool USBAudioReadTx(float32_t *dst, size_t n){ (void)dst; (void)n; return false; }
bool USBAudioHostActive(void){ return false; }
float USBAudioHostVolume(void){ return 1.0f; }

void USBAudioGetStats(uint32_t *underruns, uint32_t *overruns){
    if (underruns) *underruns = 0;
    if (overruns)  *overruns = 0;
}

void USBAudioGetTxStats(uint32_t *calls, uint32_t *underruns, uint32_t *trimmed,
                        uint8_t *depthMin, uint8_t *depthMax){
    if (calls) *calls = 0;
    if (underruns) *underruns = 0;
    if (trimmed) *trimmed = 0;
    if (depthMin) *depthMin = 0;
    if (depthMax) *depthMax = 0;
}

#endif // AUDIO_INTERFACE
