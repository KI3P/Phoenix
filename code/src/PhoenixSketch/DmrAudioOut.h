#ifndef DMR_AUDIO_OUT_H
#define DMR_AUDIO_OUT_H
#include "SDT.h"

/**
 * @file DmrAudioOut.h
 * @brief Bridge AMBE+2 8 kHz PCM into the Phoenix DSP audio path.
 *
 * AmbeDvsi pops decoded voice in 20 ms / 160-sample int16 frames at
 * 8 kHz. The Phoenix RX audio path runs at the post-DecimateBy8 rate
 * (typically 24 kHz with 256-sample blocks for the default 192 kHz I/Q
 * setup). This module:
 *
 *   1. Polls AmbePopPcm20ms() each DSP block, pulls available frames.
 *   2. Upsamples 8 kHz → DSP rate via integer-factor polyphase
 *      interpolation with a windowed-sinc lowpass.
 *   3. Pushes upsampled samples into a ring buffer that absorbs the
 *      jitter between burst arrivals (~60 ms apart) and DSP block
 *      cadence (~10.7 ms apart at 24 kHz/256).
 *   4. Drains the ring into the caller's output buffer; underruns are
 *      filled with silence.
 *
 * Architectural placement:
 *
 *     ThumbDV (USB)
 *        │ 8 kHz int16 PCM (AmbePopPcm20ms)
 *        ▼
 *     DmrAudioOut       ─── polyphase upsample + ring buffer (this file)
 *        │ DSP-rate float audio
 *        ▼
 *     data->I[] in DSP.cpp::DmrRxBridgeFmAudio
 *        │
 *        ▼
 *     existing audio chain (BandEQ → NR → InterpolateReceiveData →
 *                           AdjustVolume → PlayBuffer)
 *        │
 *        ▼
 *     SGTL5000 codec → speaker
 */

/**
 * @brief Initialize the upsampler and clear the ring.
 * @param dspAudioRateHz  Sample rate of the DSP audio path that will
 *                        consume the output (typically SR/8, e.g.
 *                        24 000 Hz when SR = 192 kHz). Must be an
 *                        integer multiple of 8 kHz between 8 kHz and
 *                        192 kHz; non-integer ratios are rejected.
 * @return true on success; false if the rate is unsupported (in which
 *         case DmrAudioPumpAndFill will fall back to silence).
 *
 * Builds an integer-factor polyphase windowed-sinc interpolator (Hann
 * window, ~3.6 kHz passband, normalized for unity DC gain). Static
 * cost: 384 taps × 4 bytes = 1.5 KB FIR + 8 KB ring + 64 byte history.
 */
bool DmrAudioInit(float dspAudioRateHz);

/**
 * @brief Pull pending AMBE PCM, upsample, fill caller's audio buffer.
 * @param dst  Destination buffer at the DSP audio rate, normalized
 *             float in [-1, +1].
 * @param n    Number of samples to fill.
 * @return Number of samples actually filled (always == n; underruns
 *         are zero-filled rather than reported short).
 *
 * Drains AmbePopPcm20ms() greedily on each call so the ring stays
 * primed; pulls n samples out of the ring head into @p dst, padding
 * with silence on underrun. Designed to be called once per RX DSP
 * block from DmrRxBridgeFmAudio() in DSP.cpp.
 */
size_t DmrAudioPumpAndFill(float *dst, size_t n);

/**
 * @brief Clear the ring + history. Call when leaving DMR mode or
 *        when a call ends, so stale audio doesn't leak into the next
 *        call.
 */
void DmrAudioReset(void);

#endif /* DMR_AUDIO_OUT_H */
