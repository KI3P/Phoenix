/**
 * Tests of the SWR bridge calibration calculations (SWRCalibration.cpp), from
 * code/docs/swr_calibration_approach.md Sections 2, 6 and 7.
 */

#include <gtest/gtest.h>
#include "SDT.h"

// A detector reading for a power, given the line P = V/m + b
static float32_t Reading(float32_t P_dBm, float32_t m, float32_t b){
    return m * (P_dBm - b);
}

static float32_t dBm(float32_t P_W){
    return 10.0f * log10f(1000.0f * P_W);
}

TEST(SWRCalibration, FitLineRecoversSlopeAndIntercept){
    const float32_t m = 24.4f, b = -45.2f;
    float32_t V[6], P[6];
    for (int k = 0; k < 6; k++){
        P[k] = 20.0f + 3.0f * k;
        V[k] = Reading(P[k], m, b);
    }
    struct SWRLineFit f = SWRCalFitLine(V, P, 6);
    ASSERT_TRUE(f.valid);
    EXPECT_NEAR(f.m, m, 1e-3);
    EXPECT_NEAR(f.b, b, 1e-3);
    EXPECT_NEAR(f.rms_dB, 0.0f, 1e-4);
}

TEST(SWRCalibration, FitLineWithNoiseReportsResiduals){
    const float32_t m = 25.1f, b = -43.0f;
    const float32_t noise_dB[7] = {0.1f, -0.1f, 0.05f, -0.05f, 0.1f, -0.1f, 0.0f};
    float32_t V[7], P[7];
    for (int k = 0; k < 7; k++){
        P[k] = 18.0f + 3.0f * k;
        V[k] = Reading(P[k] + noise_dB[k], m, b);
    }
    struct SWRLineFit f = SWRCalFitLine(V, P, 7);
    ASSERT_TRUE(f.valid);
    EXPECT_NEAR(f.m, m, 0.3);
    EXPECT_NEAR(f.b, b, 0.3);
    EXPECT_GT(f.rms_dB, 0.0f);
    EXPECT_LT(f.rms_dB, 0.1f);
    EXPECT_LE(f.rms_dB, f.worst_dB);
}

TEST(SWRCalibration, FitLineNeedsTwoDistinctPoints){
    float32_t V[2] = {1000, 1000}, P[2] = {30, 31};
    EXPECT_FALSE(SWRCalFitLine(V, P, 1).valid);
    EXPECT_FALSE(SWRCalFitLine(V, P, 2).valid) << "No spread in V";
}

TEST(SWRCalibration, Level3KeepsACeilingPointOnTheLine){
    const float32_t m = 24.6f, b = -44.0f;
    const float32_t att[7] = {0, 5, 10, 15, 20, 25, 31.5};
    float32_t V[7], P_W[7];
    for (int k = 0; k < 7; k++){
        P_W[k] = 15.0f * powf(10.0f, -att[k] / 10.0f);
        V[k] = Reading(dBm(P_W[k]), m, b);
    }
    bool dropped = true;
    struct SWRLineFit f = SWRCalFitForwardLevel3(V, P_W, att, 7, &dropped);
    ASSERT_TRUE(f.valid);
    EXPECT_FALSE(dropped);
    EXPECT_NEAR(f.m, m, 1e-2);
    EXPECT_NEAR(f.b, b, 1e-2);
}

TEST(SWRCalibration, Level3DropsACeilingPointTwoDBHigh){
    // Radio 1's 10 m: the 0 dB reading sits about 2 dB above the line
    const float32_t m = 24.6f, b = -44.0f;
    const float32_t att[7] = {0, 5, 10, 15, 20, 25, 31.5};
    float32_t V[7], P_W[7];
    for (int k = 0; k < 7; k++){
        P_W[k] = 6.6f * powf(10.0f, -att[k] / 10.0f);
        V[k] = Reading(dBm(P_W[k]) + (k == 0 ? 2.0f : 0.0f), m, b);
    }
    bool dropped = false;
    struct SWRLineFit f = SWRCalFitForwardLevel3(V, P_W, att, 7, &dropped);
    ASSERT_TRUE(f.valid);
    EXPECT_TRUE(dropped);
    EXPECT_NEAR(f.m, m, 1e-2);
    EXPECT_NEAR(f.b, b, 1e-2);
}

TEST(SWRCalibration, Level3DropsSaturatedPointsTheDetectorOverReads){
    // Radio 1's 10 m: the PA is saturated at 0 and 5 dB (6.6 W), where the
    // forward detector reads 2.3 and 1.1 dB high; the other points are fine
    const float32_t m = 24.27f, b = -44.64f;
    const float32_t att[8] = {0, 5, 10, 15, 20, 25, 31.5, 5};
    const float32_t over_dB[8] = {2.28f, 1.14f, 0, 0, 0, 0, 0, 1.14f};
    float32_t V[8], P_W[8];
    for (int k = 0; k < 8; k++){
        P_W[k] = 6.6f * tanhf(16.8f * powf(10.0f, -att[k] / 10.0f));
        V[k] = Reading(dBm(P_W[k]) + over_dB[k], m, b);
    }
    bool dropped = false;
    struct SWRLineFit f = SWRCalFitForwardLevel3(V, P_W, att, 8, &dropped);
    ASSERT_TRUE(f.valid);
    EXPECT_TRUE(dropped);
    EXPECT_NEAR(f.m, m, 1e-2);
    EXPECT_NEAR(f.b, b, 1e-2);
    EXPECT_LT(f.worst_dB, 1e-3);
}

TEST(SWRCalibration, Level3KeepsSaturatedPointsOnTheLine){
    // The other bands: saturated points the detector reads correctly stay in
    const float32_t m = 24.7f, b = -43.1f;
    const float32_t att[7] = {0, 5, 10, 15, 20, 25, 31.5};
    float32_t V[7], P_W[7];
    for (int k = 0; k < 7; k++){
        P_W[k] = 15.2f * tanhf(15.9f * powf(10.0f, -att[k] / 10.0f));
        V[k] = Reading(dBm(P_W[k]), m, b);
    }
    bool dropped = true;
    struct SWRLineFit f = SWRCalFitForwardLevel3(V, P_W, att, 7, &dropped);
    ASSERT_TRUE(f.valid);
    EXPECT_FALSE(dropped);
    EXPECT_NEAR(f.b, b, 1e-2);
}

TEST(SWRCalibration, Level2InterceptFromOneReading){
    // 2 W is 33.01 dBm; at 1700 mV with the nominal slope b = 33.01 - 68
    EXPECT_NEAR(SWRCalForwardInterceptLevel2(2.0f, 1700.0f), 33.0103f - 68.0f, 1e-3);
}

TEST(SWRCalibration, Level1DefaultsAtTheMeasuredBands){
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_160M, 10), -4.03f, 1e-4);
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_40M, 10), -3.68f, 1e-4);
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_20M, 10), -4.73f, 1e-4);
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_10M, 10), -4.42f, 1e-4);
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_6M, 10), -4.42f, 1e-4) << "6 m takes the 10 m value";
}

TEST(SWRCalibration, Level1DefaultsInterpolateInLogFrequency){
    // 80 m at 3.75 MHz between 160 m (1.9) and 40 m (7.1)
    float32_t t = (log10f(3.75f) - log10f(1.9f)) / (log10f(7.1f) - log10f(1.9f));
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_80M, 10), -4.03f + t * (-3.68f + 4.03f), 1e-4);
    // 15 m between 20 m and 10 m
    t = (log10f(21.2f) - log10f(14.2f)) / (log10f(28.5f) - log10f(14.2f));
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_15M, 10), -4.73f + t * (-4.42f + 4.73f), 1e-4);
    // 30 m between 40 m and 20 m
    float32_t o30 = SWRCalLevel1ForwardOffset(BAND_30M, 10);
    EXPECT_LT(o30, -3.68f);
    EXPECT_GT(o30, -4.73f);
}

TEST(SWRCalibration, Level1DefaultsAddTheTurnsCorrection){
    // Radio 2 had 20 turns per core: +6.02 dB
    EXPECT_NEAR(SWRCalLevel1ForwardOffset(BAND_40M, 20) - SWRCalLevel1ForwardOffset(BAND_40M, 10),
                6.0206f, 1e-3);
}

/**
 * The open and the short see the same reflected line, but the bridge's leak
 * adds to one and subtracts from the other. The fit must cancel it.
 */
TEST(SWRCalibration, ReflectedFitCancelsTheLeak){
    const float32_t m_f = 24.5f, b_f = -44.5f;   // forward line
    const float32_t m_r = 25.4f, b_r = -40.7f;   // true reflected line
    const float32_t leak_dB = 0.2f;
    float32_t Vfo[6], Vro[6], Vfs[6], Vrs[6];
    for (int k = 0; k < 6; k++){
        float32_t Po = 31.0f - 2.0f * k, Ps = 30.5f - 2.0f * k; // the short draws a little less
        Vfo[k] = Reading(Po, m_f, b_f);
        Vro[k] = Reading(Po + leak_dB, m_r, b_r);
        Vfs[k] = Reading(Ps, m_f, b_f);
        Vrs[k] = Reading(Ps - leak_dB, m_r, b_r);
    }
    struct SWRReflectedFit f = SWRCalFitReflected(Vfo, Vro, 6, Vfs, Vrs, 6, m_f, b_f);
    ASSERT_TRUE(f.valid);
    EXPECT_FALSE(f.openOnly);
    EXPECT_FALSE(f.shortOnly);
    EXPECT_NEAR(f.m, m_r, 1e-2);
    EXPECT_NEAR(f.b, b_r, 1e-2) << "Mean of the two intercepts";
    EXPECT_NEAR(f.b_open - f.b_short, -2.0f * leak_dB, 1e-2);
    EXPECT_NEAR(f.rms_dB, 0.0f, 1e-3);
}

TEST(SWRCalibration, ReflectedFitFallsBackToTheOpen){
    const float32_t m_f = 25.0f, b_f = -44.0f, m_r = 24.8f, b_r = -41.0f;
    float32_t Vfo[5], Vro[5];
    for (int k = 0; k < 5; k++){
        float32_t P = 31.0f - 2.0f * k;
        Vfo[k] = Reading(P, m_f, b_f);
        Vro[k] = Reading(P, m_r, b_r);
    }
    struct SWRReflectedFit f = SWRCalFitReflected(Vfo, Vro, 5, nullptr, nullptr, 0, m_f, b_f);
    ASSERT_TRUE(f.valid);
    EXPECT_TRUE(f.openOnly);
    EXPECT_NEAR(f.m, m_r, 1e-2);
    EXPECT_NEAR(f.b, b_r, 1e-2);
    EXPECT_FLOAT_EQ(f.b_open, f.b_short);

    EXPECT_FALSE(SWRCalFitReflected(Vfo, Vro, 1, nullptr, nullptr, 0, m_f, b_f).valid)
        << "The open alone needs two points";
}

TEST(SWRCalibration, ReflectedFitFallsBackToTheShort){
    const float32_t m_f = 25.0f, b_f = -44.0f, m_r = 24.8f, b_r = -41.0f;
    const float32_t leak_dB = 0.2f;
    float32_t Vfs[6], Vrs[6];
    for (int k = 0; k < 6; k++){
        float32_t P = 30.0f - 2.0f * k;
        Vfs[k] = Reading(P, m_f, b_f);
        Vrs[k] = Reading(P - leak_dB, m_r, b_r);
    }
    struct SWRReflectedFit f = SWRCalFitReflected(nullptr, nullptr, 0, Vfs, Vrs, 6, m_f, b_f);
    ASSERT_TRUE(f.valid);
    EXPECT_FALSE(f.openOnly);
    EXPECT_TRUE(f.shortOnly);
    EXPECT_NEAR(f.m, m_r, 1e-2);
    // The leak is not cancelled: the short reads it low, so the intercept comes out high by it
    EXPECT_NEAR(f.b, b_r + leak_dB, 1e-2);
    EXPECT_FLOAT_EQ(f.b_open, f.b_short);
    EXPECT_NEAR(f.rms_dB, 0.0f, 1e-3);

    // One open point is too few to use: the short alone
    f = SWRCalFitReflected(Vfs, Vrs, 1, Vfs, Vrs, 6, m_f, b_f);
    ASSERT_TRUE(f.valid);
    EXPECT_TRUE(f.shortOnly);
    EXPECT_FALSE(SWRCalFitReflected(Vfs, Vrs, 1, Vfs, Vrs, 1, m_f, b_f).valid)
        << "Neither standard has two points";
}

TEST(SWRCalibration, SWROfKnownLoads){
    const float32_t m_f = 24.5f, b_f = -44.5f, m_r = 25.4f, b_r = -40.7f;
    float32_t Pf = 33.0f;
    // A 100 Ohm load: |Gamma| = 1/3, SWR 2
    float32_t Pr = Pf + 20.0f * log10f(1.0f / 3.0f);
    EXPECT_NEAR(SWRCalSWR(Reading(Pf, m_f, b_f), Reading(Pr, m_r, b_r), m_f, b_f, m_r, b_r), 2.0f, 1e-3);
    // A 200 Ohm load: |Gamma| = 0.6, SWR 4
    Pr = Pf + 20.0f * log10f(0.6f);
    EXPECT_NEAR(SWRCalSWR(Reading(Pf, m_f, b_f), Reading(Pr, m_r, b_r), m_f, b_f, m_r, b_r), 4.0f, 1e-3);
    // Reflected not below forward
    EXPECT_FLOAT_EQ(SWRCalSWR(Reading(Pf, m_f, b_f), Reading(Pf, m_r, b_r), m_f, b_f, m_r, b_r),
                    SWRCAL_UNCALIBRATED_SWR);
}

TEST(SWRCalibration, OscillationRules){
    // A healthy point: the drive drops 2 dB, V_f falls 50 mV
    EXPECT_EQ(SWRCalOscillationCheck(true, 1900, 2.0f, 1850, true, 1850), SWRCAL_OSC_NONE);
    // Falling just under half the drive drop: short, but not by itself an oscillation
    EXPECT_EQ(SWRCalOscillationCheck(true, 1900, 2.0f, 1876, false, 0), SWRCAL_OSC_SHORT_FALL);
    // More than 6 dB above the 50 Ohm reference
    EXPECT_EQ(SWRCalOscillationCheck(false, 0, 0, 1960, true, 1800), SWRCAL_OSC_ABOVE_REFERENCE);
    EXPECT_EQ(SWRCalOscillationCheck(false, 0, 0, 1940, true, 1800), SWRCAL_OSC_NONE);
    // Radio 2's 20 m open reads up to 128 mV (5 dB) above 50 Ohm: not an oscillation
    EXPECT_EQ(SWRCalOscillationCheck(false, 0, 0, 1800 + 128, true, 1800), SWRCAL_OSC_NONE);
    // The first point with no reference cannot be judged
    EXPECT_EQ(SWRCalOscillationCheck(false, 0, 0, 2100, false, 0), SWRCAL_OSC_NONE);
}

// Radio 2, level 1, 2026-11-02 (autocal_r2_serial2.log): real readings
TEST(SWRCalibration, CompressionIsAShortFallNotAnOscillation){
    // The PA near its ceiling into the open: 20, 17 and 12 m fell 21, 14 and 5 mV for 2 dB
    EXPECT_EQ(SWRCalOscillationCheck(true, 1772.3f, 2.0f, 1751.7f, false, 0), SWRCAL_OSC_SHORT_FALL);
    EXPECT_EQ(SWRCalOscillationCheck(true, 1730.0f, 2.0f, 1716.2f, false, 0), SWRCAL_OSC_SHORT_FALL);
    EXPECT_EQ(SWRCalOscillationCheck(true, 1684.7f, 2.0f, 1679.6f, false, 0), SWRCAL_OSC_SHORT_FALL);
    // Fold-back near the ceiling: V_f rises a little as the drive drops (10 m load, +9.7 mV for 3 dB)
    EXPECT_EQ(SWRCalOscillationCheck(true, 1806.7f, 3.0f, 1816.4f, false, 0), SWRCAL_OSC_SHORT_FALL);
    // The other bands' open sweeps fell 40-52 mV per 2 dB
    EXPECT_EQ(SWRCalOscillationCheck(true, 1609.3f, 2.0f, 1561.7f, false, 0), SWRCAL_OSC_NONE);
    EXPECT_EQ(SWRCalOscillationCheck(true, 1430.8f, 2.0f, 1390.2f, false, 0), SWRCAL_OSC_NONE);
}

TEST(SWRCalibration, ARiseIsAnOscillation){
    // Radio 2's 10 m short on 2026-11-02: V_f rose 52 mV, its spread 70 mV
    EXPECT_EQ(SWRCalOscillationCheck(true, 1594.2f, 2.0f, 1646.5f, false, 0), SWRCAL_OSC_RISE);
    // Radio 2's 10 m short before its rebuild (bench sheet radio2): locked at 1930 mV
    EXPECT_EQ(SWRCalOscillationCheck(true, 1575.0f, 3.0f, 1929.6f, false, 0), SWRCAL_OSC_RISE);
    // Once locked it stays: no rise, no fall
    EXPECT_EQ(SWRCalOscillationCheck(true, 1929.6f, 3.0f, 1931.7f, false, 0), SWRCAL_OSC_SHORT_FALL);
    EXPECT_EQ(SWRCalOscillationCheck(true, 1931.7f, 3.0f, 1930.9f, false, 0), SWRCAL_OSC_SHORT_FALL);
    // A rise of just under 1 dB is still only a short fall
    EXPECT_EQ(SWRCalOscillationCheck(true, 1800.0f, 2.0f, 1824.0f, false, 0), SWRCAL_OSC_SHORT_FALL);
    EXPECT_EQ(SWRCalOscillationCheck(true, 1800.0f, 2.0f, 1826.0f, false, 0), SWRCAL_OSC_RISE);
}

TEST(SWRCalibration, AJumpInTheSpreadIsAnOscillation){
    // Radio 2's 10 m short at level 2, 2026-11-02: -5.98 dB 1608.1 mV (sd 4.2),
    // then -7.98 dB 1602.6 mV (sd 56.1) as the oscillation began, then a 259 mV rise
    EXPECT_EQ(SWRCalOscillationCheck(true, 1608.1f, 2.0f, 1602.6f, false, 0, 4.2f, 56.1f), SWRCAL_OSC_UNSTEADY);
    // Without the spreads it would only be a short fall
    EXPECT_EQ(SWRCalOscillationCheck(true, 1608.1f, 2.0f, 1602.6f, false, 0), SWRCAL_OSC_SHORT_FALL);
    // Even with a proper fall, the jump in the spread decides
    EXPECT_EQ(SWRCalOscillationCheck(true, 1650.0f, 2.0f, 1600.0f, false, 0, 4.2f, 56.1f), SWRCAL_OSC_UNSTEADY);
    // A rise is reported as a rise (level 1: 1594.2 -> 1646.5 mV, sd 5.2 -> 70.3)
    EXPECT_EQ(SWRCalOscillationCheck(true, 1594.2f, 2.0f, 1646.5f, false, 0, 5.2f, 70.3f), SWRCAL_OSC_RISE);
    // Real sweeps' spreads grow at low drive: radio 2's 10 m open, 16.6 -> 23.4 mV
    EXPECT_EQ(SWRCalOscillationCheck(true, 1473.8f, 3.0f, 1400.0f, false, 0, 16.6f, 23.4f), SWRCAL_OSC_NONE);
    // ...and 6 m's open, 4.6 -> 8.1 mV, stays a short fall at most
    EXPECT_EQ(SWRCalOscillationCheck(true, 1578.4f, 2.0f, 1549.2f, false, 0, 4.6f, 8.1f), SWRCAL_OSC_NONE);
    // A spread below the minimum is never a jump, however large the ratio
    EXPECT_EQ(SWRCalOscillationCheck(true, 1650.0f, 2.0f, 1600.0f, false, 0, 1.0f, 19.0f), SWRCAL_OSC_NONE);
    // The sweep is abandoned at once
    uint32_t shortFalls = 0;
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_UNSTEADY, &shortFalls), SWRCAL_SWEEP_ABANDON);
}

TEST(SWRCalibration, SweepDropsCompressedPointsAndAbandonsOscillation){
    uint32_t shortFalls = 0;
    // Compression at the top of the sweep: the previous point goes, the sweep goes on
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_SHORT_FALL, &shortFalls), SWRCAL_SWEEP_DROP_PREVIOUS);
    EXPECT_EQ(shortFalls, 1u);
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_SHORT_FALL, &shortFalls), SWRCAL_SWEEP_DROP_PREVIOUS);
    // ...until V_f falls properly again
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_NONE, &shortFalls), SWRCAL_SWEEP_KEEP);
    EXPECT_EQ(shortFalls, 0u) << "a proper fall clears the run";
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_SHORT_FALL, &shortFalls), SWRCAL_SWEEP_DROP_PREVIOUS);
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_NONE, &shortFalls), SWRCAL_SWEEP_KEEP);

    // A locked oscillation never falls: abandoned at the third short fall running
    shortFalls = 0;
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_SHORT_FALL, &shortFalls), SWRCAL_SWEEP_DROP_PREVIOUS);
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_SHORT_FALL, &shortFalls), SWRCAL_SWEEP_DROP_PREVIOUS);
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_SHORT_FALL, &shortFalls), SWRCAL_SWEEP_ABANDON);

    // A rise, or the first point far above the 50 Ohm reference, abandons at once
    shortFalls = 0;
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_RISE, &shortFalls), SWRCAL_SWEEP_ABANDON);
    shortFalls = 0;
    EXPECT_EQ(SWRCalSweepStep(SWRCAL_OSC_ABOVE_REFERENCE, &shortFalls), SWRCAL_SWEEP_ABANDON);
}

TEST(SWRCalibration, LoadPresence){
    // Radio 2, 2026-11-02, 50 Ohm load: 6 m at full drive has the smallest gap of the session
    EXPECT_TRUE(SWRCalLoadPresent(1738.5f, 1222.2f));
    // The smallest on any bench sheet: radio 2's 10 m before its rebuild
    EXPECT_TRUE(SWRCalLoadPresent(1858.0f, 1692.3f));
    // The load came off during 160 m: V_r read like V_f
    EXPECT_FALSE(SWRCalLoadPresent(1572.2f, 1576.2f));
    EXPECT_FALSE(SWRCalLoadPresent(1904.6f, 1904.4f));
    // At the limit
    EXPECT_TRUE(SWRCalLoadPresent(1600.0f, 1500.0f));
    EXPECT_FALSE(SWRCalLoadPresent(1600.0f, 1500.5f));
}

TEST(SWRCalibration, Level3ReportsWhichCeilingPointsItDropped){
    const float32_t m = 24.27f, b = -44.64f;
    const float32_t att[7] = {0, 5, 10, 15, 20, 25, 31.5};
    const float32_t over_dB[7] = {2.28f, 1.14f, 0, 0, 0, 0, 0};
    float32_t V[7], P_W[7];
    for (int k = 0; k < 7; k++){
        P_W[k] = 6.6f * tanhf(16.8f * powf(10.0f, -att[k] / 10.0f));
        V[k] = Reading(dBm(P_W[k]) + over_dB[k], m, b);
    }
    bool dropped = false, pointDropped[7];
    float32_t residual[7];
    SWRCalFitForwardLevel3(V, P_W, att, 7, &dropped, pointDropped, residual);
    EXPECT_TRUE(pointDropped[0]);
    EXPECT_TRUE(pointDropped[1]);
    for (int k = 2; k < 7; k++)
        EXPECT_FALSE(pointDropped[k]) << k;
    // The residual is the wattmeter minus the line: the detector reads high
    EXPECT_NEAR(residual[0], -2.28f, 0.02f);
    EXPECT_NEAR(residual[1], -1.14f, 0.02f);
}

TEST(SWRCalibration, Median){
    float32_t odd[5] = {3, 1, 5, 2, 4};
    EXPECT_FLOAT_EQ(SWRCalMedian(odd, 5), 3.0f);
    float32_t even[4] = {4, 1, 3, 2};
    EXPECT_FLOAT_EQ(SWRCalMedian(even, 4), 2.5f);
    EXPECT_FLOAT_EQ(SWRCalMedian(even, 0), 0.0f);
}
