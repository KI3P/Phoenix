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
 * @file DSP_CESSB.cpp
 * @brief Controlled Envelope SSB (W9GR), centred form. See DSP_CESSB.h.
 *
 * Per sample n of a block, with z = I + jQ normalised so the ceiling is 1.0:
 *
 *   x[n]   = z[n] e^{-jwn}                    shift the channel to straddle DC
 *                                             (w < 0: the channel is below DC here)
 *   y      = h * clip(x)                      clip the envelope at 1, lowpass
 *   eMax   = max(|y[n]|, |y[n-1]|, |y[n-2]|, 1)
 *   d[n]   = y[n-2] (1 - 1/eMax)              what a second clip would remove
 *   out[n] = (y[n-D] - g (h * d)[n]) e^{+jwn} remove it, band-limited; shift back
 *
 * with h the channel lowpass, D = 2 + its group delay, and g CESSB_GAIN_COMPENSATE.
 * The clipper and overshoot controller are those of W7PUA's
 * radioCESSB_Z_transmit_F32.cpp (OpenAudio library, MIT licence). The two
 * frequency shifts use the same n, which makes each filter a complex bandpass
 * centred on the channel; |e^{jwn}| = 1, so the shifts do not change what is
 * clipped.
 */

#include "SDT.h"
#include "DSP_CESSB.h"

// Overshoot delay line. The longest delay read from it is CESSB_DELAY_SAMPLES
// minus the clipper filter's own delay, so a power-of-two ring of 64 is enough
// for a 123-tap filter.
#define CESSB_RING 64
#define CESSB_RING_MASK (CESSB_RING - 1)
#define CESSB_OVERSHOOT_DELAY ((CESSB_FIR_TAPS - 1) / 2 + 2)
#if CESSB_OVERSHOOT_DELAY >= CESSB_RING
#error "CESSB delay line is too short for CESSB_FIR_TAPS"
#endif

// The buffers live in OCRAM (DMAMEM), not DTCM. On the Teensy 4.1 the stack is
// whatever DTCM the variables leave, so ~13 KB of plain statics here would come
// straight out of the stack margin (wiki: memory-layout-and-stack). They are
// cleared by ResetCESSB() before use, since DMAMEM is not zeroed at boot, and
// aligned to whole 32-byte cache lines like the other DMAMEM scratch.
#define CESSB_ALIGN __attribute__((aligned(32)))
#define CESSB_STATE_LEN (CESSB_FIR_TAPS + CESSB_BLOCK_SIZE - 1)

static float32_t cessbCoeffs[CESSB_FIR_TAPS];
static float32_t DMAMEM cessbDesign[CESSB_FIR_TAPS + 1] CESSB_ALIGN;

static arm_fir_instance_f32 clipFirI, clipFirQ, osFirI, osFirQ;
static float32_t DMAMEM clipStateI[CESSB_STATE_LEN] CESSB_ALIGN;
static float32_t DMAMEM clipStateQ[CESSB_STATE_LEN] CESSB_ALIGN;
static float32_t DMAMEM osStateI[CESSB_STATE_LEN] CESSB_ALIGN;
static float32_t DMAMEM osStateQ[CESSB_STATE_LEN] CESSB_ALIGN;

static float32_t DMAMEM ringI[CESSB_RING] CESSB_ALIGN;
static float32_t DMAMEM ringQ[CESSB_RING] CESSB_ALIGN;
static uint32_t ringIndex = 0;
static float32_t envPrev1 = 0.0f, envPrev2 = 0.0f;

// Scratch, static rather than on the stack
static float32_t DMAMEM loCos[CESSB_BLOCK_SIZE] CESSB_ALIGN;
static float32_t DMAMEM loSin[CESSB_BLOCK_SIZE] CESSB_ALIGN;
static float32_t DMAMEM delayedI[CESSB_BLOCK_SIZE] CESSB_ALIGN;
static float32_t DMAMEM delayedQ[CESSB_BLOCK_SIZE] CESSB_ALIGN;
static float32_t DMAMEM diffI[CESSB_BLOCK_SIZE] CESSB_ALIGN;
static float32_t DMAMEM diffQ[CESSB_BLOCK_SIZE] CESSB_ALIGN;

static double loPhase = 0.0;      // radians, kept in (-2*pi, 2*pi)
static double loStep = 0.0;       // radians per sample
static bool needsReset = true;

/**
 * Clear the filter and delay-line history. Does not touch the coefficients.
 */
void ResetCESSB(void){
    memset(clipStateI, 0, sizeof(clipStateI));
    memset(clipStateQ, 0, sizeof(clipStateQ));
    memset(osStateI, 0, sizeof(osStateI));
    memset(osStateQ, 0, sizeof(osStateQ));
    memset(ringI, 0, sizeof(ringI));
    memset(ringQ, 0, sizeof(ringQ));
    ringIndex = 0;
    envPrev1 = 0.0f;
    envPrev2 = 0.0f;
    loPhase = 0.0;
    needsReset = false;
}

/**
 * Design the channel lowpass for this node rate and bind the filter instances.
 *
 * The filter is specified in Hz and generated with CalcFIRCoeffs, as the other
 * transmit stages that set the audio bandwidth are, so it holds its corners at
 * 176.4 ksps as well as 192 ksps.
 *
 * The overshoot controller lines its delay line up with the filter's group
 * delay, so the filter must be exactly symmetric about a whole sample.
 * CalcFIRCoeffs does not give that for an odd length (its taps straddle the
 * centre, a 61.5-sample delay for 123 taps); for an even length every tap but
 * the first mirrors about the centre. So design one tap longer and drop the
 * first, which is 44 dB below the peak.
 */
void InitializeCESSB(float32_t nodeFs_Hz){
    CalcFIRCoeffs(cessbDesign, CESSB_FIR_TAPS + 1, CESSB_HALF_BANDWIDTH_HZ,
                  CESSB_FIR_ATTENUATION_DB, Lowpass, 0.0f, nodeFs_Hz);
    memcpy(cessbCoeffs, &cessbDesign[1], sizeof(cessbCoeffs));
    float32_t sum = 0.0f;
    for (uint32_t i = 0; i < CESSB_FIR_TAPS; i++) sum += cessbCoeffs[i];
    if (sum != 0.0f)
        for (uint32_t i = 0; i < CESSB_FIR_TAPS; i++) cessbCoeffs[i] /= sum;

    arm_fir_init_f32(&clipFirI, CESSB_FIR_TAPS, cessbCoeffs, clipStateI, CESSB_BLOCK_SIZE);
    arm_fir_init_f32(&clipFirQ, CESSB_FIR_TAPS, cessbCoeffs, clipStateQ, CESSB_BLOCK_SIZE);
    arm_fir_init_f32(&osFirI, CESSB_FIR_TAPS, cessbCoeffs, osStateI, CESSB_BLOCK_SIZE);
    arm_fir_init_f32(&osFirQ, CESSB_FIR_TAPS, cessbCoeffs, osStateQ, CESSB_BLOCK_SIZE);

    loStep = 2.0 * M_PI * (double)CESSB_CENTRE_HZ / (double)nodeFs_Hz;
    ResetCESSB();
}

bool CESSBActive(void){
    return ED.cessbEnabled && (modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT);
}

void CESSBSkipped(void){
    needsReset = true;
}

void CESSBProcess(DataBlock *data){
    if (needsReset) ResetCESSB();
    float32_t *I = data->I;
    float32_t *Q = data->Q;
    const float32_t toUnit = 1.0f / CESSB_CEILING;

    // Local oscillator for this block, and the shift down, in the same pass
    for (uint32_t n = 0; n < CESSB_BLOCK_SIZE; n++){
        const float32_t c = cosf((float32_t)loPhase);
        const float32_t s = sinf((float32_t)loPhase);
        loCos[n] = c;
        loSin[n] = s;
        loPhase += loStep;
        if (loPhase >= 2.0 * M_PI) loPhase -= 2.0 * M_PI;
        if (loPhase <= -2.0 * M_PI) loPhase += 2.0 * M_PI;

        // (I + jQ)(c - js), normalised so the ceiling is 1
        const float32_t i = I[n] * toUnit;
        const float32_t q = Q[n] * toUnit;
        float32_t xi = i * c + q * s;
        float32_t xq = q * c - i * s;

        // Envelope clip: limit the magnitude, keep the phase
        const float32_t mag = sqrtf(xi * xi + xq * xq);
        if (mag > 1.0f){
            xi /= mag;
            xq /= mag;
        }
        I[n] = xi;
        Q[n] = xq;
    }

    // Remove the clipper's out-of-channel products
    arm_fir_f32(&clipFirI, I, I, CESSB_BLOCK_SIZE);
    arm_fir_f32(&clipFirQ, Q, Q, CESSB_BLOCK_SIZE);

    // Overshoot controller: find what clipping the filtered signal again would
    // remove, so it can be subtracted after the same filtering
    for (uint32_t n = 0; n < CESSB_BLOCK_SIZE; n++){
        ringI[ringIndex & CESSB_RING_MASK] = I[n];
        ringQ[ringIndex & CESSB_RING_MASK] = Q[n];

        const float32_t env = sqrtf(I[n] * I[n] + Q[n] * Q[n]);
        float32_t eMax = 1.0f;
        if (env > eMax) eMax = env;
        if (envPrev1 > eMax) eMax = envPrev1;
        if (envPrev2 > eMax) eMax = envPrev2;
        envPrev2 = envPrev1;
        envPrev1 = env;

        const float32_t i2 = ringI[(ringIndex - 2) & CESSB_RING_MASK];
        const float32_t q2 = ringQ[(ringIndex - 2) & CESSB_RING_MASK];
        const float32_t excess = 1.0f - 1.0f / eMax;
        diffI[n] = i2 * excess;
        diffQ[n] = q2 * excess;

        delayedI[n] = ringI[(ringIndex - CESSB_OVERSHOOT_DELAY) & CESSB_RING_MASK];
        delayedQ[n] = ringQ[(ringIndex - CESSB_OVERSHOOT_DELAY) & CESSB_RING_MASK];
        ringIndex++;
    }
    arm_fir_f32(&osFirI, diffI, diffI, CESSB_BLOCK_SIZE);
    arm_fir_f32(&osFirQ, diffQ, diffQ, CESSB_BLOCK_SIZE);

    // Subtract the band-limited excess, shift back up, restore the level
    for (uint32_t n = 0; n < CESSB_BLOCK_SIZE; n++){
        const float32_t yi = delayedI[n] - CESSB_GAIN_COMPENSATE * diffI[n];
        const float32_t yq = delayedQ[n] - CESSB_GAIN_COMPENSATE * diffQ[n];
        // (yi + j yq)(c + js)
        I[n] = (yi * loCos[n] - yq * loSin[n]) * CESSB_CEILING;
        Q[n] = (yq * loCos[n] + yi * loSin[n]) * CESSB_CEILING;
    }
}
