#ifndef TUNE_H
#define TUNE_H

/**
 * @brief Get effective TX/RX frequency for active VFO
 * @return Effective TX/RX frequency in deci-Hertz (Hz × 100)
 * @note Combines center frequency, fine tune offset, and sample rate quarter-shift
 * @note Formula: (centerFreq - fineTune - sampleRate/4) × 100
 */
int64_t GetTXRXFreq_dHz(void);

/**
 * @brief Get effective TX/RX frequency for specified VFO
 * @param vfo VFO selector (0 or 1)
 * @return Effective TX/RX frequency in Hz
 * @note Combines center frequency, fine tune offset, and sample rate quarter-shift
 * @note Formula: centerFreq - fineTune - sampleRate/4
 */
int64_t GetTXRXFreq(uint8_t vfo);

/**
 * @brief Get CW transmit frequency with tone offset applied
 * @return CW transmit frequency in deci-Hertz (Hz × 100)
 * @note Adds/subtracts CW tone offset from TX/RX frequency
 * @note Offset direction depends on sideband (LSB subtracts, USB adds)
 */
int64_t GetCWTXFreq_dHz(void);

/**
 * @brief Determine amateur radio band containing given frequency
 * @param freq Frequency to check in Hz
 * @return Band index (FIRST_BAND to LAST_BAND) if found, -1 if outside all bands
 * @note Searches through all defined amateur radio bands
 */
int8_t GetBand(int64_t freq);

/**
 * @brief Adjust fine tune frequency with optional fast tune acceleration
 * @param filter_change Tuning increment direction and magnitude (positive or negative)
 * @note Implements fast tune mode for rapid frequency changes with repeated steps
 * @note Enforces limits based on sample rate, zoom level, and filter bandwidth
 * @note Prevents tuning beyond band edges with filter bandwidth margin
 */
void AdjustFineTune(int32_t filter_change);

/**
 * @brief Reset fine tune offset to zero
 * @note Moves fine tune offset into center frequency to maintain same TX/RX frequency
 * @note Used to recenter the tuning display while keeping same operating frequency
 */
void ResetTuning(void);

#endif // TUNE_H