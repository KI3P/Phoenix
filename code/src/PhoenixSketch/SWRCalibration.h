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
#ifndef SWRCALIBRATION_H
#define SWRCALIBRATION_H

/**
 * Calculations for the SWR bridge calibration (code/docs/swr_calibration_approach.md
 * Sections 6 and 7). Pure functions: no hardware and no globals.
 *
 * Each detector is a straight line in dB of the power on the line:
 *     P [dBm] = V / m + b
 * with V the raw reading in mV, m the slope in mV/dB and b the intercept in dBm.
 * The firmware stores the line as a slope correction s and an offset correction o
 * (LPFBoard.cpp): m = 25 + s and b = o - 38.
 */

#define SWRCAL_NOMINAL_SLOPE_MV_PER_DB 25.0f
#define SWRCAL_OFFSET_CONSTANT_DB      38.0f  // 84 - PAD_ATTENUATION_DB - COUPLER_ATTENUATION_DB
#define SWRCAL_CEILING_RESIDUAL_DB      0.5f  // level 3: keep a ceiling point if it fits this well
#define SWRCAL_CEILING_SPAN_DB          1.0f  // level 3: a point this close to the highest power is at the ceiling
#define SWRCAL_OSC_DROP_FRACTION        0.5f  // V_f must fall by at least this share of the drive drop
#define SWRCAL_OSC_MARGIN_DB            6.0f  // V_f may exceed its 50 Ohm reference by this much: the PA
                                              // puts up to about 5 dB more into an open or short (bench sheets)
#define SWRCAL_UNCALIBRATED_SWR        99.0f  // returned when the reflected power is not below the forward

/** A detector line fitted to readings */
struct SWRLineFit {
    bool valid;          // false if there were too few points or no spread in V
    float32_t m;         // slope in mV/dB
    float32_t b;         // intercept in dBm
    float32_t rms_dB;    // rms residual
    float32_t worst_dB;  // largest absolute residual
};

/** The reflected line fitted to the open and short sweeps */
struct SWRReflectedFit {
    bool valid;          // false if the open sweep could not be fitted
    bool openOnly;       // the short was missing, so the line comes from the open alone
    float32_t m;         // slope in mV/dB
    float32_t b;         // intercept in dBm: the mean of the open and short intercepts
    float32_t b_open;    // intercept from the open
    float32_t b_short;   // intercept from the short (equal to b_open if openOnly)
    float32_t rms_dB;    // rms residual over both sweeps
};

/**
 * @brief Power on the line from a detector reading
 * @param V_mV Raw detector reading in mV
 * @param m Slope in mV/dB
 * @param b Intercept in dBm
 * @return Power in dBm
 */
float32_t SWRCalPower_dBm(float32_t V_mV, float32_t m, float32_t b);

/**
 * @brief Least-squares line through readings and powers
 * @param V_mV Detector readings in mV
 * @param P_dBm Powers on the line in dBm
 * @param n Number of points, at least 2
 * @return The fit; valid is false if n < 2 or the readings have no spread
 */
struct SWRLineFit SWRCalFitLine(const float32_t *V_mV, const float32_t *P_dBm, uint32_t n);

/**
 * @brief Level 3 forward line from a wattmeter sweep, with the ceiling rule
 * @param V_mV Forward detector readings in mV
 * @param P_W Wattmeter readings in W
 * @param att_dB Attenuation of each point
 * @param n Number of points
 * @param ceilingDropped Set true if any ceiling point was left out of the fit
 * @param pointDropped If given, n flags: true for each point left out
 * @param residual_dB If given, n values: each ceiling point's residual from the
 *        line through the others (0 for the other points), so the caller can log
 *        why a point was left out
 * @return The fit (Section 7, extended). The ceiling points are those at 0 dB
 *         and those whose wattmeter reading is within SWRCAL_CEILING_SPAN_DB of
 *         the highest: the PA is saturated there, and on radio 1's 10 m the
 *         forward detector over-reads all through saturation. The line is fitted
 *         without them; each is then kept only if its residual is within
 *         SWRCAL_CEILING_RESIDUAL_DB.
 */
struct SWRLineFit SWRCalFitForwardLevel3(const float32_t *V_mV, const float32_t *P_W,
                                         const float32_t *att_dB, uint32_t n, bool *ceilingDropped,
                                         bool *pointDropped = nullptr, float32_t *residual_dB = nullptr);

/**
 * @brief Level 2 forward intercept from one wattmeter reading, nominal slope
 * @param P_W Wattmeter reading in W
 * @param V_mV Forward detector reading at the same time, in mV
 * @return Intercept b_f in dBm
 */
float32_t SWRCalForwardInterceptLevel2(float32_t P_W, float32_t V_mV);

/**
 * @brief Level 1 default forward offset o_f for a band (Section 2)
 * @param band BAND_160M to BAND_6M
 * @param turns L44 turns per core
 * @return o_f in dB: measured on 160, 40, 20 and 10 m for 10 turns,
 *         interpolated in log frequency for the other bands, 6 m as 10 m,
 *         plus 20 log10(turns/10)
 */
float32_t SWRCalLevel1ForwardOffset(int32_t band, uint8_t turns);

/**
 * @brief Calibration frequency of a band (Section 6)
 * @param band BAND_160M to BAND_6M
 * @return Frequency in MHz, or 0 for a band without an LPF of its own
 */
float32_t SWRCalBandFrequency_MHz(int32_t band);

/**
 * @brief Reflected line from the open and short sweeps (Section 7)
 * @param Vf_open,Vr_open Forward and reflected readings on the open, in mV
 * @param n_open Number of open points
 * @param Vf_short,Vr_short Forward and reflected readings on the short, in mV
 * @param n_short Number of short points; fewer than 2 fits the open alone
 * @param m_f,b_f The band's forward line
 * @return One slope fitted to both standards, with an intercept for each; the
 *         stored intercept is their mean, which cancels the bridge's leak
 */
struct SWRReflectedFit SWRCalFitReflected(const float32_t *Vf_open, const float32_t *Vr_open, uint32_t n_open,
                                          const float32_t *Vf_short, const float32_t *Vr_short, uint32_t n_short,
                                          float32_t m_f, float32_t b_f);

/**
 * @brief SWR from one pair of readings
 * @return The SWR, or SWRCAL_UNCALIBRATED_SWR if the reflected power is not
 *         below the forward power
 */
float32_t SWRCalSWR(float32_t Vf_mV, float32_t Vr_mV, float32_t m_f, float32_t b_f,
                    float32_t m_r, float32_t b_r);

/**
 * @brief The oscillation check after an open or short point (Section 6)
 * @param hasPrevious There is a previous point on this sweep
 * @param Vf_previous Its forward reading, in mV
 * @param drop_dB How much lower this point's drive is, in dB
 * @param Vf Forward reading of this point, in mV
 * @param hasReference There is a reference reading for this drive
 * @param Vf_reference Forward reading into 50 Ohm at this drive, in mV
 * @return true if the PA looks to be oscillating: V_f fell by less than half
 *         the drive drop, or exceeds the reference by more than
 *         SWRCAL_OSC_MARGIN_DB. The caller gives a reference only for a sweep's
 *         first point, which has no previous point to fall from.
 */
bool SWRCalOscillating(bool hasPrevious, float32_t Vf_previous, float32_t drop_dB, float32_t Vf,
                       bool hasReference, float32_t Vf_reference);

/** Which of the oscillation check's rules a point failed */
enum SWRCalOscillationRule {
    SWRCAL_OSC_NONE = 0,          // no oscillation
    SWRCAL_OSC_NO_FALL,           // V_f fell by less than half the drive drop
    SWRCAL_OSC_ABOVE_REFERENCE,   // V_f above the 50 Ohm reference by more than the margin
};

/**
 * @brief The oscillation check, saying which rule failed
 * @return SWRCAL_OSC_NONE, or the first rule the point failed; arguments as
 *         for SWRCalOscillating()
 */
enum SWRCalOscillationRule SWRCalOscillationCheck(bool hasPrevious, float32_t Vf_previous, float32_t drop_dB,
                                                  float32_t Vf, bool hasReference, float32_t Vf_reference);

/**
 * @brief The median of a set of values
 * @param values The values; reordered by the call
 * @param n Number of values
 * @return The median, or 0 if n is 0
 */
float32_t SWRCalMedian(float32_t *values, uint32_t n);

#endif // SWRCALIBRATION_H
