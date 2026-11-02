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
 * Calculations for the SWR bridge calibration. See SWRCalibration.h and
 * code/docs/swr_calibration_approach.md Sections 6 and 7.
 */

#include "SDT.h"

// Calibration frequency of each band with an LPF, BAND_160M to BAND_6M (Section 6)
static const float32_t bandFrequency_MHz[BAND_6M + 1] = {
    1.9f, 3.75f, 5.357f, 7.1f, 10.125f, 14.2f, 18.118f, 21.2f, 24.94f, 28.5f, 50.2f};

// Level 1 default forward offsets for 10 turns per core, measured on two radios
// (Section 2). Other bands are interpolated between these in log frequency.
static const int32_t level1Band[4] = {BAND_160M, BAND_40M, BAND_20M, BAND_10M};
static const float32_t level1Offset_dB[4] = {-4.03f, -3.68f, -4.73f, -4.42f};

float32_t SWRCalPower_dBm(float32_t V_mV, float32_t m, float32_t b){
    return V_mV / m + b;
}

/**
 * Least-squares line y = k x + c, returned as the detector's slope m = 1/k and
 * intercept b = c, with the residuals of the points in use.
 */
static struct SWRLineFit FitPoints(const float32_t *V_mV, const float32_t *P_dBm, uint32_t n,
                                   const bool *use){
    struct SWRLineFit f = {false, 0, 0, 0, 0};
    uint32_t count = 0;
    double sx = 0, sy = 0;
    for (uint32_t k = 0; k < n; k++){
        if (use && !use[k]) continue;
        sx += V_mV[k];
        sy += P_dBm[k];
        count++;
    }
    if (count < 2)
        return f;
    double mx = sx / count, my = sy / count;
    double sxx = 0, sxy = 0;
    for (uint32_t k = 0; k < n; k++){
        if (use && !use[k]) continue;
        sxx += (V_mV[k] - mx) * (V_mV[k] - mx);
        sxy += (V_mV[k] - mx) * (P_dBm[k] - my);
    }
    if ((sxx <= 0) || (sxy == 0))
        return f;
    double slope = sxy / sxx; // dB per mV
    f.m = (float32_t)(1.0 / slope);
    f.b = (float32_t)(my - slope * mx);
    double ss = 0, worst = 0;
    for (uint32_t k = 0; k < n; k++){
        if (use && !use[k]) continue;
        double r = P_dBm[k] - (slope * V_mV[k] + f.b);
        ss += r * r;
        if (fabs(r) > worst) worst = fabs(r);
    }
    f.rms_dB = (float32_t)sqrt(ss / count);
    f.worst_dB = (float32_t)worst;
    f.valid = true;
    return f;
}

struct SWRLineFit SWRCalFitLine(const float32_t *V_mV, const float32_t *P_dBm, uint32_t n){
    return FitPoints(V_mV, P_dBm, n, nullptr);
}

struct SWRLineFit SWRCalFitForwardLevel3(const float32_t *V_mV, const float32_t *P_W,
                                         const float32_t *att_dB, uint32_t n, bool *ceilingDropped,
                                         bool *pointDropped, float32_t *residual_dB){
    float32_t P_dBm[16];
    bool use[16];
    if (n > 16) n = 16;
    float32_t top_dBm = -1e9f;
    for (uint32_t k = 0; k < n; k++){
        P_dBm[k] = 10.0f * log10f(1000.0f * P_W[k]);
        if (P_dBm[k] > top_dBm) top_dBm = P_dBm[k];
    }
    bool hasCeiling = false;
    for (uint32_t k = 0; k < n; k++){
        use[k] = (att_dB[k] > 0.0f) && (P_dBm[k] < top_dBm - SWRCAL_CEILING_SPAN_DB);
        if (!use[k]) hasCeiling = true;
    }
    *ceilingDropped = false;
    for (uint32_t k = 0; k < n; k++){
        if (pointDropped) pointDropped[k] = false;
        if (residual_dB) residual_dB[k] = 0.0f;
    }
    struct SWRLineFit without = FitPoints(V_mV, P_dBm, n, use);
    if (!hasCeiling || !without.valid)
        return SWRCalFitLine(V_mV, P_dBm, n);
    // Keep each ceiling point that lies on the line through the others
    for (uint32_t k = 0; k < n; k++){
        if (use[k]) continue;
        float32_t r = P_dBm[k] - SWRCalPower_dBm(V_mV[k], without.m, without.b);
        if (residual_dB) residual_dB[k] = r;
        if (fabsf(r) <= SWRCAL_CEILING_RESIDUAL_DB){
            use[k] = true;
        } else {
            *ceilingDropped = true;
            if (pointDropped) pointDropped[k] = true;
        }
    }
    return FitPoints(V_mV, P_dBm, n, use);
}

float32_t SWRCalForwardInterceptLevel2(float32_t P_W, float32_t V_mV){
    return 10.0f * log10f(1000.0f * P_W) - V_mV / SWRCAL_NOMINAL_SLOPE_MV_PER_DB;
}

float32_t SWRCalBandFrequency_MHz(int32_t band){
    if ((band < BAND_160M) || (band > BAND_6M))
        return 0.0f;
    return bandFrequency_MHz[band];
}

float32_t SWRCalLevel1ForwardOffset(int32_t band, uint8_t turns){
    float32_t turnsCorrection = 20.0f * log10f((turns > 0 ? turns : 10) / 10.0f);
    if ((band < BAND_160M) || (band > BAND_6M))
        return turnsCorrection;
    float32_t o;
    if (band >= BAND_10M){
        o = level1Offset_dB[3]; // 6 m takes the 10 m value, unverified
    } else {
        int32_t i = 0;
        while ((i < 2) && (band > level1Band[i + 1]))
            i++;
        float32_t x = log10f(bandFrequency_MHz[band]);
        float32_t x0 = log10f(bandFrequency_MHz[level1Band[i]]);
        float32_t x1 = log10f(bandFrequency_MHz[level1Band[i + 1]]);
        o = level1Offset_dB[i] + (level1Offset_dB[i + 1] - level1Offset_dB[i]) * (x - x0) / (x1 - x0);
    }
    return o + turnsCorrection;
}

struct SWRReflectedFit SWRCalFitReflected(const float32_t *Vf_open, const float32_t *Vr_open, uint32_t n_open,
                                          const float32_t *Vf_short, const float32_t *Vr_short, uint32_t n_short,
                                          float32_t m_f, float32_t b_f){
    struct SWRReflectedFit f = {false, false, 0, 0, 0, 0, 0};
    if (n_open < 2)
        return f;
    bool useShort = (n_short >= 2);
    // The reflected power equals the forward power, which the forward line gives
    double mxo = 0, myo = 0, mxs = 0, mys = 0;
    for (uint32_t k = 0; k < n_open; k++){
        mxo += Vr_open[k];
        myo += SWRCalPower_dBm(Vf_open[k], m_f, b_f);
    }
    mxo /= n_open; myo /= n_open;
    if (useShort){
        for (uint32_t k = 0; k < n_short; k++){
            mxs += Vr_short[k];
            mys += SWRCalPower_dBm(Vf_short[k], m_f, b_f);
        }
        mxs /= n_short; mys /= n_short;
    }
    // One slope for both standards, a separate intercept for each
    double sxx = 0, sxy = 0;
    for (uint32_t k = 0; k < n_open; k++){
        double dx = Vr_open[k] - mxo;
        sxx += dx * dx;
        sxy += dx * (SWRCalPower_dBm(Vf_open[k], m_f, b_f) - myo);
    }
    if (useShort){
        for (uint32_t k = 0; k < n_short; k++){
            double dx = Vr_short[k] - mxs;
            sxx += dx * dx;
            sxy += dx * (SWRCalPower_dBm(Vf_short[k], m_f, b_f) - mys);
        }
    }
    if ((sxx <= 0) || (sxy == 0))
        return f;
    double slope = sxy / sxx;
    f.m = (float32_t)(1.0 / slope);
    f.b_open = (float32_t)(myo - slope * mxo);
    f.b_short = useShort ? (float32_t)(mys - slope * mxs) : f.b_open;
    f.b = 0.5f * (f.b_open + f.b_short);
    f.openOnly = !useShort;
    double ss = 0;
    for (uint32_t k = 0; k < n_open; k++){
        double r = SWRCalPower_dBm(Vf_open[k], m_f, b_f) - (slope * Vr_open[k] + f.b_open);
        ss += r * r;
    }
    if (useShort){
        for (uint32_t k = 0; k < n_short; k++){
            double r = SWRCalPower_dBm(Vf_short[k], m_f, b_f) - (slope * Vr_short[k] + f.b_short);
            ss += r * r;
        }
    }
    f.rms_dB = (float32_t)sqrt(ss / (n_open + (useShort ? n_short : 0)));
    f.valid = true;
    return f;
}

float32_t SWRCalSWR(float32_t Vf_mV, float32_t Vr_mV, float32_t m_f, float32_t b_f,
                    float32_t m_r, float32_t b_r){
    float32_t ratio_dB = SWRCalPower_dBm(Vr_mV, m_r, b_r) - SWRCalPower_dBm(Vf_mV, m_f, b_f);
    if (ratio_dB >= 0.0f)
        return SWRCAL_UNCALIBRATED_SWR;
    float32_t gamma = powf(10.0f, ratio_dB / 20.0f);
    float32_t swr = (1.0f + gamma) / (1.0f - gamma);
    // Rounding can leave the ratio a hair below 0 dB for equal powers
    return (swr > SWRCAL_UNCALIBRATED_SWR) ? SWRCAL_UNCALIBRATED_SWR : swr;
}

enum SWRCalOscillationRule SWRCalOscillationCheck(bool hasPrevious, float32_t Vf_previous, float32_t drop_dB,
                                                  float32_t Vf, bool hasReference, float32_t Vf_reference){
    if (hasPrevious && (drop_dB > 0.0f) &&
        ((Vf_previous - Vf) < SWRCAL_OSC_DROP_FRACTION * drop_dB * SWRCAL_NOMINAL_SLOPE_MV_PER_DB))
        return SWRCAL_OSC_NO_FALL;
    if (hasReference &&
        (Vf > Vf_reference + SWRCAL_OSC_MARGIN_DB * SWRCAL_NOMINAL_SLOPE_MV_PER_DB))
        return SWRCAL_OSC_ABOVE_REFERENCE;
    return SWRCAL_OSC_NONE;
}

bool SWRCalOscillating(bool hasPrevious, float32_t Vf_previous, float32_t drop_dB, float32_t Vf,
                       bool hasReference, float32_t Vf_reference){
    return SWRCalOscillationCheck(hasPrevious, Vf_previous, drop_dB, Vf, hasReference, Vf_reference)
           != SWRCAL_OSC_NONE;
}

float32_t SWRCalMedian(float32_t *values, uint32_t n){
    if (n == 0)
        return 0.0f;
    // Insertion sort: n is at most a few tens
    for (uint32_t i = 1; i < n; i++){
        float32_t v = values[i];
        int32_t j = (int32_t)i - 1;
        while ((j >= 0) && (values[j] > v)){
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = v;
    }
    if (n % 2)
        return values[n / 2];
    return 0.5f * (values[n / 2 - 1] + values[n / 2]);
}
