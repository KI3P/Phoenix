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
#ifndef USBAUDIO_H
#define USBAUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <arm_math.h>

/**
 * USB audio transport for DIGITAL mode.
 *
 * Carries demodulated receive audio to the host and transmit audio back from it,
 * at 44.1 kHz. DIGITAL mode forces the radio to 176.4 ksps, where the receive
 * chain's Fs/4 tap IS 44,100 Hz and one DSP block is exactly 512 samples, so no
 * resampling happens anywhere.
 *
 * When AUDIO_INTERFACE is not defined (any USB type without an audio interface,
 * and the host unit-test build) every function below is a no-op stub, so callers
 * need no conditional compilation.
 *
 * @see USBAudio.cpp for how the stock Teensy USB audio blocks are paced 4:1
 *      against the audio library's graph clock.
 */

/** Number of audio samples exchanged per DSP block (4 x AUDIO_BLOCK_SAMPLES). */
#define USB_AUDIO_BLOCK_SAMPLES 512

/**
 * @brief Take the USB audio objects off the audio library's graph clock
 * @note Must be called once from InitializeAudio(), before any mode change
 * @note Until this runs, update_all() drives the USB audio objects at the I2S
 *       block rate - four times too fast - which churns audio blocks and makes
 *       AudioInputUSB ask the host for samples far faster than we can consume them
 */
void USBAudioInit(void);

/**
 * @brief Start the USB audio transport
 * @note Called on entry to DIGITAL mode, and again after ChangeSampleRate()
 * @note Idempotent; safe to call when already running
 */
void USBAudioBegin(void);

/**
 * @brief Stop the USB audio transport and discard any buffered audio
 * @note Called on exit from DIGITAL mode, and before ChangeSampleRate()
 */
void USBAudioEnd(void);

/**
 * @brief Send demodulated receive audio to the host
 * @param src Audio samples, nominally -1.0 to 1.0
 * @param n Sample count; must be USB_AUDIO_BLOCK_SAMPLES
 * @note Mono; the same samples are sent on both USB channels
 * @note Drops the block rather than blocking if the host is not consuming
 */
void USBAudioWriteRx(const float32_t *src, size_t n);

/**
 * @brief Fetch transmit audio from the host
 * @param dst Destination for the samples, standardized to -1.0 to 1.0
 * @param n Sample count; must be USB_AUDIO_BLOCK_SAMPLES
 * @return true if a full block was available, false on underrun (dst untouched)
 * @note Takes the USB left channel only; digital transmit audio is mono
 */
bool USBAudioReadTx(float32_t *dst, size_t n);

/**
 * @brief Report whether the transport is currently started
 * @return true between USBAudioBegin() and USBAudioEnd()
 * @note Lets ChangeSampleRate() restart the transport only if it was running
 */
bool USBAudioIsRunning(void);

/**
 * @brief Report whether the host has opened the transmit (host to radio) stream
 * @return true if the host has selected a non-zero alternate setting
 */
bool USBAudioHostActive(void);

/**
 * @brief Report the host's playback volume setting for the radio's audio device
 * @return Volume from 0.0 to 1.0, or 1.0 when USB audio is not compiled in
 * @note Provided by the Teensy core's USB audio feature-unit handling
 */
float USBAudioHostVolume(void);

/**
 * @brief Read the core's USB audio under/overrun counters
 * @param underruns Set to the number of times a buffer ran dry, may be NULL
 * @param overruns Set to the number of times a buffer overflowed, may be NULL
 * @note Both should stay flat once streaming is established; a climbing counter
 *       means the 4:1 pacing is wrong or the DSP loop is not keeping up
 */
void USBAudioGetStats(uint32_t *underruns, uint32_t *overruns);

/**
 * @brief Read the transmit-path health counters, reset by USBAudioBegin()
 * @param calls Number of USBAudioReadTx() calls, may be NULL
 * @param underruns Times the record queue held fewer than a DSP block's worth
 * @param trimmed Blocks discarded because the record queue exceeded its cap
 * @param depthMin Lowest record-queue depth seen
 * @param depthMax Highest record-queue depth seen
 * @note Underruns and trims both splice discontinuous audio into the transmit
 *       stream, which shows up on the air as sidebands at the block rate
 */
void USBAudioGetTxStats(uint32_t *calls, uint32_t *underruns, uint32_t *trimmed,
                        uint8_t *depthMin, uint8_t *depthMax);

#endif // USBAUDIO_H
