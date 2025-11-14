#include "SDT.h"

float32_t DMAMEM float_buffer_L[READ_BUFFER_SIZE];
float32_t DMAMEM float_buffer_R[READ_BUFFER_SIZE];

DataBlock data;

static int16_t *sp_L1; // used by receive chain
static int16_t *sp_R1;
static int16_t *sp_L2; // used by transmit chain
static int16_t *sp_R2;
static uint32_t n_clear;
static char *filename = nullptr;
void SaveData(DataBlock *data, uint32_t suffix); // used by the unit tests

/**
 * Perform the appropriate IQ signal processing depending on the state we're in
 */
void PerformSignalProcessing(void){
    switch (modeSM.state_id){
        case (ModeSm_StateId_CALIBRATE_RX_IQ):
        case (ModeSm_StateId_SSB_RECEIVE):
        case (ModeSm_StateId_CW_RECEIVE):{
            ReceiveProcessing(nullptr);
            break;
        }
        case (ModeSm_StateId_SSB_TRANSMIT):{
            TransmitProcessing(nullptr);
            break;
        }
        default:{
            // In all other states we don't perform IQ signal processing
            break;
        }
    }
}

/**
 * Used by the unit tests
 */
float32_t GetAmpCorrectionFactor(uint32_t bandN){
    return ED.IQAmpCorrectionFactor[bandN];
}

/**
 * Used by the unit tests
 */
float32_t GetPhaseCorrectionFactor(uint32_t bandN){
    return ED.IQPhaseCorrectionFactor[bandN];
}

/**
 * Apply gain factors to the data. Supplied factors are in units of dB, so 
 * these are converted to linear amplitude scaling factors that are use to
 * multiply float_buffer_L and float_buffer_R.
 * 
 * @param data The data block to act upon
 * @param rfGainAllBands_dB The gain, in dB, to be applied to all bands
 * @param bandGain_dB Additional gain, in dB, applied to the current band
 */
void ApplyRFGain(DataBlock *data, float32_t rfGainAllBands_dB, float32_t bandGain_dB){
    float32_t rfGainValue = pow(10, rfGainAllBands_dB / 20);
    arm_scale_f32(data->I, rfGainValue, data->I, data->N);
    arm_scale_f32(data->Q, rfGainValue, data->Q, data->N);
    
    rfGainValue = pow(10, bandGain_dB / 20);
    arm_scale_f32(data->I, rfGainValue, data->I, data->N);
    arm_scale_f32(data->Q, rfGainValue, data->Q, data->N);
}

/**
 * Read in N_BLOCKS blocks of BUFFER_SIZE samples each from Q_in_R and Q_in_L 
 * AudioRecordQueue objects into the float_buffer_L and float_buffer_R buffers. 
 * The samples are converted to normalized floats in the range -1 to +1.
 * 
 * @param data The data block to put the samples in
 * @return ESUCCESS if samples were read, EFAIL if insufficient samples are available
 */
errno_t ReadIQInputBuffer(DataBlock *data){
    if ((uint32_t)Q_in_L.available() > N_BLOCKS+0 && (uint32_t)Q_in_R.available() > N_BLOCKS+0 ) {
        usec = 0;
        // get audio samples from the audio  buffers and convert them to float
        // read in N_BLOCKS blocks á 128 samples in I and Q
        for (unsigned i = 0; i < N_BLOCKS; i++) {
            sp_L1 = Q_in_L.readBuffer(); 
            sp_R1 = Q_in_R.readBuffer();
            // Using arm_Math library, convert to float one buffer_size.
            // Float_buffer samples are now standardized from > -1.0 to < 1.0
            arm_q15_to_float(sp_L1, &data->I[BUFFER_SIZE * i], BUFFER_SIZE);
            arm_q15_to_float(sp_R1, &data->Q[BUFFER_SIZE * i], BUFFER_SIZE);
            Q_in_L.freeBuffer();
            Q_in_R.freeBuffer();
        }
        data->N = N_BLOCKS * BUFFER_SIZE;
        data->sampleRate_Hz = SR[SampleRate].rate;
        return ESUCCESS; // we filled the input buffers
    } else {
        return EFAIL; // we did not read any input data
    }
}

/**
 * This is to prevent overfilled queue buffers during each switching event
 * (band change, mode change, frequency change, the audio chain runs and fills 
 * the buffers if the buffers are full, the Teensy needs much more time in that 
 * case, we clear the buffers to keep the whole audio chain running smoothly.
 */
void ClearAudioBuffers(void){
    uint16_t threshold = 100;
    if (Q_in_L.available() > threshold) {
      Q_in_L.clear();
      n_clear++;  // just for debugging to check how often this occurs
      AudioInterrupts(); // defined by Teensy Audio library
      Debug("Cleared overfull L buffer");
    }
    if (Q_in_R.available() > threshold) {
      Q_in_R.clear();
      n_clear++;  // just for debugging to check how often this occurs
      AudioInterrupts();
      Debug("Cleared overfull R buffer");
    }
}

/**
 * Apply a "phase angle" correction to the I and Q channels.
 * 
 * @param *I_buffer Pointer to a buffer containing the I values
 * @param *Q_buffer Pointer to a buffer containing the Q values
 * @param factor The phase correction factor to be applied
 * @param blocksize The number of samples in the buffers
 */
void IQPhaseCorrection(float32_t *I_buffer, float32_t *Q_buffer, 
                        float32_t factor, uint32_t blocksize) {
    float32_t temp_buffer[blocksize];
    if (factor < 0.0) {  // mix a bit of I into Q
        arm_scale_f32(I_buffer, factor, temp_buffer, blocksize);
        arm_add_f32(Q_buffer, temp_buffer, Q_buffer, blocksize);
    } else {  // mix a bit of Q into I
        arm_scale_f32(Q_buffer, factor, temp_buffer, blocksize);
        arm_add_f32(I_buffer, temp_buffer, I_buffer, blocksize);
  }
}

/**
 * Correct for amplitude and phase errors in the I and Q channels in order to
 * improve the sideband separation / image rejection.
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param amp_factor The amp correction factor to be applied
 * @param phs_factor The phase correction factor to be applied
 */
void ApplyIQCorrection(DataBlock *data, float32_t amp_factor, float32_t phs_factor){
    // Manual IQ amplitude and phase correction
    // to be honest: we only correct the amplitude of the I channel ;-)
    arm_scale_f32(data->I, amp_factor, data->I, data->N);
    IQPhaseCorrection(data->I, data->Q, phs_factor, data->N);    
}

/**
 * Scale the volume to compensate for the FIR filter bandwidth to keep the 
 * audible bandwidth steady
 * 
 * @param data Pointer to the DataBlock to act upon
 */
void VolumeScale(DataBlock *data){
    float32_t freqKHzFcut;
    float32_t volScaleFactor;
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB) {
      freqKHzFcut = -(float32_t)bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz * 0.001; // 3
    } else {
      freqKHzFcut = (float32_t)bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz * 0.001;
    }
    volScaleFactor = 7.0874 * pow(freqKHzFcut, -1.232);
    arm_scale_f32(data->I, volScaleFactor, data->I, data->N);
    arm_scale_f32(data->Q, volScaleFactor, data->Q, data->N);
}

/**
 * Initialize the AGC structure's variables
 * 
 * @param a Pointer to the AGC structure
 * @param sampleRate_Hz The sample rate, in Hz
 */
void InitializeAGC(AGCConfig *a, uint32_t sampleRate_Hz){
    float32_t tmp;
    float32_t sample_rate = (float32_t)sampleRate_Hz;

    //calculate internal parameters
    switch (ED.agc){
        case AGCOff:
            break;

        case AGCLong:
            a->hangtime = 2.000;
            a->tau_decay = 2.000;
            break;

        case AGCSlow:
            a->hangtime = 1.000;
            a->tau_decay = 0.5;
            break;

        case AGCMed:
            a->hangtime = 0.000;
            a->tau_decay = 0.250;
            break;

        case AGCFast:
            a->hang_thresh = 1.0;
            a->hangtime = 0.0;
            a->tau_decay = 0.050;
            break;

        default:
            break;
    }

    a->max_gain = powf (10.0, (float32_t)bands[ED.currentBand[ED.activeVFO]].AGC_thresh / 20.0);
    a->attack_buffsize = (uint32_t)ceil(sample_rate * a->n_tau * a->tau_attack);
    a->in_index = a->attack_buffsize + a->out_index;
    a->attack_mult = 1.0 - expf(-1.0 / (sample_rate * a->tau_attack));
    a->decay_mult = 1.0 - expf(-1.0 / (sample_rate * a->tau_decay));
    a->fast_decay_mult = 1.0 - expf(-1.0 / (sample_rate * a->tau_fast_decay));
    a->fast_backmult = 1.0 - expf(-1.0 / (sample_rate * a->tau_fast_backaverage));

    a->onemfast_backmult = 1.0 - a->fast_backmult;

    a->out_target = a->out_targ * (1.0 - expf(-(float32_t)a->n_tau)) * 0.9999;
    a->min_volts = a->out_target / (a->var_gain * a->max_gain);
    a->inv_out_target = 1.0 / a->out_target;

    tmp = log10f(a->out_target / (a->max_input * a->var_gain * a->max_gain));
    if (tmp == 0.0)
    tmp = 1e-16;
    a->slope_constant = (a->out_target * (1.0 - 1.0 / a->var_gain)) / tmp;

    a->inv_max_input = 1.0 / a->max_input;

    tmp = powf (10.0, (a->hang_thresh - 1.0) / 0.125);
    a->hang_level = (a->max_input * tmp + (a->out_target / (a->var_gain * a->max_gain)) * (1.0 - tmp)) * 0.637;

    a->hang_backmult = 1.0 - expf(-1.0 / (sample_rate * a->tau_hang_backmult));
    a->onemhang_backmult = 1.0 - a->hang_backmult;

    a->hang_decay_mult = 1.0 - expf(-1.0 / (sample_rate * a->tau_hang_decay));

    for (size_t i = 0; i < 2*a->ring_buffsize; i++)
        a->ring[i] = 0;
    for (size_t i = 0; i < a->ring_buffsize; i++)
        a->abs_ring[i] = 0;
}

/**
 * Perform audio gain control (AGC).
 * 
 * @param data Pointer to the DataBlock to act upon
 * @param a The AGC structure containing the AGC state and variables
 */
void AGC(DataBlock *data, AGCConfig *a){
    int k;
    float32_t mult;
    float32_t abs_out_sample;
    float32_t out_sample[2];

    if (ED.agc == AGCOff)  // AGC OFF
    {
        for (unsigned i = 0; i < data->N; i++)
        {
            data->I[i] = a->fixed_gain * data->I[i];
            data->Q[i] = a->fixed_gain * data->Q[i];
        }
        return;
    }

    for (unsigned i = 0; i < data->N; i++)
    {
        if (++a->out_index >= a->ring_buffsize)
            a->out_index -= a->ring_buffsize;
        if (++a->in_index >= a->ring_buffsize)
            a->in_index -= a->ring_buffsize;

        out_sample[0] = a->ring[2 * a->out_index + 0];
        out_sample[1] = a->ring[2 * a->out_index + 1];
        abs_out_sample = a->abs_ring[a->out_index];
        a->ring[2 * a->in_index + 0] = data->I[i];
        a->ring[2 * a->in_index + 1] = data->Q[i];
        if (a->pmode == 0) // MAGNITUDE CALCULATION
            a->abs_ring[a->in_index] = fmax(fabs(a->ring[2 * a->in_index + 0]), fabs(a->ring[2 * a->in_index + 1]));
        else
            a->abs_ring[a->in_index] = sqrtf(a->ring[2 * a->in_index + 0] * a->ring[2 * a->in_index + 0] 
                                            + a->ring[2 * a->in_index + 1] * a->ring[2 * a->in_index + 1]);

        a->fast_backaverage = a->fast_backmult * abs_out_sample + a->onemfast_backmult * a->fast_backaverage;
        a->hang_backaverage = a->hang_backmult * abs_out_sample + a->onemhang_backmult * a->hang_backaverage;

        if ((abs_out_sample >= a->ring_max) && (abs_out_sample > 0.0))
        {
            a->ring_max = 0.0;
            k = a->out_index;
            for (uint32_t j = 0; j < a->attack_buffsize; j++)
            {
                if (++k == (int)a->ring_buffsize)
                    k = 0;
                if (a->abs_ring[k] > a->ring_max)
                    a->ring_max = a->abs_ring[k];
            }
        }
        if (a->abs_ring[a->in_index] > a->ring_max)
            a->ring_max = a->abs_ring[a->in_index];

        if (a->hang_counter > 0)
            --a->hang_counter;

        switch (a->state)
        {
            case 0:
                if (a->ring_max >= a->volts) {
                    a->volts += (a->ring_max - a->volts) * a->attack_mult;
                } else {
                    if (a->volts > a->pop_ratio * a->fast_backaverage) {
                        a->state = 1;
                        a->volts += (a->ring_max - a->volts) * a->fast_decay_mult;
                    } else {
                        if (a->hang_enable && (a->hang_backaverage > a->hang_level)) {
                            a->state = 2;
                            a->hang_counter = (int)(a->hangtime * data->sampleRate_Hz);
                            a->decay_type = 1;
                        } else {
                            a->state = 3;
                            a->volts += (a->ring_max - a->volts) * a->decay_mult;
                            a->decay_type = 0;
                        }
                    }
                }
                break;

            case 1:
                if (a->ring_max >= a->volts) {
                    a->state = 0;
                    a->volts += (a->ring_max - a->volts) * a->attack_mult;
                } else {
                    if (a->volts > a->save_volts) {
                        a->volts += (a->ring_max - a->volts) * a->fast_decay_mult;
                    } else {
                        if (a->hang_counter > 0) {
                            a->state = 2;
                        } else {
                            if (a->decay_type == 0) {
                                a->state = 3;
                                a->volts += (a->ring_max - a->volts) * a->decay_mult;
                            } else {
                                a->state = 4;
                                a->volts += (a->ring_max - a->volts) * a->hang_decay_mult;
                            }
                        }
                    }
                }
                break;

            case 2:
                if (a->ring_max >= a->volts) {
                    a->state = 0;
                    a->save_volts = a->volts;
                    a->volts += (a->ring_max - a->volts) * a->attack_mult;
                } else {
                    if (a->hang_counter == 0) {
                        a->state = 4;
                        a->volts += (a->ring_max - a->volts) * a->hang_decay_mult;
                    }
                }
                break;

            case 3:
                if (a->ring_max >= a->volts) {
                    a->state = 0;
                    a->save_volts = a->volts;
                    a->volts += (a->ring_max - a->volts) * a->attack_mult;
                } else {
                    a->volts += (a->ring_max - a->volts) * a->decay_mult * .05;
                }
                break;

            case 4:
                if (a->ring_max >= a->volts) {
                    a->state = 0;
                    a->save_volts = a->volts;
                    a->volts += (a->ring_max - a->volts) * a->attack_mult;
                } else {
                    a->volts += (a->ring_max - a->volts) * a->hang_decay_mult;
                }
                break;
        }
        if (a->volts < a->min_volts) {
            a->volts = a->min_volts; // no AGC action is taking place
            a->agc_action = 0;
        } else {
            a->agc_action = 1;
        }

        mult = (a->out_target - a->slope_constant * fmin (0.0, log10f_fast(a->inv_max_input * a->volts))) / a->volts;
        data->I[i] = out_sample[0] * mult;
        data->Q[i] = out_sample[1] * mult;
    }
}


/**
 * Calculate the Alpha-Beta magnitude.
 * (c) András Retzler
 * taken from libcsdr: https://github.com/simonyiszk/csdr
 * @param inphase I component
 * @param quadrature Q component
 * @return the alpha beta mag
 */
float32_t AlphaBetaMag(float32_t inphase, float32_t quadrature){
  // Min RMS Err      0.947543636291 0.392485425092
  // Min Peak Err     0.960433870103 0.397824734759
  // Min RMS w/ Avg=0 0.948059448969 0.392699081699
  const float32_t alpha = 0.960433870103;  // 1.0; //0.947543636291;
  const float32_t beta = 0.397824734759;

  float32_t abs_inphase = fabs(inphase);
  float32_t abs_quadrature = fabs(quadrature);
  if (abs_inphase > abs_quadrature) {
    return alpha * abs_inphase + beta * abs_quadrature;
  } else {
    return alpha * abs_quadrature + beta * abs_inphase;
  }
}


/**
 * Approximate calculation of atan function
 * Copied from https://www.dsprelated.com/showarticle/1052.php
 * Polynomial approximating arctangenet on the range -1,1.
 * Max error < 0.005 (or 0.29 degrees)
 * @param z value to calculate
 * @return approximation of atan(z)
 */
float ApproxAtan(float z) {
  const float n1 = 0.97239411f;
  const float n2 = -0.19194795f;
  return (n1 + n2 * z * z) * z;
}

/**
 * Approximate calculation of atan2 function
 * @param x value to calculate on 
 * @param y value to calculate on 
 * @return approximation of atan(y/x)
 */
float ApproxAtan2(float y, float x) {
  if (x != 0.0f) {
    if (fabsf(x) > fabsf(y)) {
      const float z = y / x;
      if (x > 0.0f) {
        // atan2(y,x) = atan(y/x) if x > 0
        return ApproxAtan(z);
      } else if (y >= 0.0f) {
        // atan2(y,x) = atan(y/x) + PI if x < 0, y >= 0
        return ApproxAtan(z) + PI;
      } else {
        // atan2(y,x) = atan(y/x) - PI if x < 0, y < 0
        return ApproxAtan(z) - PI;
      }
    } else  // Use property atan(y/x) = PI/2 - atan(x/y) if |y/x| > 1.
    {
      const float z = x / y;
      if (y > 0.0f) {
        // atan2(y,x) = PI/2 - atan(x/y) if |y/x| > 1, y > 0
        return -ApproxAtan(z) + TWO_PI;
      } else {
        // atan2(y,x) = -PI/2 - atan(x/y) if |y/x| > 1, y < 0
        return -ApproxAtan(z) - TWO_PI;
      }
    }
  } else {
    if (y > 0.0f)  // x = 0, y > 0
    {
      return TWO_PI;
    } else if (y < 0.0f)  // x = 0, y < 0
    {
      return -TWO_PI;
    }
  }
  return 0.0f;  // x,y = 0. Could return NaN instead.
}

static float32_t SAM_carrier_freq_offset = 0;
static float32_t SAM_carrier_freq_offsetOld = 0;

/**
 * Synchronous AM detection. Determines the carrier frequency, adjusts freq and replaces 
 * the received carrier with a steady signal to prevent fading. This alogorithm works best 
 * of those implimented. Taken from Warren Pratt´s WDSP, 2016
 * https://github.com/TAPR/OpenHPSDR-PowerSDR/blob/master/Project%20Files/Source/wdsp/amd.c
 * 
 * @param data Pointer to the DataBlock to act upon
 */
void AMDecodeSAM(DataBlock *data){
    float32_t phzerror = 0.0;
    float32_t Sin, Cos, ai, bi, aq, bq, audio, det;
    float32_t corr[2];
    uint8_t fade_leveler = 1;

    float32_t tauR = 0.02;
    if (data->sampleRate_Hz != 24000)
        Debug("SAM error: expected 24ksps rate!");
    float32_t mtauR = exp(-1.0f / (float32_t)data->sampleRate_Hz * tauR);
    float32_t onem_mtauR = 1.0 - mtauR;

    float32_t tauI = 1.4;
    float32_t mtauI = exp(-1.0f / (float32_t)data->sampleRate_Hz * tauI);
    float32_t onem_mtauI = 1.0 - mtauI;

    float32_t dc = 0.0;
    float32_t dc_insert = 0.0;

    //float32_t audiou;
    //float32_t dcu = 0.0;
    //float32_t dc_insertu = 0.0;

    float32_t fil_out = 0.0;
    float32_t del_out;
    float32_t omega2 = 0.0;
    float32_t SAM_carrier;

    float32_t pll_fmax = +4000.0;
    int zeta_help = 65;
    float32_t zeta = (float32_t)zeta_help / 100.0;  // PLL step response: smaller, slower response 1.0 - 0.1
    float32_t omegaN = 200.0;                       // PLL bandwidth 50.0 - 1000.0
    float32_t omega_min = TWO_PI * -pll_fmax * 1.0f / (float32_t)data->sampleRate_Hz;
    float32_t omega_max = TWO_PI * pll_fmax * 1.0f / (float32_t)data->sampleRate_Hz;
    float32_t g1 = 1.0 - exp(-2.0 * omegaN * zeta * 1.0f / (float32_t)data->sampleRate_Hz);
    float32_t g2 = -g1 + 2.0 * (1 - exp(-omegaN * zeta * 1.0f / (float32_t)data->sampleRate_Hz) * cosf(omegaN * 1.0f / (float32_t)data->sampleRate_Hz * sqrtf(1.0 - zeta * zeta)));

    if (data->N != 256)
        Debug("SAM decode error! Expect 256 samples");
    for (unsigned i = 0; i < data->N; i++) {
        Sin = arm_sin_f32(phzerror);
        Cos = arm_cos_f32(phzerror);

        ai = Cos * data->I[i];
        bi = Sin * data->I[i];
        aq = Cos * data->Q[i];
        bq = Sin * data->Q[i];
        corr[0] = +ai + bq;
        corr[1] = -bi + aq;
        audio = (ai - bi) + (aq + bq);
        
        if (fade_leveler) {
            dc = mtauR * dc + onem_mtauR * audio;
            dc_insert = mtauI * dc_insert + onem_mtauI * corr[0];
            audio = audio + dc_insert - dc;
        }
        data->I[i] = audio;
        // data->Q is not used, so don't do this
        // I am not sure why audiou is uninitialized. Mistake?
        //if (fade_leveler) {
        //    dcu = mtauR * dcu + onem_mtauR * audiou;
        //    dc_insertu = mtauI * dc_insertu + onem_mtauI * corr[0];
        //    audiou = audiou + dc_insertu - dcu;
        //}
        //data->Q[i] = audiou;
        det = ApproxAtan2(corr[1], corr[0]);

        del_out = fil_out;
        omega2 = omega2 + g2 * det;
        if (omega2 < omega_min) omega2 = omega_min;
        else if (omega2 > omega_max) omega2 = omega_max;
        fil_out = g1 * det + omega2;
        phzerror = phzerror + del_out;

        //wrap round 2PI, modulus
        while (phzerror >= TWO_PI) phzerror -= TWO_PI;
        while (phzerror < 0.0) phzerror += TWO_PI;
    }
    SAM_carrier =  (omega2 * (float32_t)data->sampleRate_Hz) / (2 * TWO_PI);
    SAM_carrier_freq_offset = 10 * SAM_carrier;
    SAM_carrier_freq_offset = 0.95 * SAM_carrier_freq_offsetOld + 0.05 * SAM_carrier_freq_offset;   
    SAM_carrier_freq_offsetOld = SAM_carrier_freq_offset;
    Debug("SAM carrier offset = " + String(SAM_carrier_freq_offset));
}

/**
 * Demodulate the audio.
 * 
 * @param data Pointer to the DataBlock to act upon
 */
static float32_t wold = 0;
void Demodulate(DataBlock *data, ReceiveFilterConfig *RXfilters){
    // Demodulation: our time domain output is a combination of the real part (left channel) 
    // AND the imaginary part (right channel) of the second half of the FFT_buffer
    // The demod mode is accomplished by selecting/combining the real and imaginary parts 
    // of the output of the IFFT process.
    float32_t audiotmp, w;
    switch (ED.modulation[ED.activeVFO]) {
      case LSB:
        // for SSB copy real part in both outputs
        arm_copy_f32(data->I, data->Q, data->N);
        break;
      case USB:
        // for SSB copy real part in both outputs
        arm_copy_f32(data->I, data->Q, data->N);
        break;
      case AM:
        // Magnitude estimation Lyons (2011): page 652 / libcsdr
        for (unsigned i = 0; i < data->N; i++) { 
          audiotmp = AlphaBetaMag(data->I[i], data->Q[i]);
          // There is a weird bug where sometimes we get nan samples.
          // This is a place where they can persist unless we excise them.
          // TODO: figure out a better way to not read nan samples
          if (isnan(audiotmp)){
            audiotmp = 0.0;
          }
          w = audiotmp + wold * 0.99f;  // Response to below 200Hz
          data->I[i] = w - wold;
          wold = w;
        }
        arm_biquad_cascade_df1_f32(&RXfilters->biquadAudioLowPass, data->I, data->Q, data->N);
        arm_copy_f32(data->Q, data->I, data->N);
        break;
      case SAM:
        AMDecodeSAM(data);
        break;
      default:
        break;
    }
}

/**
 * Apply noise reduction algorithm to the audio
 */
void NoiseReduction(DataBlock *data){
    switch (ED.nrOptionSelect) {
        case NROff:  // NR Off
            break;
        case NRKim:  // Kim NR
            Kim1_NR(data);
            arm_scale_f32(data->I, 30, data->I, data->N);
            arm_scale_f32(data->Q, 30, data->Q, data->N);
            break;
        case NRSpectral:  // Spectral NR
            SpectralNoiseReduction(data);
            break;
        case NRLMS:  // LMS NR
            Xanr(data, 0);
            //arm_scale_f32(data->I, 1.5, data->I, data->N);
            arm_scale_f32(data->Q, 2, data->I, data->N);
            break;
        default:
            break;
    }
}

/**
 * Interpolate the data back to the original sample rate
 */
void InterpolateReceiveData(DataBlock *data, ReceiveFilterConfig *RXfilters){
    // ======================================Interpolation  ================
    // You only need to interpolate one because they contain the same data
    arm_fir_interpolate_f32(&RXfilters->FIR_int1, data->I, data->Q, READ_BUFFER_SIZE / RXfilters->DF);
    data->N = data->N * RXfilters->DF2;
    data->sampleRate_Hz = data->sampleRate_Hz * RXfilters->DF2;
    arm_fir_interpolate_f32(&RXfilters->FIR_int2, data->Q, data->I, READ_BUFFER_SIZE / RXfilters->DF1);
    data->N = data->N * RXfilters->DF1;
    data->sampleRate_Hz = data->sampleRate_Hz * RXfilters->DF1;
    arm_copy_f32(data->I,data->Q,data->N);
}

/**
 * Convert an audio volume in the range 1..100 to an amplification factor
 */
float32_t VolumeToAmplification(int32_t volume) {
    float32_t x = volume / 100.0f;  //"volume" Range 0..100
    float32_t ampl = 5 * x * x * x * x * x;  //70dB
    return ampl;
}

/**
 * Adjust the volume
 */
void AdjustVolume(DataBlock *data, ReceiveFilterConfig *RXfilters){
    arm_scale_f32(data->I, RXfilters->DF * VolumeToAmplification(ED.audioVolume), data->I, data->N);
}

/**
 * Play the data contained in data->I on the left and right channels
 */
void PlayBuffer(DataBlock *data){
    for (unsigned i = 0; i < N_BLOCKS; i++) {
        sp_L1 = Q_out_L.getBuffer();
        sp_R1 = Q_out_R.getBuffer();
        arm_float_to_q15(&data->I[BUFFER_SIZE * i], sp_L1, BUFFER_SIZE);
        arm_float_to_q15(&data->I[BUFFER_SIZE * i], sp_R1, BUFFER_SIZE);
        Q_out_L.playBuffer();  // play it !
        Q_out_R.playBuffer();  // play it !
    }
}

/**
 * Initialize the global variables to their default startup values
 * 1) Configure the RXfilters
 * 2) Configure the AGC
 * 3) Configure the noise reduction
 */
void InitializeSignalProcessing(void){
    InitializeFilters(ED.spectrum_zoom,&RXfilters);
    InitializeTransmitFilters(&TXfilters);
    InitializeAGC(&agc, SR[SampleRate].rate/RXfilters.DF);
    InitializeKim1NoiseReduction();
    InitializeXanrNoiseReduction();
    InitializeSpectralNoiseReduction();
    InitializeCWProcessing(ED.currentWPM, &RXfilters);
}

/**
 * Used by the unit tests. Sets the name of the file to save samples to.
 */
void setfilename(char *fnm){
    filename = fnm;
}

/**
 * Used by the unit tests. Saves data to a file for offline examination.
 */
void SaveData(DataBlock *data, uint32_t suffix){
    if (filename != nullptr){
        char fn2[100];
        sprintf(fn2,"%s-%02lu.txt",filename, suffix);
        WriteIQFile(data, fn2);
    }
}

/**
 * Read a block of samples from the ADC and perform receive signal processing
 */
DataBlock * ReceiveProcessing(const char *fname){
    data.I = float_buffer_L;
    data.Q = float_buffer_R;

    // Read data from buffer
    if (ReadIQInputBuffer(&data)){
        // There is no data available, skip the rest
        return NULL;
    }
    //Flag(1);
    // Clear overfull buffers is not needed
    //ClearAudioBuffers();

    SaveData(&data, 0);
    if (fname != nullptr){
        filename = (char *)fname;
    }
    if (filename != nullptr){
        char fn2[100];
        sprintf(fn2,"IQ_%s",filename);
        WriteIQFile(&data, fn2);
    }

    // Scale data channels by the overall system RF gain and the band-specified gain adjustment
    ApplyRFGain(&data, ED.rfGainAllBands_dB, bands[ED.currentBand[ED.activeVFO]].RFgain_dB);

    // Perform IQ correction
    ApplyIQCorrection(&data,
        ED.IQAmpCorrectionFactor[ED.currentBand[ED.activeVFO]],
        ED.IQPhaseCorrectionFactor[ED.currentBand[ED.activeVFO]]);

    // Perform FFT of full spectrum for spectral display at this point if no zoom
    if (ED.spectrum_zoom == SPECTRUM_ZOOM_1) {
        ZoomFFTExe(&data, ED.spectrum_zoom, &RXfilters);
        displayFFTUpdated = true;
    }

    // First, frequency translation by +Fs/4 without multiplication from Lyons 
    // (2011): chapter 13.1.2 page 646. Together with the savings of not having 
    // to shift/rotate the FFT_buffer, this saves about 1% of processor use.
    // A signal at x Hz will be at x + 48,000 Hz after this step.
    FreqShiftFs4(&data);

    SaveData(&data, 1); // used by the unit tests

    // Perform FFT of zoomed-in spectrum for spectral display at this point if zoom != 1
    if (ED.spectrum_zoom != SPECTRUM_ZOOM_1) {
        if(ZoomFFTExe(&data, ED.spectrum_zoom, &RXfilters)) {
            // at high zoom levels, multiple calls to ZoomFFTExe might be needed to fill
            // the buffers before the FFT is actually calculated. ZoomFFTExe returns true
            // if it actually performed the FFT during this call.
            displayFFTUpdated = true;
        }
    }

    // Now, translate by the fine tune frequency. A signal at x Hz will be at 
    // x + shift Hz after this step.
    float32_t sideToneShift_Hz = 0;
    if (modeSM.state_id == ModeSm_StateId_CW_RECEIVE ) {
        if (bands[ED.currentBand[ED.activeVFO]].mode == 1) {
            sideToneShift_Hz = CWToneOffsetsHz[ED.CWToneIndex];
        } else {
            sideToneShift_Hz = -CWToneOffsetsHz[ED.CWToneIndex];
        }
    }
    float32_t shift = ED.fineTuneFreq_Hz[ED.activeVFO] + sideToneShift_Hz;
    FreqShiftF(&data,shift);
    SaveData(&data, 2); // used by the unit tests

    // Decimate by 8. Reduce the sampled band to -12,000 Hz to +12,000 Hz.
    // The 3dB bandwidth is approximately -6,000 to +6,000 Hz
    DecimateBy8(&data, &RXfilters);

    SaveData(&data, 3); // used by the unit tests

    // Volume adjust for frequency cuts
    VolumeScale(&data);

    // Apply convolution filter. Restrict signals to those between 
    // bands[currentBand].FLoCut_Hz and bands[currentBand].FHiCut_Hz
    ConvolutionFilter(&data, &RXfilters, filename);

    SaveData(&data, 4); // used by the unit tests

    // AGC
    AGC(&data, &agc);

    // Demodulate
    Demodulate(&data, &RXfilters);

    SaveData(&data, 5); // used by the unit tests

    // Receive EQ
    BandEQ(&data, &RXfilters, RX);

    // Noise reduction
    NoiseReduction(&data);

    // Notch filter
    if (ED.ANR_notchOn == 1) {
        Xanr(&data,1);
        arm_copy_f32(data.Q, data.I, data.N);
    }

    if (modeSM.state_id == ModeSm_StateId_CW_RECEIVE){
        // CW receive processing
        DoCWReceiveProcessing(&data, &RXfilters);
        // CW audio bandpass
        CWAudioFilter(&data, &RXfilters);
    }

    // Interpolate
    InterpolateReceiveData(&data, &RXfilters);

    // Volume adjust for audio volume setting. I and Q contain duplicate data, don't 
    // need to scale both
    AdjustVolume(&data, &RXfilters);

    SaveData(&data, 6); // used by the unit tests

    // Play sound on the speaker
    PlayBuffer(&data);

    elapsed_micros_sum = elapsed_micros_sum + usec;
    elapsed_micros_idx_t++;
    //Flag(0);

    return &data;
}

/**
 * Read in N_BLOCKS blocks of BUFFER_SIZE samples each from Q_in_R_Ex and Q_in_L_Ex 
 * AudioRecordQueue objects into the data float buffers. This is the transmit chain,
 * input comes from the microphone. The samples are converted to normalized floats in 
 * the range -1 to +1.
 * 
 * @param data The data block to put the samples in
 * @return ESUCCESS if samples were read, EFAIL if insufficient samples are available
 */ 
errno_t ReadMicrophoneBuffer(DataBlock *data){
    // are there at least N_BLOCKS buffers in each channel available ?
    if ((uint32_t)Q_in_L_Ex.available() > N_BLOCKS_EX+0 && (uint32_t)Q_in_R_Ex.available() > N_BLOCKS_EX+0) {
        // get audio samples from the audio  buffers and convert them to float
        // read in 32 blocks á 128 samples in I and Q
        for (unsigned i = 0; i < N_BLOCKS_EX; i++) {
            sp_L2 = Q_in_L_Ex.readBuffer();
            sp_R2 = Q_in_R_Ex.readBuffer();

            // Using arm_Math library, convert to float one buffer_size.
            // Float_buffer samples are now standardized from > -1.0 to < 1.0
            arm_q15_to_float(sp_L2, &data->I[BUFFER_SIZE * i], BUFFER_SIZE);
            arm_q15_to_float(sp_R2, &data->Q[BUFFER_SIZE * i], BUFFER_SIZE);
            Q_in_L_Ex.freeBuffer();
            Q_in_R_Ex.freeBuffer();
        }
        data->N = N_BLOCKS_EX * BUFFER_SIZE;
        data->sampleRate_Hz = SR[SampleRate].rate;
        return ESUCCESS;
    } else {
        return EFAIL;
    }
}

/**
 * Play the data contained in data->I and data->Q on the transmitter exciter output
 */
void PlayIQData(DataBlock *data){
    for (unsigned i = 0; i < N_BLOCKS_EX; i++) {
        sp_L2 = Q_out_L_Ex.getBuffer();
        sp_R2 = Q_out_R_Ex.getBuffer();
        arm_float_to_q15(&data->I[BUFFER_SIZE * i], sp_L2, BUFFER_SIZE);
        arm_float_to_q15(&data->Q[BUFFER_SIZE * i], sp_R2, BUFFER_SIZE);
        Q_out_L_Ex.playBuffer();  // play it !
        Q_out_R_Ex.playBuffer();  // play it !
    }
}

/**
 * Read a block of samples from the microphone and perform transmit signal processing
 */
DataBlock * TransmitProcessing(const char *fname){

    data.I = float_buffer_L;
    data.Q = float_buffer_R;

    // Read data from microphone input buffer
    if (ReadMicrophoneBuffer(&data)){
        // There is no data available, skip the rest
        return NULL;
    }

    TXDecimateBy4(&data,&TXfilters);// 2048 in, 512 out
    TXDecimateBy2(&data,&TXfilters);// 512 in, 256 out
    BandEQ(&data, &RXfilters, TX);
    arm_copy_f32(data.I,data.Q,256);
    TXDecimateBy2Again(&data,&TXfilters); // 256 in, 128 out
    HilbertTransform(&data,&TXfilters); // 128
    TXInterpolateBy2Again(&data,&TXfilters); // 128 in, 256 out
    SidebandSelection(&data);
    TXInterpolateBy2(&data,&TXfilters); // 256 in, 512 out
    TXInterpolateBy4(&data,&TXfilters); // 512 in, 2048 out

    // Play the data on the output buffer
    PlayIQData(&data);
    return &data;
}