#include "DSP_FFT.h"

float32_t DMAMEM buffer_spec_FFT[2*SPECTRUM_RES] __attribute__((aligned(4))); /** Used by multiple functions */
float32_t DMAMEM iFFT_buffer[2*SPECTRUM_RES] __attribute__((aligned(4)));
float32_t DMAMEM FFT_spec[SPECTRUM_RES];
float32_t DMAMEM FFT_spec_old[SPECTRUM_RES];

float32_t DMAMEM FIR_filter_mask[FFT_LENGTH * 2] __attribute__((aligned(4)));
static float32_t DMAMEM last_sample_buffer_L[FFT_LENGTH];
static float32_t DMAMEM last_sample_buffer_R[FFT_LENGTH];
// Defined as static because we want their values to persist between calls
static float32_t DMAMEM FFT_ring_buffer_x[SPECTRUM_RES];
static float32_t DMAMEM FFT_ring_buffer_y[SPECTRUM_RES];
static uint32_t zoom_sample_ptr; /** Tracks the current position in the ring buffers */
static uint32_t iFSF;
static float32_t audioPowerMax;

// These coefficients were derived by measurement, but they are approximately given by
// (float32_t)(1 << spectrum_zoom) * (0.5/((float32_t)(1<<spectrum_zoom))^2.3 + 0.5)
// = 2^(-spectrum_zoom - 1) + 2^(+spectrum_zoom - 1)
//static const float32_t zoomMultiplierCoeff[5] = {1.0, 1.21902468, 2.07876308, 3.98758528, 7.88521392};
static const float32_t zoomMultiplierCoeff[5] = {1.0, 1.0, 1.0, 1.0, 1.0};

extern float32_t* mag_coeffs[]; // in DSP_FIR.cpp

/**
 * Get pointer to the filtered FFT buffer (for unit testing)
 * @return Pointer to iFFT_buffer array
 */
float32_t * GetFilteredBufferAddress(void){
    return iFFT_buffer;
}

/**
 * Get the maximum audio power from the last FFT processing
 * @return Maximum power spectral value from most recent audio spectrum calculation
 */
float32_t GetAudioPowerMax(void){
    return audioPowerMax;
}

/**
  * Fast algorithm for log10.
  *   This is a fast approximation to log2()
  *   Y = C[0]*F*F*F + C[1]*F*F + C[2]*F + C[3] + E;
  *   log10f is exactly log2(x)/log2(10.0f);
  *   Math_log10f_fast(x) =(log2f_approx(x)*0.3010299956639812f)
  * 
  * @param X The input X to log10(X)
  * @return The fast approximation for log10(X)
  */
float32_t log10f_fast(float32_t X) {
    float Y, F;
    int E;
    F = frexpf(fabsf(X), &E);
    Y = 1.23149591368684f;
    Y *= F;
    Y += -4.11852516267426f;
    Y *= F;
    Y += 6.02197014179219f;
    Y *= F;
    Y += -3.13396450166353f;
    Y += E;
    return (Y * 0.3010299956639812f);
}

/**
 * Zero the arrays used by the PSD calculations 
 */
void ResetPSD(void){
    for (size_t x = 0; x < SPECTRUM_RES; x++) {
        FFT_spec[x] = 0;
        FFT_spec_old[x] = 0;
        psdnew[x] = 0;
    }
}

/**
 * Calculate a 512-point power spectrum from the complex data stored in real 
 * and imag arrays. A Hanning window is applied to the data. The result is
 * written into the global array psdnew. The value of the data in the psd buffers
 * is log10( I*I + Q*Q ). So the units are log10(V^2 / Hz).
 * 
 * Note that this function requires that there are at least 512 samples in the
 * arrays being passed
 * 
 * @param *I Pointer to the array containing the real part of the samples
 * @param *Q Pointer to the array containing the imag part of the samples
 */
void CalcPSD512(float32_t *I, float32_t *Q)
{
    // interleave real and imaginary input values [real, imag, real, imag . . .]
    for (size_t i = 0; i < SPECTRUM_RES; i++) { 
        // Apply a Hanning window function
        buffer_spec_FFT[i * 2] =      I[i] * (0.5 - 0.5 * cos(TWO_PI * i / SPECTRUM_RES)); 
        buffer_spec_FFT[i * 2 + 1] =  Q[i] * (0.5 - 0.5 * cos(TWO_PI * i / SPECTRUM_RES));
    }
    // perform complex FFT
    // calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
    FFT512Forward(buffer_spec_FFT);

    // calculate magnitudes and put into FFT_spec
    // we do not need to calculate magnitudes with square roots, it would seem to be sufficient to
    // calculate mag = I*I + Q*Q, because we are doing a log10-transformation later anyway
    // and simultaneously put them into the right order
    // 38.50%, saves 0.05% of processor power and 1kbyte RAM ;-)
    for (size_t i = 0; i < SPECTRUM_RES/2; i++) {
        FFT_spec[i + SPECTRUM_RES/2] = (buffer_spec_FFT[i * 2] * buffer_spec_FFT[i * 2] + buffer_spec_FFT[i * 2 + 1] * buffer_spec_FFT[i * 2 + 1]);
        FFT_spec[i]                  = (buffer_spec_FFT[(i + SPECTRUM_RES/2) * 2] * buffer_spec_FFT[(i + SPECTRUM_RES/2)  * 2] + buffer_spec_FFT[(i + SPECTRUM_RES/2)  * 2 + 1] * buffer_spec_FFT[(i + SPECTRUM_RES/2)  * 2 + 1]);
    }

    // apply spectrum AGC
    float32_t LPFcoeff = 0.7;
    for (size_t x = 0; x < SPECTRUM_RES; x++) {
      FFT_spec[x] = LPFcoeff * FFT_spec[x] + (1-LPFcoeff) * FFT_spec_old[x];
      FFT_spec_old[x] = FFT_spec[x];
    }

    // scale the magnitude values and convert to int for spectrum display
    for (size_t i = 0; i < SPECTRUM_RES; i++) {
        psdnew[i] = log10f_fast(FFT_spec[i]);
    }
    // this bin is always nan for some reason
    // TODO: figure out why
    //psdnew[170] = (psdnew[170-1]+psdnew[170+1])/2; 
    psdupdated = true;
}

/**
 * Calculate a 256-point power spectrum from complex I/Q data
 * @param I Pointer to array containing real (I) samples
 * @param Q Pointer to array containing imaginary (Q) samples
 *
 * Similar to CalcPSD512 but operates on 256 samples. Applies Hanning window,
 * performs 256-point FFT, calculates magnitudes, applies spectrum AGC, and
 * converts to log scale. Result written to first half of psdnew array.
 */
void CalcPSD256(float32_t *I, float32_t *Q)
{
    // interleave real and imaginary input values [real, imag, real, imag . . .]
    for (size_t i = 0; i < SPECTRUM_RES/2; i++) { 
        // Apply a Hanning window function
        buffer_spec_FFT[i * 2] =      I[i] * (0.5 - 0.5 * cos(TWO_PI * i / SPECTRUM_RES/2)); 
        buffer_spec_FFT[i * 2 + 1] =  Q[i] * (0.5 - 0.5 * cos(TWO_PI * i / SPECTRUM_RES/2));
    }
    // perform complex FFT
    // calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
    FFT256Forward(buffer_spec_FFT);
    for (size_t i = 0; i < SPECTRUM_RES/4; i++) {
        FFT_spec[i + SPECTRUM_RES/4] = (buffer_spec_FFT[i * 2] * buffer_spec_FFT[i * 2] + buffer_spec_FFT[i * 2 + 1] * buffer_spec_FFT[i * 2 + 1]);
        FFT_spec[i]                  = (buffer_spec_FFT[(i + SPECTRUM_RES/4) * 2] * buffer_spec_FFT[(i + SPECTRUM_RES/4)  
                    * 2] + buffer_spec_FFT[(i + SPECTRUM_RES/4)  * 2 + 1] * buffer_spec_FFT[(i + SPECTRUM_RES/4)  * 2 + 1]);
    }

    // apply spectrum AGC
    float32_t LPFcoeff = 0.7;
    for (size_t x = 0; x < SPECTRUM_RES/2; x++) {
      FFT_spec[x] = LPFcoeff * FFT_spec[x] + (1-LPFcoeff) * FFT_spec_old[x];
      FFT_spec_old[x] = FFT_spec[x];
    }

    // scale the magnitude values and convert to int for spectrum display
    for (size_t i = 0; i < SPECTRUM_RES/2; i++) {
        psdnew[i] = log10f_fast(FFT_spec[i]);
    }
}

/**
 * Frequency translation by Fs/4 without multiplication from Lyons (2011): 
 * chapter 13.1.2 page 646, 13-3. Together with the savings of not having to 
 * shift/rotate the FFT_buffer, this saves about 1% of processor use.
 * 
 * This is for +Fs/4 [moves receive frequency to the left in the spectrum display]
 *   xnew(0) =  xreal(0) + jximag(0)
 *     leave first value (DC component) as it is!
 *   xnew(1) =  - ximag(1) + jxreal(1)
 * 
 * @param data DataBlock containing the I & Q samples
 * 
 * Frequency translation is performed in-place.
 */
void FreqShiftFs4(DataBlock *data)
{
    float32_t hh1,hh2;
    for (size_t i = 0; i < data->N; i += 4) {
        hh1 = - data->Q[i + 1];  // xnew(1) =  - ximag(1) + jxreal(1)
        hh2 =   data->I[i + 1];
        data->I[i + 1] = hh1;
        data->Q[i + 1] = hh2;
        hh1 = - data->I[i + 2];
        hh2 = - data->Q[i + 2];
        data->I[i + 2] = hh1;
        data->Q[i + 2] = hh2;
        hh1 =   data->Q[i + 3];
        hh2 = - data->I[i + 3];
        data->I[i + 3] = hh1;
        data->Q[i + 3] = hh2;
    }
}

/**
 * Frequency translation by frequency F. 
 * 
 * @param data DataBlock to act upon
 * @param freqShift_Hz Frequency to shift by in Hz
 * 
 * Frequency translation is performed in-place.
 */
void FreqShiftF(DataBlock *data, float32_t freqShift_Hz){
    // We need to avoid phase discontinuities between adjacent blocks of samples
    float32_t omegaShift = TWO_PI * (freqShift_Hz); 
    float32_t tSample = 1/(float32_t)data->sampleRate_Hz;
    float32_t NCO_INC = omegaShift * tSample;
    float32_t OSC_COS, OSC_SIN,ip,qp;
    for (size_t i = 0; i < data->N; i++) {
        float32_t itheta = NCO_INC*(float32_t)iFSF;
        //OSC_COS = arm_cos_f32 (itheta); // these are too inaccurate
        //OSC_SIN = arm_sin_f32 (itheta);
        OSC_COS = cosf (itheta);
        OSC_SIN = sinf (itheta);
        ip = data->I[i];
        qp = data->Q[i];
        data->I[i] = (ip * OSC_COS - qp * OSC_SIN);
        data->Q[i] = (qp * OSC_COS + ip * OSC_SIN);
        // to avoid issues with errors due to iFSF growing too large, reset 
        // iFSF to zero if we're at a multiple of 2*pi. This is guaranteed 
        // to happen when iFSF == sample rate.
        iFSF++;
        if (iFSF == data->sampleRate_Hz) iFSF = 0;
    }
}

/**
 * Alternative frequency translation method using rotating phasor (deprecated)
 * @param I Pointer to I (real) channel data
 * @param Q Pointer to Q (imaginary) channel data
 * @param blocksize Number of samples to process
 * @param freqShift_Hz Frequency shift in Hz
 * @param sampleRate_Hz Sample rate in Hz
 *
 * WARNING: This method has problems. It saves 50us each loop but introduces
 * artifacts between adjacent blocks of samples. Use FreqShiftF() instead.
 */
void FreqShiftF2(float32_t *I, float32_t *Q, uint32_t blocksize,
                float32_t freqShift_Hz, uint32_t sampleRate_Hz){
    float32_t NCO_INC = TWO_PI * (freqShift_Hz) /(float32_t)sampleRate_Hz;
    float32_t OSC_COS = arm_cos_f32 (NCO_INC);
    float32_t OSC_SIN = arm_sin_f32 (NCO_INC);
    float32_t Osc_Vect_Q = 1.0;
    float32_t Osc_Vect_I = 1.0;
    float32_t Osc_Gain;
    float32_t Osc_Q;
    float32_t Osc_I;

    for (size_t i = 0; i < blocksize; i++) {
        // generate local oscillator on-the-fly:  This takes a lot of processor time!
        Osc_Q = (Osc_Vect_Q * OSC_COS) - (Osc_Vect_I * OSC_SIN);  // Q channel of oscillator
        Osc_I = (Osc_Vect_I * OSC_COS) + (Osc_Vect_Q * OSC_SIN);  // I channel of oscillator
        Osc_Gain = 1.95 - ((Osc_Vect_Q * Osc_Vect_Q) + (Osc_Vect_I * Osc_Vect_I));  // Amplitude control of oscillator

        // rotate vectors while maintaining constant oscillator amplitude
        Osc_Vect_Q = Osc_Gain * Osc_Q;
        Osc_Vect_I = Osc_Gain * Osc_I;
    
        // do actual frequency conversion
        float freqAdjFactor = 1.1;

        I[i] = (I[i] * freqAdjFactor * Osc_Q) + (I[i] * freqAdjFactor * Osc_I); // multiply I/Q data by sine/cosine data to do translation
        Q[i] = (Q[i] * freqAdjFactor * Osc_Q) - (Q[i] * freqAdjFactor * Osc_I);
    }
}

/**
 * Update the FIR filter mask for convolution filtering
 * @param RXfilters Filter configuration structure
 *
 * Recalculates the frequency-domain filter mask used by ConvolutionFilter().
 * Should be called whenever filter parameters change (e.g., bandwidth adjustments).
 */
void UpdateFIRFilterMask(ReceiveFilterConfig *RXfilters){
    // FIR filter mask
    InitFilterMask(FIR_filter_mask, RXfilters);
}

/**
 * Calculate the filter coefficients. This is done once at startup.
 * @param spectrum_zoom The spectrum zoom state 
 * @param RXfilters Struct holding the filter variables and objects
 */
void InitializeFilters(uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters) {
    // ****************************************************************************************
    // *  Zoom FFT: Initiate decimation and interpolation FIR RXfilters AND IIR RXfilters
    // ****************************************************************************************
    
    ZoomFFTPrep(spectrum_zoom, RXfilters);
    for (unsigned i = 0; i < 4 * RXfilters->IIR_biquad_Zoom_FFT_N_stages; i++) {
        RXfilters->biquadZoomI.pState[i] = 0.0;  // set state variables to zero
        RXfilters->biquadZoomQ.pState[i] = 0.0;  // set state variables to zero
    }

    // *********************************************
    // * Audio lowpass filter
    // *********************************************
    for (unsigned i = 0; i < 4 * RXfilters->N_stages_biquad_lowpass1; i++) {
        RXfilters->biquadAudioLowPass.pState[i] = 0.0;  // set state variables to zero
    }
    // adjust IIR AM filter
    int32_t LP_F_help = bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
    if (LP_F_help < -bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz)
        LP_F_help = -bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;
    SetIIRCoeffs(RXfilters->biquad_lowpass1_coeffs, 
            (float32_t)LP_F_help, 1.3, 
            (float32_t)SR[SampleRate].rate / RXfilters->DF, Lowpass);

    // ****************************************************************************************
    // *  Decimate: RXfilters involved with decimate by 2 and decimate by 4
    // ****************************************************************************************
    InitializeDecimationFilter(&RXfilters->DecimateRxStage1, RXfilters->DF1, (float32_t)SR[SampleRate].rate,
                                RXfilters->n_att_dB, RXfilters->n_desired_BW_Hz, READ_BUFFER_SIZE);
    InitializeDecimationFilter(&RXfilters->DecimateRxStage2, RXfilters->DF2, (float32_t)SR[SampleRate].rate / RXfilters->DF1,
                                RXfilters->n_att_dB, RXfilters->n_desired_BW_Hz, READ_BUFFER_SIZE/RXfilters->DF1);

    // FIR filter mask
    InitFilterMask(FIR_filter_mask, RXfilters); 

    // Equalizer RXfilters
    for (size_t i = 0; i<14; i++){
        for (size_t j = 0; j < RXfilters->eqNumStages*2; j++) {
            RXfilters->S_Rec[i].pState[j] = 0;
            RXfilters->S_Xmt[i].pState[j] = 0;
        }
        // Set coefficient pointers now that EQ_Coeffs is guaranteed to be initialized
        RXfilters->S_Rec[i].pCoeffs = *EQ_Coeffs[i];
        RXfilters->S_Xmt[i].pCoeffs = *EQ_Coeffs[i];
    }

    // Interpolation RXfilters
    CalcFIRCoeffs(RXfilters->FIR_int1_coeffs, 48, (float32_t)(RXfilters->n_desired_BW_Hz), RXfilters->n_att_dB, 
                    Lowpass, 0.0, SR[SampleRate].rate / RXfilters->DF1);
    CalcFIRCoeffs(RXfilters->FIR_int2_coeffs, 32, (float32_t)(RXfilters->n_desired_BW_Hz), RXfilters->n_att_dB, 
                    Lowpass, 0.0, (float32_t)SR[SampleRate].rate);

}

/**
 * Initialize transmit decimation and interpolation filter structures. This is done once at startup.
 *
 * Sets up the complete transmit DSP chain RXfilters:
 * - Decimation RXfilters (192k->48k, 48k->24k, 24k->12k)
 * - Hilbert transform RXfilters for SSB generation
 * - Interpolation RXfilters (12k->24k, 24k->48k, 48k->192k)
 * All filter states are cleared and ARM DSP filter instances are initialized.
 * 
 * @param TXfilters Struct holding the filter variables and objects
 */
void InitializeTransmitFilters(TransmitFilterConfig *TXfilters) {
    // ****************************************************************************************
    // *  Decimate by 4: 192K to 48K SPS
    // ****************************************************************************************
    CLEAR_VAR(TXfilters->FIR_dec1_EX_I_state);
    CLEAR_VAR(TXfilters->FIR_dec1_EX_Q_state);
    arm_fir_decimate_init_f32(&TXfilters->FIR_dec1_EX_I, 48, 4, coeffs192K_10K_LPF_FIR, TXfilters->FIR_dec1_EX_I_state, 2048);
    arm_fir_decimate_init_f32(&TXfilters->FIR_dec1_EX_Q, 48, 4, coeffs192K_10K_LPF_FIR, TXfilters->FIR_dec1_EX_Q_state, 2048);
    
    // ****************************************************************************************
    // *  Decimate by 2: 48K to 24K SPS
    // ****************************************************************************************
    CLEAR_VAR(TXfilters->FIR_dec2_EX_I_state);
    CLEAR_VAR(TXfilters->FIR_dec2_EX_Q_state);
    arm_fir_decimate_init_f32(&TXfilters->FIR_dec2_EX_I, 48, 2, coeffs48K_8K_LPF_FIR, TXfilters->FIR_dec2_EX_I_state, 512);
    arm_fir_decimate_init_f32(&TXfilters->FIR_dec2_EX_Q, 48, 2, coeffs48K_8K_LPF_FIR, TXfilters->FIR_dec2_EX_Q_state, 512);

    // ****************************************************************************************
    // *  Decimate by 2, again: 24K to 12K SPS
    // ****************************************************************************************
    CLEAR_VAR(TXfilters->FIR_dec3_EX_I_state);
    CLEAR_VAR(TXfilters->FIR_dec3_EX_Q_state);
    arm_fir_decimate_init_f32(&TXfilters->FIR_dec3_EX_I, 48, 2, coeffs12K_8K_LPF_FIR, TXfilters->FIR_dec3_EX_I_state, 256);
    arm_fir_decimate_init_f32(&TXfilters->FIR_dec3_EX_Q, 48, 2, coeffs12K_8K_LPF_FIR, TXfilters->FIR_dec3_EX_Q_state, 256);

    // ****************************************************************************************
    // *  Hilbert transform
    // ****************************************************************************************
    CLEAR_VAR(TXfilters->FIR_Hilbert_state_L);
    CLEAR_VAR(TXfilters->FIR_Hilbert_state_R);
    arm_fir_init_f32(&TXfilters->FIR_Hilbert_L, 100, FIR_Hilbert_coeffs_45, TXfilters->FIR_Hilbert_state_L, 128);
    arm_fir_init_f32(&TXfilters->FIR_Hilbert_R, 100, FIR_Hilbert_coeffs_neg_45, TXfilters->FIR_Hilbert_state_R, 128);

    // ****************************************************************************************
    // *  Interpolate by 2, again: 12K to 24K SPS
    // ****************************************************************************************
    CLEAR_VAR(TXfilters->FIR_int3_EX_I_state);
    CLEAR_VAR(TXfilters->FIR_int3_EX_Q_state);
    arm_fir_interpolate_init_f32(&TXfilters->FIR_int3_EX_I, 2, 48, FIR_int3_12ksps_48tap_2k7, TXfilters->FIR_int3_EX_I_state, 128);
    arm_fir_interpolate_init_f32(&TXfilters->FIR_int3_EX_Q, 2, 48, FIR_int3_12ksps_48tap_2k7, TXfilters->FIR_int3_EX_Q_state, 128);

    // ****************************************************************************************
    // *  Interpolate by 2: 24K to 48K SPS
    // ****************************************************************************************
    CLEAR_VAR(TXfilters->FIR_int1_EX_I_state);
    CLEAR_VAR(TXfilters->FIR_int1_EX_Q_state);
    arm_fir_interpolate_init_f32(&TXfilters->FIR_int1_EX_I, 2, 48, coeffs48K_8K_LPF_FIR, TXfilters->FIR_int1_EX_I_state, 256);
    arm_fir_interpolate_init_f32(&TXfilters->FIR_int1_EX_Q, 2, 48, coeffs48K_8K_LPF_FIR, TXfilters->FIR_int1_EX_Q_state, 256);

    // ****************************************************************************************
    // *  Interpolate by 4: 48K to 192K SPS
    // ****************************************************************************************
    CLEAR_VAR(TXfilters->FIR_int2_EX_I_state);
    CLEAR_VAR(TXfilters->FIR_int2_EX_Q_state);
    arm_fir_interpolate_init_f32(&TXfilters->FIR_int2_EX_I, 4, 48, coeffs192K_10K_LPF_FIR, TXfilters->FIR_int2_EX_I_state, 512);
    arm_fir_interpolate_init_f32(&TXfilters->FIR_int2_EX_Q, 4, 48, coeffs192K_10K_LPF_FIR, TXfilters->FIR_int2_EX_Q_state, 512);

}

/**
 * Change the FIR filter settings.
 * @param filter_change The positive or negative increment to the filter bandwidth
 */
void FilterSetSSB(int32_t filter_change, uint8_t changeFilterHiCut) {
    // Change the band parameters
    switch (bands[ED.currentBand[ED.activeVFO]].mode) {
        case LSB:{
            if (changeFilterHiCut == 0)  // "0" = LoCut, "1" = HiCut
            {
                bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz - filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            } else {
                bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz - filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            }
            break;
        }
        case USB:{
            if (changeFilterHiCut == 0) {
                bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);

            } else {
                bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = 
                  bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            }
            break;
        }
        case AM:
        case SAM:{
            bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz = 
              bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz + filter_change * (int32_t)(40.0 * ENCODER_FACTOR);
            bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz = -bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
            break;
        }
        case IQ:
        case DCF77:
            break;
    }
    // Calculate the new FIR filter mask
    UpdateFIRFilterMask(&RXfilters);
}

/**
 * ZoomFFTPrep() calculates the appropriate structures for the filter-and-decimate
 * functions performed during a zoom FFT.
 * 
 * @param spectrum_zoom The zoom selection, ranges from SPECTRUM_ZOOM_MIN to SPECTRUM_ZOOM_MAX
 * @param RXfilters Struct holding the filter variables and objects
 */
void ZoomFFTPrep(uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters){ 
    // take value of spectrum_zoom and initialize IIR lowpass filter for the right values
    RXfilters->zoom_M = (1 << spectrum_zoom);
    // this sets the coefficients for the ZoomFFT decimation filter
    // according to the desired magnification mode
    // for 0 the mag_coeffs will a NULL  ptr, since the filter is not going to be used in this mode!
    RXfilters->biquadZoomI.pCoeffs = mag_coeffs[spectrum_zoom];
    RXfilters->biquadZoomQ.pCoeffs = mag_coeffs[spectrum_zoom];
    zoom_sample_ptr = 0;
}

/**
 * Zoom FFT. Calculate a 512 point PSD from complex number input arrays, but 
 * apply decimation beforehand in order to decrease the sample rate, and hence 
 * increase the frequency resolution of the PSD. 
 * 
 * | Zoom |   Fsample |   Nsamples   |   PSD bin width |
 * |------|-----------|--------------|-----------------|
 * | 1    |   192k    |    2048      |      375 Hz     |
 * | 2    |   96k     |    1024      |      187.5 Hz   |
 * | 4    |   48k     |    512       |      93.75 Hz   |
 * | 8    |   24k     |    256       |      46.875 Hz  |
 * | 16   |   12k     |    128       |      23.4375 Hz |
 * 
 * For higher zoom factors (8 and above) there are not enough samples available 
 * in a single call to this function, so we need to write the decimated samples
 * to a buffer and call the PSD function only when this buffer fills up. 
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param spectrum_zoom The zoom factor
 * @param RXfilters Struct holding the filter variables and objects
 * @return true if we calculated a PSD, false if we did not
 * 
 * The resulting PSD is written to the psdnew global array. 
 */
bool ZoomFFTExe(DataBlock *data, uint32_t spectrum_zoom, ReceiveFilterConfig *RXfilters)
{
    if (spectrum_zoom == SPECTRUM_ZOOM_1) {
        // No decimation required
        CalcPSD512(data->I,data->Q);
        return true;
    }
    float32_t x_buffer[data->N];
    float32_t y_buffer[data->N];
    // We use a biquad to filter first,
    arm_biquad_cascade_df1_f32 (&(RXfilters->biquadZoomI), data->I, x_buffer, data->N);
    arm_biquad_cascade_df1_f32 (&(RXfilters->biquadZoomQ), data->Q, y_buffer, data->N);
    // then decimate. We don't need a FIR decimate because of the IIR filter
    decimate_f32(x_buffer,x_buffer,RXfilters->zoom_M,data->N);
    decimate_f32(y_buffer,y_buffer,RXfilters->zoom_M,data->N);
    
    // and then copy the decimated samples into the FFT buffer, but no more than SPECTRUM_RES of them
    uint32_t Nsamples = data->N / (1 << spectrum_zoom); // Samples after decimation
    if (Nsamples > SPECTRUM_RES) {
        Nsamples = SPECTRUM_RES;
    }
    // This multiplier overcomes the effects of the filter and decimate functions. Keeps 
    // the amplitude in the PSD more stable as zoom increases.
    float32_t multiplier = zoomMultiplierCoeff[spectrum_zoom];
    for (size_t i = 0; i < Nsamples; i++) {
        FFT_ring_buffer_x[zoom_sample_ptr] = multiplier*x_buffer[i];
        FFT_ring_buffer_y[zoom_sample_ptr] = multiplier*y_buffer[i];
        zoom_sample_ptr++;
    }

    if (zoom_sample_ptr < SPECTRUM_RES){
        // we haven't filled up FFT_ring_buffers yet, do no more until they fill
        return false;
    } 
    // FFT_ring_buffers are full, reset the sample pointer and then continue
    zoom_sample_ptr = 0;
    CalcPSD512(FFT_ring_buffer_x,FFT_ring_buffer_y);

    return true;
}

/**
 * Resample (Decimate) the signal by 2. Only works on input arrays of READ_BUFFER_SIZE/4 samples
 * sampled at SR[SampleRate].rate / 4.
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param RXfilters Struct holding the filter variables and objects
 */
errno_t DecimateBy2(DataBlock *data, ReceiveFilterConfig *RXfilters){
    // decimation-by-2 in-place
    if (data->N != READ_BUFFER_SIZE/RXfilters->DF1){
        return EFAIL;
    }
    arm_fir_decimate_f32(&(RXfilters->DecimateRxStage2.FIR_dec_I), data->I, data->I, data->N);
    arm_fir_decimate_f32(&(RXfilters->DecimateRxStage2.FIR_dec_Q), data->Q, data->Q, data->N);
    data->N = data->N/RXfilters->DF2;
    data->sampleRate_Hz = data->sampleRate_Hz/RXfilters->DF2;
    return ESUCCESS;
}

/**
 * Resample (Decimate) the signal by 4. Only works on input arrays of READ_BUFFER_SIZE samples
 * sampled at the original sample rate SR[SampleRate].rate
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param RXfilters Struct holding the filter variables and objects
 */
errno_t DecimateBy4(DataBlock *data, ReceiveFilterConfig *RXfilters){
    // decimation-by-4 in-place
    if (data->N != READ_BUFFER_SIZE){
        return EFAIL;
    }
    arm_fir_decimate_f32(&(RXfilters->DecimateRxStage1.FIR_dec_I), data->I, data->I, data->N);
    arm_fir_decimate_f32(&(RXfilters->DecimateRxStage1.FIR_dec_Q), data->Q, data->Q, data->N);
    data->N = data->N/RXfilters->DF1;
    data->sampleRate_Hz = data->sampleRate_Hz/RXfilters->DF1;
    return ESUCCESS;
}

/**
 * Resample (Decimate) the signal, first by 4, then by 2.  Each time the signal is decimated 
 * by an even number, the spectrum is reversed.  Resampling twice returns the spectrum to the 
 * correct orientation. Only works on input arrays of READ_BUFFER_SIZE samples sampled at the 
 * original sample rate SR[SampleRate].rate
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param RXfilters Struct holding the filter variables and objects
 */
errno_t DecimateBy8(DataBlock *data, ReceiveFilterConfig *RXfilters){
    errno_t err;
    err = DecimateBy4(data, RXfilters);
    err += DecimateBy2(data, RXfilters);
    return err;
}

/**
 * Digital FFT convolution filtering is accomplished by combining (multiplying) 
 * spectra in the frequency domain. Basis for this was Lyons, R. (2011): 
 * Understanding Digital Processing. "Fast FIR Filtering using the FFT", pages 688 - 694.
 * Method used here: overlap-and-save.
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param RXfilters Struct holding the filter variables and objects
 */
errno_t ConvolutionFilter(DataBlock *data, ReceiveFilterConfig *RXfilters, const char *fname){
    // Prepare the audio signal buffers. Filter operates on 512 complex samples. Each 
    // block that is read from the ADC starts as READ_BUFFER_SIZE (2048) samples. 
    // It is then decimated by M (8) so that the buffers I & Q are only blocksize 
    // (256) samples. Note that last_sample_buffer_L,R will start as all zeros 
    // because they are declared as static.

    // block size = READ_BUFFER_SIZE / (uint32_t)(RXfilters->DF) = 256

    if (data->N != READ_BUFFER_SIZE / RXfilters->DF){
        return EFAIL;
    }

    // used by unit tests
    if (fname != nullptr){
        char fn2[100];
        sprintf(fn2,"fIQ_%s",fname);
        WriteIQFile(data, fn2);
    }

    // fill first half of FFT buffer with previous event's audio samples
    for (unsigned i = 0; i < data->N; i++) {
        buffer_spec_FFT[i * 2] = last_sample_buffer_L[i];      // real
        buffer_spec_FFT[i * 2 + 1] = last_sample_buffer_R[i];  // imaginary
    }
    // copy recent samples to last_sample_buffer for next time!
    for (unsigned i = 0; i < data->N; i++) {
        last_sample_buffer_L[i] = data->I[i];
        last_sample_buffer_R[i] = data->Q[i];
    }
    // now fill recent audio samples into second half of FFT buffer
    for (unsigned i = 0; i < data->N; i++) {
        buffer_spec_FFT[FFT_LENGTH + i * 2] = data->I[i];      // real
        buffer_spec_FFT[FFT_LENGTH + i * 2 + 1] = data->Q[i];  // imaginary
    }

    // used by unit tests
    if (fname != nullptr){
        WriteFloatFile(buffer_spec_FFT, 2*FFT_LENGTH, fname);
    }

    //   Perform complex FFT on the audio time signals
    //   calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
    FFT512Forward(buffer_spec_FFT);

    // The filter mask is initialized using InitFilterMask(). Only need to do 
    // this once for each filter setting.Allows efficient real-time variable LP 
    // and HP audio RXfilters, without the overhead of time-domain convolution 
    // filtering. Complex multiply filter mask with the frequency domain audio data.
    arm_cmplx_mult_cmplx_f32(buffer_spec_FFT, FIR_filter_mask, iFFT_buffer, FFT_LENGTH);
    
    // Save the audio spectrum
    // The sampled band is notionally -12,000 Hz to +12,000 Hz. 192khz / 8 = 24ksps
    // The number of samples in I and Q are each 2048 / 8 = 256. We buffer multiple
    // blocks of samples to get FFTs that are 512 samples long.
    // The bandwidth of each frequency bin is 24000/512 = 46.875 Hz
    // We want to display DC to 6 kHz, which is 1/4 of the total bandwidth
    // So we have 512 / 4 = 128 bins to save and plot
    // Positive frequencies are from bin 1 to bin 129 in iFFT_buffer
    // Negative frequencies are from bin 1023 to 895 in iFFT_buffer
    audioPowerMax = -1.0;
    for (size_t k = 0; k < FFT_LENGTH/4; k++){
        float32_t psq;
        switch (ED.modulation[ED.activeVFO]){
            case LSB:
                psq = iFFT_buffer[1023-(2*k)] * iFFT_buffer[1023-(2*k)] + iFFT_buffer[1023-(2*k+1)] * iFFT_buffer[1023-(2*k+1)];
                break;
            default: // USB, SAM, AM, etc
                psq = iFFT_buffer[1+(2*k)] * iFFT_buffer[1+(2*k)] + iFFT_buffer[1+(2*k+1)] * iFFT_buffer[1+(2*k+1)];
                break;           
        }
        if (psq > audioPowerMax) 
            audioPowerMax = psq;
        audioYPixel[k] = 50 + map(15 * log10f(psq), 0, 100, 0, 120);
        if (audioYPixel[k] < 0)
            audioYPixel[k] = 0;
    }

    // After the frequency domain filter mask and other processes are complete, do a
    // complex inverse FFT to return to the time domain (if sample rate = 192kHz, 
    // we are in 24ksps now, because we decimated by 8)
    FFT512Reverse(iFFT_buffer);

    // Now discard the first 256 complex samples
    for (unsigned i = 0; i < data->N; i++) {
        data->I[i] = iFFT_buffer[256*2 + 2*i];
        data->Q[i] = iFFT_buffer[256*2 + 2*i+1];
    }
    return ESUCCESS;
}

/**
 * Apply a single equalizer band filter to the audio data
 * @param data Data block containing audio samples to filter
 * @param RXfilters Filter configuration structure containing EQ filter instances
 * @param bf Band filter index (0-13 for 14 EQ bands)
 * @param TXRX RX or TX mode selector
 *
 * Applies one band of the 14-band graphic equalizer, scales by the band's
 * gain setting, and accumulates result in eqSumBuffer. Handles NaN detection
 * and recovery in filter state. Alternating bands have inverted sign.
 */
void ApplyEQBandFilter(DataBlock *data, ReceiveFilterConfig *RXfilters, uint8_t bf, TXRXType TXRX){
    int sign = 1;
    if (bf%2 == 0) sign = -1;
    float32_t scale;
    if (TXRX == RX) scale = (float)ED.equalizerRec[bf] / 100.0;
    else scale = (float)ED.equalizerXmt[bf] / 100.0;

    // Fix the weird bug where the pState array gets a nan in it upon entering loop
    // after a power cycle
    for (size_t i = 0; i < 2*RXfilters->eqNumStages; i++){
        if (isnan(RXfilters->S_Rec[bf].pState[i])){
            memset(RXfilters->S_Rec[bf].pState,0,sizeof(float32_t)*2*RXfilters->eqNumStages);
            break;
        }
    }

    // Filter I with this band's biquad
    if (TXRX == RX){
        arm_biquad_cascade_df2T_f32(&RXfilters->S_Rec[bf], data->I, RXfilters->eqFiltBuffer, data->N);
    }else{
        arm_biquad_cascade_df2T_f32(&RXfilters->S_Xmt[bf], data->I, RXfilters->eqFiltBuffer, data->N);
    }
    // Scale the amplitude by the overall level scaler
    arm_scale_f32(RXfilters->eqFiltBuffer, (float32_t)sign*scale, RXfilters->eqFiltBuffer, data->N);
    // Add to the accumulator buffer
    arm_add_f32(RXfilters->eqSumBuffer, RXfilters->eqFiltBuffer, RXfilters->eqSumBuffer, data->N);
}

/**
 * Apply 14-band graphic equalizer to audio signal
 * @param data Data block containing audio samples (I channel only)
 * @param RXfilters Filter configuration structure
 * @param TXRX RX or TX mode selector
 *
 * Processes audio through all EQUALIZER_CELL_COUNT (14) equalizer bands, 
 * accumulating filtered and scaled outputs. Final equalized audio replaces 
 * original data in data->I. Uses either receive or transmit EQ settings 
 * based on TXRX parameter.
 */
void BandEQ(DataBlock *data, ReceiveFilterConfig *RXfilters, TXRXType TXRX){
    // Apply 14 successive RXfilters, accumulating in RXfilters->eqSumBuffer as we go
    memset(RXfilters->eqSumBuffer, 0, sizeof(float32_t) * (READ_BUFFER_SIZE/RXfilters->DF));
    for (int i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        ApplyEQBandFilter(data, RXfilters, i, TXRX);
    }
    // Overwrite data->I with the filtered and accumulated data
    for (size_t i =0; i<data->N; i++){
        data->I[i] = RXfilters->eqSumBuffer[i];
    }
}

//////////////////////////////////////////////////////////////////////////
// Transmit DSP chain
//////////////////////////////////////////////////////////////////////////

/**
 * Apply Hilbert transform to create I/Q signals from audio
 * @param data Pointer to DataBlock containing I (real) channel buffer (in/out) and Q (imaginary) channel buffer (in/out)
 * @param TXfilters Pointer to TransmitFilterConfig struct containing filter objects
 *
 * Applies 45-degree and -45-degree phase shift FIR RXfilters to create quadrature
 * signals from real audio input. Operates at 12 kHz sample rate with 128-sample
 * blocks. Used in transmit chain for SSB generation.
 */
void HilbertTransform(DataBlock *data, TransmitFilterConfig *TXfilters){
    //Hilbert transforms at 12K, with 5KHz bandwidth, buffer size 128
    arm_fir_f32(&TXfilters->FIR_Hilbert_L, data->I, data->I, 128);
    arm_fir_f32(&TXfilters->FIR_Hilbert_R, data->Q, data->Q, 128);
}

/**
 * Select upper or lower sideband by inverting I channel if needed
 * @param data Pointer to DataBlock with I (real) and Q (imaginary) channel buffers
 *
 * For USB mode, inverts the I channel signal (multiplies by -1).
 * LSB is selected by default (no inversion). Operates on 256-sample blocks.
 */
void SidebandSelection(DataBlock *data){
    // Math works out for selecting LSB by default
    if (ED.modulation[ED.activeVFO] == USB){
        arm_scale_f32(data->I,-1,data->I,256);
    }
}

/**
 * Decimate transmit signal by factor of 4 (192 kHz -> 48 kHz)
 * @param data Pointer to DataBlock containing I and Q channel buffers (2048 samples in, 512 out)
 * @param TXfilters Pointer to TransmitFilterConfig struct containing filter objects
 *
 * Applies FIR decimation filter in-place. Input: 192 kHz, Output: 48 kHz.
 */
void TXDecimateBy4(DataBlock *data, TransmitFilterConfig *TXfilters){
    // 192KHz effective sample rate here
    // decimation-by-4 in-place!
    arm_fir_decimate_f32(&TXfilters->FIR_dec1_EX_I, data->I, data->I, BUFFER_SIZE * N_BLOCKS);
    arm_fir_decimate_f32(&TXfilters->FIR_dec1_EX_Q, data->Q, data->Q, BUFFER_SIZE * N_BLOCKS);
    data->N = data->N/4;
    data->sampleRate_Hz = data->sampleRate_Hz/4;
}

/**
 * Decimate transmit signal by factor of 2 (48 kHz -> 24 kHz)
 * @param data Pointer to DataBlock containing I and Q channel buffers (512 samples in, 256 out)
 * @param TXfilters Pointer to TransmitFilterConfig struct containing filter objects
 *
 * Applies FIR decimation filter in-place. Input: 48 kHz, Output: 24 kHz.
 */
void TXDecimateBy2(DataBlock *data, TransmitFilterConfig *TXfilters){
    // 48KHz effective sample rate here
    // decimation-by-2 in-place
    arm_fir_decimate_f32(&TXfilters->FIR_dec2_EX_I, data->I, data->I, 512);
    arm_fir_decimate_f32(&TXfilters->FIR_dec2_EX_Q, data->Q, data->Q, 512);
    data->N = data->N/2;
    data->sampleRate_Hz = data->sampleRate_Hz/2;
}

/**
 * Decimate transmit signal by factor of 2 again (24 kHz -> 12 kHz)
 * @param data Pointer to DataBlock containing I and Q channel buffers (256 samples in, 128 out)
 * @param TXfilters Pointer to TransmitFilterConfig struct containing filter objects
 *
 * Applies FIR decimation filter in-place. Input: 24 kHz, Output: 12 kHz.
 * This is the third decimation stage in the transmit chain.
 */
void TXDecimateBy2Again(DataBlock *data, TransmitFilterConfig *TXfilters){
    //Decimate by 2 to 12K SPS sample rate
    arm_fir_decimate_f32(&TXfilters->FIR_dec3_EX_I, data->I, data->I, 256);
    arm_fir_decimate_f32(&TXfilters->FIR_dec3_EX_Q, data->Q, data->Q, 256);
    data->N = data->N/2;
    data->sampleRate_Hz = data->sampleRate_Hz/2;
}

float32_t DMAMEM Itmp[READ_BUFFER_SIZE];
float32_t DMAMEM Qtmp[READ_BUFFER_SIZE];
/**
 * Interpolate transmit signal by factor of 2 (12 kHz -> 24 kHz)
 * @param data Pointer to DataBlock containing input I & Q channel buffers (128 samples)
 * @param TXfilters Pointer to TransmitFilterConfig struct containing filter objects
 *
 * Applies FIR interpolation filter and scales by 2 to compensate for interpolation.
 * First interpolation stage in transmit chain. data.I and data.Q are overwritten.
 */
void TXInterpolateBy2Again(DataBlock *data, TransmitFilterConfig *TXfilters){
    //Interpolate back to 24K SPS
    arm_fir_interpolate_f32(&TXfilters->FIR_int3_EX_I, data->I, Itmp, 128);
    arm_scale_f32(Itmp,2,data->I,256);
    arm_fir_interpolate_f32(&TXfilters->FIR_int3_EX_Q, data->Q, Qtmp, 128);
    arm_scale_f32(Qtmp,2,data->Q,256);
    data->N = data->N*2;
    data->sampleRate_Hz = data->sampleRate_Hz*2;
}

/**
 * Interpolate transmit signal by factor of 2 (24 kHz -> 48 kHz)
 * @param data Pointer to DataBlock containing input I & Q channel buffers (256 samples)
 * @param TXfilters Pointer to TransmitFilterConfig struct containing filter objects
 *
 * Applies FIR interpolation filter and scales by 2 to compensate for interpolation.
 * Second interpolation stage in transmit chain.
 */
void TXInterpolateBy2(DataBlock *data, TransmitFilterConfig *TXfilters){
    //24KHz effective sample rate input, 48 kHz output
    arm_fir_interpolate_f32(&TXfilters->FIR_int1_EX_I, data->I, Itmp, 256);
    arm_scale_f32(Itmp,2,data->I,512);
    arm_fir_interpolate_f32(&TXfilters->FIR_int1_EX_Q, data->Q, Qtmp, 256);
    arm_scale_f32(Qtmp,2,data->Q,512);
    data->N = data->N*2;
    data->sampleRate_Hz = data->sampleRate_Hz*2;
}

/**
 * Interpolate transmit signal by factor of 4 (48 kHz -> 192 kHz)
 * @param data Pointer to DataBlock containing input I & Q channel buffers (512 samples)
 * @param TXfilters Pointer to TransmitFilterConfig struct containing filter objects
 *
 * Applies FIR interpolation filter and scales by 4 to compensate for interpolation.
 * Final interpolation stage in transmit chain, produces output at DAC sample rate.
 */
void TXInterpolateBy4(DataBlock *data, TransmitFilterConfig *TXfilters){
    //48KHz effective sample rate input, 128 kHz output
    arm_fir_interpolate_f32(&TXfilters->FIR_int2_EX_I, data->I, Itmp, 512);
    arm_scale_f32(Itmp,4,data->I,2048);
    arm_fir_interpolate_f32(&TXfilters->FIR_int2_EX_Q, data->Q, Qtmp, 512);
    arm_scale_f32(Qtmp,4,data->Q,2048);
    data->N = data->N*4;
    data->sampleRate_Hz = data->sampleRate_Hz*4;
}
