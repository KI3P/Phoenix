#include "SDT.h"

#ifdef FAST_TUNE
// The fast tune parameters
static uint64_t MS_temp, FT_delay, FT_last_time;
static bool FT_ON = false;
const uint64_t FT_cancel_ms = 500;  // time between steps above which FT is cancelled
const uint64_t FT_on_ms = 100;      // time between FTsteps below which increases the step size
const int32_t FT_trig = 4;          // number of short steps to trigger fast tune,
const int FT_step = 1000;           // Hz step in Fast Tune
static int64_t last_FT_step_size = 1;
static uint32_t FT_step_counter = 0;
#endif

/**
 * Adjust the fine tune frequency (2nd stage software mixer)
 * 
 * @param filter_change The positive or negative increment to the filter setting
 */
void AdjustFineTune(int32_t filter_change){
    #ifdef FAST_TUNE
    MS_temp = millis();
    FT_delay = MS_temp - FT_last_time;
    FT_last_time = MS_temp;
    if (FT_ON) {  // Check if FT should be cancelled (FT_delay>=FT_cancel_ms)
        if (FT_delay >= FT_cancel_ms) {
            FT_ON = false;
            ED.stepFineTune = last_FT_step_size;
        }
    } else {  //  FT is off so check for short delays
        if (FT_delay <= FT_on_ms) {
            FT_step_counter += 1;
        }
        if (FT_step_counter >= FT_trig) {
            last_FT_step_size = ED.stepFineTune;
            ED.stepFineTune = FT_step;
            FT_step_counter = 0;
            FT_ON = true;
        }
    }
    #endif  // FAST_TUNE

    ED.fineTuneFreq_Hz[ED.activeVFO] += ED.stepFineTune * filter_change;
    //Debug("Fine tune pre: " + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
    // If the zoom level is 0, then the valid range of fine tune window is between
    // -samplerate/2 and +samplerate/2. 
    int32_t lower_limit = -(int32_t)(SR[SampleRate].rate)/2;
    int32_t upper_limit = (int32_t)(SR[SampleRate].rate)/2;
    if (ED.spectrum_zoom != 0) {
        // The fine tune frequency must stay within the visible tuning window, which
        // is determined by the zoom level. The visible window is determined by shifting
        // the spectrum by +48 kHz so the center is at EEPROMData.centerFreq_Hz - 48 kHz, 
        // then zooming in around the new center frequency.
        uint32_t visible_bandwidth = SR[SampleRate].rate / (1 << ED.spectrum_zoom);
        lower_limit = -(int32_t)visible_bandwidth/2;
        upper_limit = +(int32_t)visible_bandwidth/2;
        //Debug("Visible bandwidth: " + String(visible_bandwidth));
    }
    // Don't approach within the filter bandwidth of the band edge
    switch (ED.modulation[ED.activeVFO]){
        case LSB:
            lower_limit -= bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz; // FLoCut_Hz is negative
            break;
        case USB:
            upper_limit -= bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
            break;
        case AM:
        case SAM:
        case IQ:
        case DCF77:
            #define MAXABS(a, b) ((abs(a)) > (abs(b)) ? (abs(a)) : (abs(b)))
            int32_t edge_Hz = MAXABS(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz,
                                    bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz); 
            lower_limit += edge_Hz;
            upper_limit -= edge_Hz;
            break;
    }
    //Debug("Upper limit: " + String(upper_limit));
    //Debug("Lower limit: " + String(lower_limit));
    if (-ED.fineTuneFreq_Hz[ED.activeVFO] > upper_limit) 
        ED.fineTuneFreq_Hz[ED.activeVFO] = -upper_limit;
    if (-ED.fineTuneFreq_Hz[ED.activeVFO] < lower_limit) 
        ED.fineTuneFreq_Hz[ED.activeVFO] = -lower_limit;
    //Debug("Fine tune post: " + String(ED.fineTuneFreq_Hz[ED.activeVFO]));
}

/**
 * Return the effective transmit/receive frequency, which is a combination of the center
 * frequency, the fine tune frequency, and the sample rate.
 * 
 * @return The effective transmit/receive frequency in units of Hz * 100
 */
int64_t GetTXRXFreq_dHz(void){
    int64_t val = 100*(ED.centerFreq_Hz[ED.activeVFO] - ED.fineTuneFreq_Hz[ED.activeVFO] - SR[SampleRate].rate/4);
    return val;
}

/**
 * Return the effective transmit/receive frequency, which is a combination of the center
 * frequency, the fine tune frequency, and the sample rate, for a particular VFO.
 * 
 * @return The effective transmit/receive frequency in units of Hz
 */
int64_t GetTXRXFreq(uint8_t vfo){
    int64_t val = (ED.centerFreq_Hz[vfo] - ED.fineTuneFreq_Hz[vfo] - SR[SampleRate].rate/4);
    return val;
}

/**
 * Return the CW transmit frequency, which is a combination of the RX/TX frequency and the
 * CW tone offset.
 * 
 * @return The CW tone transmit frequency in units of Hz * 100
 */
int64_t GetCWTXFreq_dHz(void){
    int64_t txrx = GetTXRXFreq_dHz();
    int64_t offset = (int64_t)(100*CWToneOffsetsHz[ED.CWToneIndex]);
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB) {
        return txrx - offset;
    } else {
        return txrx + offset;
    }
}

/**
 * Set the fine tune frequency to 0 Hz and change the center frequency such that 
 * the RXTX frequency remains the same.
 */
void ResetTuning(void){
    ED.centerFreq_Hz[ED.activeVFO] = ED.centerFreq_Hz[ED.activeVFO] - ED.fineTuneFreq_Hz[ED.activeVFO];
    ED.fineTuneFreq_Hz[ED.activeVFO] = 0.0;
    return;
}

/**
 * Determine which amateur radio band a given frequency belongs to.
 * Searches through all defined bands to find which one contains the frequency.
 *
 * @param freq Frequency in Hz
 * @return Band index (FIRST_BAND to LAST_BAND) if found, or -1 if frequency is not within any defined band
 */
int8_t GetBand(int64_t freq){
    for(uint8_t i = FIRST_BAND; i <= LAST_BAND; i++){
        if(freq >= bands[i].fBandLow_Hz && freq <= bands[i].fBandHigh_Hz){
            return i;
        }
    }
    return -1; // Frequency not within one of the defined ham bands
}

/*
 * The output power of a radio is well-fit by a hyperbolic tan function:
 *    Pout ​= Psat tanh( Psat*​G / Pin )
 * Where Psat is the power at saturation and G parameterizes the power at which
 * the radio starts to saturate.
 * 
 * A more convenient way for us to write this is based on the attenuation setting:
 *    Pout ​= Psat tanh( k 10^{-A/10}​ )
 */

/**
 * Return the predicted output power given an attenuation setting, PA selection
 * and mode selection (SSB or CW). The output power is given by the equation:
 *   Pout [W] ​= Psat [mW] tanh( k 10^{-Attenuation [dB]/10}​ ) / 1000
 * This function is only used in testing
 * 
 * @param atten Attenuation setting in dB
 * @param PAsel 0 for 20W, 1 for 100W
 * @param mode 1 for SSB, 0 for CW
 * @return Output power in mW
 */
float32_t PredictPowerLevel(float32_t atten_dB, int8_t PAsel, int8_t mode){
    if (atten_dB < 0){
        Debug("Atten must be positive!");
        return 0;
    }
    if (atten_dB > 31.5){
        Debug("Atten must be <31.5!");
        return 0;
    }
    float32_t Psat,k,Aoff;
    if (PAsel == 0){
        Psat = ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]];
        k = ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]];
        if (mode == 1){ // SSB
            Aoff = ED.PowerCal_20W_att_offset_dB[ED.currentBand[ED.activeVFO]];
        } else {
            Aoff = 0; // CW
        }
    } else {
        Psat = ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]];
        k = ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]];
        if (mode == 1){ // SSB
            Aoff = ED.PowerCal_100W_att_offset_dB[ED.currentBand[ED.activeVFO]];
        } else {
            Aoff = 0; // CW
        }
    }
    // The CW power is higher than the SSB power. So when we're in SSB mode, increase
    // the applied attenuation by the offset value
    float32_t a = pow(10.0f,-(atten_dB + Aoff)/10.0f);
    float32_t P_mW = Psat * tanhf( k * a );
    return P_mW;
}


/**
 * Return the attenuation setting necessary to produce the requested output power 
 * given a mode selection (SSB or CW). The attenuation is given by the equation:
 *    Atten [dB] ​= -10*log10( 1/k * arctanh( ​Power [W]/(Psat [mW] *1000) ) )
 * 
 * @param Power_W Desired power in W
 * @param mode 1 for SSB, 0 for CW
 * @param *PAsel Pointer to PA setting, we set this to 1 if 100W amp is needed
 * @return Attenuation setting in dB (float32_t)
 */
float32_t CalculateAttenuation(float32_t Power_W, int8_t mode, int8_t *PAsel){
    if (Power_W < 0){
        Debug("Power must be positive!");
        return 0;
    }
    if (Power_W > 100){
        Debug("Power must be <100!");
        return 0;
    }
    if (Power_W >= ED.PowerCal_20W_to_100W_threshold_W){
        *PAsel = 1;
    } else {
        *PAsel = 0;
    }
    float32_t Psat,k,Aoff;
    if (*PAsel == 0){
        Psat = ED.PowerCal_20W_Psat_mW[ED.currentBand[ED.activeVFO]];
        k = ED.PowerCal_20W_kindex[ED.currentBand[ED.activeVFO]];
        if (mode == 1){ // SSB
            Aoff = ED.PowerCal_20W_att_offset_dB[ED.currentBand[ED.activeVFO]];
        } else {
            Aoff = 0; // CW
        }
    } else {
        Psat = ED.PowerCal_100W_Psat_mW[ED.currentBand[ED.activeVFO]];
        k = ED.PowerCal_100W_kindex[ED.currentBand[ED.activeVFO]];
        if (mode == 1){ // SSB
            Aoff = ED.PowerCal_100W_att_offset_dB[ED.currentBand[ED.activeVFO]];
        } else {
            Aoff = 0; // CW
        }
    }
    // The CW power is higher than the SSB power for the same attenuation value. 
    // So when we're in SSB mode, reduce the applied attenuation by the offset value
    return ((-10.0*log10f((1.0/k)*atanhf(Power_W*1000.0/Psat))) - Aoff);
}

///////////////////////////////////////////////////////////////////////////////
// Functions used to fit hyperbolic tan function to saturation curve
// Model: P_out = P_sat * tanh(k * 10^(-Att/10))
///////////////////////////////////////////////////////////////////////////////

// Model function
float32_t model(float32_t att, float32_t P_sat, float32_t k) {
    return P_sat * tanh(k * powf(10.0f, -att / 10.0f));
}

// Gauss-Newton least squares fit
FitResult fitTanhModel(float32_t* att, float32_t* pout, int32_t n, 
                       float32_t P_sat_init, float32_t k_init,
                       int32_t max_iter = 100, float32_t tol = 1e-6) {
    
    float32_t P_sat = P_sat_init;
    float32_t k = k_init;
    
    float32_t J[2];      // Jacobian row
    float32_t JtJ[4];    // 2x2 normal matrix
    float32_t Jtr[2];    // J^T * residual
    float32_t delta[2];  // Parameter update
    
    int32_t iter;
    for (iter = 0; iter < max_iter; iter++) {
        // Reset accumulators
        memset(JtJ, 0, sizeof(JtJ));
        memset(Jtr, 0, sizeof(Jtr));
        
        for (int32_t i = 0; i < n; i++) {
            float32_t x = powf(10.0f, -att[i] / 10.0f);
            float32_t t = tanh(k * x);
            float32_t sech2 = 1.0f - t * t;  // sech^2 = 1 - tanh^2
            
            float32_t y_pred = P_sat * t;
            float32_t residual = pout[i] - y_pred;
            
            // Partial derivatives
            J[0] = t;              // dF/dP_sat
            J[1] = P_sat * sech2 * x;  // dF/dk
            
            // Accumulate J^T * J
            JtJ[0] += J[0] * J[0];
            JtJ[1] += J[0] * J[1];
            JtJ[2] += J[1] * J[0];
            JtJ[3] += J[1] * J[1];
            
            // Accumulate J^T * r
            Jtr[0] += J[0] * residual;
            Jtr[1] += J[1] * residual;
        }
        
        // Solve 2x2 system: JtJ * delta = Jtr
        float32_t det = JtJ[0] * JtJ[3] - JtJ[1] * JtJ[2];
        if (fabsf(det) < 1e-10f) break;
        
        delta[0] = (JtJ[3] * Jtr[0] - JtJ[1] * Jtr[1]) / det;
        delta[1] = (JtJ[0] * Jtr[1] - JtJ[2] * Jtr[0]) / det;
        
        // Update parameters
        P_sat += delta[0];
        k += delta[1];
        
        // Keep parameters positive
        if (P_sat < 1.0f) P_sat = 1.0f;
        if (k < 0.01f) k = 0.01f;
        
        // Check convergence
        if (fabsf(delta[0]) < tol && fabsf(delta[1]) < tol) break;
    }
    
    // Calculate RMS error
    float32_t sse = 0;
    for (int32_t i = 0; i < n; i++) {
        float32_t err = pout[i] - model(att[i], P_sat, k);
        sse += err * err;
    }
    
    FitResult result;
    result.P_sat = P_sat;
    result.k = k;
    result.iterations = iter;
    result.rms_error = sqrtf(sse / n);
    
    return result;
}

FitResult FitPowerCurve(float32_t *att_dB, float32_t *pout_mW, int32_t Npoints,
                    float32_t P_sat_init = 15000.0f, float32_t k_init = 10.0f) {    
    // Initial guesses for P_sat_init and k_init are close for 20W amp case
    char buff[100];
    Serial.println("Fit to data points:");
    Serial.println("| Att [dB] | Power [mW] |");
    Serial.println("|----------|------------|");
    for (int32_t k=0; k<Npoints; k++){
        sprintf(buff,  "| %2.1f     | %3.2f |",att_dB[k],pout_mW[k]);
        Serial.println(buff);
    }

    // Perform fit
    FitResult fit = fitTanhModel(att_dB, pout_mW, Npoints, P_sat_init, k_init);
    
    Serial.println("Power Curve Fit Results:");
    Serial.println("| Parameter  | Value |");
    Serial.println("|------------|-------|");
    Serial.print(  "| P_sat      | "); Serial.print(fit.P_sat); Serial.println(" mW |");
    Serial.print(  "| P_sat      | "); Serial.print(10.0f * log10f(fit.P_sat)); Serial.println(" dBm |");
    Serial.print(  "| k          | "); Serial.print(fit.k); Serial.println(" |");
    Serial.print(  "| Iterations | "); Serial.print(fit.iterations); Serial.println(" |");
    Serial.print(  "| RMS Error  | "); Serial.print(fit.rms_error); Serial.println(" mW |");

    return fit;
}

void SetPower(float32_t power_W, ModeSm_StateId targetState){   
    int8_t mode;
    switch (targetState){
        case ModeSm_StateId_SSB_TRANSMIT:
            mode = 1;
            break;
        case ModeSm_StateId_CW_TRANSMIT_MARK:
            mode = 0;
            break;
        default:
            return;
            break;
    }

    // Calculate the necessary attenuation:
    int8_t PAsel;
    float32_t att = CalculateAttenuation(power_W, mode, &PAsel);
    Debug("Attenuation is " + String(att));
    Debug("PAsel is " + String(PAsel));
    ED.PA100Wactive = (bool)PAsel;

    switch (targetState){
        case ModeSm_StateId_SSB_TRANSMIT:
            ED.XAttenSSB[ED.currentBand[ED.activeVFO]] = att;
            Debug("SSB XAtten is " + String(ED.XAttenSSB[ED.currentBand[ED.activeVFO]]));
            break;
        case ModeSm_StateId_CW_TRANSMIT_MARK:
            ED.XAttenCW[ED.currentBand[ED.activeVFO]] = att;
            Debug("CW XAtten is " + String(ED.XAttenCW[ED.currentBand[ED.activeVFO]]));
            break;
        default:
            Debug("You should never be able to get here! SetPower function.");
            break;
    }
    // Now invoke the hardware state machine change to make these changes
    SetInterrupt(iPOWER_CHANGE);
}