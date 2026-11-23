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
#ifndef POWERMETER_H
#define POWERMETER_H

/**
 * Transmit power meter: PEP and average power on SSB voice.
 *
 * Two sources, combined:
 *  - Digital. The SSB chain is calibrated: an envelope of CESSB_CEILING at the
 *    CESSB node is ED.powerOutSSB watts (DSP_CESSB.h). So every I/Q sample after
 *    TXGain() is an instantaneous power. TransmitProcessing() hands each block's
 *    peak and sum of that power to PowerMeterAddDigitalBlock(). This gives an
 *    exact PEP and an exact average power, but it cannot see PA compression, a
 *    wrong band calibration, or the load.
 *  - Bridge. The AD8307 detectors follow the RF envelope, and the AD7991 takes
 *    one pair of readings about every 16 ms, at random points of the speech. The
 *    mean of those readings *in watts* is the true average power whatever the
 *    envelope. The mean in dB, which the firmware used to take, reads 3 dB low on
 *    two tones and 5-20 dB low on speech (code/tools/power_meter_eval).
 *    The samples are too sparse for a PEP: a bridge peak hold misses it by ~2 dB.
 *
 * The meter shows the digital PEP and average, scaled by a correction k: the
 * ratio of the bridge average to the digital average over the same second, in dB,
 * followed with a time constant of PM_K_TAU_MS and kept per band and PA. k learns
 * only from fully keyed seconds of steady carrier (PEP/AVG below PM_K_MAX_PAPR_DB:
 * tune, a single tone, FT8), because the bridge reads a fast-moving envelope low
 * (see FinishBin() in PowerMeter.cpp). On a correctly calibrated band into a
 * linear PA, k stays near 0 dB; a compressing PA pulls it down.
 *
 * Time is binned into PM_BIN_MS bins. PEP is the highest instantaneous power over
 * the last PM_PEP_BINS bins (1.5 s hold, then it falls); the average is over the
 * last PM_AVG_BINS bins (1 s).
 */

#define PM_BIN_MS          100     // bin width
#define PM_NBINS           16      // ring size: at least PM_PEP_BINS + 1
#define PM_PEP_BINS        15      // PEP hold: 1.5 s
#define PM_AVG_BINS        10      // average window: 1 s
#define PM_K_TAU_MS        1000    // time constant of the bridge correction, of steady carrier
#define PM_K_LIMIT_DB      3.0f    // the correction is clamped to this
#define PM_K_MIN_AVG_W     0.2f    // track k only when the digital average is above this
#define PM_K_MIN_BRIDGE_N  20      // ... and the window holds this many bridge samples
#define PM_K_MAX_PAPR_DB   1.0f    // ... and the window is a steady carrier: PEP/AVG below this
#define PM_SWR_MIN_W       0.2f    // SWR from windows whose bridge average is above this
#define PM_FLAG_DB         1.5f    // |k| above this: the display flags the reading
#define PM_CONTEXTS        32      // band x PA slots for k (NUMBER_OF_BANDS * 2 <= this)

/** Clear the bins, every stored correction and the held SWR. */
void PowerMeterReset(void);

/**
 * @brief Select the band and PA the following samples belong to
 * @note A change clears the bins (the old band's readings say nothing about the
 *       new one) and selects that band's correction.
 */
void PowerMeterSetContext(int32_t band, bool pa100W);

/**
 * @brief Watts per unit |IQ|^2 after TXGain()
 * @param calPoint_W The power the band's calibration tone is set to
 *        (SSB_20W_CAL_POWER_POINT_W or SSB_100W_CAL_POWER_POINT_W)
 * @param correction_dB The band's PowerCal_*_DSP_Gain_correction_dB
 * @param ceiling The calibration tone's envelope at the CESSB node (CESSB_CEILING)
 */
float32_t PowerMeterDigitalScale(float32_t calPoint_W, float32_t correction_dB, float32_t ceiling);

/**
 * @brief Peak and sum of the instantaneous power of a block of I/Q samples
 * @param scale Watts per unit |IQ|^2, from PowerMeterDigitalScale()
 * @param peak_W Out: the highest instantaneous power
 * @param sum_W Out: the sum of the instantaneous powers
 */
void PowerMeterBlockPower(const float32_t *I, const float32_t *Q, uint32_t n, float32_t scale,
                          float32_t *peak_W, float32_t *sum_W);

/** @brief Add one block of the digital meter (see PowerMeterBlockPower()) */
void PowerMeterAddDigitalBlock(float32_t peak_W, float32_t sum_W, uint32_t n, uint32_t now_ms);

/** @brief Add one pair of bridge readings, each already converted to watts */
void PowerMeterAddBridgeSample(float32_t fwd_W, float32_t ref_W, uint32_t now_ms);

/** @brief True if digital samples arrived within the PEP hold: SSB or digital-mode TX */
bool PowerMeterHasDigital(uint32_t now_ms);

/** @brief PEP over the last 1.5 s, corrected by the bridge; 0 without digital samples */
float32_t PowerMeterPEP_W(uint32_t now_ms);

/** @brief Average power over the last 1 s, corrected by the bridge */
float32_t PowerMeterAvg_W(uint32_t now_ms);

/** @brief Bridge forward power, averaged in watts over the last 1 s */
float32_t PowerMeterBridgeAvg_W(uint32_t now_ms);

/**
 * @brief SWR from the 1 s averages of the forward and reflected powers
 * @note Both detectors see the same envelope, so the ratio of the averages is
 *       |Gamma|^2 during speech too. Below PM_SWR_MIN_W of forward power the last
 *       good value is held; 1.0 before there is one.
 */
float32_t PowerMeterSWR(uint32_t now_ms);

/** @brief The bridge correction k for the current band and PA, dB */
float32_t PowerMeterCorrection_dB(void);

#endif // POWERMETER_H
