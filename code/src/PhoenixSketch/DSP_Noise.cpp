#include "DSP_Noise.h"

// DMAMEM places these in OCRAM, which is accessible by DMA and is slower
float32_t DMAMEM NR_FFT_buffer[2 * NR_FFT_L] __attribute__((aligned(4)));
float32_t DMAMEM NR_last_sample_buffer_L[NR_FFT_L / 2];
float32_t DMAMEM NR_X[NR_FFT_L / 2][3];
float32_t DMAMEM NR_E[NR_FFT_L / 2][15];
float32_t DMAMEM NR_M[NR_FFT_L / 2];
float32_t DMAMEM NR_lambda[NR_FFT_L / 2];
float32_t DMAMEM NR_G[NR_FFT_L / 2];
float32_t DMAMEM NR_Gts[NR_FFT_L / 2][2];
float32_t DMAMEM NR_output_audio_buffer[NR_FFT_L];
float32_t DMAMEM NR_last_iFFT_result[NR_FFT_L / 2];
float32_t ANR_d[ANR_DLINE_SIZE];
float32_t ANR_w[ANR_DLINE_SIZE];
float32_t DMAMEM NR_Hk_old[NR_FFT_L / 2];
float32_t DMAMEM NR_Nest[NR_FFT_L / 2][2];
float32_t DMAMEM NR_SNR_post[NR_FFT_L / 2];
float32_t DMAMEM NR_SNR_prio[NR_FFT_L / 2];
float32_t DMAMEM NR_long_tone_gain[NR_FFT_L / 2];

const uint8_t NR_L_frames = 3;
const uint8_t NR_N_frames = 15;
const uint8_t NR_use_X = 0;
const float32_t NR_PSI = 3.0;
const float32_t NR_KIM_K = 1.0;
const float32_t NR_alpha = 0.95;
const float32_t NR_onemalpha = (1.0 - NR_alpha);
const float32_t NR_beta = 0.85;
const float32_t NR_onemtwobeta = (1.0 - (2.0 * NR_beta));
const float32_t NR_onembeta = 1.0 - NR_beta;
const uint16_t ANR_buff_size = FFT_LENGTH / 2.0;
const uint8_t ANR_taps = 64;
const uint8_t ANR_delay = 16;
const uint32_t ANR_mask = ANR_DLINE_SIZE - 1;
const float32_t ANR_two_mu = 0.0001;
const float32_t ANR_lincr = 1.0;
const float32_t ANR_lidx_max = 200.0;
const float32_t ANR_ldecr = 3.0;
const float32_t ANR_lidx_min = 120.0;
const float32_t ANR_gamma = 0.1;
const float32_t ANR_den_mult = 6.25e-10;

uint32_t NR_X_pointer = 0;
uint32_t NR_E_pointer = 0;
float32_t NR_sum = 0;

uint32_t ANR_in_idx = 0;
float32_t ANR_ngamma = 0.001;
float32_t ANR_lidx = 120.0;

#ifdef TESTMODE
arm_cfft_radix2_instance_f32 Sfor;
arm_cfft_radix2_instance_f32 Srev;
#endif

const float32_t sqrtHann[256] = {
  0, 0.01231966, 0.024637449, 0.036951499, 0.049259941, 0.061560906,
  0.073852527, 0.086132939, 0.098400278, 0.110652682, 0.122888291, 0.135105247, 0.147301698,
  0.159475791, 0.171625679, 0.183749518, 0.195845467, 0.207911691, 0.219946358, 0.231947641, 0.24391372,
  0.255842778, 0.267733003, 0.279582593, 0.291389747, 0.303152674, 0.314869589, 0.326538713, 0.338158275,
  0.349726511, 0.361241666, 0.372701992, 0.384105749, 0.395451207, 0.406736643, 0.417960345, 0.429120609,
  0.440215741, 0.451244057, 0.462203884, 0.473093557, 0.483911424, 0.494655843, 0.505325184, 0.515917826,
  0.526432163, 0.536866598, 0.547219547, 0.557489439, 0.567674716, 0.577773831, 0.587785252, 0.597707459,
  0.607538946, 0.617278221, 0.626923806, 0.636474236, 0.645928062, 0.65528385, 0.664540179, 0.673695644,
  0.682748855, 0.691698439, 0.700543038, 0.709281308, 0.717911923, 0.726433574, 0.734844967, 0.743144825,
  0.75133189, 0.759404917, 0.767362681, 0.775203976, 0.78292761, 0.790532412, 0.798017227, 0.805380919,
  0.812622371, 0.819740483, 0.826734175, 0.833602385, 0.840344072, 0.846958211, 0.853443799, 0.859799851,
  0.866025404, 0.872119511, 0.878081248, 0.88390971, 0.889604013, 0.895163291, 0.900586702, 0.905873422,
  0.911022649, 0.916033601, 0.920905518, 0.92563766, 0.930229309, 0.934679767, 0.938988361, 0.943154434,
  0.947177357, 0.951056516, 0.954791325, 0.958381215, 0.961825643, 0.965124085, 0.968276041, 0.971281032,
  0.974138602, 0.976848318, 0.979409768, 0.981822563, 0.984086337, 0.986200747, 0.988165472, 0.989980213,
  0.991644696, 0.993158666, 0.994521895, 0.995734176, 0.996795325, 0.99770518, 0.998463604, 0.999070481,
  0.99952572, 0.99982925, 0.999981027, 0.999981027, 0.99982925, 0.99952572, 0.999070481, 0.998463604,
  0.99770518, 0.996795325, 0.995734176, 0.994521895, 0.993158666, 0.991644696, 0.989980213, 0.988165472,
  0.986200747, 0.984086337, 0.981822563, 0.979409768, 0.976848318, 0.974138602, 0.971281032, 0.968276041,
  0.965124085, 0.961825643, 0.958381215, 0.954791325, 0.951056516, 0.947177357, 0.943154434, 0.938988361,
  0.934679767, 0.930229309, 0.92563766, 0.920905518, 0.916033601, 0.911022649, 0.905873422, 0.900586702,
  0.895163291, 0.889604013, 0.88390971, 0.878081248, 0.872119511, 0.866025404, 0.859799851, 0.853443799,
  0.846958211, 0.840344072, 0.833602385, 0.826734175, 0.819740483, 0.812622371, 0.805380919, 0.798017227,
  0.790532412, 0.78292761, 0.775203976, 0.767362681, 0.759404917, 0.75133189, 0.743144825, 0.734844967,
  0.726433574, 0.717911923, 0.709281308, 0.700543038, 0.691698439, 0.682748855, 0.673695644, 0.664540179,
  0.65528385, 0.645928062, 0.636474236, 0.626923806, 0.617278221, 0.607538946, 0.597707459, 0.587785252,
  0.577773831, 0.567674716, 0.557489439, 0.547219547, 0.536866598, 0.526432163, 0.515917826, 0.505325184,
  0.494655843, 0.483911424, 0.473093557, 0.462203884, 0.451244057, 0.440215741, 0.429120609, 0.417960345,
  0.406736643, 0.395451207, 0.384105749, 0.372701992, 0.361241666, 0.349726511, 0.338158275, 0.326538713,
  0.314869589, 0.303152674, 0.291389747, 0.279582593, 0.267733003, 0.255842778, 0.24391372, 0.231947641,
  0.219946358, 0.207911691, 0.195845467, 0.183749518, 0.171625679, 0.159475791, 0.147301698, 0.135105247,
  0.122888291, 0.110652682, 0.098400278, 0.086132939, 0.073852527, 0.061560906, 0.049259941, 0.036951499,
  0.024637449, 0.01231966, 0
};

void InitializeKim1NoiseReduction(void){
    CLEAR_VAR(NR_FFT_buffer);
    CLEAR_VAR(NR_last_sample_buffer_L);
    CLEAR_VAR(NR_X);
    CLEAR_VAR(NR_E);
    CLEAR_VAR(NR_M);
    CLEAR_VAR(NR_lambda);
    CLEAR_VAR(NR_G);
    CLEAR_VAR(NR_Gts);
    CLEAR_VAR(NR_output_audio_buffer);
    CLEAR_VAR(NR_last_iFFT_result);
    #ifdef TESTMODE
    arm_cfft_radix2_init_f32(&Sfor, 256, 0, 1);
    arm_cfft_radix2_init_f32(&Srev, 256, 1, 1);
    #endif
}

/**
 * Apply Kim & Ruwisch 2002 noise reduction. Thanks to Michael DL2FW for your support!
 * This is exactly the implementation by
 * Kim & Ruwisch 2002 - 7th International Conference on Spoken Language Processing Denver, Colorado, USA
 * with two exceptions:
 * 1.) we use power instead of magnitude for X
 * 2.) we need to clamp for negative gains
 * 
 * @param data Pointer to the DataBlock containing the data to act upon
 */
void Kim1_NR(DataBlock *data){
    // perform a loop two times (each time process 128 new samples)
    // FFT 256 points
    // frame step 128 samples
    // half-overlapped data buffers
    uint8_t VAD_low = 0;
    uint8_t VAD_high = 127;
    float32_t lf_freq; // = (offset - width/2) / (12000 / NR_FFT_L); // bin BW is 46.9Hz [12000Hz / 256 bins] @96kHz
    float32_t uf_freq;
    if (bands[EEPROMData.currentBand].FLoCut_Hz <= 0 && bands[EEPROMData.currentBand].FHiCut_Hz >= 0) {
        lf_freq = 0.0;
        uf_freq = fmax(-(float32_t)bands[EEPROMData.currentBand].FLoCut_Hz, (float32_t)bands[EEPROMData.currentBand].FHiCut_Hz);
    } else {
        if (bands[EEPROMData.currentBand].FLoCut_Hz > 0) {
            lf_freq = (float32_t)bands[EEPROMData.currentBand].FLoCut_Hz;
            uf_freq = (float32_t)bands[EEPROMData.currentBand].FHiCut_Hz;
        } else {
            uf_freq = -(float32_t)bands[EEPROMData.currentBand].FLoCut_Hz;
            lf_freq = -(float32_t)bands[EEPROMData.currentBand].FHiCut_Hz;
        }
    }
    lf_freq /= (data->sampleRate_Hz / NR_FFT_L); // bin BW is 46.9Hz [12000Hz / 256 bins] @96kHz
    uf_freq /= (data->sampleRate_Hz / NR_FFT_L);

    VAD_low = (int)lf_freq;
    VAD_high = (int)uf_freq;
    if (VAD_low == VAD_high) {
        VAD_high++;
    }
    if (VAD_low < 1) {
        VAD_low = 1;
    } else if (VAD_low > NR_FFT_L / 2 - 2) {
        VAD_low = NR_FFT_L / 2 - 2;
    }
    if (VAD_high < 1) {
        VAD_high = 1;
    } else if (VAD_high > NR_FFT_L / 2) {
        VAD_high = NR_FFT_L / 2;
    }

    for (int k = 0; k < 2; k++) {
        // NR_FFT_buffer is 512 floats big
        // interleaved r, i, r, i . . .
        // fill first half of FFT_buffer with last events audio samples
        for (int i = 0; i < NR_FFT_L / 2; i++) {
            NR_FFT_buffer[i * 2] = NR_last_sample_buffer_L[i]; // real
            NR_FFT_buffer[i * 2 + 1] = 0.0; // imaginary
        }
        // copy recent samples to last_sample_buffer for next time!
        for (int i = 0; i < NR_FFT_L  / 2; i++) {
            NR_last_sample_buffer_L [i] = data->I[i + k * (NR_FFT_L / 2)];
        }
        // now fill recent audio samples into second half of FFT_buffer
        for (int i = 0; i < NR_FFT_L / 2; i++) {
            NR_FFT_buffer[NR_FFT_L + i * 2] = data->I[i + k * (NR_FFT_L / 2)]; // real
            NR_FFT_buffer[NR_FFT_L + i * 2 + 1] = 0.0;
        }
        // perform windowing on 256 real samples in the NR_FFT_buffer
        for (int idx = 0; idx < NR_FFT_L; idx++)  {     // Hann window
            float32_t temp_sample = 0.5 * (float32_t)(1.0 - (cosf(PI * 2.0 * (float32_t)idx / (float32_t)((NR_FFT_L) - 1))));
            NR_FFT_buffer[idx * 2] *= temp_sample;
        }
        #ifdef TESTMODE
        arm_cfft_radix2_f32(&Sfor,NR_FFT_buffer);
        #else
        arm_cfft_f32(&arm_cfft_sR_f32_len256, NR_FFT_buffer, 0, 1);
        #endif
        // take first 128 bin values of the FFT result
        for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) { 
            // it seems that taking power works better than taking magnitude . . . !?
            NR_X[bindx][NR_X_pointer] = (NR_FFT_buffer[bindx * 2] * NR_FFT_buffer[bindx * 2] + NR_FFT_buffer[bindx * 2 + 1] * NR_FFT_buffer[bindx * 2 + 1]);
        }
        // take first 128 bin values of the FFT result
        for (int bindx = VAD_low; bindx < VAD_high; bindx++) { 
            NR_sum = 0.0;
            for (int j = 0; j < NR_L_frames; j++)
            { // sum up the L_frames |X|
            NR_sum = NR_sum + NR_X[bindx][j];
            }
            // divide sum of L_frames |X| by L_frames to calculate the average and save in NR_E
            NR_E[bindx][NR_E_pointer] = NR_sum / (float32_t)NR_L_frames;
        }
        for (int bindx = VAD_low; bindx < VAD_high; bindx++) { // take first 128 bin values of the FFT result
                                                // we have to reset the minimum value to the first E value every time we start with a bin
            NR_M[bindx] = NR_E[bindx][0];
                                                // therefore we start with the second E value (index j == 1)
            for (uint8_t j = 1; j < NR_N_frames; j++) {
            if (NR_E[bindx][j] < NR_M[bindx]) {
                NR_M[bindx] = NR_E[bindx][j];
            }
            }
        }
        for (int bindx = VAD_low; bindx < VAD_high; bindx++) { // take first 128 bin values of the FFT result
            float32_t NR_T = NR_X[bindx][NR_X_pointer] / NR_M[bindx]; // dies scheint mir besser zu funktionieren !
            if (NR_T > NR_PSI) {
            NR_lambda[bindx] = NR_M[bindx];
            } else {
            NR_lambda[bindx] = NR_E[bindx][NR_E_pointer];
            }
        }

        for (int bindx = VAD_low; bindx < VAD_high; bindx++) { // take first 128 bin values of the FFT result
            if (NR_use_X) {
            NR_G[bindx] = 1.0 - (NR_lambda[bindx] * NR_KIM_K / NR_X[bindx][NR_X_pointer]);
            if (NR_G[bindx] < 0.0)
                NR_G[bindx] = 0.0;
            } else {
            NR_G[bindx] = 1.0 - (NR_lambda[bindx] * NR_KIM_K / NR_E[bindx][NR_E_pointer]);
            if (NR_G[bindx] < 0.0)
                NR_G[bindx] = 0.0;
            }

            // time smoothing
            NR_Gts[bindx][0] = NR_alpha * NR_Gts[bindx][1] + (NR_onemalpha) * NR_G[bindx];
            NR_Gts[bindx][1] = NR_Gts[bindx][0]; // copy for next FFT frame
        }

        // NR_G is always positive, however often 0.0
        for (int bindx = 1; bindx < ((NR_FFT_L / 2) - 1); bindx++) {// take first 128 bin values of the FFT result
            NR_G[bindx] = NR_beta * NR_Gts[bindx - 1][0] + NR_onemtwobeta * NR_Gts[bindx][0] + NR_beta * NR_Gts[bindx + 1][0];
        }
                                                                                // take care of bin 0 and bin NR_FFT_L/2 - 1
        NR_G[0] = (NR_onemtwobeta + NR_beta) * NR_Gts[0][0] + NR_beta * NR_Gts[1][0];
        NR_G[(NR_FFT_L / 2) - 1] = NR_beta * NR_Gts[(NR_FFT_L / 2) - 2][0] + (NR_onemtwobeta + NR_beta) * NR_Gts[(NR_FFT_L / 2) - 1][0];
        for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) {                                      // try 128:
            NR_FFT_buffer[bindx * 2] = NR_FFT_buffer [bindx * 2] * NR_G[bindx];                     // real part
            NR_FFT_buffer[bindx * 2 + 1] = NR_FFT_buffer [bindx * 2 + 1] * NR_G[bindx];             // imag part
            NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 2] = NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 2] * NR_G[bindx]; // real part conjugate symmetric
            NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 1] = NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 1] * NR_G[bindx]; // imag part conjugate symmetric
        }
        NR_X_pointer = NR_X_pointer + 1;
        if (NR_X_pointer >= NR_L_frames) {
            NR_X_pointer = 0;
        }
        // 3b ++NR_E_pointer
        NR_E_pointer = NR_E_pointer + 1;
        if (NR_E_pointer >= NR_N_frames) {
            NR_E_pointer = 0;
        }
        #ifdef TESTMODE
        arm_cfft_radix2_f32(&Srev,NR_FFT_buffer);
        #else
        arm_cfft_f32(&arm_cfft_sR_f32_len256, NR_FFT_buffer, 1, 1);
        #endif
        for (int i = 0; i < NR_FFT_L / 2; i++) { // take real part of first half of current iFFT result and add to 2nd half of last iFFT_result
            NR_output_audio_buffer[i + k * (NR_FFT_L / 2)] = NR_FFT_buffer[i * 2] + NR_last_iFFT_result[i];
        }
        for (int i = 0; i < NR_FFT_L / 2; i++) {
            NR_last_iFFT_result[i] = NR_FFT_buffer[NR_FFT_L + i * 2];
        }
    }

    for (int i = 0; i < NR_FFT_L; i++) {
        data->I[i] = NR_output_audio_buffer[i]; // * 9.0; // * 5.0;
        data->Q[i] = data->I[i];
    }
}

void InitializeXanrNoiseReduction(void){
    CLEAR_VAR(ANR_d);
    CLEAR_VAR(ANR_w);
}

/**
 * Perform variable leak LMS algorithm for automatic notch or noise reduction.
 * The output of this step is placed in data->Q
 * (c) Warren Pratt wdsp library 2016
 * 
 * @param data Pointer to the DataBlock containing the data to act upon
 * @param ANR_notch 0 if noise reduction, 1 if notch
 */
void Xanr(DataBlock *data, uint8_t ANR_notch) {
  int idx;
  float32_t c0, c1;
  float32_t y, error, sigma, inv_sigp;
  float32_t nel, nev;

  for (int i = 0; i < ANR_buff_size; i++) {
    ANR_d[ANR_in_idx] = data->I[i];

    y = 0;
    sigma = 0;

    for (int j = 0; j < ANR_taps; j++) {
      idx = (ANR_in_idx + j + ANR_delay) & ANR_mask;
      y += ANR_w[j] * ANR_d[idx];
      sigma += ANR_d[idx] * ANR_d[idx];
    }
    inv_sigp = 1.0 / (sigma + 1e-10);
    error = ANR_d[ANR_in_idx] - y;

    if (ANR_notch)
      data->Q[i] = error;                            // NOTCH FILTER
    else
      data->Q[i] = y;                                // NOISE REDUCTION

    if ((nel = error * (1.0 - ANR_two_mu * sigma * inv_sigp)) < 0.0)
      nel = -nel;
    if ((nev = ANR_d[ANR_in_idx] - (1.0 - ANR_two_mu * ANR_ngamma) * y - ANR_two_mu * error * sigma * inv_sigp) < 0.0)
      nev = -nev;
    if (nev < nel) {
      if ((ANR_lidx += ANR_lincr) > ANR_lidx_max)
        ANR_lidx = ANR_lidx_max;
      else if ((ANR_lidx -= ANR_ldecr) < ANR_lidx_min)
        ANR_lidx = ANR_lidx_min;
    }
    ANR_ngamma = ANR_gamma * (ANR_lidx * ANR_lidx) * (ANR_lidx * ANR_lidx) * ANR_den_mult;

    c0 = 1.0 - ANR_two_mu * ANR_ngamma;
    c1 = ANR_two_mu * error * inv_sigp;

    for (int j = 0; j < ANR_taps; j++) {
      idx = (ANR_in_idx + j + ANR_delay) & ANR_mask;
      ANR_w[j] = c0 * ANR_w[j] + c1 * ANR_d[idx];
    }
    ANR_in_idx = (ANR_in_idx + ANR_mask) & ANR_mask;
  }
}


void InitializeSpectralNoiseReduction(void){
    #ifdef TESTMODE
    arm_cfft_radix2_init_f32(&Sfor, 256, 0, 1);
    arm_cfft_radix2_init_f32(&Srev, 256, 1, 1);
    #endif
    for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++){
        NR_last_sample_buffer_L[bindx] = 0.1;
        NR_Hk_old[bindx] = 0.1; // old gain
        NR_Nest[bindx][0] = 0.01;
        NR_Nest[bindx][1] = 0.015;
        NR_Gts[bindx][1] = 0.1;
        NR_M[bindx] = 500.0;
        NR_E[bindx][0] = 0.1;
        NR_X[bindx][1] = 0.5;
        NR_SNR_post[bindx] = 2.0;
        NR_SNR_prio[bindx] = 1.0;
        NR_long_tone_gain[bindx] = 1.0;
    }
}

/**
 * Noise reduction with spectral subtraction rule
 * based on Romanin et al. 2009 & Schmitt et al. 2002
 * and MATLAB voicebox
 * and Gerkmann & Hendriks 2002
 * and Yao et al. 2016
 * STAND: UHSDR github 14.1.2018
 */
void SpectralNoiseReduction(DataBlock *data){
    static uint8_t NR_init_counter = 0;
    uint8_t VAD_low = 0;
    uint8_t VAD_high = 127;
    float32_t lf_freq; // = (offset - width/2) / (12000 / NR_FFT_L); // bin BW is 46.9Hz [12000Hz / 256 bins] @96kHz
    float32_t uf_freq; //= (offset + width/2) / (12000 / NR_FFT_L);

    const float32_t tinc = 0.00533333;  // frame time 5.3333ms
    const float32_t tax = 0.0239;       // noise output smoothing time constant = -tinc/ln(0.8)
    const float32_t tap = 0.05062;      // speech prob smoothing time constant = -tinc/ln(0.9) tinc = frame time (5.33ms)
    const float32_t psthr = 0.99;       // threshold for smoothed speech probability [0.99]
    const float32_t pnsaf = 0.01;       // noise probability safety value [0.01]
    const float32_t asnr = 20;          // active SNR in dB
    const float32_t psini = 0.5;        // initial speech probability [0.5]
    const float32_t pspri = 0.5;        // prior speech probability [0.5]
    static float32_t ax;                //=0.8;       // ax=exp(-tinc/tax); % noise output smoothing factor
    static float32_t ap;                //=0.9;        // ap=exp(-tinc/tap); % noise output smoothing factor
    static float32_t xih1;              // = 31.6;
    ax = expf(-tinc / tax);
    ap = expf(-tinc / tap);
    xih1 = powf(10, (float32_t)asnr / 10.0);
    static float32_t xih1r = 1.0 / (1.0 + xih1) - 1.0;
    static float32_t pfac = (1.0 / pspri - 1.0) * (1.0 + xih1);
    float32_t snr_prio_min = powf(10, - (float32_t)20 / 20.0);
    static float32_t pslp[NR_FFT_L / 2];
    static float32_t xt[NR_FFT_L / 2];
    static float32_t xtr;
    static float32_t pre_power;
    static float32_t post_power;
    static float32_t power_ratio;
    static int16_t NN;
    const int16_t NR_width = 4;
    const float32_t power_threshold = 0.4;
    float32_t ph1y[NR_FFT_L / 2];
    static int NR_first_time_2 = 1;

    if (bands[EEPROMData.currentBand].FLoCut_Hz <= 0 && bands[EEPROMData.currentBand].FHiCut_Hz >= 0) {
        lf_freq = 0.0;
        uf_freq = fmax(-(float32_t)bands[EEPROMData.currentBand].FLoCut_Hz, (float32_t)bands[EEPROMData.currentBand].FHiCut_Hz);
    } else {
        if (bands[EEPROMData.currentBand].FLoCut_Hz > 0) {
            lf_freq = (float32_t)bands[EEPROMData.currentBand].FLoCut_Hz;
            uf_freq = (float32_t)bands[EEPROMData.currentBand].FHiCut_Hz;
        } else {
            uf_freq = -(float32_t)bands[EEPROMData.currentBand].FLoCut_Hz;
            lf_freq = -(float32_t)bands[EEPROMData.currentBand].FHiCut_Hz;
        }
    }
    // / rate DF SR[SampleRate].rate/DF
    lf_freq /= (data->sampleRate_Hz / NR_FFT_L); // bin BW is 46.9Hz [12000Hz / 256 bins] @96kHz
    uf_freq /= (data->sampleRate_Hz / NR_FFT_L);


    // INITIALIZATION ONCE 1
    if (NR_first_time_2 == 1) { // TODO: properly initialize all the variables
        for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) {
            NR_last_sample_buffer_L[bindx] = 0.0;
            NR_G[bindx] = 1.0;
            //xu[bindx] = 1.0;  //has to be replaced by other variable
            NR_Hk_old[bindx] = 1.0; // old gain or xu in development mode
            NR_Nest[bindx][0] = 0.0;
            NR_Nest[bindx][1] = 1.0;
            pslp[bindx] = 0.5;
        }
        NR_first_time_2 = 2; // we need to do some more a bit later down
    }

    for (int k = 0; k < 2; k++) {
        // NR_FFT_buffer is 512 floats big
        // interleaved r, i, r, i . . .
        // fill first half of FFT_buffer with last events audio samples
        for (int i = 0; i < NR_FFT_L / 2; i++) {
            NR_FFT_buffer[i * 2] = NR_last_sample_buffer_L[i]; // real
            NR_FFT_buffer[i * 2 + 1] = 0.0; // imaginary
        }
        // copy recent samples to last_sample_buffer for next time!
        for (int i = 0; i < NR_FFT_L  / 2; i++) {
            NR_last_sample_buffer_L [i] = data->I[i + k * (NR_FFT_L / 2)];
        }
        // now fill recent audio samples into second half of FFT_buffer
        for (int i = 0; i < NR_FFT_L / 2; i++) {
            NR_FFT_buffer[NR_FFT_L + i * 2] = data->I[i + k * (NR_FFT_L / 2)]; // real
            NR_FFT_buffer[NR_FFT_L + i * 2 + 1] = 0.0;
        }
        // perform windowing on samples in the NR_FFT_buffer
        for (int idx = 0; idx < NR_FFT_L; idx++) { // sqrt Hann window
            //float32_t temp_sample = 0.5 * (float32_t)(1.0 - (cosf(PI * 2.0 * (float32_t)idx / (float32_t)((NR_FFT_L) - 1))));
            //NR_FFT_buffer[idx * 2] *= temp_sample;
            NR_FFT_buffer[idx * 2] *= sqrtHann[idx];
        }

        // NR_FFT
        // calculation is performed in-place the FFT_buffer [re, im, re, im, re, im . . .]
        #ifdef TESTMODE
        arm_cfft_radix2_f32(&Sfor,NR_FFT_buffer);
        #else
        arm_cfft_f32(&arm_cfft_sR_f32_len256, NR_FFT_buffer, 0, 1);
        #endif

        for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) {
            // this is squared magnitude for the current frame
            NR_X[bindx][0] = (NR_FFT_buffer[bindx * 2] * NR_FFT_buffer[bindx * 2] + NR_FFT_buffer[bindx * 2 + 1] * NR_FFT_buffer[bindx * 2 + 1]);
        }

        if (NR_first_time_2 == 2) { // TODO: properly initialize all the variables
            for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) {
                NR_Nest[bindx][0] = NR_Nest[bindx][0] + 0.05 * NR_X[bindx][0]; // we do it 20 times to average over 20 frames for app. 100ms only on NR_on/bandswitch/modeswitch,...
                xt[bindx] = psini * NR_Nest[bindx][0];
            }
            NR_init_counter++;
            if (NR_init_counter > 19)  { //average over 20 frames for app. 100ms
                NR_init_counter = 0;
                NR_first_time_2 = 3;  // now we did all the necessary initialization to actually start the noise reduction
            }
        }

        if (NR_first_time_2 == 3) {
        for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) { // 1. Step of NR - calculate the SNR's
            ph1y[bindx] = 1.0 / (1.0 + pfac * expf(xih1r * NR_X[bindx][0] / xt[bindx]));
            pslp[bindx] = ap * pslp[bindx] + (1.0 - ap) * ph1y[bindx];

            if (pslp[bindx] > psthr) {
                ph1y[bindx] = 1.0 - pnsaf;
            } else {
                ph1y[bindx] = fmin(ph1y[bindx] , 1.0);
            }
            xtr = (1.0 - ph1y[bindx]) * NR_X[bindx][0] + ph1y[bindx] * xt[bindx];
            xt[bindx] = ax * xt[bindx] + (1.0 - ax) * xtr;
        }
        for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) { // 1. Step of NR - calculate the SNR's
            NR_SNR_post[bindx] = fmax(fmin(NR_X[bindx][0] / xt[bindx], 1000.0), snr_prio_min); // limited to +30 /-15 dB, might be still too much of reduction, let's try it?
            NR_SNR_prio[bindx] = fmax(NR_alpha * NR_Hk_old[bindx] + (1.0 - NR_alpha) * fmax(NR_SNR_post[bindx] - 1.0, 0.0), 0.0);
        }

        VAD_low = (int)lf_freq;
        VAD_high = (int)uf_freq;
        if (VAD_low == VAD_high) {
            VAD_high++;
        }
        if (VAD_low < 1) {
            VAD_low = 1;
        } else if (VAD_low > NR_FFT_L / 2 - 2) {
            VAD_low = NR_FFT_L / 2 - 2;
        }
        if (VAD_high < 1) {
            VAD_high = 1;
        } else if (VAD_high > NR_FFT_L / 2) {
            VAD_high = NR_FFT_L / 2;
        }

        float32_t v;
        for (int bindx = VAD_low; bindx < VAD_high; bindx++) { // maybe we should limit this to the signal containing bins (filtering!!)
            {
            v = NR_SNR_prio[bindx] * NR_SNR_post[bindx] / (1.0 + NR_SNR_prio[bindx]);
            NR_G[bindx] = 1.0 / NR_SNR_post[bindx] * sqrtf((0.7212 * v + v * v));
            NR_Hk_old[bindx] = NR_SNR_post[bindx] * NR_G[bindx] * NR_G[bindx]; //
            }

            // MUSICAL NOISE TREATMENT HERE, DL2FW

            // musical noise "artefact" reduction by dynamic averaging - depending on SNR ratio
            pre_power  = 0.0;
            post_power = 0.0;
            for (int bindx = VAD_low; bindx < VAD_high; bindx++) {
                pre_power += NR_X[bindx][0];
                post_power += NR_G[bindx] * NR_G[bindx]  * NR_X[bindx][0];
            }

            power_ratio = post_power / pre_power;
            if (power_ratio > power_threshold) {
                power_ratio = 1.0;
                NN = 1;
            } else {
                NN = 1 + 2 * (int)(0.5 + NR_width * (1.0 - power_ratio / power_threshold));
            }

            for (int bindx = VAD_low + NN / 2; bindx < VAD_high - NN / 2; bindx++) {
                NR_Nest[bindx][0] = 0.0;
                for (int m = bindx - NN / 2; m <= bindx + NN / 2; m++) {
                    NR_Nest[bindx][0] += NR_G[m];
                }
                NR_Nest[bindx][0] /= (float32_t)NN;
            }

            // and now the edges - only going NN steps forward and taking the average
            // lower edge
            for (int bindx = VAD_low; bindx < VAD_low + NN / 2; bindx++) {
                NR_Nest[bindx][0] = 0.0;
                for (int m = bindx; m < (bindx + NN); m++) {
                    NR_Nest[bindx][0] += NR_G[m];
                }
                NR_Nest[bindx][0] /= (float32_t)NN;
            }

            // upper edge - only going NN steps backward and taking the average
            for (int bindx = VAD_high - NN; bindx < VAD_high; bindx++) {
                NR_Nest[bindx][0] = 0.0;
                for (int m = bindx; m > (bindx - NN); m--) {
                    NR_Nest[bindx][0] += NR_G[m];
                }
                NR_Nest[bindx][0] /= (float32_t)NN;
            }

            // end of edge treatment

            for (int bindx = VAD_low + NN / 2; bindx < VAD_high - NN / 2; bindx++) {
                NR_G[bindx] = NR_Nest[bindx][0];
            }
            // end of musical noise reduction
        } //end of "if ts.nr_first_time == 3"

        // FINAL SPECTRAL WEIGHTING: Multiply current FFT results with NR_FFT_buffer for 128 bins with the 128 bin-specific gain factors G
        for (int bindx = 0; bindx < NR_FFT_L / 2; bindx++) {                              // try 128:
            NR_FFT_buffer[bindx * 2]                    = NR_FFT_buffer [bindx * 2] * NR_G[bindx] * NR_long_tone_gain[bindx];              // real part
            NR_FFT_buffer[bindx * 2 + 1]                = NR_FFT_buffer [bindx * 2 + 1] * NR_G[bindx] * NR_long_tone_gain[bindx];      // imag part
            NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 2] = NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 2] * NR_G[bindx] * NR_long_tone_gain[bindx]; // real part conjugate symmetric
            NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 1] = NR_FFT_buffer[NR_FFT_L * 2 - bindx * 2 - 1] * NR_G[bindx] * NR_long_tone_gain[bindx]; // imag part conjugate symmetric
        }

        // *****************************************************************
        //  NOISE REDUCTION CODE ENDS HERE
        // ***************************************************************** 
        // very interesting!
        // if I leave the FFT_buffer as is and just multiply the 19 bins below with 0.1, the audio
        // is distorted a little bit !
        // To me, this is an indicator of a problem with windowing . . .
        //

    #if 0
        for (int bindx = 1; bindx < 20; bindx++)
            // bins 2 to 29 attenuated
            // set real values to 0.1 of their original value
        {
            NR_FFT_buffer[bindx * 2] *= 0.1;
            NR_FFT_buffer[bindx * 2 + 1] *= 0.1; //NR_iFFT_buffer[idx] * 0.1;
        }
    #endif
        #ifdef TESTMODE
        arm_cfft_radix2_f32(&Srev,NR_FFT_buffer);
        #else
        arm_cfft_f32(&arm_cfft_sR_f32_len256, NR_FFT_buffer, 1, 1);
        #endif
        
        for (int idx = 0; idx < NR_FFT_L; idx++) {
            NR_FFT_buffer[idx * 2] *= sqrtHann[idx];      // sqrt Hann window
        }

        // do the overlap & add
        for (int i = 0; i < NR_FFT_L / 2; i++) {        // take real part of first half of current iFFT result and add to 2nd half of last iFFT_result
            data->I[i + k * (NR_FFT_L / 2)] = NR_FFT_buffer[i * 2] + NR_last_iFFT_result[i];
            data->Q[i + k * (NR_FFT_L / 2)] = data->I[i + k * (NR_FFT_L / 2)];
        }
        for (int i = 0; i < NR_FFT_L / 2; i++) {
            NR_last_iFFT_result[i] = NR_FFT_buffer[NR_FFT_L + i * 2];
        }
        // end of "for" loop which repeats the FFT_iFFT_chain two times !!!
        }
    }
}
