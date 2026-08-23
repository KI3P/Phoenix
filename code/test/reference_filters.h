/**
 * Reference copies of the filter coefficient tables Phoenix used to ship.
 * See reference_filters.cpp for what these are and why they are kept.
 *
 * Test-only. Not part of the firmware build.
 */

#ifndef REFERENCE_FILTERS_H
#define REFERENCE_FILTERS_H

#include "arm_math.h"

/** CW audio lowpass filters: 12 pole Chebyshev type I, 0.02 dB ripple, 24 ksps.
 *  6 biquad sections of {b0, b1, b2, -a1, -a2}. Labelled cutoffs, in order:
 *  840, 1080, 1320, 1800 and 2000 Hz. */
extern const float32_t CW_AudioFilterCoeffs1_ref[30];
extern const float32_t CW_AudioFilterCoeffs2_ref[30];
extern const float32_t CW_AudioFilterCoeffs3_ref[30];
extern const float32_t CW_AudioFilterCoeffs4_ref[30];
extern const float32_t CW_AudioFilterCoeffs5_ref[30];

/** CW decoder input filter: 64 tap Parks-McClellan lowpass, 24 ksps.
 *  -3 dB at 1562 Hz, -6 dB at 1749 Hz, DC gain 0.9992. */
extern const float32_t CW_Filter_Coeffs2_ref[64];

/** Audio equaliser cells: 4 stagger-tuned bandpass biquads each, 24 ksps,
 *  peak normalised to unity. Centres run 198.425 Hz to 4000 Hz. */
extern const float32_t EQ_Band1Coeffs_ref[20];
extern const float32_t EQ_Band2Coeffs_ref[20];
extern const float32_t EQ_Band3Coeffs_ref[20];
extern const float32_t EQ_Band4Coeffs_ref[20];
extern const float32_t EQ_Band5Coeffs_ref[20];
extern const float32_t EQ_Band6Coeffs_ref[20];
extern const float32_t EQ_Band7Coeffs_ref[20];
extern const float32_t EQ_Band8Coeffs_ref[20];
extern const float32_t EQ_Band9Coeffs_ref[20];
extern const float32_t EQ_Band10Coeffs_ref[20];
extern const float32_t EQ_Band11Coeffs_ref[20];
extern const float32_t EQ_Band12Coeffs_ref[20];
extern const float32_t EQ_Band13Coeffs_ref[20];
extern const float32_t EQ_Band14Coeffs_ref[20];

/** Transmit interpolate-by-2 (12k -> 24k). This is what set the transmit audio
 *  bandwidth: 48 tap Kaiser lowpass, -3 dB at 2759 Hz when run at 24 ksps. */
extern const float32_t FIR_int3_12ksps_48tap_2k7_ref[48];

/** Transmit decimate-by-2 (24k -> 12k). Note this table is flat to 0.425 * Fs,
 *  well past the 0.25 * Fs a decimate-by-2 stage needs, so it aliases. Kept for
 *  the before/after comparison, not as a target to reproduce. */
extern const float32_t coeffs12K_8K_LPF_FIR_ref[48];

#endif  // REFERENCE_FILTERS_H
