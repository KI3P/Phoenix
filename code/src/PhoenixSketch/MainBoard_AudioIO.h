#ifndef AUDIOIO_H
#define AUDIOIO_H
#include "SDT.h"

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <OpenAudio_ArduinoLibrary.h>

#define SIDETONE_FREQUENCY 100

/**
 * @brief Configure I2S sample rate via PLL clock calculations
 * @param freq Desired sample rate in Hz (typically 48000, 96000, or 192000)
 * @return Configured frequency in Hz, or 0 if freq exceeds hardware limits
 * @note Calculates PLL divisors (n1, n2, c0, c1, c2) for Teensy 4.1 audio subsystem
 * @note Configures both SAI1 and SAI2 peripherals for quad I2S operation
 * @note PLL must operate between 648 MHz (27*24) and 1296 MHz (54*24)
 */
int SetI2SFreq(int freq);

/**
 * @brief Initialize dual-codec audio subsystem for Phoenix SDR
 * @note Configures SGTL5000 codecs for transmit path (Teensy Audio Board) and receive path (main board)
 * @note Allocates audio memory: 500 blocks for int16 samples, 10 blocks for float32 samples
 * @note Initializes sidetone generator for CW monitoring (600-800 Hz sine wave)
 * @note Calls WarmUpAudioIO() to clear I2S/codec initialization issues
 * @note Must be called during radio initialization before entering main loop
 */
void InitializeAudio(void);

/**
 * @brief Reconfigure audio routing based on current radio mode state
 * @note Responds to ModeSm state changes by reconfiguring mixers and queues
 * @note SSB_RECEIVE/CW_RECEIVE: Routes RX I/Q → DSP → speaker
 * @note SSB_TRANSMIT: Routes microphone → DSP → TX I/Q
 * @note CW_TRANSMIT_*_MARK: Routes sidetone → speaker (RF keying handled by RFBoard)
 * @note Tracks previousAudioIOState to avoid redundant reconfiguration
 * @note Should be called from main loop when mode transitions occur
 */
void UpdateAudioIOState(void);

/**
 * @brief Get the previous ModeSm state for which audio routing was configured
 * @return ModeSm_StateId that audio routing was last configured for
 * @note Used to detect state changes and avoid unnecessary audio graph reconfiguration
 */
ModeSm_StateId GetAudioPreviousState(void);

/**
 * @brief Apply current microphone gain setting to transmit codec
 * @note Applies ED.currentMicGain to SGTL5000 codec on Teensy Audio Board
 * @note Called when user adjusts mic gain or when transitioning to SSB transmit mode
 */
void UpdateTransmitAudioGain(void);

/**
 * @brief Warm up audio I/O by cycling transmit/receive routing without RF changes
 * @note Clears I2S and SGTL5000 codec initialization issues that cause anomalous output on first PTT press
 * @note Performs two complete transmit/receive cycles with 50ms delays for audio interrupt processing
 * @note Called once during radio initialization, after InitializeAudio()
 * @note Does not change RF hardware state - only exercises audio routing
 */
void WarmUpAudioIO(void);

#endif // AUDIOIO_H