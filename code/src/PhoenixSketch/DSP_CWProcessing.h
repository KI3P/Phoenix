#ifndef DSP_CWPROCESSING_H
#define DSP_CWPROCESSING_H
#include "SDT.h"

#define DECODER_BUFFER_SIZE 128  // Max chars in binary search string with , . ?
#define LOWEST_ATOM_TIME 20      // 60WPM has an atom of 20ms
#define HISTOGRAM_ELEMENTS 750
#define ADAPTIVE_SCALE_FACTOR 0.8                             // The amount of old histogram values are preserved
#define SCALE_CONSTANT (1.0 / (1.0 - ADAPTIVE_SCALE_FACTOR))  // Insure array has enough observations to scale

/**
 * @brief Initialize CW processing subsystem
 * @param wpm Words per minute for Morse code timing
 * @param RXfilters Pointer to receive filter configuration
 * @return Pointer to internal CW processing buffer
 * @note Sets up Goertzel tone detector, histogram analyzers, and Morse decoder
 */
float32_t * InitializeCWProcessing(uint32_t wpm, ReceiveFilterConfig *RXfilters);

/**
 * @brief Process received CW signal to extract Morse code
 * @param data Pointer to DataBlock containing received audio
 * @param RXfilters Pointer to receive filter configuration
 * @note Applies CW audio filter and feeds samples to Morse decoder
 */
void DoCWReceiveProcessing(DataBlock *data, ReceiveFilterConfig *RXfilters);

/**
 * @brief Compute magnitude of specific frequency using Goertzel algorithm
 * @param numSamples Number of audio samples to analyze
 * @param TARGET_FREQUENCY CW tone frequency to detect in Hz
 * @param SAMPLING_RATE Audio sampling rate in Hz
 * @param data Pointer to audio sample array
 * @return Magnitude of detected tone (0.0 = no tone, higher = stronger tone)
 * @note Efficient single-frequency DFT for CW tone detection
 */
float32_t goertzel_mag(uint32_t numSamples, int32_t TARGET_FREQUENCY, uint32_t SAMPLING_RATE, float32_t *data);

/**
 * @brief Decode Morse code from audio envelope
 * @param audioValue Current audio envelope value (0=space, 1=mark)
 * @note Implements adaptive Morse decoder with automatic speed tracking
 * @note Updates internal Morse character buffer when characters decoded
 */
void DoCWDecoding(uint8_t audioValue);

/**
 * @brief Update histogram of gap (space) durations
 * @param gapLen Duration of space in milliseconds
 * @note Analyzes inter-element and inter-character spacing patterns
 * @note Used for adaptive dit/dah timing estimation
 */
void DoGapHistogram(int64_t gapLen);

/**
 * @brief Set Morse code dit length based on WPM
 * @param wpm Words per minute (5-60 typical range)
 * @note Standard timing: 1 dit = 1200ms / WPM
 * @note Configures decoder timing for specified speed
 */
void SetDitLength(uint32_t wpm);

/**
 * @brief Find maximum value in clustered histogram data
 * @param array Pointer to histogram array
 * @param elements Number of histogram bins
 * @param maxCount Output: count of most common value
 * @param maxIndex Output: bin index of most common value
 * @param firstNonZero Output: first non-zero bin index
 * @param spread Clustering tolerance (bins within spread are grouped)
 * @note Used to find dominant dit/dah/space durations from timing histograms
 */
void JackClusteredArrayMax(int32_t *array, int32_t elements, int32_t *maxCount, int32_t *maxIndex, int32_t *firstNonZero, int32_t spread);

/**
 * @brief Update histogram of signal (mark) durations
 * @param val Duration of mark in milliseconds
 * @note Analyzes dit and dah duration patterns
 * @note Used for adaptive dit/dah timing estimation
 */
void DoSignalHistogram(int64_t val);

/**
 * @brief Reset all timing histograms
 * @note Clears signal and gap histograms for fresh decoder adaptation
 * @note Called when CW speed changes significantly
 */
void ResetHistograms(void);

/**
 * @brief Apply narrow bandpass filter optimized for CW reception
 * @param data Pointer to DataBlock containing received audio
 * @param RXfilters Pointer to receive filter configuration (contains CW filter index)
 * @note Implements very narrow filter (50-500 Hz typical) centered on CW tone
 * @note Improves SNR dramatically in presence of adjacent signals
 */
void CWAudioFilter(DataBlock *data, ReceiveFilterConfig *RXfilters);

/**
 * @brief Check if CW decoder has synchronized to incoming Morse
 * @return true if decoder is locked to timing, false if still adapting
 * @note Decoder analyzes timing statistics to determine lock status
 */
bool IsCWDecodeLocked(void);

/**
 * @brief Get pointer to decoded Morse character buffer
 * @return Pointer to null-terminated string of decoded characters
 * @note Buffer contains most recently decoded Morse characters
 */
char *GetMorseCharacterBuffer(void);

/**
 * @brief Check if Morse character buffer has new data
 * @return true if new characters have been decoded since last check
 * @note Allows display update only when new characters available
 */
bool IsMorseCharacterBufferUpdated(void);

#endif // DSP_CWPROCESSING_H
