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

/**
 * @file DSP_CESSB.h
 * @brief Controlled Envelope SSB (W9GR) for the SSB transmit chain
 *
 * Limits the envelope of the transmit I/Q to the power the operator asked for,
 * without the splatter of a hard clip. Runs at the Fs/8 node of
 * TransmitProcessing(), between TXInterpolateBy2Again() and TXGain().
 *
 * This is the "centred" form: the SSB channel is shifted down so it straddles
 * DC, clipped and filtered, and shifted back. The clipping is the same as W7PUA's
 * zero-IF radioCESSB_Z_transmit_F32; centring lets the lowpass filters remove
 * clip products in the opposite sideband too. See wiki/roadmap/cessb.md and
 * code/tools/cessb_model/ for the reasoning and the numbers.
 */

#ifndef DSP_CESSB_H
#define DSP_CESSB_H
#include "SDT.h"

/** Samples per call: one TransmitProcessing() block at the Fs/8 node. */
#define CESSB_BLOCK_SIZE 256

/** Length of the clipper and overshoot lowpass filters (odd, linear phase). */
#define CESSB_FIR_TAPS 123

/** Centre of the SSB channel at the node, signed. The audio sits on negative
 *  frequencies here whatever the sideband: arm_fir_f32 takes its coefficients
 *  time-reversed, which puts the Hilbert pair's output on the LSB side, and
 *  SidebandSelection() only flips it for USB later.
 *  Pinned by TransmitChainCESSB.ChannelIsWhereTheStageCentresIt. */
#define CESSB_CENTRE_HZ (-1500.0f)

/** -6 dB half-width of the channel lowpass: 50 to 2950 Hz once shifted back. */
#define CESSB_HALF_BANDWIDTH_HZ 1450.0f

/** Stopband asked of the channel lowpass. A gentler filter rings less, so the
 *  overshoot controller has less to do (1.3 % against 1.5 % at 60 dB). */
#define CESSB_FIR_ATTENUATION_DB 40.0f

/** Overshoot over-correction. Hershberger's value for the Weaver form, which
 *  the centred form behaves like; 1.4 (W7PUA's Z class) left 4 % overshoot. */
#define CESSB_GAIN_COMPENSATE 1.9f

/** Envelope ceiling at the node, before TXGain.
 *
 *  This is the envelope the SSB power calibration tone (800 Hz, amplitude 0.08
 *  into the chain, default transmit equaliser) has at the node. TXGain maps
 *  that tone to the requested power, so a ceiling here is a ceiling on PEP at
 *  ED.powerOutSSB. Pinned by TransmitChainCESSB.CeilingIsTheCalibrationTone. */
#define CESSB_CEILING 0.1295f

/** Samples of delay the stage adds: the clipper filter's group delay plus
 *  the overshoot controller's delay line. */
#define CESSB_DELAY_SAMPLES ((CESSB_FIR_TAPS - 1) / 2 + (CESSB_FIR_TAPS - 1) / 2 + 2)

/**
 * @brief Design the CESSB filters for the node rate and clear the stage's state
 * @param nodeFs_Hz Sample rate of the node, Fs/8
 * @note Called from InitializeTransmitFilters(), so it runs at startup and on
 *       every sample rate change.
 */
void InitializeCESSB(float32_t nodeFs_Hz);

/**
 * @brief Clear the stage's filter and delay-line history
 * @note Also done automatically the first time CESSBProcess() runs after a block
 *       it was not asked to process, so a new transmission never starts with the
 *       tail of the previous one.
 */
void ResetCESSB(void);

/**
 * @brief True if the CESSB stage should process the current transmit block
 * @return true in SSB_TRANSMIT with ED.cessbEnabled set; false otherwise
 * @note Digital mode and the calibration states need a linear chain, so they
 *       never run it.
 */
bool CESSBActive(void);

/**
 * @brief Apply CESSB to one block at the Fs/8 node, in place
 * @param data CESSB_BLOCK_SIZE complex samples (data->I, data->Q)
 */
void CESSBProcess(DataBlock *data);

/**
 * @brief Note that a transmit block went by without CESSBProcess()
 * @note The next CESSBProcess() call clears the stage's history first.
 */
void CESSBSkipped(void);

#endif // DSP_CESSB_H
