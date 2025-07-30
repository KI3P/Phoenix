#include "DSP_FFT.h"
#ifdef TESTMODE
#include "../../test/SignalProcessing_mock.h"
#endif

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

// These coefficients were derived by measurement, but they are approximately given by
// (float32_t)(1 << spectrum_zoom) * (0.5/((float32_t)(1<<spectrum_zoom))^2.3 + 0.5)
// = 2^(-spectrum_zoom - 1) + 2^(+spectrum_zoom - 1)
//static const float32_t zoomMultiplierCoeff[5] = {1.0, 1.21902468, 2.07876308, 3.98758528, 7.88521392};
static const float32_t zoomMultiplierCoeff[5] = {1.0, 1.0, 1.0, 1.0, 1.0};

extern float32_t* mag_coeffs[]; // in FIR.cpp


#ifdef TESTMODE
float32_t * GetFilteredBufferAddress(void){
    return iFFT_buffer;
}
#endif

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
      psdold[x] = 0;
    }
}

/**
 * Calculate a 512-point power spectrum from the complex data stored in real 
 * and imag arrays. A Hanning window is applied to the data. The result is
 * written into the global array psdnew. Before overwriting, the data in psdnew
 * is copied into psdold. The value of the data in the psd buffers is
 * log10( I*I + Q*Q ). So the units are log10(V^2 / Hz).
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
    
    // We use a different FFT function during testing because arm_cfft_f32 calls a function 
    // for bit reversal that is written in assembly using the ARM ISA. This function won't 
    // compile on an x86 processor. Based on testing on the Teensy4.1, the arm_cfft_f32 
    // routine is about 38% faster than arm_cfft_radix2_f32.
    //
    // | Algorithm           | Time to complete one 512-point FFT |
    // |---------------------|------------------------------------|
    // | arm_cfft_f32        | 41 us                              |
    // | arm_cfft_radix2_f32 | 66 us                              |
    // Teensy 4.1 running at 600 MHz.

    #ifdef TESTMODE
    arm_cfft_radix2_instance_f32 S;
    arm_cfft_radix2_init_f32(&S, 512, 0, 1);
    arm_cfft_radix2_f32(&S,buffer_spec_FFT);
    #else
    arm_cfft_f32(&arm_cfft_sR_f32_len512, buffer_spec_FFT, 0, 1);
    #endif

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
        psdold[i] = psdnew[i];
        psdnew[i] = log10f_fast(FFT_spec[i]);
    }
}

#ifdef TESTMODE
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

    #ifdef TESTMODE
    arm_cfft_radix2_instance_f32 S;
    arm_cfft_radix2_init_f32(&S, 256, 0, 1);
    arm_cfft_radix2_f32(&S,buffer_spec_FFT);
    #else
    arm_cfft_f32(&arm_cfft_sR_f32_len256, buffer_spec_FFT, 0, 1);
    #endif
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
        psdold[i] = psdnew[i];
        psdnew[i] = log10f_fast(FFT_spec[i]);
    }
}
#endif

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

// This method has problems. It saves 50us each loop, but introduces artifacts between adjacent
// blocks of samples
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

void UpdateFIRFilterMask(FilterConfig *filters){
    // FIR filter mask
    InitFilterMask(FIR_filter_mask, filters); 
}

/**
 * Calculate the filter coefficients. This is done once at startup.
 * @param spectrum_zoom The spectrum zoom state 
 * @param filters Struct holding the filter variables and objects
 */
void InitializeFilters(uint32_t spectrum_zoom, FilterConfig *filters) {
    // ****************************************************************************************
	// *  Zoom FFT: Initiate decimation and interpolation FIR filters AND IIR filters
	// ****************************************************************************************
    filters->zoom_M = (1 << spectrum_zoom);

    for (unsigned i = 0; i < 4 * filters->IIR_biquad_Zoom_FFT_N_stages; i++) {
        filters->biquadZoomI.pState[i] = 0.0;  // set state variables to zero
        filters->biquadZoomQ.pState[i] = 0.0;  // set state variables to zero
    }
    // this sets the coefficients for the ZoomFFT decimation filter
    // according to the desired magnification mode
    // for 0 the mag_coeffs will a NULL  ptr, since the filter is not going to be used in this mode!
    filters->biquadZoomI.pCoeffs = mag_coeffs[spectrum_zoom];
    filters->biquadZoomQ.pCoeffs = mag_coeffs[spectrum_zoom];

    // *********************************************
    // * Audio lowpass filter
    // *********************************************
    for (unsigned i = 0; i < 4 * filters->N_stages_biquad_lowpass1; i++) {
        filters->biquadAudioLowPass.pState[i] = 0.0;  // set state variables to zero
    }
    // adjust IIR AM filter
    int32_t LP_F_help = bands[EEPROMData.currentBand].FHiCut_Hz;
    if (LP_F_help < -bands[EEPROMData.currentBand].FLoCut_Hz)
        LP_F_help = -bands[EEPROMData.currentBand].FLoCut_Hz;
    SetIIRCoeffs(filters->biquad_lowpass1_coeffs, 
            (float32_t)LP_F_help, 1.3, 
            (float32_t)SR[SampleRate].rate / filters->DF, Lowpass);

    // ****************************************************************************************
	// *  Decimate: filters involved with decimate by 2 and decimate by 4
	// ****************************************************************************************
    InitializeDecimationFilter(&filters->DecimateRxStage1, filters->DF1, (float32_t)SR[SampleRate].rate,
                                filters->n_att_dB, filters->n_desired_BW_Hz, READ_BUFFER_SIZE);
    InitializeDecimationFilter(&filters->DecimateRxStage2, filters->DF2, (float32_t)SR[SampleRate].rate / filters->DF1,
                                filters->n_att_dB, filters->n_desired_BW_Hz, READ_BUFFER_SIZE/filters->DF1);

    // FIR filter mask
    InitFilterMask(FIR_filter_mask, filters); 

    // Equalizer filters
    for (size_t i = 0; i<14; i++){
        for (size_t j = 0; j < filters->eqNumStages*2; j++) {
            filters->S_Rec[i].pState[j] = 0;
            filters->S_Xmt[i].pState[j] = 0;
        }
    }

    // Interpolation filters
    CalcFIRCoeffs(filters->FIR_int1_coeffs, 48, (float32_t)(filters->n_desired_BW_Hz), filters->n_att_dB, 
                    Lowpass, 0.0, SR[SampleRate].rate / filters->DF1);
    CalcFIRCoeffs(filters->FIR_int2_coeffs, 32, (float32_t)(filters->n_desired_BW_Hz), filters->n_att_dB, 
                    Lowpass, 0.0, (float32_t)SR[SampleRate].rate);

}


/**
 * ZoomFFTPrep() calculates the appropriate structures for the filter-and-decimate
 * functions performed during a zoom FFT.
 * 
 * @param spectrum_zoom The zoom selection, ranges from SPECTRUM_ZOOM_MIN to SPECTRUM_ZOOM_MAX
 * @param filters Struct holding the filter variables and objects
 */
void ZoomFFTPrep(uint32_t spectrum_zoom, FilterConfig *filters){ 
    // take value of spectrum_zoom and initialize IIR lowpass filter for the right values
    filters->zoom_M = (1 << spectrum_zoom);
    filters->biquadZoomI.pCoeffs = mag_coeffs[spectrum_zoom];
    filters->biquadZoomI.pCoeffs = mag_coeffs[spectrum_zoom];
    
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
 * @param filters Struct holding the filter variables and objects
 * @return true if we calculated a PSD, false if we did not
 * 
 * The resulting PSD is written to the psdnew and psdold global arrays. 
 */
bool ZoomFFTExe(DataBlock *data, uint32_t spectrum_zoom, FilterConfig *filters)
{
    if (spectrum_zoom == SPECTRUM_ZOOM_1) {
        // No decimation required
        CalcPSD512(data->I,data->Q);
        return true;
    }
    float32_t x_buffer[data->N];
    float32_t y_buffer[data->N];
    // We use a biquad to filter first,
    arm_biquad_cascade_df1_f32 (&(filters->biquadZoomI), data->I, x_buffer, data->N);
    arm_biquad_cascade_df1_f32 (&(filters->biquadZoomQ), data->Q, y_buffer, data->N);
    // then decimate. We don't need a FIR decimate because of the IIR filter
    decimate_f32(x_buffer,x_buffer,filters->zoom_M,data->N);
    decimate_f32(y_buffer,y_buffer,filters->zoom_M,data->N);
    
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
 * @param filters Struct holding the filter variables and objects
 */
errno_t DecimateBy2(DataBlock *data, FilterConfig *filters){
    // decimation-by-2 in-place
    if (data->N != READ_BUFFER_SIZE/filters->DF1){
        return EFAIL;
    }
    arm_fir_decimate_f32(&(filters->DecimateRxStage2.FIR_dec_I), data->I, data->I, data->N);
    arm_fir_decimate_f32(&(filters->DecimateRxStage2.FIR_dec_Q), data->Q, data->Q, data->N);
    data->N = data->N/filters->DF2;
    data->sampleRate_Hz = data->sampleRate_Hz/filters->DF2;
    return ESUCCESS;
}

/**
 * Resample (Decimate) the signal by 4. Only works on input arrays of READ_BUFFER_SIZE samples
 * sampled at the original sample rate SR[SampleRate].rate
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param filters Struct holding the filter variables and objects
 */
errno_t DecimateBy4(DataBlock *data, FilterConfig *filters){
    // decimation-by-4 in-place
    if (data->N != READ_BUFFER_SIZE){
        return EFAIL;
    }
    arm_fir_decimate_f32(&(filters->DecimateRxStage1.FIR_dec_I), data->I, data->I, data->N);
    arm_fir_decimate_f32(&(filters->DecimateRxStage1.FIR_dec_Q), data->Q, data->Q, data->N);
    data->N = data->N/filters->DF1;
    data->sampleRate_Hz = data->sampleRate_Hz/filters->DF1;
    return ESUCCESS;
}

/**
 * Resample (Decimate) the signal, first by 4, then by 2.  Each time the signal is decimated 
 * by an even number, the spectrum is reversed.  Resampling twice returns the spectrum to the 
 * correct orientation. Only works on input arrays of READ_BUFFER_SIZE samples sampled at the 
 * original sample rate SR[SampleRate].rate
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param filters Struct holding the filter variables and objects
 */
errno_t DecimateBy8(DataBlock *data, FilterConfig *filters){
    errno_t err;
    err = DecimateBy4(data, filters);
    err += DecimateBy2(data, filters);
    return err;
}

/**
 * Digital FFT convolution filtering is accomplished by combining (multiplying) 
 * spectra in the frequency domain. Basis for this was Lyons, R. (2011): 
 * Understanding Digital Processing. "Fast FIR Filtering using the FFT", pages 688 - 694.
 * Method used here: overlap-and-save.
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param filters Struct holding the filter variables and objects
 */
errno_t ConvolutionFilter(DataBlock *data, FilterConfig *filters, const char *fname){
    // Prepare the audio signal buffers. Filter operates on 512 complex samples. Each 
    // block that is read from the ADC starts as READ_BUFFER_SIZE (2048) samples. 
    // It is then decimated by M (8) so that the buffers I & Q are only blocksize 
    // (256) samples. Note that last_sample_buffer_L,R will start as all zeros 
    // because they are declared as static.

    // block size = READ_BUFFER_SIZE / (uint32_t)(filters->DF) = 256

    if (data->N != READ_BUFFER_SIZE / filters->DF){
        return EFAIL;
    }

    #ifdef TESTMODE
    char fn2[100];
    if (fname != nullptr){
        sprintf(fn2,"fIQ_%s",fname);
        FILE *file2 = fopen(fn2, "w");
        for (size_t i = 0; i < data->N; i++) {
            fprintf(file2, "%d,%7.6f,%7.6f\n", i,data->I[i],data->Q[i]);
        }
        fclose(file2);
    }
    #endif

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

    #ifdef TESTMODE
    if (fname != nullptr){
        FILE *file = fopen(fname, "w");
        for (size_t i = 0; i < 2*FFT_LENGTH; i++) {
            fprintf(file, "%d,%7.6f\n", i,buffer_spec_FFT[i]);
        }
        fclose(file);
    }
    #endif

    //   Perform complex FFT on the audio time signals
    //   calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
    #ifdef TESTMODE
    arm_cfft_radix2_instance_f32 S;
    arm_cfft_radix2_init_f32(&S, 512, 0, 1);
    arm_cfft_radix2_f32(&S,buffer_spec_FFT);
    #else
    arm_cfft_f32(&arm_cfft_sR_f32_len512, buffer_spec_FFT, 0, 1);
    #endif

    // The filter mask is initialized using InitFilterMask(). Only need to do 
    // this once for each filter setting.Allows efficient real-time variable LP 
    // and HP audio filters, without the overhead of time-domain convolution 
    // filtering. Complex multiply filter mask with the frequency domain audio data.
    arm_cmplx_mult_cmplx_f32(buffer_spec_FFT, FIR_filter_mask, iFFT_buffer, FFT_LENGTH);
    
    // After the frequency domain filter mask and other processes are complete, do a
    // complex inverse FFT to return to the time domain (if sample rate = 192kHz, 
    // we are in 24ksps now, because we decimated by 8)
    // perform iFFT (in-place) by setting the IFFT flag=1 in the Arm CFFT function.
    #ifdef TESTMODE
    arm_cfft_radix2_init_f32(&S, 512, 1, 1);
    arm_cfft_radix2_f32(&S,iFFT_buffer);
    #else
    arm_cfft_f32(&arm_cfft_sR_f32_len512, iFFT_buffer, 1, 1);
    #endif

    // Now discard the first 256 complex samples
    for (unsigned i = 0; i < data->N; i++) {
        data->I[i] = iFFT_buffer[256*2 + 2*i];
        data->Q[i] = iFFT_buffer[256*2 + 2*i+1];
    }
    return ESUCCESS;
}

void ApplyEQBandFilter(DataBlock *data, FilterConfig *filters, uint8_t bf, TXRXType TXRX){
    int sign = 1;
    if (bf%2 == 0) sign = -1;
    float32_t scale;
    if (TXRX == RX) scale = (float)EEPROMData.equalizerRec[bf] / 100.0;
    else scale = (float)EEPROMData.equalizerXmt[bf] / 100.0;
    // Filter I with this band's biquad
    if (TXRX == RX)
        arm_biquad_cascade_df2T_f32(&filters->S_Rec[bf], data->I, filters->eqFiltBuffer, data->N);
    else
        arm_biquad_cascade_df2T_f32(&filters->S_Xmt
            [bf], data->I, filters->eqFiltBuffer, data->N);
    // Scale the amplitude by the overall level scaler
    arm_scale_f32(filters->eqFiltBuffer, (float32_t)sign*scale, filters->eqFiltBuffer, data->N);
    // Add to the accumulator buffer
    arm_add_f32(filters->eqSumBuffer, filters->eqFiltBuffer, filters->eqSumBuffer, data->N);
}

void BandEQ(DataBlock *data, FilterConfig *filters, TXRXType TXRX){
    // Apply 14 successive filters, accumulating in filters->eqSumBuffer as we go
    memset(filters->eqSumBuffer, 0, sizeof(float32_t)*data->N);
    for (int i = 0; i < 14; i++) {
        ApplyEQBandFilter(data, filters, i, TXRX);
    }
    // Overwrite data->I with the filtered and accumulated data
    for (size_t i =0; i<data->N; i++){
        data->I[i] = filters->eqSumBuffer[i];
    }
}

//////////////////////////////////////////////////////////////////////////
#define FIXED

arm_fir_decimate_instance_f32 FIR_dec1_EX_I;
arm_fir_decimate_instance_f32 FIR_dec1_EX_Q;
float32_t DMAMEM FIR_dec1_EX_I_state[2095];
float32_t DMAMEM FIR_dec1_EX_Q_state[2095];
extern float32_t coeffs192K_10K_LPF_FIR[48];

arm_fir_decimate_instance_f32 FIR_dec2_EX_I;
arm_fir_decimate_instance_f32 FIR_dec2_EX_Q;
#ifdef FIXED
float32_t DMAMEM FIR_dec2_EX_I_state[559]; // was 535 before being fixed
float32_t DMAMEM FIR_dec2_EX_Q_state[559];
#else
float32_t DMAMEM FIR_dec2_EX_I_state[535];
float32_t DMAMEM FIR_dec2_EX_Q_state[535];
#endif
extern float32_t coeffs48K_8K_LPF_FIR[48];

arm_fir_decimate_instance_f32 FIR_dec3_EX_I;
arm_fir_decimate_instance_f32 FIR_dec3_EX_Q;
#ifdef FIXED
float32_t FIR_dec3_EX_I_state[303];// State vector size should be numtaps+blocksize-1 = 48+256-1 = 303
float32_t FIR_dec3_EX_Q_state[303];
#else
float32_t FIR_dec3_EX_I_state[280]; //267, but this segfaults
float32_t FIR_dec3_EX_Q_state[280];
#endif
extern float32_t coeffs12K_8K_LPF_FIR[48];

arm_fir_instance_f32 FIR_Hilbert_L;
arm_fir_instance_f32 FIR_Hilbert_R;
float32_t FIR_Hilbert_state_L[100 + 256 - 1];
float32_t FIR_Hilbert_state_R[100 + 256 - 1];
extern float32_t FIR_Hilbert_coeffs_45[100];
extern float32_t FIR_Hilbert_coeffs_neg_45[100];

arm_fir_interpolate_instance_f32 FIR_int3_EX_I;
arm_fir_interpolate_instance_f32 FIR_int3_EX_Q;
#ifdef FIXED
float32_t DMAMEM FIR_int3_EX_I_state[175]; // was 151 
float32_t DMAMEM FIR_int3_EX_Q_state[175]; // 48+128-1 = 175
#else
float32_t DMAMEM FIR_int3_EX_I_state[519];
float32_t DMAMEM FIR_int3_EX_Q_state[519];
#endif

#ifdef FIXED
float32_t FIR_int3_12ksps_48tap_2k7[48] = {
 -2.285169471669272310E-6,
-33.85457593285185850E-6,
-90.05495214272963270E-6,
-102.5104398169568180E-6,
 58.41569034528380660E-6,
 454.7904191315104190E-6,
 871.4162433760077420E-6,
 749.7881965886456330E-6,
-468.4956850258753320E-6,
-0.002616105921239791,
-0.004246098300098818,
-0.003078738032270572,
 0.002226282725246520,
 0.009889569653623405,
 0.014414561535852018,
 0.009226938890949424,
-0.007959807904613685,
-0.030336057178768423,
-0.042078062750789499,
-0.025361321875102511,
 0.028447546456669172,
 0.110201064956298028,
 0.193569812629121624,
 0.246261318040085941,
 0.246261318040085941,
 0.193569812629121624,
 0.110201064956298028,
 0.028447546456669172,
-0.025361321875102511,
-0.042078062750789499,
-0.030336057178768423,
-0.007959807904613685,
 0.009226938890949424,
 0.014414561535852018,
 0.009889569653623405,
 0.002226282725246520,
-0.003078738032270572,
-0.004246098300098818,
-0.002616105921239791,
-468.4956850258753320E-6,
 749.7881965886456330E-6,
 871.4162433760077420E-6,
 454.7904191315104190E-6,
 58.41569034528380660E-6,
-102.5104398169568180E-6,
-90.05495214272963270E-6,
-33.85457593285185850E-6,
-2.285169471669272310E-6,
};
#endif

arm_fir_interpolate_instance_f32 FIR_int1_EX_I;
arm_fir_interpolate_instance_f32 FIR_int1_EX_Q;
#ifdef FIXED
float32_t DMAMEM FIR_int1_EX_I_state[303]; // 256 + 48 - 1 = 303
float32_t DMAMEM FIR_int1_EX_Q_state[303]; // 256 + 48 - 1 = 303
#else
float32_t DMAMEM FIR_int1_EX_I_state[279];
float32_t DMAMEM FIR_int1_EX_Q_state[279];
#endif

arm_fir_interpolate_instance_f32 FIR_int2_EX_I;
arm_fir_interpolate_instance_f32 FIR_int2_EX_Q;
#ifdef FIXED
float32_t DMAMEM FIR_int2_EX_I_state[559]; // 512+48-1 = 559
float32_t DMAMEM FIR_int2_EX_Q_state[559]; // 512+48-1 = 559
#else
float32_t DMAMEM FIR_int2_EX_I_state[519];
float32_t DMAMEM FIR_int2_EX_Q_state[519];
#endif

void HilbertTransform(float32_t *I,float32_t *Q){
    //Hilbert transforms at 12K, with 5KHz bandwidth, buffer size 128
    arm_fir_f32(&FIR_Hilbert_L, I, I, 128);
    arm_fir_f32(&FIR_Hilbert_R, Q, Q, 128);
}

void TXDecInit(void){
    CLEAR_VAR(FIR_dec1_EX_I_state);
    CLEAR_VAR(FIR_dec1_EX_Q_state);
    arm_fir_decimate_init_f32(&FIR_dec1_EX_I, 48, 4, coeffs192K_10K_LPF_FIR, FIR_dec1_EX_I_state, 2048);
    arm_fir_decimate_init_f32(&FIR_dec1_EX_Q, 48, 4, coeffs192K_10K_LPF_FIR, FIR_dec1_EX_Q_state, 2048);

    CLEAR_VAR(FIR_dec2_EX_I_state);
    CLEAR_VAR(FIR_dec2_EX_Q_state);
    #ifdef FIXED
    arm_fir_decimate_init_f32(&FIR_dec2_EX_I, 48, 2, coeffs48K_8K_LPF_FIR, FIR_dec2_EX_I_state, 512);
    arm_fir_decimate_init_f32(&FIR_dec2_EX_Q, 48, 2, coeffs48K_8K_LPF_FIR, FIR_dec2_EX_Q_state, 512);
    #else
    arm_fir_decimate_init_f32(&FIR_dec2_EX_I, 24, 2, coeffs48K_8K_LPF_FIR, FIR_dec2_EX_I_state, 512);
    arm_fir_decimate_init_f32(&FIR_dec2_EX_Q, 24, 2, coeffs48K_8K_LPF_FIR, FIR_dec2_EX_Q_state, 512);    
    #endif

    //3rd Decimate Excite 2x to 12K SPS
    CLEAR_VAR(FIR_dec3_EX_I_state);
    CLEAR_VAR(FIR_dec3_EX_Q_state);
    #ifdef FIXED
    arm_fir_decimate_init_f32(&FIR_dec3_EX_I, 48, 2, coeffs12K_8K_LPF_FIR, FIR_dec3_EX_I_state, 256);
    arm_fir_decimate_init_f32(&FIR_dec3_EX_Q, 48, 2, coeffs12K_8K_LPF_FIR, FIR_dec3_EX_Q_state, 256);
    #else
    arm_fir_decimate_init_f32(&FIR_dec3_EX_I, 24, 2, coeffs12K_8K_LPF_FIR, FIR_dec3_EX_I_state, 256);
    arm_fir_decimate_init_f32(&FIR_dec3_EX_Q, 24, 2, coeffs12K_8K_LPF_FIR, FIR_dec3_EX_Q_state, 256);
    #endif

    CLEAR_VAR(FIR_Hilbert_state_L);
    CLEAR_VAR(FIR_Hilbert_state_R);
    arm_fir_init_f32(&FIR_Hilbert_L, 100, FIR_Hilbert_coeffs_45, FIR_Hilbert_state_L, 128);
    arm_fir_init_f32(&FIR_Hilbert_R, 100, FIR_Hilbert_coeffs_neg_45, FIR_Hilbert_state_R, 128);

    CLEAR_VAR(FIR_int3_EX_I_state);
    CLEAR_VAR(FIR_int3_EX_Q_state);
    #ifdef FIXED
    arm_fir_interpolate_init_f32(&FIR_int3_EX_I, 2, 48, FIR_int3_12ksps_48tap_2k7, FIR_int3_EX_I_state, 128);
    arm_fir_interpolate_init_f32(&FIR_int3_EX_Q, 2, 48, FIR_int3_12ksps_48tap_2k7, FIR_int3_EX_Q_state, 128);
    #else
    arm_fir_interpolate_init_f32(&FIR_int3_EX_I, 2, 48, coeffs12K_8K_LPF_FIR, FIR_int3_EX_I_state, 128);
    arm_fir_interpolate_init_f32(&FIR_int3_EX_Q, 2, 48, coeffs12K_8K_LPF_FIR, FIR_int3_EX_Q_state, 128);
    #endif

    CLEAR_VAR(FIR_int1_EX_I_state);
    CLEAR_VAR(FIR_int1_EX_Q_state);
    arm_fir_interpolate_init_f32(&FIR_int1_EX_I, 2, 48, coeffs48K_8K_LPF_FIR, FIR_int1_EX_I_state, 256);
    arm_fir_interpolate_init_f32(&FIR_int1_EX_Q, 2, 48, coeffs48K_8K_LPF_FIR, FIR_int1_EX_Q_state, 256);

    CLEAR_VAR(FIR_int2_EX_I_state);
    CLEAR_VAR(FIR_int2_EX_Q_state);
    #ifdef FIXED
    arm_fir_interpolate_init_f32(&FIR_int2_EX_I, 4, 48, coeffs192K_10K_LPF_FIR, FIR_int2_EX_I_state, 512);
    arm_fir_interpolate_init_f32(&FIR_int2_EX_Q, 4, 48, coeffs192K_10K_LPF_FIR, FIR_int2_EX_Q_state, 512);
    #else
    arm_fir_interpolate_init_f32(&FIR_int2_EX_I, 4, 32, coeffs192K_10K_LPF_FIR, FIR_int2_EX_I_state, 512);
    arm_fir_interpolate_init_f32(&FIR_int2_EX_Q, 4, 32, coeffs192K_10K_LPF_FIR, FIR_int2_EX_Q_state, 512);
    #endif

}

void SidebandSelection(float32_t *I, float32_t *Q){
    // Math works out for selecting LSB by default
    if (bands[EEPROMData.currentBand].mode == USB){
        arm_scale_f32(I,-1,I,256);
    }
}

void TXDecimateBy4(float32_t *I,float32_t *Q){
    // 192KHz effective sample rate here
    // decimation-by-4 in-place!
    arm_fir_decimate_f32(&FIR_dec1_EX_I, I, I, BUFFER_SIZE * N_BLOCKS);
    arm_fir_decimate_f32(&FIR_dec1_EX_Q, Q, Q, BUFFER_SIZE * N_BLOCKS);
}

void TXDecimateBy2(float32_t *I,float32_t *Q){
    // 48KHz effective sample rate here
    // decimation-by-2 in-place
    arm_fir_decimate_f32(&FIR_dec2_EX_I, I, I, 512);
    arm_fir_decimate_f32(&FIR_dec2_EX_Q, Q, Q, 512);
}

void TXDecimateBy2Again(float32_t *I,float32_t *Q){
    //Decimate by 2 to 12K SPS sample rate
    arm_fir_decimate_f32(&FIR_dec3_EX_I, I, I, 256);
    arm_fir_decimate_f32(&FIR_dec3_EX_Q, Q, Q, 256);
}

void TXInterpolateBy2Again(float32_t *I,float32_t *Q, float32_t *Iout,float32_t *Qout){
    //Interpolate back to 24K SPS
    arm_fir_interpolate_f32(&FIR_int3_EX_I, I, Iout, 128);
    arm_scale_f32(Iout,2,Iout,256);
    arm_fir_interpolate_f32(&FIR_int3_EX_Q, Q, Qout, 128);
    arm_scale_f32(Qout,2,Qout,256);
}

void TXInterpolateBy2(float32_t *I,float32_t *Q, float32_t *Iout,float32_t *Qout){
    //24KHz effective sample rate input, 48 kHz output
    arm_fir_interpolate_f32(&FIR_int1_EX_I, I, Iout, 256);
    arm_scale_f32(Iout,2,Iout,512);
    arm_fir_interpolate_f32(&FIR_int1_EX_Q, Q, Qout, 256);
    arm_scale_f32(Qout,2,Qout,512);
}

void TXInterpolateBy4(float32_t *I,float32_t *Q, float32_t *Iout,float32_t *Qout){
    //48KHz effective sample rate input, 128 kHz output
    arm_fir_interpolate_f32(&FIR_int2_EX_I, I, Iout, 512);
    arm_scale_f32(Iout,4,Iout,2048);
    arm_fir_interpolate_f32(&FIR_int2_EX_Q, Q, Qout, 512);
    arm_scale_f32(Qout,4,Qout,2048);
}



#define IIR_NUMSTAGES 4
extern float32_t EQ_Band1Coeffs[20];
extern float32_t EQ_Band2Coeffs[20];
extern float32_t EQ_Band3Coeffs[20];
extern float32_t EQ_Band4Coeffs[20];
extern float32_t EQ_Band5Coeffs[20];
extern float32_t EQ_Band6Coeffs[20];
extern float32_t EQ_Band7Coeffs[20];
extern float32_t EQ_Band8Coeffs[20];
extern float32_t EQ_Band9Coeffs[20];
extern float32_t EQ_Band10Coeffs[20];
extern float32_t EQ_Band11Coeffs[20];
extern float32_t EQ_Band12Coeffs[20];
extern float32_t EQ_Band13Coeffs[20];
extern float32_t EQ_Band14Coeffs[20];
float32_t xmt_EQ_Band1_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };  //declare and zero biquad state variables
float32_t xmt_EQ_Band2_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band3_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band4_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band5_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band6_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band7_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band8_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band9_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band10_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band11_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band12_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band13_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
float32_t xmt_EQ_Band14_state[IIR_NUMSTAGES * 2] = { 0, 0, 0, 0, 0, 0, 0, 0 };
arm_biquad_cascade_df2T_instance_f32 S1_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band1_state, EQ_Band1Coeffs };
arm_biquad_cascade_df2T_instance_f32 S2_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band2_state, EQ_Band2Coeffs };
arm_biquad_cascade_df2T_instance_f32 S3_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band3_state, EQ_Band3Coeffs };
arm_biquad_cascade_df2T_instance_f32 S4_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band4_state, EQ_Band4Coeffs };
arm_biquad_cascade_df2T_instance_f32 S5_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band5_state, EQ_Band5Coeffs };
arm_biquad_cascade_df2T_instance_f32 S6_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band6_state, EQ_Band6Coeffs };
arm_biquad_cascade_df2T_instance_f32 S7_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band7_state, EQ_Band7Coeffs };
arm_biquad_cascade_df2T_instance_f32 S8_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band8_state, EQ_Band8Coeffs };
arm_biquad_cascade_df2T_instance_f32 S9_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band9_state, EQ_Band9Coeffs };
arm_biquad_cascade_df2T_instance_f32 S10_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band10_state, EQ_Band10Coeffs };
arm_biquad_cascade_df2T_instance_f32 S11_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band11_state, EQ_Band11Coeffs };
arm_biquad_cascade_df2T_instance_f32 S12_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band12_state, EQ_Band12Coeffs };
arm_biquad_cascade_df2T_instance_f32 S13_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band13_state, EQ_Band13Coeffs };
arm_biquad_cascade_df2T_instance_f32 S14_Xmt = { IIR_NUMSTAGES, xmt_EQ_Band14_state, EQ_Band14Coeffs };
float32_t DMAMEM xmt_EQ1_float_buffer_L[256];
float32_t DMAMEM xmt_EQ2_float_buffer_L[256];
float32_t DMAMEM xmt_EQ3_float_buffer_L[256];
float32_t DMAMEM xmt_EQ4_float_buffer_L[256];
float32_t DMAMEM xmt_EQ5_float_buffer_L[256];
float32_t DMAMEM xmt_EQ6_float_buffer_L[256];
float32_t DMAMEM xmt_EQ7_float_buffer_L[256];
float32_t DMAMEM xmt_EQ8_float_buffer_L[256];
float32_t DMAMEM xmt_EQ9_float_buffer_L[256];
float32_t DMAMEM xmt_EQ10_float_buffer_L[256];
float32_t DMAMEM xmt_EQ11_float_buffer_L[256];
float32_t DMAMEM xmt_EQ12_float_buffer_L[256];
float32_t DMAMEM xmt_EQ13_float_buffer_L[256];
float32_t DMAMEM xmt_EQ14_float_buffer_L[256];

void DoExciterEQ(float32_t *float_buffer_L_EX)  
{
  arm_biquad_cascade_df2T_f32(&S1_Xmt, float_buffer_L_EX, xmt_EQ1_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S2_Xmt, float_buffer_L_EX, xmt_EQ2_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S3_Xmt, float_buffer_L_EX, xmt_EQ3_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S4_Xmt, float_buffer_L_EX, xmt_EQ4_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S5_Xmt, float_buffer_L_EX, xmt_EQ5_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S6_Xmt, float_buffer_L_EX, xmt_EQ6_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S7_Xmt, float_buffer_L_EX, xmt_EQ7_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S8_Xmt, float_buffer_L_EX, xmt_EQ8_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S9_Xmt, float_buffer_L_EX, xmt_EQ9_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S10_Xmt, float_buffer_L_EX, xmt_EQ10_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S11_Xmt, float_buffer_L_EX, xmt_EQ11_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S12_Xmt, float_buffer_L_EX, xmt_EQ12_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S13_Xmt, float_buffer_L_EX, xmt_EQ13_float_buffer_L, 256);
  arm_biquad_cascade_df2T_f32(&S14_Xmt, float_buffer_L_EX, xmt_EQ14_float_buffer_L, 256);

  arm_scale_f32(xmt_EQ1_float_buffer_L, -EEPROMData.equalizerXmt[0]/100, xmt_EQ1_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ2_float_buffer_L, EEPROMData.equalizerXmt[1]/100, xmt_EQ2_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ3_float_buffer_L, -EEPROMData.equalizerXmt[2]/100, xmt_EQ3_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ4_float_buffer_L, EEPROMData.equalizerXmt[3]/100, xmt_EQ4_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ5_float_buffer_L, -EEPROMData.equalizerXmt[4]/100, xmt_EQ5_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ6_float_buffer_L, EEPROMData.equalizerXmt[5]/100, xmt_EQ6_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ7_float_buffer_L, -EEPROMData.equalizerXmt[6]/100, xmt_EQ7_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ8_float_buffer_L, EEPROMData.equalizerXmt[7]/100, xmt_EQ8_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ9_float_buffer_L, -EEPROMData.equalizerXmt[8]/100, xmt_EQ9_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ10_float_buffer_L, EEPROMData.equalizerXmt[9]/100, xmt_EQ10_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ11_float_buffer_L, -EEPROMData.equalizerXmt[10]/100, xmt_EQ11_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ12_float_buffer_L, EEPROMData.equalizerXmt[11]/100, xmt_EQ12_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ13_float_buffer_L, -EEPROMData.equalizerXmt[12]/100, xmt_EQ13_float_buffer_L, 256);
  arm_scale_f32(xmt_EQ14_float_buffer_L, EEPROMData.equalizerXmt[13]/100, xmt_EQ14_float_buffer_L, 256);

  arm_add_f32(xmt_EQ1_float_buffer_L, xmt_EQ2_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ3_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ4_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ5_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ6_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ7_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ8_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ9_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ10_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ11_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ12_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ13_float_buffer_L, float_buffer_L_EX, 256);
  arm_add_f32(float_buffer_L_EX, xmt_EQ14_float_buffer_L, float_buffer_L_EX, 256);
}




