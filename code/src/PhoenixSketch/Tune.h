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

/**
 * @brief Result structure for power curve fitting
 */
struct FitResult {
    float32_t P_sat;        // Saturation power in mW
    float32_t k;            // Drive ratio parameter
    int32_t iterations;     // Number of iterations performed
    float32_t rms_error;    // RMS error of fit
};

struct FitResult FitPowerCurve(float32_t *att_dB, float32_t *pout_mW, int32_t Npoints,
                    float32_t P_sat_init, float32_t k_init);
float32_t CalculateCWPowerLevel(float32_t atten_dB, int8_t PAsel);
float32_t CalculateCWAttenuation(float32_t Power_W, bool *PAsel);
float32_t CalculateSSBTXGain(float32_t Power_W, bool *PAsel);

// Low-level conversion functions for power calibration
float32_t attenToPower_mW(float32_t att_dB, float32_t P_sat_mW, float32_t k);
float32_t powerToAtten_dB(float32_t power_mW, float32_t P_sat_mW, float32_t k);

#endif // TUNE_H