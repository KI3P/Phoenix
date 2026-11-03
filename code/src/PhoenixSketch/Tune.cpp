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
int64_t GetTXRXFreq_cHz(void){
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
int64_t GetCWTXFreq_cHz(void){
    int64_t txrx = GetTXRXFreq_cHz();
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

/*
 * With a shape exponent s (2026-11-02):
 *    Pout = Psat tanh( (k 10^{-A/10})^s )^{1/s}
 * s sets how sharp the knee is; s = 1 is the plain tanh above. On radio 2's
 * level 2 sweeps the plain tanh missed by 0.35 dB rms (worst 0.6 dB) with its
 * knee too soft; with s fitted (0.5-0.75) the misses were 0.16 dB rms (worst 0.3).
 * See code/tools/swr/pa_curve_fits.ipynb. An s of 0 or less is taken as 1, so a
 * configuration saved before the shape existed keeps its curves.
 */
static double ShapeOrOne(float32_t shape){
    return (shape > 0.0f) ? (double)shape : 1.0;
}

// Model function
float32_t attenToPower_mW(float32_t att_dB, float32_t P_sat_mW, float32_t k, float32_t shape) {
    if (att_dB < 0){
        Debug("Invalid negative attenuation provided to attenToPower_mW!");
        return 0.0;
    }
    if (P_sat_mW < 0){
        Debug("Invalid negative P_sat_mW provided to attenToPower_mW!");
        return 0.0;
    }
    double s = ShapeOrOne(shape);
    double z = pow(k * pow(10.0, -att_dB / 10.0), s);
    return (float32_t)(P_sat_mW * pow(tanh(z), 1.0 / s));
}

float32_t powerToAtten_dB(float32_t power_mW, float32_t P_sat_mW, float32_t k, float32_t shape) {
    if (power_mW >= P_sat_mW)
        return 0.0;
    if (power_mW <= 0)
        return 31.5;

    double s = ShapeOrOne(shape);
    double kx = pow(atanh(pow(power_mW / P_sat_mW, s)), 1.0 / s);
    float32_t atten = (float32_t)(-10.0*log10(kx / k));

    // Clamp attenuation to valid range [0, 31.5] dB
    if (atten < 0.0)
        return 0.0;
    if (atten > 31.5)
        return 31.5;

    return atten;
}

/**
 * Return the calculated output power given an attenuation setting and PA selection. 
 * The output power is given by the equation:
 *   Pout [W] ​= Psat [mW] tanh( k 10^{-Attenuation [dB]/10}​ ) / 1000
 * This function is only used in testing and only works for CW mode
 * 
 * @param atten Attenuation setting in dB
 * @param PAsel 0 for 20W, 1 for 100W
 * @return Output power in mW
 */
float32_t CalculateCWPowerLevel(float32_t atten_dB, int8_t PAsel){
    if (atten_dB < 0){
        Debug("Atten must be positive!");
        return 0;
    }
    if (atten_dB > 31.5){
        Debug("Atten must be <31.5!");
        return 0;
    }
    float32_t Psat,k,s;
    int32_t band = ED.currentBand[ED.activeVFO];
    if (PAsel == 0){
        Psat = ED.PowerCal_20W_Psat_mW[band];
        k = ED.PowerCal_20W_kindex[band];
        s = ED.PowerCal_20W_shape[band];
    } else {
        Psat = ED.PowerCal_100W_Psat_mW[band];
        k = ED.PowerCal_100W_kindex[band];
        s = ED.PowerCal_100W_shape[band];
    }
    float32_t P_mW = attenToPower_mW(atten_dB,Psat,k,s);
    return P_mW;
}

/**
 * Return the attenuation setting necessary to produce the requested output power 
 * in CW mode. The attenuation is given by the equation:
 *    Atten [dB] ​= -10*log10( 1/k * arctanh( ​Power [W]/(Psat [mW] *1000) ) )
 * 
 * @param Power_W Desired power in W
 * @param *PAsel Pointer to PA setting, we set this to 1 if 100W amp is needed
 * @return Attenuation setting in dB (float32_t)
 */
float32_t CalculateCWAttenuation(float32_t Power_W, bool *PAsel){
    if (Power_W < 0){
        Debug("Power must be positive!");
        return 0;
    }
    if (Power_W > 100){
        Debug("Power must be <100!");
        return 0;
    }
    if (Power_W >= ED.PowerCal_20W_to_100W_threshold_W){
        *PAsel = true;
    } else {
        *PAsel = false;
    }
    float32_t Psat,k,s;
    int32_t band = ED.currentBand[ED.activeVFO];
    if (*PAsel){
        Psat = ED.PowerCal_100W_Psat_mW[band];
        k = ED.PowerCal_100W_kindex[band];
        s = ED.PowerCal_100W_shape[band];
    } else {
        Psat = ED.PowerCal_20W_Psat_mW[band];
        k = ED.PowerCal_20W_kindex[band];
        s = ED.PowerCal_20W_shape[band];
    }
    return powerToAtten_dB(Power_W*1000.0,Psat,k,s);
}

/**
 * Return the gain required to produce the requested output power in SSB mode.
 * @param power_W The desired setpoint in W
 * @param *PAsel Pointer to PA setting, we set this to 1 if 100W amp is needed
 * @return The gain in dB
 */
float32_t CalculateSSBTXGain(float32_t Power_W, bool *PAsel){
    if (Power_W < 0){
        Debug("Power must be positive!");
        return 0;
    }
    if (Power_W > 100){
        Debug("Power must be <100!");
        return 0;
    }
    if (Power_W >= ED.PowerCal_20W_to_100W_threshold_W){
        *PAsel = true;
    } else {
        *PAsel = false;
    }
    float32_t gain_dB;
    if (ED.PA100Wactive)
        gain_dB = 10.0*log10f(Power_W / SSB_100W_CAL_POWER_POINT_W);
    else
        gain_dB = 10.0*log10f(Power_W / SSB_20W_CAL_POWER_POINT_W);
    return gain_dB;
}

///////////////////////////////////////////////////////////////////////////////
// Functions used to fit hyperbolic tan function to saturation curve
// Model: P_out = P_sat * tanh(k * 10^(-Att/10))
///////////////////////////////////////////////////////////////////////////////

/** Solve the n x n system A d = b (n <= 3) by elimination with partial pivoting */
static bool Solve3(double A[3][3], double b[3], double d[3], int32_t n) {
    double M[3][4];
    for (int32_t i = 0; i < n; i++) {
        for (int32_t j = 0; j < n; j++) M[i][j] = A[i][j];
        M[i][n] = b[i];
    }
    for (int32_t c = 0; c < n; c++) {
        int32_t piv = c;
        for (int32_t r = c + 1; r < n; r++)
            if (fabs(M[r][c]) > fabs(M[piv][c])) piv = r;
        if (fabs(M[piv][c]) < 1e-300) return false;
        if (piv != c)
            for (int32_t j = 0; j <= n; j++) { double t = M[c][j]; M[c][j] = M[piv][j]; M[piv][j] = t; }
        for (int32_t r = c + 1; r < n; r++) {
            double f = M[r][c] / M[c][c];
            for (int32_t j = c; j <= n; j++) M[r][j] -= f * M[c][j];
        }
    }
    for (int32_t i = n - 1; i >= 0; i--) {
        double acc = M[i][n];
        for (int32_t j = i + 1; j < n; j++) acc -= M[i][j] * d[j];
        d[i] = acc / M[i][i];
    }
    return true;
}

/**
 * Levenberg-Marquardt least squares fit of the PA curve in dB.
 *
 * Model: P = P_sat tanh((k x)^s)^(1/s), x = 10^(-att/10). The residuals are
 * 10 log10(measured / model), so every point counts by its relative error: a
 * power setting is wrong by a ratio, whatever the power. The parameters are
 * fitted as ln P_sat, ln k and ln s, which keeps them positive and the problem
 * well scaled. With fewer than 4 points (the manual calibration's 3) the shape
 * is held at s = 1, the plain tanh; otherwise the plain tanh is fitted first and
 * the shape then released.
 *
 * Before 2026-11-02 the plain tanh was fitted in mW, in float32: the points near
 * the ceiling outweighed the rest, so radio 2's level 2 sweeps were missed by up
 * to 1.6 dB lower down, and a 1e-6 mW convergence test the float32 maths could
 * not resolve at 10 W kept it running to the iteration limit.
 */
static FitResult fitPowerModel(const float32_t* att, const float32_t* pout, int32_t n,
                               float32_t P_sat_init, float32_t k_init, int32_t max_iter = 100) {
    const double dB = 10.0 / log(10.0);
    const double wMin = log(0.05), wMax = log(20.0);
    double th[3] = {log((double)((P_sat_init > 1.0f) ? P_sat_init : 1.0f)),
                    log((double)((k_init > 0.01f) ? k_init : 0.01f)), 0.0};
    int32_t used = 0;
    for (int32_t i = 0; i < n; i++)
        if (pout[i] > 0) used++;

    // Model in dB at one point, and its derivatives with respect to the parameters
    auto Model = [&](const double *t, double att_i, double *grad) -> double {
        double s = exp(t[2]);
        double a = t[1] - att_i * log(10.0) / 10.0;     // ln(k x)
        double z = exp(s * a);                          // (k x)^s
        double lt = log(tanh(z));
        if (grad) {
            double g = (2.0 * z < 700.0) ? (2.0 * z) / sinh(2.0 * z) : 0.0; // 2z / sinh 2z
            grad[0] = dB;
            grad[1] = dB * g;
            grad[2] = dB * (-lt / s + a * g);
        }
        return dB * (t[0] + lt / s);
    };
    auto SSE = [&](const double *t) -> double {
        double sse = 0;
        for (int32_t i = 0; i < n; i++) {
            if (pout[i] <= 0) continue;
            double r = dB * log((double)pout[i]) - Model(t, att[i], nullptr);
            if (!isfinite(r)) return (double)INFINITY;
            sse += r * r;
        }
        return sse;
    };

    int32_t iterations = 0;
    // Stage 1: the plain tanh (s = 1); stage 2: with the shape, if there are enough points
    for (int32_t np = 2; np <= ((used >= 4) ? 3 : 2); np++) {
        double sse = SSE(th);
        double lambda = 1e-3;
        for (int32_t iter = 0; iter < max_iter; iter++) {
            iterations++;
            double JtJ[3][3] = {{0}}, Jtr[3] = {0}, grad[3];
            for (int32_t i = 0; i < n; i++) {
                if (pout[i] <= 0) continue;
                double r = dB * log((double)pout[i]) - Model(th, att[i], grad);
                for (int32_t a = 0; a < np; a++) {
                    Jtr[a] += grad[a] * r;     // J = -grad, so -J^T r = grad^T r
                    for (int32_t b = 0; b < np; b++) JtJ[a][b] += grad[a] * grad[b];
                }
            }
            bool accepted = false;
            double d[3] = {0, 0, 0};
            while (lambda < 1e12) {
                double A[3][3];
                for (int32_t a = 0; a < np; a++)
                    for (int32_t b = 0; b < np; b++)
                        A[a][b] = JtJ[a][b] * ((a == b) ? (1.0 + lambda) : 1.0);
                if (!Solve3(A, Jtr, d, np)) { lambda *= 10.0; continue; }
                double trial[3] = {th[0] + d[0], th[1] + d[1], th[2]};
                if (np == 3) trial[2] = fmin(fmax(th[2] + d[2], wMin), wMax);
                double sseTrial = SSE(trial);
                if (sseTrial <= sse) {
                    accepted = (sse - sseTrial) > 1e-15 * (1.0 + sse);
                    for (int32_t a = 0; a < 3; a++) th[a] = trial[a];
                    sse = sseTrial;
                    lambda = fmax(lambda / 10.0, 1e-12);
                    break;
                }
                lambda *= 10.0;
            }
            if (!accepted || ((fabs(d[0]) < 1e-9) && (fabs(d[1]) < 1e-9) && (fabs(d[2]) < 1e-9)))
                break;
        }
    }

    FitResult result;
    result.P_sat = (float32_t)exp(th[0]);
    result.k = (float32_t)exp(th[1]);
    result.shape = (float32_t)exp(th[2]);
    result.iterations = iterations;
    double sse_mW = 0, sse_dB = 0;
    for (int32_t i = 0; i < n; i++) {
        float32_t P = attenToPower_mW(att[i], result.P_sat, result.k, result.shape);
        sse_mW += (pout[i] - P) * (pout[i] - P);
        if (pout[i] > 0) {
            double e = dB * log(P / (double)pout[i]);
            sse_dB += e * e;
        }
    }
    result.rms_error = (float32_t)sqrt(sse_mW / ((n > 0) ? n : 1));
    result.rms_dB = (float32_t)sqrt(sse_dB / ((used > 0) ? used : 1));
    return result;
}

FitResult FitPowerCurve(float32_t *att_dB, float32_t *pout_mW, int32_t Npoints,
                    float32_t P_sat_init = 15000.0f, float32_t k_init = 6.0f) {    
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
    FitResult fit = fitPowerModel(att_dB, pout_mW, Npoints, P_sat_init, k_init);
    
    Serial.println("Power Curve Fit Results:");
    Serial.println("| Parameter  | Value |");
    Serial.println("|------------|-------|");
    Serial.print(  "| P_sat      | "); Serial.print(fit.P_sat); Serial.println(" mW |");
    Serial.print(  "| P_sat      | "); Serial.print(10.0f * log10f(fit.P_sat)); Serial.println(" dBm |");
    Serial.print(  "| k          | "); Serial.print(fit.k); Serial.println(" |");
    Serial.print(  "| s (shape)  | "); Serial.print(fit.shape); Serial.println(" |");
    Serial.print(  "| Iterations | "); Serial.print(fit.iterations); Serial.println(" |");
    Serial.print(  "| RMS Error  | "); Serial.print(fit.rms_error); Serial.println(" mW |");
    sprintf(buff, "| RMS Error  | %.3f dB |", fit.rms_dB);
    Serial.println(buff);

    return fit;
}
