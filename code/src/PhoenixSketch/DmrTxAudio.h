#ifndef DMR_TX_AUDIO_H
#define DMR_TX_AUDIO_H
#include "SDT.h"

/**
 * @file DmrTxAudio.h
 * @brief Bridge mic audio into the DMR transmit path.
 *
 * Closes the operator-mic-to-air loop for DMR TX. Sits on the producer
 * side of DmrModem; the consumer side (DmrModemRender) is called from
 * DSP.cpp::TransmitProcessing right after this.
 *
 * Pipeline (typical 192 kHz IQ → 24 kHz audio Phoenix setup):
 *
 *      mic audio (24 kHz, ~10.7 ms blocks)
 *        │
 *        ▼  decimate 3:1 (windowed-sinc lowpass, ~3.6 kHz cutoff)
 *      8 kHz int16 PCM
 *        │  accumulate 160 samples = 20 ms
 *        ▼
 *      AmbeEncodePcm20ms()  ──► ThumbDV (USB) ──► AMBE+2 49-bit frame
 *                                                       │
 *                                                       ▼  AmbePopChannel()
 *                                              accumulate 3 frames = 60 ms
 *                                                       │
 *                                                       ▼
 *                              DmrTxBuildVoiceFrame()  (Golay+Hamming FEC,
 *                                                       voice burst layout
 *                                                       around SYNC)
 *                                                       │
 *                                                       ▼
 *                              4-deep pending-burst ring
 *                                                       │
 *                                                       ▼
 *                              DmrModemTxQueueBurst()  ──► 4FSK render
 *                                                          ──► FmModulate
 *                                                          ──► I/Q → codec
 *
 * On the first DMR TX block of each call (detected by a gap heuristic),
 * a Voice LC Header burst is queued ahead of the voice bursts so a
 * receiver can announce the call's src / dst / Talk Group.
 */

/**
 * @brief Initialize the mic-rate to 8 kHz decimator and clear state.
 * @param micAudioRateHz  Sample rate at which DmrTxAudioFeed() will be
 *                        called (typically SR/8 = 24 kHz on the default
 *                        Phoenix setup). Must be an integer multiple of
 *                        8 kHz between 8 kHz and 192 kHz.
 * @return true on success; false if the rate is unsupported (in which
 *         case DmrTxAudioFeed becomes a no-op).
 */
bool DmrTxAudioInit(float micAudioRateHz);

/**
 * @brief Push @p numSamples of mic audio into the DMR TX chain.
 * @param micAudio  Audio at the rate passed to DmrTxAudioInit, normalized
 *                  float in roughly [-1, +1].
 * @param numSamples  Number of samples in @p micAudio.
 *
 * Decimates to 8 kHz, accumulates 20 ms PCM frames, ships them to the
 * AMBE codec, drains encoded channel frames, assembles voice bursts,
 * and queues them on the modem. Self-detects the first block of a new
 * call (no feed for >50 ms ⇒ new call) and inserts an LC header burst
 * automatically. Designed to be called once per RX/TX DSP block from
 * TransmitProcessing.
 */
void DmrTxAudioFeed(const float *micAudio, size_t numSamples);

/**
 * @brief Force a fresh-call state: reset accumulators and queue a new
 *        LC header on the next feed. Useful when explicit PTT-down
 *        detection is available; otherwise rely on the gap heuristic
 *        inside DmrTxAudioFeed.
 */
void DmrTxAudioReset(void);

#endif /* DMR_TX_AUDIO_H */
